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



void InitRC4Encryption(uint8_t *secretKey, uint8_t *pubKeyIn, uint8_t *pubKeyOut, RC4_KEY *rc4keyIn, RC4_KEY *rc4keyOut);

void HMACsha256(const void *pData, uint32_t dataLength, const void *pKey, uint32_t keyLength, void *pResult);


extern uint8_t genuineFMSKey[];

bool ValidateClientScheme(uint8_t * pBuffer, uint8_t scheme);
