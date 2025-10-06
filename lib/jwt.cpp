#include "jwt.h"

#include "auth.h"
#include "defines.h"
#include "encode.h"
#include "shared_memory.h"
#include "timing.h"

#include <cstdint>
#include <string>
#include <time.h>

// Compacted unreadable helper function that checks whether two strings each with at most one wildcard are matching
bool equalsWithWildcard(const std::string & a, const std::string & b) {
  if (a == "*" || b == "*") return true;
  size_t apos = a.find('*'), bpos = b.find('*');
  bool awc = (apos != std::string::npos), bwc = (bpos != std::string::npos);
  if (!awc && !bwc) return a == b;
  size_t pre = std::min(awc ? apos : a.size(), bwc ? bpos : b.size());
  if (a.substr(0, pre) != b.substr(0, pre)) return false;
  size_t suf = std::min(awc ? a.size() - apos - 1 : a.size(), bwc ? b.size() - bpos - 1 : b.size());
  if (a.substr(a.size() - suf) != b.substr(b.size() - suf)) return false;
  return true;
}

#ifdef SSL
// Mapping function between mbedtls types and internal types
Secure::SHA getSHA(mbedtls_md_type_t hashMode) {
  switch (hashMode) {
    case MBEDTLS_MD_SHA256: return Secure::SHA256;
    case MBEDTLS_MD_SHA384: return Secure::SHA384;
    case MBEDTLS_MD_SHA512: return Secure::SHA512;
    default: return Secure::SHA256; // if bad assume 256
  }
}
#endif

namespace JWT {
  /// Global function to check whether a string is valid JWS, or rather, contains two dots and is base64url
  bool isJWS(const std::string & str) {
    int dots = 0;
    for (const char & c : str) {
      if (c < '-') return false;
      if (c == '.') {
        if (++dots > 2) return false;
        continue;
      }
      if (c == '/' || c == ':' || c == '@' || c == '`') return false;
      if (c == '_' || (c & 0x1F) < 27) continue;
      return false;
    }
    return dots == 2;
  }

  /// Constructor for key object that takes a JSON value according to JWK spec, sets default permissions
  Key::Key(const JSON::Value & _jwk) {
    if (!_jwk.isMember("kty")) {
      FAIL_MSG("Attempted to create key without keytype-- please check the validity of your stored keys");
      return;
    }
    jwk.extend(_jwk, emptyset, {"kty", "kid", "use", "key_ops", "alg", "x5u", "x5c", "x5t", "x5t#S256"});

    // Set permissions to default
    perms = (JWK_PERM_ADMIN * JWK_DFLT_ADMIN) + (JWK_PERM_INPUT * JWK_DFLT_INPUT) + (JWK_PERM_OUTPUT * JWK_DFLT_OUTPUT);
    stream.emplace_back(JWK_DFLT_STREAM);

    // Match the first character for different keytypes (octet, RSA and EC)
    switch (_jwk["kty"].asStringRef().at(0)) {
      case 'o': jwk.extend(_jwk, emptyset, {"k"}); break;
      case 'R': jwk.extend(_jwk, emptyset, {"n", "e", "d", "p", "q", "dp", "dq", "qi"}); break;
      case 'E': jwk.extend(_jwk, emptyset, {"crv", "x", "y", "d"}); break;
    }
  }

  // Delegated constructor that takes an additional permissions object and sets the defaults if any are missing
  Key::Key(const JSON::Value & jwk, const JSON::Value & p)
    : Key(jwk, p["stream"], [](const JSON::Value & p) {
        int perms = 0;
        perms |= JWK_PERM_INPUT * ((p.isMember("input") && p["input"].isBool()) ? p["input"].asBool() : JWK_DFLT_INPUT);
        perms |= JWK_PERM_OUTPUT * ((p.isMember("output") && p["output"].isBool()) ? p["output"].asBool() : JWK_DFLT_OUTPUT);
        perms |= JWK_PERM_ADMIN * ((p.isMember("admin") && p["admin"].isBool()) ? p["admin"].asBool() : JWK_DFLT_ADMIN);
        return perms;
      }(p)) {}

