#include "output_http.h"
#include <mist/http_parser.h>
#include <list>

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
  };
  
  struct fragSet{
    uint64_t firstPart;
    uint64_t lastPart;
    uint64_t firstTime;
    uint64_t lastTime;
  };
  class OutProgressiveMP4 : public HTTPOutput {
    public:
      OutProgressiveMP4(Socket::Connection & conn);
      ~OutProgressiveMP4();
      static void init(Util::Config * cfg);
      void parseRange(std::string header, uint64_t & byteStart, uint64_t & byteEnd, uint64_t & seekPoint, uint64_t headerSize);
      uint64_t mp4HeaderSize(uint64_t & fileSize, int fragmented = 0);
      std::string DTSCMeta2MP4Header(uint64_t & size, int fragmented = 0);
      //int fragmented values: 0 = non fragmented stream, 1 = frag stream main header
      void buildFragment();//this builds the structure of the fragment header and stores it in a member variable
      void sendFragmentHeader();//this builds the moof box for fragmented MP4
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

      //variables for fragmented
      size_t fragSeqNum;//the sequence number of the next keyframe/fragment when producing fragmented MP4's
      size_t vidTrack;//the video track we use as fragmenting base
      uint64_t realBaseOffset;//base offset for every moof packet
      //from sendnext
      size_t partListSent;//parts of current fragSet sent
      size_t partListLength;//amount of packets in current fragment
      int64_t fragKeyNumberShift;//the difference between the first fragment Number and the first keyframe number

      
      bool sending3GP;
      bool chromeWorkaround;
      uint64_t estimateFileSize();

      //This is a dirty solution... but it prevents copying and copying and copying again
      std::map<size_t, fragSet> currentPartSet;
  };
}

typedef Mist::OutProgressiveMP4 mistOut;
