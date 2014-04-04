#include "output.h"
#include <mist/http_parser.h>

namespace Mist {
  struct keyPart{
    public:
      bool operator < (const keyPart& rhs) const {
        if (time < rhs.time){
          return true;
        }
        if (time == rhs.time){
          if (trackID < rhs.trackID){
            return true;
          }
        }
        return false;
      }
      long unsigned int trackID;
      long unsigned int size;
      long long unsigned int time;
      long long unsigned int endTime;
      long unsigned int index;
  };
  
  class OutProgressiveMP4 : public Output {
    public:
      OutProgressiveMP4(Socket::Connection & conn);
      ~OutProgressiveMP4();
      static void init(Util::Config * cfg);
      void parseRange(std::string header, long long & byteStart, long long & byteEnd, long long & seekPoint, unsigned int headerSize);
      std::string DTSCMeta2MP4Header(long long & size);
      void findSeekPoint(long long byteStart, long long & seekPoint, unsigned int headerSize);
      
      void onRequest();
      void sendNext();
      bool onFinish();
      void sendHeader();
      void onFail();
    protected:
      long long fileSize;
      long long byteStart;
      long long byteEnd;
      long long leftOver;
      long long currPos;
      std::set <keyPart> sortSet;//filling sortset for interleaving parts
      HTTP::Parser HTTP_R, HTTP_S;
  };
}

typedef Mist::OutProgressiveMP4 mistOut;
