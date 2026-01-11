#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_CERT_VALIDATOR)
#undef ENABLE_DEBUG
#endif

#if !defined(CERT_VALIDATOR_OPENSSL)
// Only compile if OpenSSL validator is NOT selected (i.e., use mbedTLS or default)

#include "certificate_validator.h"
#include "emonesp.h"

#include "mbedtls/x509_crt.h"
#include "mbedtls/pk.h"

/**
 * MbedTLS implementation of CertificateValidator
 */
class MbedTLSCertificateValidator : public CertificateValidator
{
public:
    MbedTLSCertificateValidator() = default;
    virtual ~MbedTLSCertificateValidator() = default;

    ValidationResult validateCertificate(const std::string &cert_pem) override
    {
        ValidationResult result = {false, 0, "", "", ""};

        mbedtls_x509_crt x509;
        mbedtls_x509_crt_init(&x509);

        int ret = mbedtls_x509_crt_parse(&x509, (const unsigned char *)cert_pem.c_str(), cert_pem.length() + 1);
        if(ret != 0)
        {
            DBUGF("Certificate parse error: %d", ret);
            result.error = "Failed to parse certificate";
            mbedtls_x509_crt_free(&x509);
            return result;
        }

        // Extract serial number
        uint64_t serial = 0;
        for(size_t i = 0; i < x509.serial.len; i++)
        {
            serial = (serial << 8) | x509.serial.p[i];
        }
        result.serial = serial;

        // Extract issuer and subject DNs
        char dn_buffer[1024];
        if(mbedtls_x509_dn_gets(dn_buffer, sizeof(dn_buffer), &x509.issuer) > 0)
        {
            result.issuer = std::string(dn_buffer);
            DBUGF("Issuer: %s", dn_buffer);
        }

        if(mbedtls_x509_dn_gets(dn_buffer, sizeof(dn_buffer), &x509.subject) > 0)
        {
            result.subject = std::string(dn_buffer);
            DBUGF("Subject: %s", dn_buffer);
        }

        result.valid = true;
        mbedtls_x509_crt_free(&x509);
        return result;
    }

    bool validatePrivateKey(const std::string &key_pem) override
    {
        mbedtls_pk_context pk;
        mbedtls_pk_init(&pk);

        int ret = mbedtls_pk_parse_key(&pk, (const unsigned char *)key_pem.c_str(), key_pem.length() + 1, NULL, 0);
        bool valid = (ret == 0);

        if(!valid)
        {
            DBUGF("Private key parse error: %d", ret);
        }

        mbedtls_pk_free(&pk);
        return valid;
    }

    std::string getIssuerDN(const std::string &cert_pem) override
    {
        ValidationResult result = validateCertificate(cert_pem);
        return result.issuer;
    }

    std::string getSubjectDN(const std::string &cert_pem) override
    {
        ValidationResult result = validateCertificate(cert_pem);
        return result.subject;
    }

    uint64_t getSerialNumber(const std::string &cert_pem) override
    {
        ValidationResult result = validateCertificate(cert_pem);
        return result.serial;
    }
};

/**
 * Factory function to create the appropriate certificate validator
 */
CertificateValidator *createCertificateValidator()
{
    return new MbedTLSCertificateValidator();
}

#endif // !defined(CERT_VALIDATOR_OPENSSL)
