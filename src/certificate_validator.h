#ifndef CERTIFICATE_VALIDATOR_H
#define CERTIFICATE_VALIDATOR_H

#include <string>
#include <cstdint>

/**
 * Abstract base class for X.509 certificate validation and parsing
 * 
 * Provides an interface for validating certificates and private keys in PEM format,
 * as well as extracting certificate metadata such as issuer DN, subject DN, and serial numbers.
 * 
 * @note Serial number handling: X.509 certificates can have serial numbers up to 20 bytes,
 *       but this validator truncates them to 64-bit (8 bytes). Only the least significant
 *       8 bytes are retained during truncation. Applications requiring full serial number
 *       precision should implement custom validation logic.
 */
class CertificateValidator
{
public:
    /**
     * Certificate validation result
     * 
     * @note The serial field contains only the least significant 8 bytes of the certificate's
     *       serial number. Serial numbers larger than 8 bytes will be truncated. For certificates
     *       with larger serial numbers, use getSerialNumber() and handle truncation appropriately.
     */
    struct ValidationResult
    {
        bool valid;           ///< Whether the certificate is valid
        uint64_t serial;      ///< Certificate serial number (truncated to 8 bytes, LSB retained)
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
