#pragma once

void AES_set_encrypt_key(const char * userKey, const int bits, char * key);
void AES_set_decrypt_key(const char * userKey, const int bits, char * key);
void AES_encrypt(const char * in, char * out, const int bits, const char * key);
void AES_decrypt(const char * in, char * out, const char * key, unsigned int bits);
static void increaseCounter(char * counter);
void AES_CTR128_crypt(const char * in, char * out, unsigned int len, const char * key, char ivec[16], char ecount_buf[16], unsigned int & num);
void printInverted(const unsigned int * data, unsigned int len);
