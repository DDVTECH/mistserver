#pragma once
#include "input.h"

#include <set>
#include <stdio.h> //for FILE

#include <mist/dtsc.h>

namespace Mist{
  ///\brief A simple structure used for ordering byte seek positions.
  struct seekPos{
    ///\brief Less-than comparison for seekPos structures.
    ///\param rhs The seekPos to compare with.
    ///\return Whether this object is smaller than rhs.
    bool operator<(const seekPos &rhs) const{
      if (seekTime < rhs.seekTime){return true;}
      if (seekTime == rhs.seekTime){return trackID < rhs.trackID;}
      return false;
    }
    uint64_t seekTime; ///< Stores the timestamp of the DTSC packet referenced by this structure.
    uint64_t bytePos;  ///< Stores the byteposition of the DTSC packet referenced by this structure.
    uint32_t trackID;  ///< Stores the track the DTSC packet referenced by this structure is
                       ///< associated with.
  };

  class InputDTSC : public Input{
  public:
    InputDTSC(Util::Config *cfg);
    bool needsLock();

    virtual std::string getConnectedBinHost(){
      if (srcConn){return srcConn.getBinHost();}
      return Input::getConnectedBinHost();
    }

  protected:
    // Private Functions
    bool openStreamSource();
    void closeStreamSource();
    void parseStreamHeader();
    bool checkArguments();
    bool readHeader();
    bool needHeader();
    void getNext(size_t idx = INVALID_TRACK_ID);
    void getNextFromStream(size_t idx = INVALID_TRACK_ID);
    void seek(uint64_t seekTime, size_t idx = INVALID_TRACK_ID);

    FILE *F;

    Socket::Connection srcConn;

    bool lockCache;
    bool lockNeeded;

    std::set<seekPos> currentPositions;

    uint64_t lastreadpos;

    char buffer[8];

    void seekNext(uint64_t ms, size_t trackIdx, bool forceSeek = false);
  };
}// namespace Mist

#ifndef ONE_BINARY
typedef Mist::InputDTSC mistIn;
#endif