  // Delegated constructor for setting the permissions of a key if they exist
  Key::Key(const JSON::Value & _jwk, const JSON::Value & _stream, const int _perms) : Key(_jwk) {
    perms = _perms;
    stream.clear();
    if (_stream.isString())
      stream.emplace_back(_stream);
    else if (_stream.isArray() && _stream.size()) {
      jsonForEachConst (_stream, i) {
        if (i->isString()) stream.emplace_back(*i);
      }
    }
    if (!stream.size()) stream.emplace_back(JWK_DFLT_STREAM);
  }

  // Getter for the streams object, returns it as stringified JSON
  std::string Key::getStream() const {
    if (stream.empty()) return "";
    JSON::Value jsonStream;
    for (std::string s : stream) jsonStream.append(s);
    return jsonStream.toString();
  }

  // Getter for the permissions
  uint8_t Key::getPerms() const {
    return perms;
  }

  const JSON::Value & Key::operator[](const std::string & name) const {
    return jwk[name];
  }
  const JSON::Value & Key::operator[](const char *name) const {
    return (*this)[std::string(name)];
  }
  Key::operator const JSON::Value &() const {
    return jwk;
  }
  Key::operator bool() const {
    return !jwk.isNull();
  }

  /// Checks whether the right permission bit is set and whether the streamname matches the pattern in key
  bool Key::matchKeyPerms(const std::string & streamName, const uint8_t requiredPerms) const {
    if ((perms & requiredPerms) != requiredPerms) return false;
    if (stream.empty()) return true;
    for (auto & i : stream)
      if (equalsWithWildcard(streamName, i)) return true;
    return false;
  }

  // Return the JWK as stringified JSON optionally with permissions
  std::string Key::toString(bool withPerms) const {
    if (!withPerms) return jwk.toString();
    JSON::Value jsonPerms;
    jsonPerms["input"] = (bool)perms & JWK_PERM_INPUT;
    jsonPerms["output"] = (bool)perms & JWK_PERM_OUTPUT;
    jsonPerms["admin"] = (bool)perms & JWK_PERM_ADMIN;
    for (std::string s : stream) jsonPerms["stream"].append(s);
    return '[' + jwk.toString() + ',' + jsonPerms.toString() + ']';
  }

  /// Constructs a JSON web signature object from a received JSON web token packet
  JWS::JWS(const std::string & pkg, bool skipCheck) {
    if (!skipCheck) isJWS(pkg);
    size_t pos1 = pkg.find('.'), pos2 = pkg.find('.', pos1 + 1);
    std::string b64_hdr = pkg.substr(0, pos1);
    std::string b64_pld = pkg.substr(pos1 + 1, pos2 - pos1 - 1);
    hdr = JSON::fromString(Encodings::Base64::decode(b64_hdr));
    pld = JSON::fromString(Encodings::Base64::decode(b64_pld));
    sig = pkg.substr(pos2 + 1);
    sin = b64_hdr + '.' + b64_pld;
  }

  /// Delegating constructor that takes a JSON web token packet as usual as well as the desired permissions
  JWS::JWS(const std::string & pkg, const uint8_t _perms, bool skipCheck) : JWS(pkg, skipCheck) {
    perms = _perms;
  }

  /// Getters for the compact serialisation of JWS and separately a getter for the key id
  const JSON::Value & JWS::getPayload() const {
    return pld;
  }
  const std::string & JWS::getKid() const {
    return hdr["kid"].asStringRef();
  }

  /// Returns false if there is no header or signature or if header has no 'alg' member
  JWS::operator bool() const {
    if (!hdr.size() || sig.empty() || !hdr.isMember("alg")) return false;
    return true;
  }

  /// Returns true if the 'sub' field contains one or more wildcard characters
  bool JWS::hasWildcard() const {
    if (pld.isMember("sub") && pld["sub"].asStringRef().find("*") != std::string::npos) return true;
    return false;
  }

