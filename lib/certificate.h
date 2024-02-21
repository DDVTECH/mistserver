#pragma once
/*

  MBEDTLS BASED CERTIFICATE
  =========================

  This class can be used to generate a self-signed x509
  certificate which enables you to perform secure
  communication. This certificate uses a 2048 bits RSA key.

 */
#include <mbedtls/version.h>
#if MBEDTLS_VERSION_MAJOR > 2
#include <mbedtls/build_info.h>
#else
#include <mbedtls/config.h>
#endif
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/md.h>
#include <mbedtls/sha256.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/x509_csr.h>
#include <string>

class Certificate{
public:
  Certificate();
  bool loadCert(const std::string & certFile);
  bool loadKey(const std::string & certFile);
  int init(const std::string &countryName, const std::string &organization, const std::string &commonName);
  ~Certificate();
  std::string getFingerprintSha256() const;

public:
  mbedtls_x509_crt cert;
  mbedtls_pk_context key;       /* key context, stores private and public key. */
private:
  mbedtls_ctr_drbg_context rand_ctx;
};
