#ifndef NRF52_MESH_CRYPTO_TRAITS_H
#define NRF52_MESH_CRYPTO_TRAITS_H

#include <cstdint>
#include <cstddef>

namespace MeshCrypto
{
    struct NRF52CryptoTraits
    {
        static bool encryptAES_CTR(const uint8_t key[32], const uint8_t nonce[16],
                                   const uint8_t *plaintext, size_t len,
                                   uint8_t *ciphertext);
    };
}

#endif // NRF52_MESH_CRYPTO_TRAITS_H
