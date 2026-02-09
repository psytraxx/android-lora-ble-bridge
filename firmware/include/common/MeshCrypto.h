#ifndef MESH_CRYPTO_H
#define MESH_CRYPTO_H

#include <cstdint>
#include <cstddef>

/**
 * @file MeshCrypto.h
 * @brief AES256-CTR encryption for Meshtastic protocol
 *
 * Implements the Meshtastic encryption scheme:
 * - AES256-CTR mode (Counter mode)
 * - Channel PSK (Pre-Shared Key) as encryption key
 * - Nonce constructed from packet ID + sender node number
 * - Default channel: 0x01 expanded to 256-bit key
 */

namespace MeshCrypto
{
    /**
     * @brief Expand a short PSK into a 256-bit (32-byte) AES key
     *
     * Meshtastic default channel uses PSK "AQ==" (base64) = single byte 0x01.
     * This is expanded by repeating to fill 32 bytes.
     *
     * @param psk Input pre-shared key (can be 1-32 bytes)
     * @param pskLen Length of input PSK
     * @param key Output buffer for 32-byte AES-256 key
     */
    void expandKey(const uint8_t *psk, size_t pskLen, uint8_t key[32]);

    /**
     * @brief Construct AES-CTR nonce from packet ID and sender node number
     *
     * Nonce format (16 bytes):
     * [0-3]:   packet_id (little-endian)
     * [4-7]:   from_node (little-endian)
     * [8-15]:  zero padding
     *
     * @param packetId Unique packet identifier
     * @param fromNode Sender's node number
     * @param nonce Output buffer for 16-byte nonce
     */
    void constructNonce(uint32_t packetId, uint32_t fromNode, uint8_t nonce[16]);

    /**
     * @brief Encrypt payload using AES256-CTR
     *
     * Encrypts the Data protobuf payload (packet header remains cleartext).
     * Uses ESP32 hardware AES via mbedtls.
     *
     * @param plaintext Input plaintext data
     * @param plaintextLen Length of plaintext
     * @param key 32-byte AES-256 key
     * @param nonce 16-byte nonce (IV for CTR mode)
     * @param ciphertext Output buffer for encrypted data (same size as plaintext)
     * @return true on success, false on failure
     */
    bool encrypt(const uint8_t *plaintext, size_t plaintextLen,
                 const uint8_t key[32], const uint8_t nonce[16],
                 uint8_t *ciphertext);

    /**
     * @brief Decrypt payload using AES256-CTR
     *
     * Decrypts the Data protobuf payload.
     * AES-CTR is symmetric: encrypt and decrypt use the same operation.
     *
     * @param ciphertext Input encrypted data
     * @param ciphertextLen Length of ciphertext
     * @param key 32-byte AES-256 key
     * @param nonce 16-byte nonce (IV for CTR mode)
     * @param plaintext Output buffer for decrypted data (same size as ciphertext)
     * @return true on success, false on failure
     */
    bool decrypt(const uint8_t *ciphertext, size_t ciphertextLen,
                 const uint8_t key[32], const uint8_t nonce[16],
                 uint8_t *plaintext);

    /**
     * @brief Get default Meshtastic channel key (0x01 expanded to 32 bytes)
     *
     * This is the default "AQ==" base64 PSK used by Meshtastic.
     * In production, users should change this to a secure random key.
     *
     * @param key Output buffer for 32-byte key
     */
    void getDefaultChannelKey(uint8_t key[32]);
}

#endif // MESH_CRYPTO_H