  /// Checks whether the streamname matches the subject and if the ip matches the 'ipa' claims field
  bool JWS::checkClaims(const std::string & streamName, const std::string & hostIP) const {
    // For tokens in which 'sub' is not used to derive the streamname directly there may be a single wildcard character
    uint64_t now = Util::epoch();

    std::string target;
    Socket::hostBytesToStr(hostIP.c_str(), hostIP.size(), target);
    if ((!*this) || (pld.isMember("sub") && streamName.size() && !equalsWithWildcard(streamName, pld["sub"])) ||
        (pld.isMember("ipa") && !Socket::isBinAddress(hostIP, pld["ipa"])) ||
        (pld.isMember("nbf") && now < (uint64_t)pld["nbf"].asInt()) ||
        (pld.isMember("exp") && now > (uint64_t)pld["exp"].asInt())) {
      return false;
    }
    /// \todo support geo claim using maxmind (or similar) ip database
    return true;
  }

  /// Assumes the entries in the signatures vector contains signatures we can check against.
  bool JWS::validateSignature() const {
    std::string alg = hdr["alg"].asStringRef();
    std::string kid = getKid();
    if (!kid.size()) {
      HIGH_MSG("No key id was provided-- checking if any known keys succesfully verify");
      return verify_any(sin, sig, alg);
    }

    // We need to find the key based on the key id in shared memory
    IPC::sharedPage typePage(SHM_JWK, 8 * 1024 * 1024, false, false);
    if (!typePage.mapped) {
      FAIL_MSG("Could not find page to search for a valid key-- aborting");
      return false;
    }

    Util::RelAccX keys(typePage.mapped, false);
    if (!keys.isReady()) {
      FAIL_MSG("Keypage in shared memory was not ready to be read-- aborting");
      return false;
    }

    // Search the shared memory for the key id
    Key key;
    uint64_t delled = keys.getDeleted(), max = keys.getEndPos();
    Util::RelAccXFieldData fKid = keys.getFieldData("kid");
    for (uint64_t i = delled; i < max; ++i) {
      if (kid == keys.getPointer(fKid, i)) {
        key = Key(JSON::fromString(keys.getPointer("key", i)), JSON::fromString(keys.getPointer("stream", i)),
                  keys.getInt("perms", i));
        break;
      }
    }

    // Key is set if we found a key with a matching key id, if key id was not set in the header or if the key id
    // did not match any key id in the key storage we try and verify the signature with any key in storage
    if (key) return key.matchKeyPerms(pld["sub"].asStringRef(), perms) && verify(sin, key, sig, alg);
    HIGH_MSG("Could not find matching key id-- attempting to verify with known keys");
    return verify_any(sin, sig, alg);
  }

  /// Direct call for the verify function, should only be used for testing purposes
  bool JWS::validateSignature(const Key & key) const {
    return key.matchKeyPerms(pld["sub"].asStringRef(), perms) && verify(sin, key, sig, hdr["alg"].asStringRef());
  }

  // Calls the appropriate verify function for the given algorithm with the relevant parameters.
  bool JWS::verify(const std::string & msg, const JSON::Value & key, const std::string & sig, const std::string & alg) const {
#ifdef SSL
    if (alg == "HS256") return verify_hmac(msg, key, sig, MBEDTLS_MD_SHA256);
    if (alg == "HS384") return verify_hmac(msg, key, sig, MBEDTLS_MD_SHA384);
    if (alg == "HS512") return verify_hmac(msg, key, sig, MBEDTLS_MD_SHA512);
    if (alg == "RS256") return verify_rsa(msg, key, sig, MBEDTLS_RSA_PKCS_V15, MBEDTLS_MD_SHA256);
    if (alg == "RS384") return verify_rsa(msg, key, sig, MBEDTLS_RSA_PKCS_V15, MBEDTLS_MD_SHA384);
    if (alg == "RS512") return verify_rsa(msg, key, sig, MBEDTLS_RSA_PKCS_V15, MBEDTLS_MD_SHA512);
    if (alg == "ES256") return verify_ecdsa(msg, key, sig, MBEDTLS_ECP_DP_SECP256R1);
    if (alg == "ES384") return verify_ecdsa(msg, key, sig, MBEDTLS_ECP_DP_SECP384R1);
    if (alg == "ES512") return verify_ecdsa(msg, key, sig, MBEDTLS_ECP_DP_SECP521R1);
    if (alg == "PS256") return verify_rsa(msg, key, sig, MBEDTLS_RSA_PKCS_V21, MBEDTLS_MD_SHA256);
    if (alg == "PS384") return verify_rsa(msg, key, sig, MBEDTLS_RSA_PKCS_V21, MBEDTLS_MD_SHA384);
    if (alg == "PS512") return verify_rsa(msg, key, sig, MBEDTLS_RSA_PKCS_V21, MBEDTLS_MD_SHA512);
#else
    WARN_MSG("Could not verify JWS because SSL is not enabled!")
#endif
    return false; // only reached for alg=none or some invalid type
  }

