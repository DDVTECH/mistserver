#include "output.h"
#include <mist/flv_tag.h>
#include <mist/amf.h>
#include <mist/rtmpchunks.h>


namespace Mist {

 class OutRTMP : public Output {
    public:
      OutRTMP(Socket::Connection & conn);
      static void init(Util::Config * cfg);
      void onRequest();
      void sendNext();
      void sendHeader();
      bool onFinish();
    protected:
      void parseVars(std::string data);
      int64_t rtmpOffset;
      uint64_t lastOutTime;
      int64_t bootMsOffset;
      std::string app_name;
      void parseChunk(Socket::Buffer & inputBuffer);
      void parseAMFCommand(AMF::Object & amfData, int messageType, int streamId);
      void sendCommand(AMF::Object & amfReply, int messageType, int streamId);
  };
}

typedef Mist::OutRTMP mistOut;
