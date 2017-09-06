#include "output_ts_base.h"

namespace Mist {
  class OutTS : public TSOutput{
    public:
      OutTS(Socket::Connection & conn);
      ~OutTS();
      static void init(Util::Config * cfg);
      void sendTS(const char * tsData, unsigned int len=188);
      static bool listenMode();
      void initialSeek();
    private:
      unsigned int udpSize;
      bool pushOut;
      std::string packetBuffer;
      Socket::UDPConnection pushSock;
  };
}

typedef Mist::OutTS mistOut;
