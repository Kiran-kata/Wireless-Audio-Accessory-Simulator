#include "accessory/crypto.h"
#include <random>
#include <cstring>
#include <chrono>

namespace accessory {

void Crypto::generate_keypair(uint8_t* public_key, uint8_t* private_key) {
    // Simulated ECDH key generation
    // In production: use mbedTLS, OpenSSL, or hardware crypto
    
    generate_random(private_key, 32);
    
    // "Derive" public key (simplified - just XOR with constant)
    for (int i = 0; i < 32; i++) {
        public_key[i] = private_key[i] ^ 0xAA;
    }
}

void Crypto::derive_shared_secret(const uint8_t* private_key,
                                   const uint8_t* peer_public_key,
                                   uint8_t* shared_secret) {
    // Simulated ECDH shared secret derivation
    // In production: proper elliptic curve point multiplication
    
    for (int i = 0; i < 32; i++) {
        shared_secret[i] = private_key[i] ^ peer_public_key[i];
    }
}

void Crypto::encrypt_aes128(const uint8_t* plaintext, size_t length,
                            const uint8_t* key, const uint8_t* iv,
                            uint8_t* ciphertext) {
    // Simulated AES-128 encryption (XOR with key stream)
    // In production: use hardware AES or mbedTLS AES-CCM
    
    for (size_t i = 0; i < length; i++) {
        uint8_t key_byte = key[i % 16] ^ iv[i % 16];
        ciphertext[i] = plaintext[i] ^ key_byte;
    }
}

void Crypto::decrypt_aes128(const uint8_t* ciphertext, size_t length,
                            const uint8_t* key, const uint8_t* iv,
                            uint8_t* plaintext) {
    // AES decryption is same as encryption for our XOR simulation
    encrypt_aes128(ciphertext, length, key, iv, plaintext);
}

void Crypto::compute_hmac(const uint8_t* data, size_t length,
                         const uint8_t* key, size_t key_length,
                         uint8_t* hmac) {
    // Simulated HMAC-SHA256
    // In production: use proper HMAC implementation
    
    memset(hmac, 0, 32);
    
    for (size_t i = 0; i < length; i++) {
        hmac[i % 32] ^= data[i];
    }
    
    for (size_t i = 0; i < key_length; i++) {
        hmac[i % 32] ^= key[i];
    }
}

void Crypto::generate_random(uint8_t* buffer, size_t length) {
    // Use C++ random generator
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 255);
    
    for (size_t i = 0; i < length; i++) {
        buffer[i] = static_cast<uint8_t>(dis(gen));
    }
}

} // namespace accessory
