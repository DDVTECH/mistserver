#include "input.h"
#include <mist/dtsc.h>
#include <deque>

namespace Mist {
  class inputPlaylist : public Input {
    public:
      inputPlaylist(Util::Config * cfg);
      bool needsLock(){return false;}
    protected:
      bool checkArguments();
      bool readHeader() { return true; }
      void stream();
      virtual bool needHeader(){return false;}
    private:
      void reloadPlaylist();
      std::deque<std::string> playlist;
      std::string currentSource;
      size_t playlistIndex;
      bool seenValidEntry;
  };
}

typedef Mist::inputPlaylist mistIn;

