#pragma once
#include <string>
#include "dtsc.h"

namespace Encryption {
  class verimatrixData {
    public:
      void read(const char * shmPage);
      void write(char * shmPage);
      std::string url;
      std::string name;
      std::string key;
      std::string keyid;
      std::string keyseed;
      std::string laurl;
      std::string lauurl;
  };

  std::string hexString(const char * data, unsigned long dataLen);

  std::string AES_Crypt(const std::string & data, const std::string & key, std::string & ivec);
  std::string AES_Crypt(const char * data, int dataLen, const char * key, const char * ivec);

  //These functions are dangerous for your data
  void AESFullCrypt(char * data, int dataLen, const char * key, const char * ivec);
  void AESPartialCrypt(char * data, int dataLen,  char * expandedKey, char * eCount, char * iVec, unsigned int & num, bool & initialize);

  std::string PR_GenerateContentKey(std::string & keyseed, std::string & keyid);
  std::string PR_GuidToByteArray(std::string & guid);

  void encryptPlayReady(DTSC::Packet & pack, std::string & codec, const char * iVec, const char * key);

  void fillVerimatrix(verimatrixData & vmData);
}
