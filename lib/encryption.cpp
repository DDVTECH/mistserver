#include "encryption.h"
#include "auth.h"
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <cstdio>
#include <iostream>
#include "rijndael.h"
#include "defines.h"
#include "bitfields.h"
#include "http_parser.h"
#include "base64.h"
#include "nal.h"/*LTS*/
#include <sstream>

namespace Encryption {
  ///helper function for printing binary values
  std::string hexString(const char * data, unsigned long dataLen){
    std::stringstream res;
    for (int i = 0; i < dataLen; i++){
      res << std::hex << std::setw(2) << std::setfill('0') << (int)data[i];
      if (i % 4 == 3){
        res << " ";
      }
    }
    return res.str();
  }


  std::string AES_Crypt(const std::string & data, const std::string & key, std::string & ivec) {
    return AES_Crypt(data.data(), data.size(), key.data(), ivec.data());
  }

  std::string AES_Crypt(const char * data, int dataLen, const char * key, const char * ivec) {
    char * outData = (char *)malloc(dataLen * sizeof(char));
    memcpy(outData, data, dataLen);
    AESFullCrypt(outData, dataLen, key, ivec);
    std::string result = std::string(outData, dataLen);
    free(outData);
    return result;
  }

  ///This function encrypts data in-place.
  ///It alters all parameters except dataLen.
  ///Do not use it unless you know what you are doing.
  void AESPartialCrypt(char * data, int dataLen,  char * expandedKey, char * eCount, char * iVec, unsigned int & num, bool & initialize) {
    if (initialize) {
      num = 0;
      memset(eCount, 0, 16);
      ///Before use, make sure the iVec is in the UPPER 8 bytes
      memset(iVec + 8, 0, 8);
      ///Before use, make sure this is not the only copy of the key you had. It is lost upon initialization
      char cryptKey[224];
      AES_set_encrypt_key(expandedKey, 128, cryptKey);
      memcpy(expandedKey, cryptKey, 224);
      initialize = false;
    }
    char * outData = (char *)malloc(dataLen * sizeof(char));
    AES_CTR128_crypt(data, outData, dataLen, expandedKey, iVec, eCount, num);
    memcpy(data, outData, dataLen);
    free(outData);
  }

  //Generates the contentkey from a keyseed and a keyid
  std::string PR_GenerateContentKey(std::string & keyseed, std::string & keyid) {
    char contentKey[16];
    char dataBlob[92];
    char keyA[32], keyB[32], keyC[32];
    std::string keyidBytes = PR_GuidToByteArray(keyid);
    memcpy(dataBlob,  keyseed.data(), 30);
    memcpy(dataBlob + 30, keyidBytes.data(), 16);
    memcpy(dataBlob + 46, keyseed.data(), 30);
    memcpy(dataBlob + 76, keyidBytes.data(), 16);
    //KeyA is generated from  keyseed/keyid
    Secure::sha256bin(dataBlob, 46, keyA);
    //KeyB is generated from  keyseed/keyid/keyseed
    Secure::sha256bin(dataBlob, 76, keyB);
    //KeyC is generated from  keyseed/keyid/keyseed/keyid
    Secure::sha256bin(dataBlob, 92, keyC);
    for (int i = 0; i < 16; i++) {
      contentKey[i] = keyA[i] ^ keyA[i + 16] ^ keyB[i] ^ keyB[i + 16] ^ keyC[i] ^ keyC[i + 16];
    }
    return std::string(contentKey, 16);
  }

  //Transforms a guid to the MS byte array representation
  std::string PR_GuidToByteArray(std::string & guid) {
    char result[16];
    result[0] = guid[3];
    result[1] = guid[2];
    result[2] = guid[1];
    result[3] = guid[0];

    result[4] = guid[5];
    result[5] = guid[4];

    result[6] = guid[7];
    result[7] = guid[6];
    memcpy(result + 8, guid.data() + 8, 8);
    return std::string(result, 8);
  }


  ///This function encrypts data in-place.
  void AESFullCrypt(char * data, int dataLen, const char * key, const char * ivec) {
    unsigned int num = 0;
    char expandedKey[224];
    memcpy(expandedKey, key, 16);
    char eCount[16];
    char iVec[16];
    memcpy(iVec, ivec, 8);
    bool initialize = true;
    AESPartialCrypt(data, dataLen, expandedKey, eCount, iVec, num, initialize);
  }

