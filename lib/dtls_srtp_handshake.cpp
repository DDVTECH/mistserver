#include "defines.h"
#include "dtls_srtp_handshake.h"
#include <algorithm>
#include <string.h>

/* Write mbedtls into a log file. */
#define LOG_TO_FILE 0
#if LOG_TO_FILE
#include <fstream>
#endif

/* ----------------------------------------- */

static void print_mbedtls_error(int r);
static void print_mbedtls_debug_message(void *ctx, int level, const char *file, int line, const char *str);
static int on_mbedtls_wants_to_read(void *user, unsigned char *buf,
                                    size_t len); /* Called when mbedtls wants to read data from e.g. a socket. */
static int on_mbedtls_wants_to_write(void *user, const unsigned char *buf,
                                     size_t len); /* Called when mbedtls wants to write data to e.g. a socket. */

/* ----------------------------------------- */

DTLSSRTPHandshake::DTLSSRTPHandshake() : cert(NULL), key(NULL), write_callback(NULL){
  memset((void *)&entropy_ctx, 0x00, sizeof(entropy_ctx));
  memset((void *)&rand_ctx, 0x00, sizeof(rand_ctx));
  memset((void *)&ssl_ctx, 0x00, sizeof(ssl_ctx));
  memset((void *)&ssl_conf, 0x00, sizeof(ssl_conf));
  memset((void *)&cookie_ctx, 0x00, sizeof(cookie_ctx));
  memset((void *)&timer_ctx, 0x00, sizeof(timer_ctx));
}

int DTLSSRTPHandshake::init(mbedtls_x509_crt *certificate, mbedtls_pk_context *privateKey,
                            int (*writeCallback)(const uint8_t *data, int *nbytes)){

  int r = 0;
  mbedtls_ssl_srtp_profile srtp_profiles[] ={MBEDTLS_SRTP_AES128_CM_HMAC_SHA1_80,
                                              MBEDTLS_SRTP_AES128_CM_HMAC_SHA1_32};

  if (!writeCallback){
    FAIL_MSG("No writeCallack function given.");
    r = -3;
    goto error;
  }

  if (!certificate){
    FAIL_MSG("Given certificate is null.");
    r = -5;
    goto error;
  }

  if (!privateKey){
    FAIL_MSG("Given key is null.");
    r = -10;
    goto error;
  }

  cert = certificate;
  key = privateKey;

  /* init the contexts */
  mbedtls_entropy_init(&entropy_ctx);
  mbedtls_ctr_drbg_init(&rand_ctx);
  mbedtls_ssl_init(&ssl_ctx);
  mbedtls_ssl_config_init(&ssl_conf);
  mbedtls_ssl_cookie_init(&cookie_ctx);

  /* seed and setup the random number generator */
  r = mbedtls_ctr_drbg_seed(&rand_ctx, mbedtls_entropy_func, &entropy_ctx,
                            (const unsigned char *)"mist-srtp", 9);
  if (0 != r){
    print_mbedtls_error(r);
    r = -20;
    goto error;
  }

  /* load defaults into our ssl_conf */
  r = mbedtls_ssl_config_defaults(&ssl_conf, MBEDTLS_SSL_IS_SERVER, MBEDTLS_SSL_TRANSPORT_DATAGRAM,
                                  MBEDTLS_SSL_PRESET_DEFAULT);
  if (0 != r){
    print_mbedtls_error(r);
    r = -30;
    goto error;
  }

  mbedtls_ssl_conf_authmode(&ssl_conf, MBEDTLS_SSL_VERIFY_NONE);
  mbedtls_ssl_conf_rng(&ssl_conf, mbedtls_ctr_drbg_random, &rand_ctx);
  mbedtls_ssl_conf_dbg(&ssl_conf, print_mbedtls_debug_message, stdout);
  mbedtls_ssl_conf_ca_chain(&ssl_conf, cert, NULL);
  mbedtls_ssl_conf_cert_profile(&ssl_conf, &mbedtls_x509_crt_profile_default);
  mbedtls_debug_set_threshold(10);

  /* enable SRTP */
  r = mbedtls_ssl_conf_dtls_srtp_protection_profiles(&ssl_conf, srtp_profiles,
                                                     sizeof(srtp_profiles) / sizeof(srtp_profiles[0]));
  if (0 != r){
    print_mbedtls_error(r);
    r = -40;
    goto error;
  }

  /* cert certificate chain + key, so we can verify the client-hello signed data */
  r = mbedtls_ssl_conf_own_cert(&ssl_conf, cert, key);
  if (0 != r){
    print_mbedtls_error(r);
    r = -50;
    goto error;
  }

  /* cookie setup (e.g. to prevent ddos). */
  r = mbedtls_ssl_cookie_setup(&cookie_ctx, mbedtls_ctr_drbg_random, &rand_ctx);
  if (0 != r){
    print_mbedtls_error(r);
    r = -60;
    goto error;
  }

  /* register callbacks for dtls cookies (server only). */
  mbedtls_ssl_conf_dtls_cookies(&ssl_conf, mbedtls_ssl_cookie_write, mbedtls_ssl_cookie_check, &cookie_ctx);

  /* setup the ssl context for use. note that ssl_conf will be referenced internall by the context and therefore should be kept around. */
  r = mbedtls_ssl_setup(&ssl_ctx, &ssl_conf);
  if (0 != r){
    print_mbedtls_error(r);
    r = -70;
    goto error;
  }

  /* set bio handlers */
  mbedtls_ssl_set_bio(&ssl_ctx, (void *)this, on_mbedtls_wants_to_write, on_mbedtls_wants_to_read, NULL);

  /* set temp id, just adds some exta randomness */
  {
    std::string remote_id = "mist";
    r = mbedtls_ssl_set_client_transport_id(&ssl_ctx, (const unsigned char *)remote_id.c_str(),
                                            remote_id.size());
    if (0 != r){
      print_mbedtls_error(r);
      r = -80;
      goto error;
    }
  }

  /* set timer callbacks */
  mbedtls_ssl_set_timer_cb(&ssl_ctx, &timer_ctx, mbedtls_timing_set_delay, mbedtls_timing_get_delay);

  write_callback = writeCallback;

error:

  if (r < 0){shutdown();}

  return r;
}

