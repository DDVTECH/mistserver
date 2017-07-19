#include "input.h"
#include <mist/dtsc.h>
#include <mist/mp4.h>
#include <mist/mp4_generic.h>
namespace Mist {
  class mp4PartTime{
    public:
      mp4PartTime() : time(0), offset(0), trackID(0), bpos(0), size(0), index(0) {}
      bool operator < (const mp4PartTime & rhs) const {
        if (time < rhs.time){
          return true;
        }
        if (time > rhs.time){
          return false;
        }
        if (trackID < rhs.trackID){
          return true;
        }
        return (trackID == rhs.trackID && bpos < rhs.bpos);
      }
      uint64_t time;
      int32_t offset;
      size_t trackID;
      uint64_t bpos;
      uint32_t size;
      uint64_t index;
  };

  struct mp4PartBpos{
    bool operator < (const mp4PartBpos & rhs) const {
      if (time < rhs.time){
        return true;
      }
      if (time > rhs.time){
        return false;
      }
      if (trackID < rhs.trackID){
        return true;
      }
      return (trackID == rhs.trackID && bpos < rhs.bpos);
    }
    uint64_t time;
    size_t trackID;
    uint64_t bpos;
    uint64_t size;
    uint64_t stcoNr;
    int32_t timeOffset;
    bool keyframe;
  };

  class mp4TrackHeader{
    public:
      mp4TrackHeader();
      void read(MP4::TRAK & trakBox);
      MP4::STCO stcoBox;
      MP4::CO64 co64Box;
      MP4::STSZ stszBox;
      MP4::STTS sttsBox;
      bool hasCTTS;
      MP4::CTTS cttsBox;
      MP4::STSC stscBox;
      uint64_t timeScale;
      void getPart(uint64_t index, uint64_t & offset, uint32_t & size, uint64_t & timestamp, int32_t & timeOffset);
      uint64_t size();
    private:
      bool initialised;
      //next variables are needed for the stsc/stco loop
      uint64_t stscStart;
      uint64_t sampleIndex;
      //next variables are needed for the stts loop
      uint64_t deltaIndex;///< Index in STTS box
      uint64_t deltaPos;///< Sample counter for STTS box
      uint64_t deltaTotal;///< Total timestamp for STTS box
      //for CTTS box loop
      uint64_t offsetIndex;///< Index in CTTS box
      uint64_t offsetPos;///< Sample counter for CTTS box

      bool stco64;
  };

  class inputMP4 : public Input {
    public:
      inputMP4(Util::Config * cfg);
      ~inputMP4();
    protected:
      //Private Functions
      bool checkArguments();
      bool preRun();
      bool readHeader();
      void getNext(bool smart = true);
      void seek(int seekTime);
      void trackSelect(std::string trackSpec);

      FILE * inFile;
      
      std::map<unsigned int, mp4TrackHeader> headerData;
      std::set<mp4PartTime> curPositions;
      
      //remember last seeked keyframe;
      std::map <unsigned int, unsigned int> nextKeyframe;
      
      //these next two variables keep a buffer for reading from filepointer inFile;
      uint64_t malSize;
      char* data;///\todo rename this variable to a more sensible name, it is a temporary piece of memory to read from files
  };
}

typedef Mist::inputMP4 mistIn;
