#include "output_http.h"
#include <mist/http_parser.h>

namespace Mist {
  struct keyPart{
    public:
      bool operator < (const keyPart& rhs) const {
        if (time < rhs.time){
          return true;
        }
        if (time > rhs.time){
          return false;
        }
        if (trackID < rhs.trackID){
          return true;
        }
        return (trackID == rhs.trackID && index < rhs.index);
      }
      size_t trackID;
      uint64_t time;
      uint64_t byteOffset;//Stores relative bpos for fragmented MP4
      uint64_t index;
      uint32_t size;
  };
  
  class OutProgressiveMP4 : public HTTPOutput {
    public:
      OutProgressiveMP4(Socket::Connection & conn);
      ~OutProgressiveMP4();
      static void init(Util::Config * cfg);
      uint64_t mp4HeaderSize(uint64_t & fileSize);
      std::string DTSCMeta2MP4Header(uint64_t & size);
      void findSeekPoint(uint64_t byteStart, uint64_t & seekPoint, uint64_t headerSize);
      void onHTTP();
      void sendNext();
      void sendHeader();
    protected:
      uint64_t fileSize;
      uint64_t byteStart;
      uint64_t byteEnd;
      int64_t leftOver;
      uint64_t currPos;
      uint64_t seekPoint;
      
      //variables for standard MP4
      std::set <keyPart> sortSet;//needed for unfragmented MP4, remembers the order of keyparts

      uint64_t estimateFileSize();
  };
}

typedef Mist::OutProgressiveMP4 mistOut;
