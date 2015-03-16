#pragma once
#include <string>

namespace Secure {
  //MD5 hashing functions
  std::string md5(std::string input);
  std::string md5(const char * input, const unsigned int in_len);
  void md5bin(const char * input, const unsigned int in_len, char * output);

  //SHA256 hashing functions
  std::string sha256(std::string input);
  std::string sha256(const char * input, const unsigned int in_len);
  void sha256bin(const char * input, const unsigned int in_len, char * output);

  //Generic HMAC functions
  std::string hmac(std::string msg, std::string key, unsigned int hashSize, void hasher(const char *, const unsigned int, char*), unsigned int blockSize);
  std::string hmac(const char * msg, const unsigned int msg_len, const char * key, const unsigned int key_len, unsigned int hashSize, void hasher(const char *, const unsigned int, char*), unsigned int blockSize);
  void hmacbin(const char * msg, const unsigned int msg_len, const char * key, const unsigned int key_len, unsigned int hashSize, void hasher(const char*, const unsigned int, char*), unsigned int blockSize, char * output);
  //Specific HMAC functions
  std::string hmac_sha256(std::string msg, std::string key);
  std::string hmac_sha256(const char * msg, const unsigned int msg_len, const char * key, const unsigned int key_len);
  void hmac_sha256bin(const char * msg, const unsigned int msg_len, const char * key, const unsigned int key_len, char * output);

}

