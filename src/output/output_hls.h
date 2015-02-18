#include "output_http.h"
#include <mist/ts_packet.h>
#include <mist/mp4.h>
#include <mist/mp4_generic.h>

namespace Mist {
  class OutHLS : public HTTPOutput {
    public:
      OutHLS(Socket::Connection & conn);
      ~OutHLS();
      static void init(Util::Config * cfg);
      void onHTTP();
      void sendNext();
    protected:
      std::string createPMT();
      void fillPacket(bool & first, const char * data, size_t dataLen, char & ContCounter);
      std::string liveIndex();
      std::string liveIndex(int tid);
      int canSeekms(unsigned int ms);
      int keysToSend;
      long long int playUntil;
      TS::Packet PackData;
      unsigned int PacketNumber;
      bool haveAvcc;
      char VideoCounter;
      char AudioCounter;
      MP4::AVCC avccbox;
      bool AppleCompat;
      bool VLCworkaround;
      long long unsigned int lastVid;
      long long unsigned int until;
      unsigned int vidTrack;
      unsigned int audTrack;
  };
}

typedef Mist::OutHLS mistOut;
