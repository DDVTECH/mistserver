#pragma once
#include <mist/defines.h>
#include "output.h"
#include <mbedtls/certs.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/net.h>
#include <mbedtls/ssl.h>
#include <mbedtls/timing.h>
#include <mbedtls/x509.h>

namespace Mist {

  class OutHTTPS : public Output {
    public:
      OutHTTPS(Socket::Connection & C);
      virtual ~OutHTTPS();
      void onRequest(){};
      int run();
      static bool listenMode(){return true;}
      static void init(Util::Config * cfg);
      static void listener(Util::Config & conf, int (*callback)(Socket::Connection & S));
    private:
      mbedtls_net_context client_fd;
      mbedtls_ssl_context ssl;
      static mbedtls_entropy_context entropy;
      static mbedtls_ctr_drbg_context ctr_drbg;
      static mbedtls_ssl_config sslConf;
      static mbedtls_x509_crt srvcert;
      static mbedtls_pk_context pkey;
  };
}

typedef Mist::OutHTTPS mistOut;
