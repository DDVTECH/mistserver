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
    protected:
      //Private Functions
      bool setup();
      void updateMeta();
      bool readHeader();
      void getNext(bool smart = true);
      void updateMetaFromPage(int tNum, int pageNum);
      void seek(int seekTime);
      void trackSelect(std::string trackSpec); 
      bool removeKey(unsigned int tid);
      void removeUnused();
      void userCallback(char * data, size_t len, unsigned int id);
      std::set<unsigned long> negotiateTracks;
      std::set<unsigned long> givenTracks;
      std::map<unsigned long, IPC::sharedPage> metaPages;
      ///Maps trackid to a pagenum->pageData map
      std::map<unsigned long, std::map<unsigned long, DTSCPageData> > inputLoc;
      std::map<unsigned long, char *> pushedLoc;
      inputBuffer * singleton;

      std::string recName;/*LTS*/
      DTSC::Meta recMeta;/*LTS*/
      std::ofstream recFile;/*LTS*/
      long long int recBpos;/*LTS*/
  };
}

typedef Mist::inputBuffer mistIn;


