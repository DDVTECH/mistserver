#pragma once
#include "json.h"

#include <string>

namespace Secure{
  // Enums for the different SHA algorithms
  enum SHA { SHA256 = 256, SHA384 = 384, SHA512 = 512 };

  // MD5 hashing functions
  std::string md5(std::string input);
  std::string md5(const char *input, size_t in_len);
  void md5bin(const char *input, size_t in_len, char *output);

  // Hashing functions (SHA-256)
  std::string sha256(std::string input);
  std::string sha256(const char *input, size_t in_len);
  void sha256bin(const char *input, size_t in_len, char *output);

  // Hashing functions (SHA-384/512)
  std::string sha512(std::string input);
  std::string sha512(const char *input, size_t in_len);
  void sha384bin(const char *input, size_t in_len, char *output);
  void sha512bin(const char *input, size_t in_len, char *output);
  void sha512bin(const char *input, size_t in_len, char *output, bool is384);

  // Generic hashing functions
  void shabin(const char *input, size_t in_len, char *output, SHA alg);

  // Generic HMAC functions
  std::string digest_hmac(const std::string & msg, const JSON::Value & key, SHA alg);
  std::string hmac(std::string msg, std::string key, size_t hashSize, void hasher(const char *, size_t, char *), size_t blockSize);
  std::string hmac(const char *msg, size_t msg_len, const char *key, size_t key_len, size_t hashSize,
                   void hasher(const char *, size_t, char *), size_t blockSize);
  void hmacbin(const char *msg, size_t msg_len, const char *key, size_t key_len, size_t hashSize,
               void hasher(const char *, size_t, char *), size_t blockSize, char *output);
  std::string hmac_sha(std::string msg, std::string key, SHA alg);
  std::string hmac_sha(const char *msg, size_t msg_len, const char *key, size_t key_len, SHA alg);
  void hmac_shabin(const char *msg, size_t msg_len, const char *key, size_t key_len, char *output, SHA alg);

}// namespace Secure
