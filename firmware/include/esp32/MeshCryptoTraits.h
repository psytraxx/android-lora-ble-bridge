#ifndef ESP32_MESH_CRYPTO_TRAITS_H
#define ESP32_MESH_CRYPTO_TRAITS_H

#include <cstdint>
#include <cstddef>

namespace MeshCrypto
{
    struct ESP32CryptoTraits
    {
        static bool encryptAES_CTR(const uint8_t key[32], const uint8_t nonce[16],
                                   const uint8_t *plaintext, size_t len,
                                   uint8_t *ciphertext);
    };
}

#endif // ESP32_MESH_CRYPTO_TRAITS_H
