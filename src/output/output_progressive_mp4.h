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
        if (time == rhs.time){
          if (trackID < rhs.trackID){
            return true;
          }
          if (trackID == rhs.trackID){
            return index < rhs.index;
          }
        }
        return false;
      }
      long unsigned int trackID;
      long unsigned int size;
      long long unsigned int time;
      long long unsigned int endTime;
      long long unsigned int byteOffset;//added for MP4 fragmented
      long int timeOffset;//added for MP4 fragmented
      long unsigned int duration;//added for MP4 fragmented
      long unsigned int index;
  };
  
  struct fragSet{
    long unsigned int firstPart;
    long unsigned int lastPart;
    long long unsigned int firstTime;
    long long unsigned int lastTime;
  };
  class OutProgressiveMP4 : public HTTPOutput {
    public:
      OutProgressiveMP4(Socket::Connection & conn);
      ~OutProgressiveMP4();
      static void init(Util::Config * cfg);
      void parseRange(std::string header, long long & byteStart, long long & byteEnd, long long & seekPoint, unsigned int headerSize);
      std::string DTSCMeta2MP4Header(long long & size, int fragmented = 0);
      //int fragmented values: 0 = non fragmented stream, 1 = frag stream main header
      void buildFragment();//this builds the structure of the fragment header and stores it in a member variable
      void sendFragmentHeader();//this builds the moof box for fragmented MP4
      void findSeekPoint(long long byteStart, long long & seekPoint, unsigned int headerSize);
      void onHTTP();
      void sendNext();
      void sendHeader();
      void initialSeek();
    protected:
      long long fileSize;
      long long byteStart;
      long long byteEnd;
      long long leftOver;
      long long currPos;
      long long seekPoint;
      
      //variables for standard MP4
      std::set <keyPart> sortSet;//needed for unfragmented MP4, remembers the order of keyparts

      //functions for fragmented MP4
      void buildTrafPart();

      //variables for fragmented
      int fragSeqNum;//the sequence number of the next keyframe/fragment when producing fragmented MP4's
      long unsigned int vidTrack;//the video track we use as fragmenting base
      long long unsigned int realBaseOffset;//base offset for every moof packet
      //from sendnext
      long unsigned int partListSent;//parts of current fragSet sent
      long unsigned int partListLength;//amount of packets in current fragment
      long int fragKeyNumberShift;//the difference between the first fragment Number and the first keyframe number

      
      bool sending3GP;
      long long unsigned estimateFileSize();

      //This is a dirty solution... but it prevents copying and copying and copying again
      std::map<long unsigned int, fragSet> currentPartSet;
  };
}

typedef Mist::OutProgressiveMP4 mistOut;
