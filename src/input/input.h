#include <set>
#include <map>
#include <cstdlib>
#include <mist/config.h>
#include <mist/json.h>
#include <mist/timing.h>
#include <mist/dtsc.h>
#include <mist/shared_memory.h>
#include <fstream>

#include "../io.h"

namespace Mist {
  struct booking {
    int first;
    int curKey;
    int curPart;
  };

  class Input : public InOutBase {
    public:
      Input(Util::Config * cfg);
      virtual int run();
      virtual void onCrash();
      virtual int boot(int argc, char * argv[]);
      virtual ~Input() {};

      virtual bool needsLock(){return true;}
    protected:
      static void callbackWrapper(char * data, size_t len, unsigned int id);
      virtual bool checkArguments() = 0;
      virtual bool readHeader() = 0;
      virtual bool needHeader(){return !readExistingHeader();}
      virtual bool preRun(){return true;}
      virtual bool readExistingHeader();
      virtual bool atKeyFrame();
      virtual void getNext(bool smart = true) {}
      virtual void seek(int seekTime){};
      virtual void finish();
      virtual bool keepRunning();
      virtual bool openStreamSource() { return false; }
      virtual void closeStreamSource() {}
      virtual void parseStreamHeader() {}
      void play(int until = 0);
      void playOnce();
      void quitPlay();
      void checkHeaderTimes(std::string streamFile);
      virtual void removeUnused();
      virtual void trackSelect(std::string trackSpec);
      virtual void userCallback(char * data, size_t len, unsigned int id);
      virtual void convert();
      virtual void serve();
      virtual void stream();
      virtual std::string streamMainLoop();
      bool isAlwaysOn();

      virtual void parseHeader();
      bool bufferFrame(unsigned int track, unsigned int keyNum);

      unsigned int packTime;///Media-timestamp of the last packet.
      int lastActive;///Timestamp of the last time we received or sent something.
      int initialTime;
      int playing;
      unsigned int playUntil;

      bool isBuffer;
      uint64_t activityCounter;

      JSON::Value capa;
      
      std::map<int,std::set<int> > keyTimes;

      //Create server for user pages
      IPC::sharedServer userPage;
      IPC::sharedPage streamStatus;

      std::map<unsigned int, std::map<unsigned int, unsigned int> > pageCounter;

      static Input * singleton;

      bool hasSrt;
      std::ifstream srtSource;
      unsigned int srtTrack;

      void readSrtHeader();
      void getNextSrt(bool smart = true);
      DTSC::Packet srtPack;
  };

}

