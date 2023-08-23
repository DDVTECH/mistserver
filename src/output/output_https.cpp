#include "output_https.h"
#include <mist/procs.h>

namespace Mist{
  mbedtls_entropy_context OutHTTPS::entropy;
  mbedtls_ctr_drbg_context OutHTTPS::ctr_drbg;
  mbedtls_ssl_config OutHTTPS::sslConf;
  mbedtls_x509_crt OutHTTPS::srvcert;
  mbedtls_pk_context OutHTTPS::pkey;

  void OutHTTPS::init(Util::Config *cfg){
    Output::init(cfg);
    capa["name"] = "HTTPS";
    capa["friendly"] = "HTTPS (HTTP+TLS)";
    capa["desc"] = "HTTPS connection handler, provides all enabled HTTP-based outputs";
    capa["provides"] = "HTTP";
    capa["protocol"] = "https://";
    capa["required"]["cert"]["name"] = "Certificate";
    capa["required"]["cert"]["help"] = "(Root) certificate(s) file(s) to append to chain";
    capa["required"]["cert"]["option"] = "--cert";
    capa["required"]["cert"]["short"] = "C";
    capa["required"]["cert"]["default"] = "";
    capa["required"]["cert"]["type"] = "str";
    capa["required"]["key"]["name"] = "Key";
    capa["required"]["key"]["help"] = "Private key for SSL";
    capa["required"]["key"]["option"] = "--key";
    capa["required"]["key"]["short"] = "K";
    capa["required"]["key"]["default"] = "";
    capa["required"]["key"]["type"] = "str";

    capa["optional"]["wrappers"]["name"] = "Active players";
    capa["optional"]["wrappers"]["help"] = "Which players are attempted and in what order.";
    capa["optional"]["wrappers"]["default"] = "";
    capa["optional"]["wrappers"]["type"] = "ord_multi_sel";
    capa["optional"]["wrappers"]["allowed"].append("html5");
    capa["optional"]["wrappers"]["allowed"].append("videojs");
    capa["optional"]["wrappers"]["allowed"].append("dashjs");
    capa["optional"]["wrappers"]["allowed"].append("flash_strobe");
    capa["optional"]["wrappers"]["allowed"].append("silverlight");
    capa["optional"]["wrappers"]["allowed"].append("img");
    capa["optional"]["wrappers"]["option"] = "--wrappers";
    capa["optional"]["wrappers"]["short"] = "w";
    cfg->addConnectorOptions(4433, capa);
    cfg->addOption("nostreamtext",
                   JSON::fromString("{\"arg\":\"string\", \"default\":\"\", "
                                    "\"short\":\"t\",\"long\":\"nostreamtext\",\"help\":\"Text or "
                                    "HTML to display when streams are unavailable.\"}"));
    capa["optional"]["nostreamtext"]["name"] = "Stream unavailable text";
    capa["optional"]["nostreamtext"]["help"] =
        "Text or HTML to display when streams are unavailable.";
    capa["optional"]["nostreamtext"]["default"] = "";
    capa["optional"]["nostreamtext"]["type"] = "str";
    capa["optional"]["nostreamtext"]["option"] = "--nostreamtext";
    cfg->addOption("pubaddr",
                   JSON::fromString("{\"arg\":\"string\", \"default\":\"\", "
                                    "\"short\":\"A\",\"long\":\"public-address\",\"help\":\"Full "
                                    "public address this output is available as.\"}"));
    capa["optional"]["pubaddr"]["name"] = "Public address";
    capa["optional"]["pubaddr"]["help"] =
        "Full public address this output is available as, if being proxied";
    capa["optional"]["pubaddr"]["default"] = "";
    capa["optional"]["pubaddr"]["type"] = "inputlist";
    capa["optional"]["pubaddr"]["option"] = "--public-address";
    config = cfg;
  }

