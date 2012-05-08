/// \file crypto.h
/// Holds all headers needed for RTMP cryptography functions.

#pragma once
#include <stdint.h>
#include <string>
#include <openssl/bn.h>
#include <openssl/dh.h>
#include <openssl/rc4.h>
#include <openssl/ssl.h>
#include <openssl/rand.h>
#include <openssl/err.h>
#include <openssl/bio.h>
#include <openssl/hmac.h>

class DHWrapper {
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


void InitRC4Encryption(uint8_t *secretKey, uint8_t *pubKeyIn, uint8_t *pubKeyOut, RC4_KEY *rc4keyIn, RC4_KEY *rc4keyOut);
std::string md5(std::string source, bool textResult);
std::string b64(std::string source);
std::string b64(uint8_t *pBuffer, uint32_t length);
std::string unb64(std::string source);
std::string unb64(uint8_t *pBuffer, uint32_t length);

void HMACsha256(const void *pData, uint32_t dataLength, const void *pKey, uint32_t keyLength, void *pResult);

uint32_t GetDigestOffset0(uint8_t *pBuffer);
uint32_t GetDigestOffset1(uint8_t *pBuffer);
uint32_t GetDigestOffset(uint8_t *pBuffer, uint8_t scheme);
uint32_t GetDHOffset0(uint8_t *pBuffer);
uint32_t GetDHOffset1(uint8_t *pBuffer);
uint32_t GetDHOffset(uint8_t *pBuffer, uint8_t scheme);

extern uint8_t genuineFMSKey[];

bool ValidateClientScheme(uint8_t * pBuffer, uint8_t scheme);
