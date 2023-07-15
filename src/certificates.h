#ifndef CERTIFICATES_h
#define CERTIFICATES_h

#include <Arduino.h>
#include <ArduinoJson.h>

#include <vector>

#include "json_serialize.h"

class CertificateStore
{
  public:
    class Certificate : virtual public JsonSerialize<4096>
    {
      public:
        class Type
        {
          public:
            enum Value : uint8_t {
              Invalid,
              Root,
              Client
            };

          Type() = default;
          constexpr Type(Value value) : _value(value) { }

          const char *toString()
          {
            return Type::Root == _value ? "root" :
                  Type::Client == _value ? "client" :
                  "unknown";
          }

          operator Value() const { return _value; }
          explicit operator bool() = delete;        // Prevent usage: if(type)
          Type operator= (const Value val) {
            _value = val;
            return *this;
          }

          private:
            Value _value;
        };

      private:
        Type _type;
        uint32_t _id;
        std::string _name;
        std::string _cert;
        std::string _key;

      public:
        Certificate() = default;
        Certificate(uint32_t id, const char *cert, const char *key) :
          _type(Type::Client),
          _id(id),
          _name(""),
          _cert(cert),
          _key(key)
        { }

        Certificate(uint32_t id, const char *cert) :
          _type(Type::Root),
          _id(id),
          _name(""),
          _cert(cert),
          _key("")
        { }

        Certificate(uint32_t id) :
          _type(Type::Invalid),
          _id(id),
          _name(""),
          _cert(""),
          _key("")
        { }

        uint32_t getId() const { return _id; }
        Type getType() const { return _type; }
        const char *getCert() const { return _cert.c_str(); };
        size_t getCertLen() const { return _cert.length(); };
        const char *getKey() const { return _key.c_str(); }

        using JsonSerialize::deserialize;
        virtual bool deserialize(JsonObject &obj);
        using JsonSerialize::serialize;
        virtual bool serialize(JsonObject &obj);
    };

  private:
    static uint32_t _next_cert_id;
    std::vector<Certificate *> _certs;

  public:
    CertificateStore();
    ~CertificateStore();

    bool begin();

    const char *getRootCa();

    bool addCertificate(const char *name, const char *cert, const char *key, uint32_t *id = nullptr) {
      return addCertificate(new Certificate(_next_cert_id++, cert, key), id);
    }
    bool addCertificate(const char *name, const char *cert, uint32_t *id = nullptr) {
      return addCertificate(new Certificate(_next_cert_id++, cert), id);
    }
    bool addCertificate(DynamicJsonDocument &doc, uint32_t *id = nullptr) {
      Certificate *cert = new Certificate(_next_cert_id++);
      cert->deserialize(doc);
      return addCertificate(cert, id);
    }
    bool removeCertificate(uint32_t id);

    const char *getCertificate(uint32_t id);
    const char *getKey(uint32_t id);

    bool serializeCertificates(DynamicJsonDocument &doc);
    bool serializeCertificate(DynamicJsonDocument &doc, uint32_t id);

  private:
    bool loadCertificates();
    bool saveCertificates();

    bool addCertificate(Certificate *cert, uint32_t *id);

    bool findCertificate(uint32_t id, Certificate *&cert);
    bool findCertificate(uint32_t id, int &index);
};



extern CertificateStore certs;

# endif // CERTIFICATES_h
