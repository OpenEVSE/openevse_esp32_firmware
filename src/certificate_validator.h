#ifndef CERTIFICATE_VALIDATOR_H
#define CERTIFICATE_VALIDATOR_H

#include <string>
#include <cstdint>

/**
 * Abstract interface for certificate validation operations.
 * This decouples the certificate management logic from specific crypto libraries.
 */
class CertificateValidator
{
public:
    /**
     * Certificate validation result
     */
    struct ValidationResult
    {
        bool valid;           ///< Whether the certificate is valid
        uint64_t serial;      ///< Certificate serial number
        std::string issuer;   ///< Certificate issuer DN string
        std::string subject;  ///< Certificate subject DN string
        std::string error;    ///< Error message if invalid
    };

    virtual ~CertificateValidator() = default;

    /**
     * Validate and parse an X.509 certificate in PEM format
     * @param cert_pem The certificate in PEM format
     * @return ValidationResult containing parsed data or error info
     */
    virtual ValidationResult validateCertificate(const std::string &cert_pem) = 0;

    /**
     * Validate a private key in PEM format
     * @param key_pem The private key in PEM format
     * @return true if the key is valid, false otherwise
     */
    virtual bool validatePrivateKey(const std::string &key_pem) = 0;

    /**
     * Get the issuer DN string from a certificate
     * @param cert_pem The certificate in PEM format
     * @return Issuer DN string, or empty string on error
     */
    virtual std::string getIssuerDN(const std::string &cert_pem) = 0;

    /**
     * Get the subject DN string from a certificate
     * @param cert_pem The certificate in PEM format
     * @return Subject DN string, or empty string on error
     */
    virtual std::string getSubjectDN(const std::string &cert_pem) = 0;

    /**
     * Extract the serial number from a certificate
     * @param cert_pem The certificate in PEM format
     * @return Serial number as uint64_t
     */
    virtual uint64_t getSerialNumber(const std::string &cert_pem) = 0;
};

/**
 * Factory function to create the appropriate certificate validator
 * This is defined in the implementation file (certificate_validator_*.cpp)
 * The correct implementation is selected based on build flags:
 * - CERT_VALIDATOR_MBEDTLS: Uses mbedTLS (default for ESP32)
 * - CERT_VALIDATOR_OPENSSL: Uses OpenSSL (for native builds)
 */
CertificateValidator *createCertificateValidator();

#endif // CERTIFICATE_VALIDATOR_H
