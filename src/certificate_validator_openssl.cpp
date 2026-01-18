#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_CERT_VALIDATOR)
#undef ENABLE_DEBUG
#endif

#if defined(CERT_VALIDATOR_OPENSSL)
// Only compile if OpenSSL validator is explicitly selected

#include "certificate_validator.h"
#include "emonesp.h"

#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#include <cstring>

/**
 * OpenSSL implementation of CertificateValidator
 * This provides native OpenSSL-based certificate validation for platforms
 * that have OpenSSL available (e.g., native builds, Linux, macOS)
 */
class OpenSSLCertificateValidator : public CertificateValidator
{
public:
    OpenSSLCertificateValidator()
    {
        // OpenSSL library initialization (not needed in OpenSSL 1.1.0+)
        // These functions are deprecated in OpenSSL 1.1.0+ and no longer needed
    }

    virtual ~OpenSSLCertificateValidator()
    {
        // No cleanup needed in OpenSSL 1.1.0+
    }

    ValidationResult validateCertificate(const std::string &cert_pem) override
    {
        ValidationResult result = {false, 0, "", "", ""};

        // Create BIO for reading PEM data
        BIO *bio = BIO_new_mem_buf((void *)cert_pem.c_str(), -1);
        if(!bio)
        {
            result.error = "Failed to create BIO";
            DBUGF("OpenSSL error: %s", result.error.c_str());
            return result;
        }

        // Read X.509 certificate
        X509 *x509 = PEM_read_bio_X509(bio, NULL, NULL, NULL);
        BIO_free(bio);

        if(!x509)
        {
            unsigned long err = ERR_get_error();
            char err_str[256];
            ERR_error_string(err, err_str);
            result.error = std::string("Failed to parse certificate: ") + err_str;
            DBUGF("Certificate parse error: %s", err_str);
            return result;
        }

        // Extract serial number
        BIGNUM *serial_bn = ASN1_INTEGER_to_BN(X509_get_serialNumber(x509), NULL);
        if(serial_bn)
        {
            // Convert BIGNUM to uint64_t (take last 8 bytes for now)
            unsigned char buffer[8];
            int len = BN_bn2bin(serial_bn, buffer);
            uint64_t serial = 0;
            for(int i = 0; i < len && i < 8; i++)
            {
                serial = (serial << 8) | buffer[i];
            }
            result.serial = serial;
            BN_free(serial_bn);
        }

        // Extract issuer DN
        char issuer_str[1024];
        X509_NAME_oneline(X509_get_issuer_name(x509), issuer_str, sizeof(issuer_str));
        result.issuer = std::string(issuer_str);
        DBUGF("Issuer: %s", issuer_str);

        // Extract subject DN
        char subject_str[1024];
        X509_NAME_oneline(X509_get_subject_name(x509), subject_str, sizeof(subject_str));
        result.subject = std::string(subject_str);
        DBUGF("Subject: %s", subject_str);

        result.valid = true;
        X509_free(x509);
        return result;
    }

    bool validatePrivateKey(const std::string &key_pem) override
    {
        BIO *bio = BIO_new_mem_buf((void *)key_pem.c_str(), -1);
        if(!bio)
        {
            DBUGLN("Failed to create BIO for private key");
            return false;
        }

        // Try to read as PKCS#1 RSA key
        EVP_PKEY *pkey = PEM_read_bio_PrivateKey(bio, NULL, NULL, NULL);
        BIO_free(bio);

        if(!pkey)
        {
            unsigned long err = ERR_get_error();
            char err_str[256];
            ERR_error_string(err, err_str);
            DBUGF("Private key parse error: %s", err_str);
            return false;
        }

        EVP_PKEY_free(pkey);
        return true;
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
    return new OpenSSLCertificateValidator();
}

#endif // defined(CERT_VALIDATOR_OPENSSL)