  // True is returned if signature verification succeeds for any one of the keys in shared memory and false otherwise
  bool JWS::verify_any(const std::string & msg, const std::string & sig, const std::string & alg) const {
    if (alg.empty()) {
      FAIL_MSG("Algorithm was not specified-- aborting!");
      return false;
    }

    IPC::sharedPage typePage(SHM_JWK, 8 * 1024 * 1024, false, false);
    if (!typePage.mapped) {
      FAIL_MSG("Could not open page to search for a matching signature-- aborting");
      return false;
    }

    Util::RelAccX keys(typePage.mapped, false);
    if (!keys.isReady()) {
      FAIL_MSG("Page to read keys from is not ready-- aborting");
      return false;
    }

    char aty = 0;
    if (alg[0] == 'H') aty = 'o'; // octet -> HS[256|384|512]
    if (alg[0] == 'R' || alg[0] == 'P') aty = 'R'; // RSA   -> [RS|PS][256|384|512]
    if (alg[0] == 'E') aty = 'E'; // EC    -> ES[256|384|512]
    if (aty == 0) return false;

    std::string streamName = pld["sub"].asStringRef();
    uint64_t delled = keys.getDeleted(), max = keys.getEndPos();
    for (uint64_t i = delled; i < max; ++i) {
      // Only attempt to verify if the algorithm matches the keytype based on the first characters
      char *kty = keys.getPointer("kty", i);
      if (!kty || *kty != aty) continue;

      // Retrieve the full key and attempt to verify with the respective algorithm
      Key key = Key(JSON::fromString(keys.getPointer("key", i)), JSON::fromString(keys.getPointer("stream", i)),
                    keys.getInt("perms", i));
      if (key.matchKeyPerms(streamName, perms) && verify(msg, key, sig, alg)) return true;
    }
    WARN_MSG("Could not verify the signature with known stored keys");
    return false;
  }

#ifdef SSL
  /// Returns true for a valid HMAC; accepts SHA-256, SHA-384 and SHA-512 as valid hashing modes
  bool JWS::verify_hmac(const std::string & msg, const JSON::Value & key, const std::string & sig, mbedtls_md_type_t hashMode) const {
    // Call internal function for getting the base64-encoded SHA HMAC digest
    std::string digest = Secure::digest_hmac(msg, key, getSHA(hashMode));

    HIGH_MSG("Trying to match digest [%s] to signature [%s]", digest.c_str(), sig.c_str());
    if (digest != sig) {
      WARN_MSG("Signature did not match HMAC: expected %s but got %s", sig.c_str(), digest.c_str());
      return false;
    }
    HIGH_MSG("Signature verification was successful");
    return true;
  }