int DTLSSRTPHandshake::shutdown(){

  /* cleanup the refs from the settings. */
  cert = NULL;
  key = NULL;
  buffer.clear();
  cipher.clear();
  remote_key.clear();
  remote_salt.clear();
  local_key.clear();
  local_salt.clear();

  /* free our contexts; we do not free the `settings.cert` and `settings.key` as they are owned by the user of this class. */
  mbedtls_entropy_free(&entropy_ctx);
  mbedtls_ctr_drbg_free(&rand_ctx);
  mbedtls_ssl_free(&ssl_ctx);
  mbedtls_ssl_config_free(&ssl_conf);
  mbedtls_ssl_cookie_free(&cookie_ctx);

  return 0;
}

/* ----------------------------------------- */

int DTLSSRTPHandshake::parse(const uint8_t *data, size_t nbytes){

  if (NULL == data){
    ERROR_MSG("Given `data` is NULL.");
    return -1;
  }

  if (0 == nbytes){
    ERROR_MSG("Given nbytes is 0.");
    return -2;
  }

  if (MBEDTLS_SSL_HANDSHAKE_OVER == ssl_ctx.state){
    ERROR_MSG("Already finished the handshake.");
    return -3;
  }

  /* copy incoming data into a temporary buffer which is read via our `bio` read function. */
  int r = 0;
  std::copy(data, data + nbytes, std::back_inserter(buffer));

  do{

    r = mbedtls_ssl_handshake(&ssl_ctx);

    switch (r){
    /* 0 = handshake done. */
    case 0:{
      if (0 != extractKeyingMaterial()){
        ERROR_MSG("Failed to extract keying material after handshake was done.");
        return -2;
      }
      return 0;
    }
      /* see the dtls server example; this is used to prevent certain attacks (ddos) */
    case MBEDTLS_ERR_SSL_HELLO_VERIFY_REQUIRED:{
      if (0 != resetSession()){
        ERROR_MSG(
            "Failed to reset the session which is necessary when we need to verify the HELLO.");
        return -3;
      }
      break;
    }
    case MBEDTLS_ERR_SSL_WANT_READ:{
      DONTEVEN_MSG(
          "mbedtls wants a bit more data before it can continue parsing the DTLS handshake.");
      break;
    }
    default:{
      ERROR_MSG("A serious mbedtls error occured.");
      print_mbedtls_error(r);
      return -2;
    }
    }
  }while (MBEDTLS_ERR_SSL_WANT_WRITE == r);

  return 0;
}

/* ----------------------------------------- */

int DTLSSRTPHandshake::resetSession(){

  std::string remote_id = "mist"; /* @todo for now we hardcoded this... */
  int r = 0;

  r = mbedtls_ssl_session_reset(&ssl_ctx);
  if (0 != r){
    print_mbedtls_error(r);
    return -1;
  }

  r = mbedtls_ssl_set_client_transport_id(&ssl_ctx, (const unsigned char *)remote_id.c_str(),
                                          remote_id.size());
  if (0 != r){
    print_mbedtls_error(r);
    return -2;
  }

  buffer.clear();

  return 0;
}