  OutHTTPS::OutHTTPS(Socket::Connection &C) : Output(C){
    int ret;
    mbedtls_net_init(&client_fd);
    client_fd.fd = C.getSocket();
    mbedtls_ssl_init(&ssl);
    if ((ret = mbedtls_ctr_drbg_reseed(&ctr_drbg, (const unsigned char *)"child", 5)) != 0){
      FAIL_MSG("Could not reseed");
      C.close();
      return;
    }

    // Set up the SSL connection
    if ((ret = mbedtls_ssl_setup(&ssl, &sslConf)) != 0){
      FAIL_MSG("Could not set up SSL connection");
      C.close();
      return;
    }

    // Inform mbedtls how we'd like to use the connection (uses default bio handlers)
    // We tell it to use non-blocking IO here
    mbedtls_net_set_nonblock(&client_fd);
    mbedtls_ssl_set_bio(&ssl, &client_fd, mbedtls_net_send, mbedtls_net_recv, NULL);
    // do the SSL handshake
    while ((ret = mbedtls_ssl_handshake(&ssl)) != 0){
      if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE){
        char error_buf[200];
        mbedtls_strerror(ret, error_buf, 200);
        MEDIUM_MSG("Could not handshake, SSL error: %s (%d)", error_buf, ret);
        Util::logExitReason(ER_READ_START_FAILURE, "Could not handshake, SSL error: %s (%d)", error_buf, ret);
        C.close();
        return;
      }else{
        Util::sleep(20);
      }
    }
    HIGH_MSG("Started SSL connection handler");
  }

  int OutHTTPS::run(){
    unsigned char buf[1024 * 4]; // 4k internal buffer
    int ret;

    // Start a MistOutHTTP process, connected to this SSL connection
    int fderr = 2;
    int fd[2];
    if (socketpair(PF_LOCAL, SOCK_STREAM, 0, fd) != 0){
      FAIL_MSG("Could not open anonymous socket for SSL<->HTTP connection!");
      Util::logExitReason(ER_READ_START_FAILURE, "Could not open anonymous socket for SSL<->HTTP connection!");
      return 1;
    }
    std::deque<std::string> args;
    args.push_back(Util::getMyPath() + "MistOutHTTP");
    args.push_back("--ip");
    args.push_back(myConn.getHost());
    if (config->getString("nostreamtext").size()){
      args.push_back("--nostreamtext");
      args.push_back(config->getString("nostreamtext"));
    }
    if (config->getOption("pubaddr", true).size()){
      JSON::Value pubAddrs = config->getOption("pubaddr", true);
      jsonForEach(pubAddrs, jIt){
        args.push_back("--public-address");
        args.push_back(jIt->asStringRef());
      }
    }
    args.push_back("");
    Util::Procs::socketList.insert(fd[0]);
    setenv("MIST_BOUND_ADDR", myConn.getBoundAddress().c_str(), 1);
    pid_t http_proc = Util::Procs::StartPiped(args, &(fd[1]), &(fd[1]), &fderr);
    unsetenv("MIST_BOUND_ADDR");
    close(fd[1]);
    if (http_proc < 2){
      FAIL_MSG("Could not spawn MistOutHTTP process for SSL connection!");
      Util::logExitReason(ER_EXEC_FAILURE, "Could not spawn MistOutHTTP process for SSL connection!");
      return 1;
    }
    Socket::Connection http(fd[0]);
    http.setBlocking(false);
    Socket::Buffer &http_buf = http.Received();
    http_buf.splitter.clear();

    // pass data back and forth between the SSL connection and HTTP process while connected
    while (config->is_active && http){
      bool activity = false;
      // attempt to read SSL data, pass to HTTP
      ret = mbedtls_ssl_read(&ssl, buf, sizeof(buf));
      if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE){
        if (ret <= 0){
          HIGH_MSG("SSL disconnect!");
          Util::logExitReason(ER_CLEAN_REMOTE_CLOSE, "SSL client disconnected");
          break;
        }
        // we received ret bytes of data to pass on. Do so.
        activity = true;
        http.SendNow((const char *)buf, ret);
      }

      // attempt to read HTTP data, pass to SSL
      if (http.spool() || http_buf.size()){
        // We have data - pass it on
        activity = true;
        while (http_buf.size() && http){
          int toSend = http_buf.get().size();
          int done = 0;
          while (done < toSend){
            ret = mbedtls_ssl_write(&ssl, (const unsigned char *)http_buf.get().data() + done, toSend - done);
            if (ret == MBEDTLS_ERR_NET_CONN_RESET || ret == MBEDTLS_ERR_SSL_CLIENT_RECONNECT){
              HIGH_MSG("SSL disconnect!");
              Util::logExitReason(ER_CLEAN_REMOTE_CLOSE, "SSL client disconnected");
              http.close();
              break;
            }
            if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE){
              done += ret;
            }else{
              Util::sleep(20);
            }
          }
          http_buf.get().clear();
        }
      }
      if (!activity){Util::sleep(20);}
    }
    // close the HTTP process (close stdio, kill its PID)
    http.close();
    uint16_t waiting = 0;
    while (++waiting < 50){
      if (!Util::Procs::isRunning(http_proc)){break;}
      Util::sleep(100);
    }
    return 0;
  }

  OutHTTPS::~OutHTTPS(){
    HIGH_MSG("Ending SSL connection handler");
    // close when we're done
    mbedtls_ssl_close_notify(&ssl);
    mbedtls_ssl_free(&ssl);
    mbedtls_net_free(&client_fd);
    myConn.close();
  }

  /// Listens for HTTPS requests, accepting them and connecting them to a HTTP socket
  void OutHTTPS::listener(Util::Config &conf, int (*callback)(Socket::Connection &S)){
    if (config->getOption("cert", true).size() < 2 || config->getOption("key", true).size() < 2){
      FAIL_MSG("The cert/key required options were not passed!");
      return;
    }

    // Declare and set up all required mbedtls structures
    int ret;
    mbedtls_ssl_config_init(&sslConf);
    mbedtls_entropy_init(&entropy);
    mbedtls_pk_init(&pkey);
    mbedtls_x509_crt_init(&srvcert);
    mbedtls_ctr_drbg_init(&ctr_drbg);

    // seed the rng
    if ((ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                                     (const unsigned char *)APPNAME, strlen(APPNAME))) != 0){
      FAIL_MSG("Could not seed the random number generator!");
    }

    // Read certificate chain(s) from cmdline option(s)
    JSON::Value certs = config->getOption("cert", true);
    jsonForEach(certs, it){
      if (it->asStringRef().size()){// Ignore empty entries (default is empty)
        ret = mbedtls_x509_crt_parse_file(&srvcert, it->asStringRef().c_str());
        if (ret != 0){
          WARN_MSG("Could not load any certificates from file: %s", it->asStringRef().c_str());
        }
      }
    }

    // Read key from cmdline option
    ret = mbedtls_pk_parse_keyfile(&pkey, config->getString("key").c_str(), 0);
    if (ret != 0){
      FAIL_MSG("Could not load any keys from file: %s", config->getString("key").c_str());
      return;
    }

    if ((ret = mbedtls_ssl_config_defaults(&sslConf, MBEDTLS_SSL_IS_SERVER, MBEDTLS_SSL_TRANSPORT_STREAM,
                                           MBEDTLS_SSL_PRESET_DEFAULT)) != 0){
      FAIL_MSG("SSL config defaults failed");
      return;
    }
    mbedtls_ssl_conf_rng(&sslConf, mbedtls_ctr_drbg_random, &ctr_drbg);
    mbedtls_ssl_conf_ca_chain(&sslConf, srvcert.next, NULL);
    if ((ret = mbedtls_ssl_conf_own_cert(&sslConf, &srvcert, &pkey)) != 0){
      FAIL_MSG("SSL config own certificate failed");
      return;
    }

    Output::listener(conf, callback);

    // Free all the mbedtls structures
    mbedtls_x509_crt_free(&srvcert);
    mbedtls_pk_free(&pkey);
    mbedtls_ssl_config_free(&sslConf);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);
  }
}// namespace Mist
