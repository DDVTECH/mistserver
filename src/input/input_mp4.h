#include "input.h"
#include <mist/dtsc.h>
#include <mist/mp4.h>
#include <mist/mp4_generic.h>
namespace Mist {
  class mp4PartTime{
    public:
      mp4PartTime() : time(0), trackID(0), bpos(0), size(0), index(0) {}
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
      unsigned int trackID;
      long long unsigned int bpos;
      unsigned int size;
      long unsigned int index;
  };

  struct mp4PartBpos{
    bool operator < (const mp4PartBpos & rhs) const {
      return (bpos < rhs.bpos);
    }
    long long unsigned int time;
    long long unsigned int trackID;
    long long unsigned int bpos;
    long long unsigned int size;
    long long unsigned int stcoNr;
    bool keyframe;
  };

  class mp4TrackHeader{
    public:
      mp4TrackHeader();
      void read(MP4::TRAK & trakBox);
      MP4::STCO stcoBox;
      MP4::STSZ stszBox;
      MP4::STTS sttsBox;
      MP4::STSC stscBox;
      long unsigned int timeScale;
      void getPart(long unsigned int index, long long unsigned int & offset,unsigned int& size, long long unsigned int & timestamp);
      long unsigned int size();
    private:
      long long unsigned int lastIndex;//remembers the last called sample index
      bool initialised;
      //next variables are needed for the stsc/stco loop
      long long unsigned int stcoPlace;
      long long unsigned int stszStart;
      long long unsigned int stscStart;
      long long unsigned int sampleIndex;
      long long unsigned int nextSampleIndex;
      //next variables are needed for the stts loop
      long long unsigned int totalDelta;//total of time delta;
      long long unsigned int prevTotalDelta;//total of time delta, from previous stsc box;
      long long unsigned int sttsStart;
      long long unsigned int countedSamples;//the amount of samples we're walking through
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
