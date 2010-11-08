#ifndef _CRYPTO_H
#define	_CRYPTO_H
#define DLLEXP

#include <openssl/bn.h>
#include <openssl/dh.h>
#include <openssl/rc4.h>
#include <openssl/ssl.h>
#include <openssl/rand.h>
#include <openssl/err.h>
#include <openssl/bio.h>
#include <openssl/hmac.h>

class DLLEXP DHWrapper {
private:
    int32_t _bitsCount;
    DH *_pDH;
    uint8_t *_pSharedKey;
    int32_t _sharedKeyLength;
    BIGNUM *_peerPublickey;
public:
    DHWrapper(int32_t bitsCount);
    virtual ~DHWrapper();

    bool Initialize();
    bool CopyPublicKey(uint8_t *pDst, int32_t dstLength);
    bool CopyPrivateKey(uint8_t *pDst, int32_t dstLength);
    bool CreateSharedKey(uint8_t *pPeerPublicKey, int32_t length);
    bool CopySharedKey(uint8_t *pDst, int32_t dstLength);
private:
    void Cleanup();
    bool CopyKey(BIGNUM *pNum, uint8_t *pDst, int32_t dstLength);
};


DLLEXP void InitRC4Encryption(uint8_t *secretKey, uint8_t *pubKeyIn, uint8_t *pubKeyOut,
        RC4_KEY *rc4keyIn, RC4_KEY *rc4keyOut);
DLLEXP std::string md5(std::string source, bool textResult);
DLLEXP std::string b64(std::string source);
DLLEXP std::string b64(uint8_t *pBuffer, uint32_t length);
DLLEXP std::string unb64(std::string source);
DLLEXP std::string unb64(uint8_t *pBuffer, uint32_t length);

#endif /* _CRYPTO_H */

