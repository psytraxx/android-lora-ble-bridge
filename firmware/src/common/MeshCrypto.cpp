#include "common/MeshCrypto.h"
#include "common/Logging.h"
#include <cstring>

// Select platform-specific crypto traits
#if defined(ARDUINO_ARCH_ESP32)
#include "esp32/MeshCryptoTraits.h"
using CryptoTraits = MeshCrypto::ESP32CryptoTraits;
#elif defined(ARDUINO_ARCH_NRF52)
#include "nrf52/MeshCryptoTraits.h"
using CryptoTraits = MeshCrypto::NRF52CryptoTraits;
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

        bool result = CryptoTraits::encryptAES_CTR(key, nonce, plaintext, plaintextLen, ciphertext);
        if (result)
        {
            LOG_D(TAG, "Encrypted %d bytes", plaintextLen);
        }
        return result;
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
