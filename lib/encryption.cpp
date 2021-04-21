#include "bitfields.h"
#include "defines.h"
#include "encryption.h"
#include "h264.h"

namespace Encryption{
  AES::AES(){mbedtls_aes_init(&ctx);}

  AES::~AES(){mbedtls_aes_free(&ctx);}

  void AES::setEncryptKey(const char *key){
    mbedtls_aes_setkey_enc(&ctx, (const unsigned char *)key, 128);
  }
  void AES::setDecryptKey(const char *key){
    mbedtls_aes_setkey_dec(&ctx, (const unsigned char *)key, 128);
  }

  DTSC::Packet AES::encryptPacketCTR(const DTSC::Meta &M, const DTSC::Packet &src, uint64_t ivec, size_t newTrack){
    DTSC::Packet res;
    if (newTrack == INVALID_TRACK_ID){
      FAIL_MSG("No target track given for track encryption!");
      return res;
    }

    char *data;
    size_t dataLen;
    src.getString("data", data, dataLen);

    size_t trackIdx = M.getSourceTrack(newTrack);

    char *encData = (char *)malloc(dataLen);

    size_t dataOffset = 0;

    if (M.getType(trackIdx) == "video" && dataLen > 96){
      dataOffset = dataLen - (int((dataLen - 96) / 16) * 16);
      memcpy(encData, data, dataOffset);
    }

    if (!encryptBlockCTR(ivec, data + dataOffset, encData + dataOffset, dataLen - dataOffset)){
      FAIL_MSG("Failed to encrypt packet");
      free(encData);
      return res;
    }

    res.genericFill(src.getTime(), src.getInt("offset"), newTrack, encData, dataLen, 0, src.getFlag("keyframe"));
    free(encData);
    return res;
  }

  std::string AES::encryptBlockCTR(uint64_t ivec, const std::string &inp){
    char *resPtr = (char *)malloc(inp.size());
    if (!encryptBlockCTR(ivec, inp.c_str(), resPtr, inp.size())){
      free(resPtr);
      return "";
    }
    std::string result(resPtr, inp.size());
    free(resPtr);
    return result;
  }

  bool AES::encryptBlockCTR(uint64_t ivec, const char *src, char *dest, size_t dataLen){
    size_t ncOff = 0;
    unsigned char streamBlock[] ={0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

    unsigned char nonceCtr[] ={0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    Bit::htobll((char *)nonceCtr, ivec);

    return mbedtls_aes_crypt_ctr(&ctx, dataLen, &ncOff, nonceCtr, streamBlock,
                                 (const unsigned char *)src, (unsigned char *)dest) == 0;
  }

  DTSC::Packet AES::encryptPacketCBC(const DTSC::Meta &M, const DTSC::Packet &src, char *ivec, size_t newTrack){
    DTSC::Packet res;
    if (newTrack == INVALID_TRACK_ID){
      FAIL_MSG("No target track given for track encryption!");
      return res;
    }

    char *data;
    size_t dataLen;
    src.getString("data", data, dataLen);

    size_t trackIdx = M.getSourceTrack(newTrack);

    bool encrypt = false;
    if (M.getCodec(trackIdx) == "H264"){
      std::deque<nalu::nalData> nalUnits = h264::analysePackets(data, dataLen);
      for (std::deque<nalu::nalData>::iterator it = nalUnits.begin(); it != nalUnits.end(); it++){
        if (it->nalType != 1 && it->nalType != 5){continue;}
        if (it->nalSize <= 48){continue;}
        encrypt = true;
        break;
      }
    }
    if (!encrypt){
      res.genericFill(src.getTime(), src.getInt("offset"), newTrack, data, dataLen, 0, src.getFlag("keyframe"));
      return res;
    }

    char *encData = (char *)malloc(dataLen);

    if (M.getCodec(trackIdx) == "H264"){
      if (!encryptH264BlockFairplay(ivec, data, encData, dataLen)){
        ERROR_MSG("Failed to encrypt a block of 16 bytes!");
        free(encData);
        return res;
      }
    }else{
      INFO_MSG("Going to fully CBC encrypt a %s packet of %zu bytes", M.getType(trackIdx).c_str(), dataLen);
      if (!encryptBlockCBC(ivec, data, encData, dataLen)){
        FAIL_MSG("Failed to encrypt packet");
        free(encData);
        return res;
      }
    }

    res.genericFill(src.getTime(), src.getInt("offset"), newTrack, encData, dataLen, 0, src.getFlag("keyframe"));
    free(encData);
    return res;
  }

  bool AES::encryptH264BlockFairplay(char *ivec, const char *src, char *dest, size_t dataLen){
    size_t offset = 0;
    std::deque<nalu::nalData> nalUnits = h264::analysePackets(src, dataLen);
    for (std::deque<nalu::nalData>::iterator it = nalUnits.begin(); it != nalUnits.end(); it++){
      if ((it->nalType != 1 && it->nalType != 5) || it->nalSize <= 48){
        memcpy(dest + offset, src + offset, it->nalSize + 4);
        offset += it->nalSize + 4;
        continue;
      }
      memcpy(dest + offset, src + offset, 36);
      offset += 36;
      size_t encryptedBlocks = 0;
      size_t lenToGo = it->nalSize - 32;
      while (lenToGo){
        if (lenToGo > 16){
          if (!encryptBlockCBC(ivec, src + offset, dest + offset, 16)){
            ERROR_MSG("Failed to encrypt a block of 16 bytes!");
            return false;
          }
          offset += 16;
          lenToGo -= 16;
          ++encryptedBlocks;
        }
        memcpy(dest + offset, src + offset, std::min(lenToGo, (size_t)144));
        offset += std::min(lenToGo, (size_t)144);
        lenToGo -= std::min(lenToGo, (size_t)144);
      }
    }
    return true;
  }

  std::string AES::encryptBlockCBC(char *ivec, const std::string &inp){
    char *resPtr = (char *)malloc(inp.size());
    if (!encryptBlockCBC(ivec, inp.c_str(), resPtr, inp.size())){
      free(resPtr);
      return "";
    }
    std::string result(resPtr, inp.size());
    free(resPtr);
    return result;
  }

  bool AES::encryptBlockCBC(char *ivec, const char *src, char *dest, size_t dataLen){
    if (dataLen % 16){WARN_MSG("Encrypting a non-multiple of 16 bytes: %zu", dataLen);}
    return mbedtls_aes_crypt_cbc(&ctx, MBEDTLS_AES_ENCRYPT, dataLen, (unsigned char *)ivec,
                                 (const unsigned char *)src, (unsigned char *)dest) == 0;
  }
}// namespace Encryption
