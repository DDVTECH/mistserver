#include "output.h"
#include <mist/mp4_generic.h>
#include <mist/ts_packet.h>
#include <string>

namespace Mist {
  class OutTSPush : public Output {
    public:
      OutTSPush(Socket::Connection & conn);
      ~OutTSPush();
      static void init(Util::Config * cfg);
      void sendNext();
      void sendHeader();
    protected:
      TS::Packet PackData;
      unsigned int PacketNumber;
      bool haveAvcc;
      bool haveHvcc;
      char VideoCounter;
      char AudioCounter;
      MP4::AVCC avccbox;
      MP4::HVCC hvccbox;
      std::string createPMT();
      void fillPacket(bool & first, const char * data, size_t dataLen, char & ContCounter);
      void fillBuffer(const char * data, size_t dataLen);
      std::string packetBuffer;
      Socket::UDPConnection pushSock;
  };
}

typedef Mist::OutTSPush mistOut;
