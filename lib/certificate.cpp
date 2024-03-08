#include "certificate.h"
#include "defines.h"
#include <string.h>
#include <ctime>


Certificate::Certificate(){
  mbedtls_pk_init(&key);
  mbedtls_x509_crt_init(&cert);
}

int Certificate::init(const std::string &countryName, const std::string &organization,
                      const std::string &commonName){

  mbedtls_ctr_drbg_context rand_ctx ={};
  mbedtls_entropy_context entropy_ctx ={};
  mbedtls_x509write_cert write_cert ={};
  mbedtls_rsa_context *rsa_ctx;

  const char *personalisation = "mbedtls-self-signed-key";
  std::string subject_name = "C=" + countryName + ",O=" + organization + ",CN=" + commonName;
  time_t time_from ={0};
  time_t time_to ={0};
  char time_from_str[20] ={0};
  char time_to_str[20] ={0};
  mbedtls_mpi serial_mpi ={0};
  char serial_hex[17] ={0};
  uint64_t serial_num = 0;
  uint8_t *serial_ptr = (uint8_t *)&serial_num;
  int r = 0;
  int i = 0;
  uint8_t buf[4096] ={0};

  // validate
  if (countryName.empty()){
    FAIL_MSG("Given `countryName`, C=<countryName>, is empty.");
    r = -1;
    goto error;
  }
  if (organization.empty()){
    FAIL_MSG("Given `organization`, O=<organization>, is empty.");
    r = -2;
    goto error;
  }
  if (commonName.empty()){
    FAIL_MSG("Given `commonName`, CN=<commonName>, is empty.");
    r = -3;
    goto error;
  }

  // initialize random number generator
  mbedtls_ctr_drbg_init(&rand_ctx);
  mbedtls_entropy_init(&entropy_ctx);
  r = mbedtls_ctr_drbg_seed(&rand_ctx, mbedtls_entropy_func, &entropy_ctx,
                            (const unsigned char *)personalisation, strlen(personalisation));
  if (0 != r){
    FAIL_MSG("Failed to initialize and seed the entropy context.");
    r = -10;
    goto error;
  }

  // initialize the public key context
  r = mbedtls_pk_setup(&key, mbedtls_pk_info_from_type(MBEDTLS_PK_RSA));
  if (0 != r){
    FAIL_MSG("Faild to initialize the PK context.");
    r = -20;
    goto error;
  }

  //This call returns a reference to the existing RSA context inside the key.
  //Hence, it does not need to be cleaned up later.
  rsa_ctx = mbedtls_pk_rsa(key);
  if (NULL == rsa_ctx){
    FAIL_MSG("Failed to get the RSA context from from the public key context (key).");
    r = -30;
    goto error;
  }

  r = mbedtls_rsa_gen_key(rsa_ctx, mbedtls_ctr_drbg_random, &rand_ctx, 2048, 65537);
  if (0 != r){
    FAIL_MSG("Failed to generate a private key.");
    r = -40;
    goto error;
  }

  // calc the valid from and until time.
  time_from = time(NULL);
  time_from = (time_from < 1000000000) ? 1000000000 : time_from;
  time_to = time_from + (60 * 60 * 24 * 365); // valid for a year

  if (time_to < time_from){time_to = INT_MAX;}

  r = strftime(time_from_str, sizeof(time_from_str), "%Y%m%d%H%M%S", gmtime(&time_from));
  if (0 == r){
    FAIL_MSG("Failed to generate the valid-from time string.");
    r = -50;
    goto error;
  }

  r = strftime(time_to_str, sizeof(time_to_str), "%Y%m%d%H%M%S", gmtime(&time_to));
  if (0 == r){
    FAIL_MSG("Failed to generate the valid-to time string.");
    r = -60;
    goto error;
  }

  r = mbedtls_ctr_drbg_random((void *)&rand_ctx, (uint8_t *)&serial_num, sizeof(serial_num));
  if (0 != r){
    FAIL_MSG("Failed to generate a random u64.");
    r = -70;
    goto error;
  }

  for (i = 0; i < 8; ++i){sprintf(serial_hex + (i * 2), "%02x", serial_ptr[i]);}

  // start creating the certificate
  mbedtls_x509write_crt_init(&write_cert);
  mbedtls_x509write_crt_set_md_alg(&write_cert, MBEDTLS_MD_SHA256);
  mbedtls_x509write_crt_set_issuer_key(&write_cert, &key);
  mbedtls_x509write_crt_set_subject_key(&write_cert, &key);

  r = mbedtls_x509write_crt_set_subject_name(&write_cert, subject_name.c_str());
  if (0 != r){
    FAIL_MSG("Failed to set the subject name.");
    r = -80;
    goto error;
  }

  r = mbedtls_x509write_crt_set_issuer_name(&write_cert, subject_name.c_str());
  if (0 != r){
    FAIL_MSG("Failed to set the issuer name.");
    r = -90;
    goto error;
  }

  r = mbedtls_x509write_crt_set_validity(&write_cert, time_from_str, time_to_str);
  if (0 != r){
    FAIL_MSG("Failed to set the x509 validity string.");
    r = -100;
    goto error;
  }

  r = mbedtls_x509write_crt_set_basic_constraints(&write_cert, 0, -1);
  if (0 != r){
    FAIL_MSG("Failed ot set the basic constraints for the certificate.");
    r = -110;
    goto error;
  }

  r = mbedtls_x509write_crt_set_subject_key_identifier(&write_cert);
  if (0 != r){
    FAIL_MSG("Failed to set the subjectKeyIdentifier.");
    r = -120;
    goto error;
  }

  r = mbedtls_x509write_crt_set_authority_key_identifier(&write_cert);
  if (0 != r){
    FAIL_MSG("Failed to set the authorityKeyIdentifier.");
    r = -130;
    goto error;
  }

  // set certificate serial; mpi is used to perform i/o
  mbedtls_mpi_init(&serial_mpi);
  mbedtls_mpi_read_string(&serial_mpi, 16, serial_hex);
  r = mbedtls_x509write_crt_set_serial(&write_cert, &serial_mpi);
  if (0 != r){
    FAIL_MSG("Failed to set the certificate serial.");
    r = -140;
    goto error;
  }

  // write the certificate into a PEM structure
  r = mbedtls_x509write_crt_pem(&write_cert, buf, sizeof(buf), mbedtls_ctr_drbg_random, &rand_ctx);
  if (0 != r){
    FAIL_MSG("Failed to create the PEM data from the x509 write structure.");
    r = -150;
    goto error;
  }

  // convert the PEM data into out `mbedtls_x509_cert` member.
  // len should be PEM including the string null terminating
  // char.  @todo there must be a way to convert the write
  // struct into a `mbedtls_x509_cert` w/o calling this parse
  // function.
  r = mbedtls_x509_crt_parse(&cert, (const unsigned char *)buf, strlen((char *)buf) + 1);
  if (0 != r){
    mbedtls_strerror(r, (char *)buf, sizeof(buf));
    FAIL_MSG("Failed to convert the mbedtls_x509write_crt into a mbedtls_x509_crt: %s", buf);
    r = -160;
    goto error;
  }

error:

  // cleanup
  mbedtls_ctr_drbg_free(&rand_ctx);
  mbedtls_entropy_free(&entropy_ctx);
  mbedtls_x509write_crt_free(&write_cert);
  mbedtls_mpi_free(&serial_mpi);
  return r;
}

