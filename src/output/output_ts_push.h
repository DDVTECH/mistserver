#include "output_ts_base.h"

namespace Mist {
  class OutTSPush : public TSOutput{
    public:
      OutTSPush(Socket::Connection & conn);
      ~OutTSPush();
      static void init(Util::Config * cfg);
      static bool listenMode(){return false;}
      void sendTS(const char * tsData, unsigned int len=188);
  protected:    
      void fillBuffer(const char * data, size_t dataLen);
      std::string packetBuffer;
      Socket::UDPConnection pushSock;
  };
}

typedef Mist::OutTSPush mistOut;
