#include "input.h"
#include <deque>
#include <mist/dtsc.h>

namespace Mist{
  class InputPlaylist : public Input{
  public:
    InputPlaylist(Util::Config *cfg);
    bool needsLock(){return false;}

  protected:
    bool checkArguments();
    bool readHeader(){return true;}
    virtual void parseStreamHeader(){}
    void streamMainLoop();
    virtual bool needHeader(){return false;}
    virtual bool publishesTracks(){return false;}

  private:
    void reloadPlaylist();
    std::deque<std::string> playlist;
    std::deque<uint16_t> playlist_startTime;
    std::string currentSource;
    size_t playlistIndex;
    size_t minIndex, maxIndex;
    uint32_t wallTime;
    uint32_t reloadOn;
  };
}// namespace Mist

#ifndef ONE_BINARY
typedef Mist::InputPlaylist mistIn;
#endif
