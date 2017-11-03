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
      static bool listenMode();
      void requestHandler();
      bool onFinish();
    protected:
      std::string streamOut;///<When pushing out, the output stream name
      int64_t rtmpOffset;
      uint64_t lastOutTime;
      unsigned int maxbps;
      void parseVars(std::string data);
      std::string app_name;
      void parseChunk(Socket::Buffer & inputBuffer);
      void parseAMFCommand(AMF::Object & amfData, int messageType, int streamId);
      void sendCommand(AMF::Object & amfReply, int messageType, int streamId);
  };
}

typedef Mist::OutRTMP mistOut;
