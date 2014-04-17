#include "output.h"
#include <mist/flv_tag.h>
#include <mist/amf.h>
#include <mist/rtmpchunks.h>


namespace Mist {
  struct DTSCPageData {
    DTSCPageData() : pageNum(0), keyNum(0), partNum(0), dataSize(0), curOffset(0), firstTime(0), lastKeyTime(-5000){}
    int pageNum;///<The current page number
    int keyNum;///<The number of keyframes in this page.
    int partNum;///<The number of parts in this page.
    unsigned long long int dataSize;///<The full size this page should be.
    unsigned long long int curOffset;///<The current write offset in the page.
    unsigned long long int firstTime;///<The first timestamp of the page.
    long long int lastKeyTime;///<The time of the last keyframe of the page.
  };

  class OutRTMP : public Output {
    public:
      OutRTMP(Socket::Connection & conn);
      ~OutRTMP();
      static void init(Util::Config * cfg);
      
      void onRequest();
      void sendNext();
      void sendHeader();
      void bufferPacket(JSON::Value & pack);
    protected:
      DTSC::Meta meta_out;
      void negotiatePushTracks();
      std::string app_name;
      bool sending;
      int counter;
      bool streamReset;
      int playTransaction;///<The transaction number of the reply.
      int playStreamId;///<The stream id of the reply.
      int playMessageType;///<The message type of the reply.
      void parseChunk(Socket::Buffer & inputBuffer);
      void parseAMFCommand(AMF::Object & amfData, int messageType, int streamId);
      void sendCommand(AMF::Object & amfReply, int messageType, int streamId);
      std::deque<JSON::Value> preBuf;
      std::map<int,int> trackMap;
      std::map<int,IPC::sharedPage> metaPages;
      std::map<int,DTSCPageData> bookKeeping;
  };
}

typedef Mist::OutRTMP mistOut;