  void encryptPlayReady(DTSC::Packet & thisPack, std::string & codec, const char * iVec, const char * key) {
    char * data;
    unsigned int dataLen;
    thisPack.getString("data", data, dataLen);

    if (codec == "H264") {
      unsigned int num = 0;
      char expandedKey[224];
      memcpy(expandedKey, key, 16);
      char eCount[16];
      char initVec[16];
      memcpy(initVec, iVec, 8);
      bool initialize = true;

      int pos = 0;

      std::deque<int> nalSizes = h264::parseNalSizes(thisPack);
      for (std::deque<int>::iterator it = nalSizes.begin(); it != nalSizes.end(); it++) {
        int encrypted = (*it - 5) & ~0xF;//Bitmask to a multiple of 16
        int clear = *it - encrypted;
        Encryption::AESPartialCrypt(data + pos + clear, encrypted, expandedKey, eCount, initVec, num, initialize);
        pos += *it;
      }
    }
    if (codec == "AAC") {
      Encryption::AESFullCrypt(data, dataLen, key, iVec);
    }
  }

  /// Converts a hexidecimal string format key to binary string format.
  std::string binKey(std::string hexkey) {
    char newkey[16];
    memset(newkey, 0, 16);
    for (size_t i = 0; i < hexkey.size(); ++i) {
      char c = hexkey[i];
      newkey[i >> 1] |= ((c & 15) + (((c & 64) >> 6) | ((c & 64) >> 3))) << ((~i & 1) << 2);
    }
    return std::string(newkey, 16);
  }


  /// Helper function for urlescape.
  /// Encodes a character as two hex digits.
  std::string hex(char dec) {
    char dig1 = (dec & 0xF0) >> 4;
    char dig2 = (dec & 0x0F);
    if (dig1 <= 9) dig1 += 48;
    if (10 <= dig1 && dig1 <= 15) dig1 += 97 - 10;
    if (dig2 <= 9) dig2 += 48;
    if (10 <= dig2 && dig2 <= 15) dig2 += 97 - 10;
    std::string r;
    r.append(&dig1, 1);
    r.append(&dig2, 1);
    return r;
  }
  std::string hex(const std::string & input) {
    std::string res;
    res.reserve(input.size() * 2);
    for (unsigned int i = 0; i < input.size(); i++) {
      res += hex(input[i]);
    }
    return res;
  }

  void fillVerimatrix(verimatrixData & vmData) {
    int hostPos = vmData.url.find("://") + 3;
    int portPos = vmData.url.find(":", hostPos);

    std::string hostName = vmData.url.substr(hostPos, (portPos == std::string::npos ? portPos : portPos - hostPos));
    int port = (portPos == std::string::npos ? 80 : atoi(vmData.url.data() + portPos + 1));
    Socket::Connection veriConn(hostName, port, true);

    HTTP::Parser H;
    H.url = "/CAB/keyfile?PROTECTION-TYPE=PLAYREADY&TYPE=DTV&POSITION=0&RESOURCE-ID=" + vmData.name;
    H.SetHeader("Host", vmData.url.substr(hostPos));
    H.SendRequest(veriConn);
    H.Clean();
    while (veriConn && (!veriConn.spool() || !H.Read(veriConn))) {}
    vmData.key = binKey(H.GetHeader("Key"));
    vmData.keyid = H.GetHeader("KeyId");
    vmData.laurl = H.GetHeader("LAURL");
    vmData.lauurl = H.GetHeader("LAUURL");
  }

  void verimatrixData::read(const char * shmPage) {
    int offset = 0;
    url = std::string(shmPage + offset);
    offset += url.size() + 1;//+1 for the concluding 0-byte
    name = std::string(shmPage + offset);
    offset += name.size() + 1;//+1 for the concluding 0-byte
    key = std::string(shmPage + offset);
    offset += key.size() + 1;//+1 for the concluding 0-byte
    keyid = std::string(shmPage + offset);
    offset += keyid.size() + 1;//+1 for the concluding 0-byte
    laurl = std::string(shmPage + offset);
    offset += laurl.size() + 1;//+1 for the concluding 0-byte
    lauurl = std::string(shmPage + offset);

    key = binKey(key);
  }

  void verimatrixData::write(char * shmPage) {
    int offset = 0;
    memcpy(shmPage + offset, url.c_str(), url.size() + 1);
    offset += url.size() + 1;//+1 for the concluding 0-byte
    memcpy(shmPage + offset, name.c_str(), name.size() + 1);
    offset += name.size() + 1;//+1 for the concluding 0-byte
    std::string tmpKey = hex(key);
    memcpy(shmPage + offset, tmpKey.c_str(), tmpKey.size() + 1);
    offset += tmpKey.size() + 1;//+1 for the concluding 0-byte
    memcpy(shmPage + offset, keyid.c_str(), keyid.size() + 1);
    offset += keyid.size() + 1;//+1 for the concluding 0-byte
    memcpy(shmPage + offset, laurl.c_str(), laurl.size() + 1);
    offset += laurl.size() + 1;//+1 for the concluding 0-byte
    memcpy(shmPage + offset, lauurl.c_str(), lauurl.size() + 1);
  }
}

