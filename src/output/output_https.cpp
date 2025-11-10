#include "output_https.h"
#include <mist/procs.h>

namespace Mist{
  mbedtls_entropy_context OutHTTPS::entropy;
  mbedtls_ctr_drbg_context OutHTTPS::ctr_drbg;
  mbedtls_ssl_config OutHTTPS::sslConf;

  class crtAndKey {
    public:
      crtAndKey() {
        mbedtls_x509_crt_init(&crt);
        mbedtls_pk_init(&key);
      }
      mbedtls_x509_crt crt;
      mbedtls_pk_context key;
      std::string crtFile, keyFile;
  };
  std::deque<crtAndKey> srvcerts;

  static int cert_callback(void *p_info, mbedtls_ssl_context *ssl, const unsigned char *name, size_t name_len) {
    std::string sniName((char *)name, name_len);

    for (auto & it : srvcerts) {
      std::string subject((char *)it.crt.subject.val.p, it.crt.subject.val.len);
      if (sniName != subject) {
        bool found = false;
        mbedtls_asn1_sequence *cur = &it.crt.subject_alt_names;
        while (cur) {
          if (sniName == std::string((char *)cur->buf.p, cur->buf.len)) {
            found = true;
            break;
          }
          if (cur->buf.len && cur->buf.p[0] == '*' && sniName.size() >= cur->buf.len) {
            if (sniName.substr(sniName.size() - (cur->buf.len - 1)) == std::string((char *)cur->buf.p + 1, cur->buf.len - 1)) {
              if (sniName.substr(0, sniName.size() - (cur->buf.len - 1)).find('.') == std::string::npos) {
                found = true;
                break;
              }
            }
          }
          cur = cur->next;
        }
        if (!found) { continue; }
      }
      MEDIUM_MSG("Matched %s to (%s, %s)!", sniName.c_str(), it.crtFile.c_str(), it.keyFile.c_str());

      int r = mbedtls_ssl_set_hs_own_cert(ssl, &(it.crt), &(it.key));
      if (r) { WARN_MSG("Could not set certificate!"); }
      return r;
    }
    WARN_MSG("Could not find matching certificate for %s; using default certificate instead", sniName.c_str());
    int r = mbedtls_ssl_set_hs_own_cert(ssl, &(srvcerts.begin()->crt), &(srvcerts.begin()->key));
    if (r) { WARN_MSG("Could not set certificate!"); }
    return r;
  }

  void OutHTTPS::init(Util::Config *cfg){
    Output::init(cfg);
    capa["name"] = "HTTPS";
    capa["friendly"] = "HTTPS (HTTP+TLS)";
    capa["desc"] = "HTTPS connection handler, provides all enabled HTTP-based outputs";
    capa["provides"] = "HTTP";
    capa["protocol"] = "https://";
    capa["required"]["cert"]["name"] = "Path to certificate";
    capa["required"]["cert"]["help"] =
      "Path to the file(s) containing certificate chain(s). When multiple chains are used make sure to "
      "provide their matching keys in the same order.";
    capa["required"]["cert"]["option"] = "--cert";
    capa["required"]["cert"]["short"] = "C";
    capa["required"]["cert"]["default"] = "";
    capa["required"]["cert"]["type"] = "inputlist";
    capa["required"]["cert"]["input"]["type"] = "browse";

    capa["required"]["key"]["name"] = "Path to key";
    capa["required"]["key"]["help"] =
      "Path to private key for SSL. When multiple are used make sure they are in order matching the certificates.";
    capa["required"]["key"]["option"] = "--key";
    capa["required"]["key"]["short"] = "K";
    capa["required"]["key"]["default"] = "";
    capa["required"]["key"]["type"] = "inputlist";
    capa["required"]["key"]["input"]["type"] = "browse";

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

  OutHTTPS::OutHTTPS(Socket::Connection & C) : Output(C) {
    if (config->getOption("cert", true).size() < 2 || config->getOption("key", true).size() < 2) {
      FAIL_MSG("The cert/key required options were not passed!");
      C.close();
      return;
    }

    // Declare and set up all required mbedtls structures
    int ret;
    mbedtls_ssl_config_init(&sslConf);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);

    mbedtls_debug_set_threshold(3);
    mbedtls_ssl_conf_dbg(&sslConf, [](void *ctx, int level, const char *file, int line, const char *str) {
      const int lvl = (level == 1) ? 4 : level + 6;
      if (Util::printDebugLevel >= lvl) {
        fprintf(stderr, "%.8s|%.30s|%d|%.100s:%d|%.200s|TLS: %s\n", DBG_LVL_LIST[lvl], MIST_PROG, getpid(), file, line,
                Util::streamName, str);
      }
    }, 0);

    // seed the rng
    if ((ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                                     (const unsigned char *)APPNAME, strlen(APPNAME))) != 0) {
      FAIL_MSG("Could not seed the random number generator!");
    }

