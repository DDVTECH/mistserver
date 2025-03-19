/// \file jwt.h Implementation for JSON web tokens.
#include "json.h"

#include <string>

#ifdef SSL
#include <mbedtls/bignum.h>
#endif

namespace JWT {
  bool isJWS(const std::string & str);

  class Key {
    public:
      Key() = default;
      Key(const JSON::Value & jwk);
      Key(const JSON::Value & jwk, const JSON::Value & perms);
      Key(const JSON::Value & jwk, const JSON::Value & stream, const int perms);
      std::string getStream() const;
      uint8_t getPerms() const;
      const JSON::Value & operator[](const std::string & name) const;
      const JSON::Value & operator[](const char *name) const;
      operator bool() const;
      operator const JSON::Value &() const;
      bool matchKeyPerms(const std::string & streamName, const uint8_t requiredPerms) const;
      std::string toString(bool withPerms) const;

    private:
      JSON::Value jwk;
      std::deque<std::string> stream;
      uint8_t perms = 0;
  };

  class JWS {
    public:
      JWS() = default;
      JWS(const std::string & pkg, bool skipCheck = false);
      JWS(const std::string & pkg, const uint8_t perms, bool skipCheck = false);
      const JSON::Value & getPayload() const;
      const std::string & getKid() const;
      operator bool() const;
      bool hasWildcard() const;
      bool checkClaims(const std::string & streamName = "", const std::string & hostIP = "") const;
      bool validateSignature() const;
      bool validateSignature(const Key & key) const;

    private:
      JSON::Value hdr; // protected header contains almost everything
      JSON::Value pld; // the claims set as json object
      std::string sig; // the actual signature
      std::string sin; // the signing input 'header.payload'
      uint8_t perms = 0; // int that contains flags for the permissions

      bool verify(const std::string & msg, const JSON::Value & key, const std::string & sig, const std::string & alg) const;
      bool verify_any(const std::string & msg, const std::string & sig, const std::string & alg) const;
#ifdef SSL
      bool verify_hmac(const std::string & msg, const JSON::Value & key, const std::string & sig, mbedtls_md_type_t hashMode) const;
      bool verify_rsa(const std::string & msg, const JSON::Value & key, const std::string & sig, int paddingMode,
                      mbedtls_md_type_t hashMode) const;
      bool verify_ecdsa(const std::string & msg, const JSON::Value & key, const std::string & sig, mbedtls_ecp_group_id gid) const;
#endif
  };
} // namespace JWT
