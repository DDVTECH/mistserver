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

  private:
    unsigned int lastActive; ///< Time of last sending of data.
    std::string getStatsName();
    std::string salt;
    HTTP::URL pushUrl;
    void handlePush(DTSC::Scan &dScan);
    void handlePlay(DTSC::Scan &dScan);
    bool isSyncReceiver;
  };
}// namespace Mist

typedef Mist::OutDTSC mistOut;
