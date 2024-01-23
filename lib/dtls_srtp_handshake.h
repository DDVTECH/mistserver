#pragma once

#include <deque>
#include <mbedtls/version.h>
#if MBEDTLS_VERSION_MAJOR == 2
#include <mbedtls/certs.h>
#include <mbedtls/config.h>
#else
#include <mbedtls/build_info.h>
//#include <mbedtls/pk.h>
#endif
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/debug.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/ssl.h>
#include <mbedtls/ssl_cookie.h>
#include <mbedtls/timing.h>
#include <mbedtls/x509.h>
#include <stdint.h>

/* ----------------------------------------- */

class DTLSSRTPHandshake{
public:
  DTLSSRTPHandshake();
  int init(mbedtls_x509_crt *certificate, mbedtls_pk_context *privateKey,
           int (*writeCallback)(const uint8_t *data,
                                int *nbytes)); // writeCallback should return 0 on succes < 0 on error.
                                               // nbytes holds the number of bytes to be sent and needs
                                               // to be set to the number of bytes actually sent.
  int shutdown();
  int parse(const uint8_t *data, size_t nbytes);
  bool hasKeyingMaterial();

private:
  int extractKeyingMaterial();
  int resetSession();

private:
  mbedtls_x509_crt *cert; /* Certificate, we do not own the key. Make sure it's kept alive during the livetime of this class instance. */
  mbedtls_pk_context *key; /* Private key, we do not own the key. Make sure it's kept alive during the livetime of this class instance. */
  mbedtls_entropy_context entropy_ctx;
  mbedtls_ctr_drbg_context rand_ctx;
  mbedtls_ssl_context ssl_ctx;
  mbedtls_ssl_config ssl_conf;
  mbedtls_ssl_cookie_ctx cookie_ctx;
  mbedtls_timing_delay_context timer_ctx;
#if HAVE_UPSTREAM_MBEDTLS_SRTP
  unsigned char master_secret[48];
  unsigned char randbytes[64];
  mbedtls_tls_prf_types tls_prf_type;
#if MBEDTLS_VERSION_MAJOR > 2
  static void dtlsExtractKeyData( void *user,
                              mbedtls_ssl_key_export_type type,
                              const unsigned char *ms,
                              size_t,
                              const unsigned char client_random[32],
                              const unsigned char server_random[32],
                              mbedtls_tls_prf_types tls_prf_type );
#else
  static int dtlsExtractKeyData( void *user,
                              const unsigned char *ms,
                              const unsigned char *,
                              size_t,
                              size_t,
                              size_t,
                              const unsigned char client_random[32],
                              const unsigned char server_random[32],
                              mbedtls_tls_prf_types tls_prf_type );
#endif
#endif
public:
  int (*write_callback)(const uint8_t *data, int *nbytes);
  std::deque<uint8_t> buffer; /* Accessed from BIO callbback. We copy the bytes you pass into `parse()` into this
                                 temporary buffer which is read by a trigger to `mbedlts_ssl_handshake()`. */
  std::string cipher; /* selected SRTP cipher. */
  std::string remote_key;
  std::string remote_salt;
  std::string local_key;
  std::string local_salt;
};

/* ----------------------------------------- */

inline bool DTLSSRTPHandshake::hasKeyingMaterial(){
  return (0 != remote_key.size() && 0 != remote_salt.size() && 0 != local_key.size() &&
          0 != local_salt.size());
}

/* ----------------------------------------- */
