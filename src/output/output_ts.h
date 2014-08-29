#include "output.h"
#include <mist/mp4_generic.h>
#include <mist/ts_packet.h>
#include <string>

namespace Mist {
  class OutTS : public Output {
    public:
      OutTS(Socket::Connection & conn);
      ~OutTS();
      static void init(Util::Config * cfg);
      void sendNext();
      void sendHeader();
    protected:
      TS::Packet PackData;
      unsigned int PacketNumber;
      bool haveAvcc;
      char VideoCounter;
      char AudioCounter;
      MP4::AVCC avccbox;
      std::string createPMT();
      void fillPacket(bool & first, const char * data, size_t dataLen, char & ContCounter);
  };
}

typedef Mist::OutTS mistOut;
