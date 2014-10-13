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
            return endTime < rhs.endTime;
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
  
  struct fragPart{
    public:
      bool operator < (const fragPart& rhs) const {
        if (trackID < rhs.trackID){
          return true;
        }
        if (trackID == rhs.trackID){
          if (time < rhs.time){
            return true;
          }else{
            return false;
          }
        }
        return false;
      }
      long unsigned int trackID;
      long long unsigned int time;
      std::string data;
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
      std::map<long unsigned int, fragSet> buildFragment(int fragNum);//this builds the structure of the fragment header for fragment number fragNum
      std::string fragmentHeader(int fragNum, std::map<long unsigned int, fragSet> partMap);//this builds the moof box for fragmented MP4
      void findSeekPoint(long long byteStart, long long & seekPoint, unsigned int headerSize);
      void onHTTP();
      void sendNext();
      void sendHeader();
    protected:
      long long fileSize;
      long long byteStart;
      long long byteEnd;
      long long leftOver;
      long long currPos;
      long long seekPoint;
      
      //todo: weed out unused variables. If this todo is still in the code when merging, notify Oswald.
      
      

      //variables for standard MP4
      std::set <keyPart> sortSet;//needed for unfragmented MP4, remembers the order of keyparts

      //functions for fragmented MP4
      unsigned int putInFragBuffer(DTSC::Packet& inPack);
      void buildTrafPart();
      void setvidTrack();//searching for the current vid track

      //variables for fragmented
      int fragSeqNum;//the sequence number of the next keyframe/fragment when producing fragmented MP4's
      long unsigned int vidTrack;//the video track we use as fragmenting base
      std::set <fragPart> sortFragBuffer;//buffer to save packet content for fragmented MP4
      long long unsigned int realBaseOffset;//base offset for every moof packet
      //from buildFragment
      std::map<long unsigned int, long unsigned int> buildFragmentDSP;//Dynamic Starting point for buildFragment
      std::map<long unsigned int, long long unsigned int> buildFragmentDSPTime;//Dynamic Starting point for buildFragment
      //from sendnext
      std::map<long unsigned int, fragSet> partMap;//structure of current fragment
      long unsigned int partListSent;//parts of current fragSet sent
      std::vector<long unsigned int> partList;//trackID order for packet content
      long int fragKeyNumberShift;//the difference between the first fragment Number and the first keyframe number

  };
}

typedef Mist::OutProgressiveMP4 mistOut;
