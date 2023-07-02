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
        const char *_cert;
        size_t _cert_len;
        const char *_key;

      public:
        Certificate() = default;
        Certificate(uint32_t id, const char *cert, const char *key) :
          _type(Type::Client),
          _id(id),
          _cert(cert),
          _key(key)
        { }

        Certificate(uint32_t id, const char *cert) :
          _type(Type::Root),
          _id(id),
          _cert(cert)
        { }

        uint32_t getId() const { return _id; }
        Type getType() const { return _type; }
        const char *getCert() const { return _cert; };
        size_t getCertLen() const { return _cert_len; };
        const char *getKey() const { return _key; }

        using JsonSerialize::deserialize;
        virtual bool deserialize(JsonObject &obj);
        using JsonSerialize::serialize;
        virtual bool serialize(JsonObject &obj);
    };

  private:
    static uint32_t _next_cert_id;
    std::vector<Certificate> _certs;

  public:
    CertificateStore();
    ~CertificateStore();

    bool begin();

    const char *getRootCa();

    bool addCertificate(const char *name, const char *cert, const char *key = nullptr, uint32_t *id = nullptr);
    bool removeCertificate(uint32_t id);

    const char *getCertificate(uint32_t id);
    const char *getKey(uint32_t id);

    bool serializeCertificates(DynamicJsonDocument &doc);
    bool serializeCertificate(DynamicJsonDocument &doc, uint32_t id);

};



extern CertificateStore certs;

# endif // CERTIFICATES_h