  /// Returns true if the signature matches the hash (SHA-256/385/512) and RSA(-PKCS/PSS) key
  bool JWS::verify_rsa(const std::string & msg, const JSON::Value & key, const std::string & sig, int paddingMode,
                       mbedtls_md_type_t hashMode) const {
    // Parse the JSON to extract n and e to "bignum" string and return if not present
    HIGH_MSG("Verifiying RSA with key=[%s] and sig=[%s]", key.toString().c_str(), sig.c_str());
    if (!key.isMember("n") || !key.isMember("e")) {
      FAIL_MSG("The RSA key was not a valid public key-- could not verify");
      return false;
    }

    // The padding mode must be either v1.5 (0) or 2.1 (1)
    if (paddingMode != MBEDTLS_RSA_PKCS_V15 && paddingMode != MBEDTLS_RSA_PKCS_V21) {
      FAIL_MSG("The padding mode was invalid; must be either v1.5 for PKCS; or v2.1 for PSS");
      return false;
    }

    // Hash the message using SHA-256/384/512
    char hash[MBEDTLS_MD_MAX_SIZE];
    Secure::shabin(msg.data(), msg.size(), hash, getSHA(hashMode));

    // Decode the core RSA public parameters and the signature
    const std::string n = Encodings::Base64::decode(key["n"].asString());
    const std::string e = Encodings::Base64::decode(key["e"].asString());
    const std::string s = Encodings::Base64::decode(sig);

    // Setup RSA context with correct padding mode
    mbedtls_rsa_context rsa;
#if MBEDTLS_VERSION_MAJOR >= 3
    mbedtls_rsa_init(&rsa);
#else
    mbedtls_rsa_init(&rsa, paddingMode, hashMode);
#endif
    mbedtls_rsa_set_padding(&rsa, paddingMode, hashMode);

    // Import the public key variables into the context
    int ret = mbedtls_rsa_import_raw(&rsa, (const unsigned char *)n.data(), n.size(), // n
                                     nullptr, 0, // p
                                     nullptr, 0, // q
                                     nullptr, 0, // d
                                     (const unsigned char *)e.data(), e.size()); // e

    // Check if import failed and try to complete the context
    if (ret != 0 || (ret = mbedtls_rsa_complete(&rsa)) != 0) {
      FAIL_MSG("Could not import RSA using mbedtls (%d)", ret);
      mbedtls_rsa_free(&rsa);
      return false;
    }

    // Verify the signature (see RFC7515:[Example using RSASSA-PKCS1-v1_5 SHA-256] for an example)
    const size_t hash_len = getSHA(hashMode) / 8;
#if MBEDTLS_VERSION_MAJOR >= 3
    if (paddingMode == MBEDTLS_RSA_PKCS_V15) {
      ret = mbedtls_rsa_rsassa_pkcs1_v15_verify(&rsa, hashMode, static_cast<unsigned int>(hash_len),
                                                (const unsigned char *)hash, (const unsigned char *)s.data());
    } else {
      ret = mbedtls_rsa_rsassa_pss_verify(&rsa, hashMode, hash_len, (const unsigned char *)hash,
                                          (const unsigned char *)s.data());
    }
#else
    if (paddingMode == MBEDTLS_RSA_PKCS_V15) {
      ret = mbedtls_rsa_rsassa_pkcs1_v15_verify(&rsa, NULL, NULL, MBEDTLS_RSA_PUBLIC, hashMode, static_cast<unsigned int>(hash_len),
                                                (const unsigned char *)hash, (const unsigned char *)s.data());
    } else {
      ret = mbedtls_rsa_rsassa_pss_verify(&rsa, NULL, NULL, MBEDTLS_RSA_PUBLIC, hashMode, static_cast<unsigned int>(hash_len),
                                          (const unsigned char *)hash, (const unsigned char *)s.data());
    }
#endif

    // Perform cleanup regardless of success, then print the status and return accordingly
    mbedtls_rsa_free(&rsa);
    if (ret != 0) {
      char error_buf[100];
      mbedtls_strerror(ret, error_buf, 100);
      WARN_MSG("Signature verification (RSA) failed: %s", error_buf);
      return false;
    }
    HIGH_MSG("Signature verification was succesful");
    return true; // verify success for ret == 0
  }

