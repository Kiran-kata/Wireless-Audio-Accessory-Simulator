#ifndef ACCESSORY_CRYPTO_H
#define ACCESSORY_CRYPTO_H

#include <cstdint>
#include <cstddef>

namespace accessory {

// Simulated cryptographic operations
// In production, use hardware crypto or library like mbedTLS

class Crypto {
public:
    // Key generation (simulated ECDH)
    static void generate_keypair(uint8_t* public_key, uint8_t* private_key);
    
    // Shared secret derivation (simulated)
    static void derive_shared_secret(const uint8_t* private_key,
                                     const uint8_t* peer_public_key,
                                     uint8_t* shared_secret);
    
    // AES-128 simulation (XOR with key stream for demo)
    static void encrypt_aes128(const uint8_t* plaintext, size_t length,
                               const uint8_t* key, const uint8_t* iv,
                               uint8_t* ciphertext);
    
    static void decrypt_aes128(const uint8_t* ciphertext, size_t length,
                               const uint8_t* key, const uint8_t* iv,
                               uint8_t* plaintext);
    
    // HMAC-SHA256 simulation (simplified)
    static void compute_hmac(const uint8_t* data, size_t length,
                            const uint8_t* key, size_t key_length,
                            uint8_t* hmac);
    
    // Random number generation
    static void generate_random(uint8_t* buffer, size_t length);
};

} // namespace accessory

#endif // ACCESSORY_CRYPTO_H
