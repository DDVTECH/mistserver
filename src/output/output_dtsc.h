#include "output.h"

namespace Mist {

 class OutDTSC : public Output {
    public:
      OutDTSC(Socket::Connection & conn);
      ~OutDTSC();
      static void init(Util::Config * cfg);
      void onRequest();
      void sendNext();
      void sendHeader();
      void initialSeek();
      void onFail(const std::string & msg, bool critical = false);
      void stats(bool force = false);
      void sendCmd(const JSON::Value &data);
      void sendOk(const std::string &msg);
    private:
      unsigned int lastActive;///<Time of last sending of data.
      std::string getStatsName();
      std::string salt;
      void handlePush(DTSC::Scan & dScan);
      void handlePlay(DTSC::Scan & dScan);
      unsigned long long fastAsPossibleTime;
  };
}

typedef Mist::OutDTSC mistOut;