  /// Returns true if signature verifies for the ECDSA key, message, and group (SECP[256/384/512])
  bool JWS::verify_ecdsa(const std::string & _msg, const JSON::Value & _key, const std::string & _sig, mbedtls_ecp_group_id _gid) const {
    HIGH_MSG("Verifiying ECDSA with key=[%s] and sig=[%s]", _key.toString().c_str(), _sig.c_str());
    if (!_key.isMember("crv") || !_key.isMember("x") || !_key.isMember("y")) {
      FAIL_MSG("The ECDSA public key had no valid parameters-- could not verify");
      return false;
    }

    // Decode the core ECDSA public parameters and the signature
    const std::string x = Encodings::Base64::decode(_key["x"].asString());
    const std::string y = Encodings::Base64::decode(_key["y"].asString());
    const std::string sig = Encodings::Base64::decode(_sig);

    // Construct the uncompressed point as 0x04 || x || y
    std::string pk = std::string(1, 0x04) + x + y;

    // Setup the ECDSA context, group, public key (point) and decomposed signature (r, s)
    mbedtls_ecdsa_context ecdsa;
    mbedtls_ecp_group grp;
    mbedtls_ecp_point pt;
    mbedtls_mpi r, s;

    // Initialise each of the mbedtls structures and the hash array
    mbedtls_ecdsa_init(&ecdsa);
    mbedtls_ecp_group_init(&grp);
    mbedtls_ecp_point_init(&pt);
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);
    char *hash = nullptr;

    // Parse the signature with the right size into r and s given the group id
    size_t half_sig_size = 0, hash_len = 0;
    switch (_gid) {
      case MBEDTLS_ECP_DP_SECP256R1: half_sig_size = 32, hash_len = 32; break;
      case MBEDTLS_ECP_DP_SECP384R1: half_sig_size = 48, hash_len = 48; break;
      case MBEDTLS_ECP_DP_SECP521R1: half_sig_size = 66, hash_len = 64; break;
      default: return false;
    }

    // Helper function called before returning
    auto cleanup = [&]() {
      mbedtls_ecdsa_free(&ecdsa);
      mbedtls_ecp_point_free(&pt);
      mbedtls_ecp_group_free(&grp);
      mbedtls_mpi_free(&r);
      mbedtls_mpi_free(&s);
      delete[] hash;
    };

    /// Split the signature into r and s as multi-precision integers
    const unsigned char *sbin = (const unsigned char *)sig.data();
    int ret;
    if (((ret = mbedtls_mpi_read_binary(&r, sbin, half_sig_size)) != 0) ||
        ((ret = mbedtls_mpi_read_binary(&s, sbin + half_sig_size, half_sig_size)) != 0)) {
      FAIL_MSG("Could not load the signature (as r/s) using mbedtls (%d)", ret);
      cleanup();
      return false;
    }

    // Load the group with the correct group id (SECP256R1/SECP384R1/SECP521R1)
    if ((ret = mbedtls_ecp_group_load(&grp, _gid)) != 0) {
      FAIL_MSG("Could not load the ECDSA group using mbedtls (%d)", ret);
      cleanup();
      return false;
    }

    // Parse the coordinates into an ECP point from binary format
    ret = mbedtls_ecp_point_read_binary(&grp, &pt, (const unsigned char *)pk.data(), pk.size());
    if (ret != 0 || (ret = mbedtls_ecp_check_pubkey(&grp, &pt)) != 0) {
      FAIL_MSG("Failed to import a valid ECC public point using mbedtls (%d)", ret);
      cleanup();
      return false;
    }

    // We compute the SHA-256/384/512 hash of the message
    Secure::SHA shaType;
    switch (_gid) {
      case MBEDTLS_ECP_DP_SECP256R1: shaType = Secure::SHA256; break;
      case MBEDTLS_ECP_DP_SECP384R1: shaType = Secure::SHA384; break;
      case MBEDTLS_ECP_DP_SECP521R1: shaType = Secure::SHA512; break;
      default: cleanup(); return false;
    }
    hash = new char[hash_len];
    Secure::shabin(_msg.data(), _msg.size(), hash, shaType);

    // Finally, call the verify function
    ret = mbedtls_ecdsa_verify(&grp, (const unsigned char *)hash, hash_len, &pt, &r, &s);
    if (ret != 0) {
      FAIL_MSG("Could not verify the ECDSA signature using mbedtls (%d)", ret);
      cleanup();
      return false;
    }

    HIGH_MSG("Signature verification was succesful");
    cleanup();
    return true; // verify success for ret == 0
  }
#endif
} // namespace JWT
