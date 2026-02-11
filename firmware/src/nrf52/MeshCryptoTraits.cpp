#include "nrf52/MeshCryptoTraits.h"
#include "common/Logging.h"
#include <Crypto.h>
#include <AES.h>
#include <CTR.h>

namespace MeshCrypto
{
    bool NRF52CryptoTraits::encryptAES_CTR(const uint8_t key[32], const uint8_t nonce[16],
                                           const uint8_t *plaintext, size_t len,
                                           uint8_t *ciphertext)
    {
        CTR<AES256> ctr;
        ctr.setKey(key, 32);
        ctr.setIV(nonce, 16);
        ctr.setCounterSize(4);
        ctr.encrypt(ciphertext, plaintext, len);
        return true;
    }
}
