#pragma once
#include <string>
#include <time.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>

class Auth{
  private:
    RSA * pubkey; ///< Holds the public key.
  public:
    Auth(); ///< Attempts to load the GBv2 public key.
    bool PubKey_Check(std::string & data, std::string basesign); ///< Attempts to verify RSA signature using the public key.
};
