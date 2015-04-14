#include "output.h"
#include <mist/flv_tag.h>
#include <mist/amf.h>
#include <mist/rtmpchunks.h>


namespace Mist {

  class pushData {
    public:
      DTSC::Meta meta;
      bool sending;
      int counter;
      std::deque<JSON::Value> preBuf;
      pushData(){
        sending = false;
        counter = 0;
      }
  };


 class OutRTMP : public Output {
    public:
      OutRTMP(Socket::Connection & conn);
      ~OutRTMP();
      static void init(Util::Config * cfg);
      void onRequest();
      void sendNext();
      void sendHeader();
    protected:
      void parseVars(std::string data);
      std::string app_name;
      void parseChunk(Socket::Buffer & inputBuffer);
      void parseAMFCommand(AMF::Object & amfData, int messageType, int streamId);
      void sendCommand(AMF::Object & amfReply, int messageType, int streamId);
      std::map<unsigned int, pushData> pushes;
  };
}

typedef Mist::OutRTMP mistOut;
