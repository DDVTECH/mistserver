#include <fstream>

#include "input.h"
#include <mist/dtsc.h>
#include <mist/shared_memory.h>

namespace Mist {
  class inputBuffer : public Input {
    public:
      inputBuffer(Util::Config * cfg);
      ~inputBuffer();
    private:
      unsigned int bufferTime;
      unsigned int cutTime;
      unsigned int segmentSize; /*LTS*/
      unsigned int lastReTime; /*LTS*/
      bool hasPush; /*LTS*/
      bool resumeMode; /*LTS*/
    protected:
      //Private Functions
      bool setup();
      void updateMeta();
      bool readHeader();
      void getNext(bool smart = true);
      void updateTrackMeta(unsigned long tNum);
      void updateMetaFromPage(unsigned long tNum, unsigned long pageNum);
      void seek(int seekTime);
      void trackSelect(std::string trackSpec); 
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

      std::string recName;/*LTS*/
      DTSC::Meta recMeta;/*LTS*/
      std::ofstream recFile;/*LTS*/
      long long int recBpos;/*LTS*/
       //This is used for an ugly fix to prevent metadata from dissapearing in some cases.
      std::map<unsigned long, std::string> initData;

      /* begin-roxlu */
      int recordingPid; // pid of the process that does the recording. Currently only MP4 supported. 
      /* end-roxlu */
  };
}

typedef Mist::inputBuffer mistIn;


