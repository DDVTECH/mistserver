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
      void stats(bool force = false);
    private:
      unsigned int lastActive;///<Time of last sending of data.
      std::string getStatsName();
      std::string salt;
      bool pushing;
      void handlePush(DTSC::Scan & dScan);
      void handlePlay(DTSC::Scan & dScan);
      unsigned long long fastAsPossibleTime;
  };
}

typedef Mist::OutDTSC mistOut;

