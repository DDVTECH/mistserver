#pragma once
#include <string>

namespace Secure{
  class Auth{
    private:
      void * pubkey; ///< Holds the public key.
    public:
      Auth(); ///< Attempts to load the GBv2 public key.
      bool PubKey_Check(std::string & data, std::string basesign); ///< Attempts to verify RSA signature using the public key.
  };

  std::string md5(std::string input); ///< Wrapper function for openssl MD5 implementation

}
