#include "common/MeshCrypto.h"
#include "common/Logging.h"
#include <cstring>

// Platform-specific AES implementation
#if defined(ARDUINO_ARCH_ESP32)
// ESP32: Use hardware AES via mbedtls
#include <mbedtls/aes.h>
#elif defined(ARDUINO_ARCH_NRF52)
// nRF52: Use Tiny-AES or mbedtls if available
// For now, we'll use mbedtls which is available in Adafruit nRF52 core
#include <mbedtls/aes.h>
#else
#error "Unsupported platform for MeshCrypto"
#endif

static const char *TAG = "MeshCrypto";

namespace MeshCrypto
{
    void expandKey(const uint8_t *psk, size_t pskLen, uint8_t key[32])
    {
        if (pskLen == 0 || pskLen > 32)
        {
            LOG_E(TAG, "Invalid PSK length: %d", pskLen);
            // Fallback to default
            pskLen = 1;
            psk = (const uint8_t *)"\x01";
        }

        // Expand by repeating the PSK to fill 32 bytes
        for (size_t i = 0; i < 32; i++)
        {
            key[i] = psk[i % pskLen];
        }

        LOG_D(TAG, "Key expanded from %d-byte PSK", pskLen);
    }

    void constructNonce(uint32_t packetId, uint32_t fromNode, uint8_t nonce[16])
    {
        // Clear nonce
        memset(nonce, 0, 16);

        // Bytes 0-3: packet ID (little-endian)
        nonce[0] = (packetId >> 0) & 0xFF;
        nonce[1] = (packetId >> 8) & 0xFF;
        nonce[2] = (packetId >> 16) & 0xFF;
        nonce[3] = (packetId >> 24) & 0xFF;

        // Bytes 4-7: from node (little-endian)
        nonce[4] = (fromNode >> 0) & 0xFF;
        nonce[5] = (fromNode >> 8) & 0xFF;
        nonce[6] = (fromNode >> 16) & 0xFF;
        nonce[7] = (fromNode >> 24) & 0xFF;

        // Bytes 8-15: zero padding (already done by memset)

        LOG_D(TAG, "Nonce constructed: pktId=%u, from=%u", packetId, fromNode);
    }

    bool encrypt(const uint8_t *plaintext, size_t plaintextLen,
                 const uint8_t key[32], const uint8_t nonce[16],
                 uint8_t *ciphertext)
    {
        if (plaintextLen == 0)
        {
            LOG_D(TAG, "Empty plaintext, nothing to encrypt");
            return true;
        }

        mbedtls_aes_context aes;
        mbedtls_aes_init(&aes);

        // Set encryption key (256-bit)
        int ret = mbedtls_aes_setkey_enc(&aes, key, 256);
        if (ret != 0)
        {
            LOG_E(TAG, "AES setkey failed: %d", ret);
            mbedtls_aes_free(&aes);
            return false;
        }

        // CTR mode: need a counter buffer (initialized from nonce)
        uint8_t stream_block[16];
        size_t nc_off = 0;
        uint8_t nonce_counter[16];
        memcpy(nonce_counter, nonce, 16);

        // Encrypt using AES-CTR
        ret = mbedtls_aes_crypt_ctr(&aes, plaintextLen, &nc_off,
                                     nonce_counter, stream_block,
                                     plaintext, ciphertext);

        mbedtls_aes_free(&aes);

        if (ret != 0)
        {
            LOG_E(TAG, "AES-CTR encrypt failed: %d", ret);
            return false;
        }

        LOG_D(TAG, "Encrypted %d bytes", plaintextLen);
        return true;
    }

    bool decrypt(const uint8_t *ciphertext, size_t ciphertextLen,
                 const uint8_t key[32], const uint8_t nonce[16],
                 uint8_t *plaintext)
    {
        // AES-CTR is symmetric: encryption and decryption use the same operation
        return encrypt(ciphertext, ciphertextLen, key, nonce, plaintext);
    }

    void getDefaultChannelKey(uint8_t key[32])
    {
        // Default Meshtastic channel: PSK = 0x01 (base64 "AQ==")
        // Expanded to 32 bytes by repeating
        const uint8_t defaultPsk = 0x01;
        expandKey(&defaultPsk, 1, key);
        LOG_D(TAG, "Default channel key loaded (0x01 expanded)");
    }
}
