#include <set>
#include <map>
#include <cstdlib>
#include <mist/config.h>
#include <mist/json.h>
#include <mist/timing.h>
#include <mist/dtsc.h>
#include <mist/shared_memory.h>

namespace Mist {
  struct DTSCPageData {
    DTSCPageData() : keyNum(0), partNum(0), dataSize(0), curOffset(0), firstTime(0){}
    int keyNum;///<The number of keyframes in this page.
    int partNum;///<The number of parts in this page.
    unsigned long long int dataSize;///<The full size this page should be.
    unsigned long long int curOffset;///<The current write offset in the page.
    unsigned long long int firstTime;///<The first timestamp of the page.
    unsigned long lastKeyTime;///<The last key time encountered on this track.
  };

  struct booking {
    int first;
    int curKey;
    int curPart;
  };

  class Input {
    public:
      Input(Util::Config * cfg);
      int run();
      virtual ~Input() {};
    protected:
      static void doNothing(char * data, size_t len, unsigned int id);
      virtual bool setup() = 0;
      virtual bool readHeader() = 0;
      virtual bool atKeyFrame();
      virtual void getNext(bool smart = true) {};
      virtual void seek(int seekTime){};
      void play(int until = 0);
      void playOnce();
      void quitPlay();
      void checkHeaderTimes(std::string streamFile);
      virtual void removeUnused();
      virtual void trackSelect(std::string trackSpec){};
      virtual void userCallback(char * data, size_t len, unsigned int id);
      
      void parseHeader();
      bool bufferFrame(int track, int keyNum);

      unsigned int packTime;///Media-timestamp of the last packet.
      int lastActive;///Timestamp of the last time we received or sent something.
      int initialTime;
      int playing;
      unsigned int playUntil;
      unsigned int benchMark;
      std::set<int> selectedTracks;

      bool isBuffer;

      Util::Config * config;
      JSON::Value capa;
      DTSC::Meta myMeta;
      DTSC::Packet lastPack;
      
      std::map<int,std::set<int> > keyTimes;
      IPC::sharedPage metaPage;
      //Create server for user pages
      IPC::sharedServer userPage;
      
    
      //TrackIndex pages
      std::map<int, IPC::sharedPage> indexPages;
      std::map<int, std::map<int, IPC::sharedPage> > dataPages;

      //Page Overview
      std::map<int, std::map<int, DTSCPageData> > pagesByTrack;

      std::map<unsigned int, std::map<unsigned int, unsigned int> > pageCounter;

      static Input * singleton;
  };

}

