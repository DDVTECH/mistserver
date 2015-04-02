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
        }else{
          if (time == rhs.time){
            if (trackID < rhs.trackID){
              return true;
            }
          }
        }
        return false;
      }
      long long unsigned int time;
      long long unsigned int offset;
      unsigned int trackID;
      long long unsigned int bpos;
      unsigned int size;
      long unsigned int index;
  };

  struct mp4PartBpos{
    bool operator < (const mp4PartBpos & rhs) const {
      if (time < rhs.time){
        return true;
      }else{
        if (time == rhs.time){
          if (trackID < rhs.trackID){
            return true;
          }
        }
      }
      return false;
    }
    long long unsigned int time;
    long long unsigned int trackID;
    long long unsigned int bpos;
    long long unsigned int size;
    long long unsigned int stcoNr;
    long unsigned int timeOffset;
    bool keyframe;
  };

  class mp4TrackHeader{
    public:
      mp4TrackHeader();
      void read(MP4::TRAK & trakBox);
      MP4::STCO stcoBox;
      MP4::STSZ stszBox;
      MP4::STTS sttsBox;
      MP4::CTTS cttsBox;
      MP4::STSC stscBox;
      long unsigned int timeScale;
      void getPart(long unsigned int index, long long unsigned int & offset,unsigned int& size, long long unsigned int & timestamp, long long unsigned int & timeOffset);
      long unsigned int size();
    private:
      bool initialised;
      //next variables are needed for the stsc/stco loop
      long long unsigned int stscStart;
      long long unsigned int sampleIndex;
      //next variables are needed for the stts loop
      long long unsigned deltaIndex;///< Index in STTS box
      long long unsigned deltaPos;///< Sample counter for STTS box
      long long unsigned deltaTotal;///< Total timestamp for STTS box
      //for CTTS box loop
      long long unsigned offsetIndex;///< Index in CTTS box
      long long unsigned offsetPos;///< Sample counter for CTTS box
  };

  class inputMP4 : public Input {
    public:
      inputMP4(Util::Config * cfg);
      ~inputMP4();
    protected:
      //Private Functions
      bool setup();
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
      unsigned long long int malSize;
      char* data;///\todo rename this variable to a more sensible name, it is a temporary piece of memory to read from files
  };
}

typedef Mist::inputMP4 mistIn;
