#include "output.h"
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
            return index < rhs.index;
          }
        }
        return false;
      }
      long unsigned int trackID;
      long unsigned int size;
      long long unsigned int time;
      long long unsigned int endTime;
      long long unsigned int byteOffset;//added for MP4 fragmented
      long int timeOffset;//added for MP4 fragmented
      long unsigned int duration;//added for MP4 fragmented
      long unsigned int index;
  };
  
  class OutSanityCheck : public Output {
    public:
      OutSanityCheck(Socket::Connection & conn);
      static void init(Util::Config * cfg);
      void sendNext();
      static bool listenMode(){return false;}
    protected:
      std::deque<std::string> packets;
      std::set <keyPart> sortSet;//needed for unfragmented MP4, remembers the order of keyparts
  };
}

typedef Mist::OutSanityCheck mistOut;