Certificate::~Certificate(){
  mbedtls_pk_free(&key);
  mbedtls_x509_crt_free(&cert);
}

/// Loads a single file into the certificate. Returns true on success.
bool Certificate::loadCert(const std::string & certFile){
  if (!certFile.size()){return true;}
  return mbedtls_x509_crt_parse_file(&cert, certFile.c_str()) == 0;
}

/// Loads a single key. Returns true on success.
bool Certificate::loadKey(const std::string & keyFile){
  if (!keyFile.size()){return true;}
  return  mbedtls_pk_parse_keyfile(&key, keyFile.c_str(), 0) == 0;
}

/// Calculates SHA256 fingerprint over the loaded certificate(s)
/// Returns the fingerprint as hex-string.
std::string Certificate::getFingerprintSha256() const{
  uint8_t fingerprint_raw[32] ={};
  uint8_t fingerprint_hex[128] ={};
  mbedtls_sha256(cert.raw.p, cert.raw.len, fingerprint_raw, 0);

  for (int i = 0; i < 32; ++i){
    sprintf((char *)(fingerprint_hex + (i * 3)), ":%02X", (int)fingerprint_raw[i]);
  }

  fingerprint_hex[32 * 3] = '\0';
  std::string result = std::string((char *)fingerprint_hex + 1, (32 * 3) - 1);
  return result;
}
