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

        class Flags
        {
          public:
            static const uint32_t SHOW_PRIVATE_KEY = 0;
            static const uint32_t REDACT_PRIVATE_KEY = 1 << 0;
        };

      private:
        Type _type;
        uint64_t _id;
        std::string _name;
        std::string _cert;
        std::string _key;

      public:
        Certificate(const char *cert, const char *key) :
          JsonSerialize(),
          _type(Type::Client),
          _name(""),
          _cert(cert),
          _key(key)
        { }

        Certificate(const char *cert) :
          JsonSerialize(),
          _type(Type::Root),
          _name(""),
          _cert(cert),
          _key("")
        { }

        Certificate() :
          JsonSerialize(),
          _type(Type::Invalid),
          _name(""),
          _cert(""),
          _key("")
        { }

        uint64_t getId() const { return _id; }
        Type getType() const { return _type; }
        std::string &getCert() { return _cert; };
        std::string &getKey() { return _key; }

        using JsonSerialize::deserialize;
        virtual bool deserialize(JsonObject &obj);
        using JsonSerialize::serialize;
        virtual bool serialize(JsonObject &obj) {
          return serialize(obj, Flags::REDACT_PRIVATE_KEY);
        }
        bool serialize(JsonObject &obj, uint32_t flags);
    };

  private:
    std::vector<Certificate *> _certs;
    const char *_root_ca;

  public:
    CertificateStore();
    ~CertificateStore();

    bool begin();

    const char *getRootCa();

    bool addCertificate(const char *name, const char *cert, const char *key, uint64_t *id = nullptr);
    bool addCertificate(const char *name, const char *cert, uint64_t *id = nullptr);
    bool addCertificate(DynamicJsonDocument &doc, uint64_t *id = nullptr, bool save = true);

    bool removeCertificate(uint64_t id);

    const char *getCertificate(uint64_t id);
    const char *getKey(uint64_t id);

    bool getCertificate(uint64_t id, std::string &certificate);
    bool getKey(uint64_t id, std::string &key);

    bool serializeCertificates(DynamicJsonDocument &doc, uint32_t flags = Certificate::Flags::REDACT_PRIVATE_KEY);
    bool serializeCertificate(DynamicJsonDocument &doc, uint64_t id, uint32_t flags = Certificate::Flags::REDACT_PRIVATE_KEY);

    // Serialising the whole store into one document needs a buffer large
    // enough to hold every PEM body at once. These let a caller emit them one
    // at a time, so the peak allocation is a single certificate rather than
    // the entire store.
    size_t certificateCount();
    bool serializeCertificateAt(DynamicJsonDocument &doc, size_t index, uint32_t flags = Certificate::Flags::REDACT_PRIVATE_KEY);

  private:
    bool loadCertificates();

    bool loadCertificate(String &name);
    bool saveCertificate(Certificate *cert);
    bool removeCertificate(Certificate *cert);

    bool addCertificate(Certificate *cert, uint64_t *id, bool save = true);

    bool findCertificate(uint64_t id, Certificate *&cert);
    bool findCertificate(uint64_t id, int &index);

    bool buildRootCa();
};



extern CertificateStore certs;

// Parse a certificate id as written by CertificateStore::Certificate::serialize()
// -- a bare hex string of 1 to 16 digits. Returns false, leaving id untouched,
// for anything else.
//
// std::stoull() throws on a malformed value and nothing on the paths that parse
// these ids catches it, so an uncaught exception reboots the device: a corrupt
// stored id would boot-loop the unit and a bad id in a POSTed certificate would
// reboot it from the API. Every parse of a stored or user-supplied id goes
// through here instead.
bool certificate_id_from_string(const char *str, uint64_t &id);

# endif // CERTIFICATES_h
