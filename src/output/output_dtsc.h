#include "output.h"
#include <mist/url.h>

namespace Mist{

  class OutDTSC : public Output{
  public:
    OutDTSC(Socket::Connection & conn, Util::Config & cfg, JSON::Value & capa);
    ~OutDTSC();
    static void init(Util::Config *cfg, JSON::Value & capa);
    void onRequest();
    void sendNext();
    void sendHeader();
    static bool listenMode(Util::Config *config) { return !(config->getString("target").size()); }
    void onFail(const std::string &msg, bool critical = false);
    void stats(bool force = false);
    void sendCmd(const JSON::Value &data);
    void sendOk(const std::string &msg);
    bool isFileTarget();
#ifdef SSL
    bool setupTLS();
#endif

  private:
    unsigned int lastActive; ///< Time of last sending of data.
    std::string getStatsName();
    std::string salt;
    HTTP::URL pushUrl;
    void handlePush(DTSC::Scan &dScan);
    void handlePlay(DTSC::Scan &dScan);
    bool isSyncReceiver;

#ifdef SSL
    // TLS-related
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_ssl_config sslConf;
    mbedtls_x509_crt srvcert;
    mbedtls_pk_context pkey;
    bool isTLSEnabled;
#endif
  };
}// namespace Mist

typedef Mist::OutDTSC mistOut;