/*
  master key is 128 bits => 16 bytes.
  master salt is 112 bits => 14 bytes
*/
int DTLSSRTPHandshake::extractKeyingMaterial(){

  int r = 0;
  uint8_t keying_material[MBEDTLS_DTLS_SRTP_MAX_KEY_MATERIAL_LENGTH] ={};
  size_t keying_material_len = sizeof(keying_material);

  r = mbedtls_ssl_get_dtls_srtp_key_material(&ssl_ctx, keying_material, &keying_material_len);
  if (0 != r){
    print_mbedtls_error(r);
    return -1;
  }

  /* @todo following code is for server mode only */
  mbedtls_ssl_srtp_profile srtp_profile = mbedtls_ssl_get_dtls_srtp_protection_profile(&ssl_ctx);
  switch (srtp_profile){
  case MBEDTLS_SRTP_AES128_CM_HMAC_SHA1_80:{
    cipher = "SRTP_AES128_CM_SHA1_80";
    break;
  }
  case MBEDTLS_SRTP_AES128_CM_HMAC_SHA1_32:{
    cipher = "SRTP_AES128_CM_SHA1_32";
    break;
  }
  default:{
    ERROR_MSG("Unhandled SRTP profile, cannot extract keying material.");
    return -6;
  }
  }

  remote_key.assign((char *)(&keying_material[0]) + 0, 16);
  local_key.assign((char *)(&keying_material[0]) + 16, 16);
  remote_salt.assign((char *)(&keying_material[0]) + 32, 14);
  local_salt.assign((char *)(&keying_material[0]) + 46, 14);

  DONTEVEN_MSG("Extracted the DTLS SRTP keying material with cipher %s.", cipher.c_str());
  DONTEVEN_MSG("Remote DTLS SRTP key size is %zu.", remote_key.size());
  DONTEVEN_MSG("Remote DTLS SRTP salt size is %zu.", remote_salt.size());
  DONTEVEN_MSG("Local DTLS SRTP key size is %zu.", local_key.size());
  DONTEVEN_MSG("Local DTLS SRTP salt size is %zu.", local_salt.size());

  return 0;
}

/* ----------------------------------------- */

/*

  This function is called by mbedtls whenever it wants to read
  some data. The documentation states the following: "For DTLS,
  you need to provide either a non-NULL f_recv_timeout
  callback, or a f_recv that doesn't block." As this
  implementation is completely decoupled from any I/O and uses
  a "push" model instead of a "pull" model we have to copy new
  input bytes into a temporary buffer (see parse), but we act
  as if we were using a non-blocking socket, which means:

  - we return MBETLS_ERR_SSL_WANT_READ when there is no data left to read
  - when there is data in our temporary buffer, we read from that

*/
static int on_mbedtls_wants_to_read(void *user, unsigned char *buf, size_t len){

  DTLSSRTPHandshake *hs = static_cast<DTLSSRTPHandshake *>(user);
  if (NULL == hs){
    ERROR_MSG("Failed to cast the user pointer into a DTLSSRTPHandshake.");
    return -1;
  }

  /* figure out how much we can read. */
  if (hs->buffer.size() == 0){return MBEDTLS_ERR_SSL_WANT_READ;}

  size_t nbytes = hs->buffer.size();
  if (nbytes > len){nbytes = len;}

  /* "read" into the given buffer. */
  memcpy(buf, &hs->buffer[0], nbytes);
  hs->buffer.erase(hs->buffer.begin(), hs->buffer.begin() + nbytes);

  return (int)nbytes;
}

static int on_mbedtls_wants_to_write(void *user, const unsigned char *buf, size_t len){

  DTLSSRTPHandshake *hs = static_cast<DTLSSRTPHandshake *>(user);
  if (!hs){
    FAIL_MSG("Failed to cast the user pointer into a DTLSSRTPHandshake.");
    return -1;
  }

  if (!hs->write_callback){
    FAIL_MSG("The `write_callback` member is NULL.");
    return -2;
  }

  int nwritten = (int)len;
  if (0 != hs->write_callback(buf, &nwritten)){
    FAIL_MSG("Failed to write some DTLS handshake data.");
    return -3;
  }

  if (nwritten != (int)len){
    FAIL_MSG("The DTLS-SRTP handshake listener MUST write all the data.");
    return -4;
  }

  return nwritten;
}

/* ----------------------------------------- */

static void print_mbedtls_error(int r){
  char buf[1024] ={};
  mbedtls_strerror(r, buf, sizeof(buf));
  ERROR_MSG("mbedtls error: %s", buf);
}

static void print_mbedtls_debug_message(void *ctx, int level, const char *file, int line, const char *str){
  DONTEVEN_MSG("%s:%04d: %.*s", file, line, (int)strlen(str) - 1, str);

#if LOG_TO_FILE
  static std::ofstream ofs;
  if (!ofs.is_open()){ofs.open("mbedtls.log", std::ios::out);}
  if (!ofs.is_open()){return;}
  ofs << str;
  ofs.flush();
#endif
}

/* ---------------------------------------- */
