#include "input.h"
#include <mist/dtsc.h>
#include <mist/shared_memory.h>

namespace Mist {
  class inputBuffer : public Input {
    public:
      inputBuffer(Util::Config * cfg);
      ~inputBuffer();
      void onCrash();
    private:
      unsigned int bufferTime;
      unsigned int cutTime;
      bool hasPush;
      bool resumeMode;
      IPC::semaphore * liveMeta;
    protected:
      //Private Functions
      bool preRun();
      bool checkArguments(){return true;}
      void updateMeta();
      bool readHeader(){return false;}
      bool needHeader(){return false;}
      void getNext(bool smart = true){}
      void updateTrackMeta(unsigned long tNum);
      void updateMetaFromPage(unsigned long tNum, unsigned long pageNum);
      void seek(int seekTime){}
      void trackSelect(std::string trackSpec){}
      bool removeKey(unsigned int tid);
      void removeUnused();
      void eraseTrackDataPages(unsigned long tid);
      void finish();
      void userCallback(char * data, size_t len, unsigned int id);
      std::set<unsigned long> negotiatingTracks;
      std::set<unsigned long> activeTracks;
      std::map<unsigned long, unsigned long long> lastUpdated;
      std::map<unsigned long, unsigned long long> negotiationTimeout;
      ///Maps trackid to a pagenum->pageData map
      std::map<unsigned long, std::map<unsigned long, DTSCPageData> > bufferLocations;
      std::map<unsigned long, char *> pushLocation;
      inputBuffer * singleton;
      //This is used for an ugly fix to prevent metadata from disappearing in some cases.
      std::map<unsigned long, std::string> initData;
  };
}

typedef Mist::inputBuffer mistIn;


