#pragma once
#include "dtsc.h"
#include <mbedtls/aes.h>
#include <string>

namespace Encryption{
  class AES{
  public:
    AES();
    ~AES();

    void setEncryptKey(const char *key);
    void setDecryptKey(const char *key);

    DTSC::Packet encryptPacketCTR(const DTSC::Meta &M, const DTSC::Packet &src, uint64_t ivec, size_t newTrack);
    std::string encryptBlockCTR(uint64_t ivec, const std::string &inp);
    bool encryptBlockCTR(uint64_t ivec, const char *src, char *dest, size_t dataLen);

    bool encryptH264BlockFairplay(char *ivec, const char *src, char *dest, size_t dataLen);

    DTSC::Packet encryptPacketCBC(const DTSC::Meta &M, const DTSC::Packet &src, char *ivec, size_t newTrack);
    std::string encryptBlockCBC(char *ivec, const std::string &inp);
    bool encryptBlockCBC(char *ivec, const char *src, char *dest, size_t dataLen);

  protected:
    mbedtls_aes_context ctx;
  };
}// namespace Encryption