    // Read certificate chain(s) from cmdline option(s)
    bool ignoreKeys = false;
    JSON::Value certs = config->getOption("cert", true);
    jsonForEach (certs, it) {
      const std::string & cFile = it->asStringRef();
      if (!cFile.size()) { continue; } // Ignore empty entries (default is empty)
      srvcerts.push_back(crtAndKey());
      crtAndKey & srvcert = srvcerts.back();
      if (cFile[0] == '[') {
        ignoreKeys = true;
        const JSON::Value crtCnf = JSON::fromString(cFile);
        jsonForEachConst (crtCnf, jt) {
          if (!jt->asStringRef().size()) { continue; }
          if (jt.num() + 1 != crtCnf.size()) {
            if (!srvcert.crtFile.size()) { srvcert.crtFile = jt->asStringRef(); }
            ret = mbedtls_x509_crt_parse_file(&srvcert.crt, jt->asStringRef().c_str());
            if (ret) { WARN_MSG("Could not load any certificates from file: %s", jt->asStringRef().c_str()); }
          } else {
            srvcert.keyFile = jt->asStringRef();
            ret = mbedtls_pk_parse_keyfile(&(srvcert.key), jt->asStringRef().c_str(), NULL
#if MBEDTLS_VERSION_MAJOR > 2
                                           ,
                                           mbedtls_ctr_drbg_random, &ctr_drbg
#endif
            );
            if (ret) { WARN_MSG("Could not load any keys from file: %s", jt->asStringRef().c_str()); }
          }
        }
        continue;
      }
      srvcert.crtFile = cFile;
      ret = mbedtls_x509_crt_parse_file(&srvcert.crt, cFile.c_str());
      if (ret != 0) { WARN_MSG("Could not load any certificates from file: %s", cFile.c_str()); }
    }

    if (!ignoreKeys) {
      auto crtIt = srvcerts.begin();
      // Read key from cmdline option
      JSON::Value keys = config->getOption("key", true);
      jsonForEach (keys, it) {
        if (!it->asStringRef().size()) { continue; } // Ignore empty entries (default is empty)
        if (crtIt == srvcerts.end()) { break; }
        crtIt->keyFile = it->asStringRef();
        ret = mbedtls_pk_parse_keyfile(&(crtIt->key), it->asStringRef().c_str(), NULL
#if MBEDTLS_VERSION_MAJOR > 2
                                       ,
                                       mbedtls_ctr_drbg_random, &ctr_drbg
#endif
        );
        if (ret != 0) { WARN_MSG("Could not load any keys from file: %s", config->getString("key").c_str()); }
        ++crtIt;
      }
    }

    if ((ret = mbedtls_ssl_config_defaults(&sslConf, MBEDTLS_SSL_IS_SERVER, MBEDTLS_SSL_TRANSPORT_STREAM,
                                           MBEDTLS_SSL_PRESET_DEFAULT)) != 0) {
      FAIL_MSG("SSL config defaults failed");
      C.close();
      return;
    }
    mbedtls_ssl_conf_rng(&sslConf, mbedtls_ctr_drbg_random, &ctr_drbg);

    mbedtls_ssl_conf_ca_chain(&sslConf, srvcerts.begin()->crt.next, NULL);
    if ((ret = mbedtls_ssl_conf_own_cert(&sslConf, &srvcerts.begin()->crt, &srvcerts.begin()->key)) != 0) {
      FAIL_MSG("SSL config own certificate failed");
      C.close();
      return;
    }

    mbedtls_ssl_conf_sni(&sslConf, cert_callback, (void *)&srvcerts);

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
    int fdErr = 2;
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
    pid_t http_proc = Util::Procs::StartPiped(args, &(fd[1]), &(fd[1]), &fdErr);
    unsetenv("MIST_BOUND_ADDR");
    close(fd[1]);
    if (http_proc < 2){
      FAIL_MSG("Could not spawn MistOutHTTP process for SSL connection!");
      Util::logExitReason(ER_EXEC_FAILURE, "Could not spawn MistOutHTTP process for SSL connection!");
      return 1;
    }
    Util::Procs::forget(http_proc);
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

}// namespace Mist
