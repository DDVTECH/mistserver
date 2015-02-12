#include "output_http.h"
#include <mist/http_parser.h>
#include <mist/ts_packet.h>
#include <mist/mp4.h>
#include <mist/mp4_generic.h>

namespace Mist {
  class OutHTTPTS : public HTTPOutput {
    public:
      OutHTTPTS(Socket::Connection & conn);
      ~OutHTTPTS();
      static void init(Util::Config * cfg);
      void onHTTP();
      void sendNext();
    protected:
      std::string createPMT();
      void fillPacket(bool & first, const char * data, size_t dataLen, char & ContCounter);
      int keysToSend;
      long long int playUntil;
      TS::Packet PackData;
      unsigned int PacketNumber;
      bool haveAvcc;
      bool haveHvcc;
      char VideoCounter;
      char AudioCounter;
      MP4::AVCC avccbox;
      MP4::HVCC hvccbox;
      bool AppleCompat;
      long long unsigned int lastVid;
      long long unsigned int until;
      unsigned int vidTrack;
      unsigned int audTrack;
  };
}

typedef Mist::OutHTTPTS mistOut;
