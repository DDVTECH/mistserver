#include "input.h"
#include <mist/nal.h>
#include <mist/dtsc.h>
#include <mist/ts_packet.h>
#include <string>
#include <set>


namespace Mist {
  struct pesBuffer {
    ///\brief Less-than comparison for seekPos structures.
    ///\param rhs The seekPos to compare with.
    ///\return Whether this object is smaller than rhs.
    bool operator < (const pesBuffer & rhs) const {
      if (time < rhs.time) {
        return true;
      } else {
        if (time == rhs.time){
          if (tid < rhs.tid){
            return true;
          }
        }
      }
      return false;
    }
    int tid;//When used for buffering, not for header generation
    long long int lastPos;//set by readFullPES, stores the byteposition directly after the last read ts packet
    int len;
    std::string data;
    long long int time;
    long long int offset;
    long long int bpos;
    bool isKey;
    std::string sps;
    std::string pps;
  };


/*
  /// This struct stores all metadata of a track, and sends them once a whole PES has been analyzed and sent
  struct trackInfo{
    //saves all data that needs to be sent.
    //as packets can be interleaved, the data needs to be temporarily stored
    long long int lastPos;//last byte position of trackSelect
    long long int pesTime;//the pes time of the current pes packet
    bool keyframe;//whether the current pes packet of the track has a keyframe or not
    std::string curPayload;//payload to be sent to user
    unsigned int packetCount;//number of TS packets read between between and end (to set bpos correctly)
  };
  
*/

  /// This class contains all functions needed to implement TS Input
  class inputTS : public Input {
    public:
      inputTS(Util::Config * cfg);
    protected:
      //Private Functions
      bool setup();
      bool readHeader();
      void getNext(bool smart = true);
      void seek(int seekTime);
      void trackSelect(std::string trackSpec);
      void parsePESHeader(int tid, pesBuffer & buf);
      void parsePESPayload(int tid, pesBuffer & buf);
      void parseH264PES(int tid, pesBuffer & buf);
      void parseAACPES(int tid, pesBuffer & buf);
      pesBuffer readFullPES(int tid);
      FILE * inFile;///<The input file with ts data
      h264::NAL nal;///<Used to analyze raw h264 data
      long long int lastPos;///<last position played in file
      std::set<pesBuffer> playbackBuf;///Used for buffering playback items
  };
}

typedef Mist::inputTS mistIn;

