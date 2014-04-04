#include "encryption.h"

#include <openssl/aes.h>
#include <openssl/sha.h>
#include <cstdlib>
#include <cstring>

#include <iomanip>

#include <cstdio>
#include <cstdio>

namespace Encryption {
  std::string AES_Crypt(const std::string & data, const std::string & key, std::string & ivec){
    unsigned char * outData = (unsigned char*)malloc(data.size() * sizeof(char));
    unsigned int stateNum = 0;
    unsigned char stateEcount[16];
    unsigned char stateIvec[16];
    memset(stateEcount, 0, 16);      
    memcpy(stateIvec, ivec.c_str(), 8); 
    memset(stateIvec + 8, 0, 8);  
    AES_KEY cryptKey;
    if(AES_set_encrypt_key((unsigned char *)key.c_str(), 128, &cryptKey)){
      abort();
    }
    ///\todo loop per 128 bytes....
    AES_ctr128_encrypt((unsigned char *)data.c_str(), outData, data.size(), &cryptKey, stateIvec, stateEcount, &stateNum);
    std::string result = std::string((char*)outData,data.size());
    free(outData);
    return result;
  }

  std::string PR_GenerateContentKey(std::string & keyseed, std::string & keyid){
    char contentKey[16];
    char truncSeed[30];
    memcpy(truncSeed,keyseed.c_str(),30);
    std::string keyidBytes = PR_GuidToByteArray(keyid);
    unsigned char keyA[32];
    SHA256_CTX contextA;
    SHA256_Init(&contextA);
    SHA256_Update(&contextA, truncSeed, 30);
    SHA256_Update(&contextA, keyidBytes.c_str(), 16);
    SHA256_Final(keyA, &contextA);

    unsigned char keyB[32];
    SHA256_CTX contextB;
    SHA256_Init(&contextB);
    SHA256_Update(&contextB, truncSeed, 30);
    SHA256_Update(&contextB, keyidBytes.c_str(), 16);
    SHA256_Update(&contextB, truncSeed, 30);
    SHA256_Final(keyB, &contextB);

    unsigned char keyC[32];
    SHA256_CTX contextC;
    SHA256_Init(&contextC);
    SHA256_Update(&contextC, truncSeed, 30);
    SHA256_Update(&contextC, keyidBytes.c_str(), 16);
    SHA256_Update(&contextC, truncSeed, 30);
    SHA256_Update(&contextC, keyidBytes.c_str(), 16);
    SHA256_Final(keyC, &contextC);

    for (int i = 0; i < 16; i++){
      contentKey[i] = keyA[i] ^ keyA[i+16] ^ keyB[i] ^ keyB[i+16] ^ keyC[i] ^ keyC[i+16];
    }
    return std::string(contentKey,16);
  }

  std::string PR_GuidToByteArray(std::string & guid){
    std::string result;
    result = guid[3];
    result += guid[2];
    result += guid[1];
    result += guid[0];
    result += guid[5];
    result += guid[4];
    result += guid[7];
    result += guid[6];
    result += guid.substr(8);
    return result;
  }
}
