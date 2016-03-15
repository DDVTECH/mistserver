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
    private:
      std::string salt;
      bool pushing;
      void handlePush(DTSC::Scan & dScan);
      void handlePlay(DTSC::Scan & dScan);
      unsigned long long fastAsPossibleTime;
  };
}

typedef Mist::OutDTSC mistOut;

