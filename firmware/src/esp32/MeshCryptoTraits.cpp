#include "esp32/MeshCryptoTraits.h"
#include "common/Logging.h"
#include <mbedtls/aes.h>
#include <cstring>

static const char *TAG = "MeshCrypto";

namespace MeshCrypto
{
    bool ESP32CryptoTraits::encryptAES_CTR(const uint8_t key[32], const uint8_t nonce[16],
                                           const uint8_t *plaintext, size_t len,
                                           uint8_t *ciphertext)
    {
        mbedtls_aes_context aes;
        mbedtls_aes_init(&aes);

        int ret = mbedtls_aes_setkey_enc(&aes, key, 256);
        if (ret != 0)
        {
            LOG_E(TAG, "AES setkey failed: %d", ret);
            mbedtls_aes_free(&aes);
            return false;
        }

        uint8_t stream_block[16];
        size_t nc_off = 0;
        uint8_t nonce_counter[16];
        memcpy(nonce_counter, nonce, 16);

        ret = mbedtls_aes_crypt_ctr(&aes, len, &nc_off,
                                     nonce_counter, stream_block,
                                     plaintext, ciphertext);
        mbedtls_aes_free(&aes);

        if (ret != 0)
        {
            LOG_E(TAG, "AES-CTR encrypt failed: %d", ret);
            return false;
        }
        return true;
    }
}
