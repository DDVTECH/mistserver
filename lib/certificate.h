#pragma once
/*

  MBEDTLS BASED CERTIFICATE
  =========================

  This class can be used to generate a self-signed x509
  certificate which enables you to perform secure
  communication. This certificate uses a 2048 bits RSA key.

 */

#include <mbedtls/config.h>
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
  int init(const std::string &countryName, const std::string &organization, const std::string &commonName);
  int shutdown();
  std::string getFingerprintSha256();

public:
  mbedtls_x509_crt cert;
  mbedtls_pk_context key;       /* key context, stores private and public key. */
  mbedtls_rsa_context *rsa_ctx; /* rsa context, stored in key_ctx */
};
