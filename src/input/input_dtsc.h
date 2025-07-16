#include "input.h"

#include <mist/dtsc.h>
#include <mist/urireader.h>

#include <set>
#include <stdio.h> //for FILE

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
    virtual ~InputDTSC();
    bool needsLock();

    virtual std::string getConnectedBinHost(){
      if (srcConn){return srcConn.getBinHost();}
      return Input::getConnectedBinHost();
    }

  protected:
    bool openStreamSource();
    bool openRemoteSource();
    void closeStreamSource();
    void parseStreamHeader();
    bool checkArguments();
    bool getRemoteInfo();
    bool readHeader();
    bool needHeader();
    void postHeader();
    void getNext(size_t idx = INVALID_TRACK_ID);
    void getNextFromStream(size_t idx = INVALID_TRACK_ID, bool returnAfterMetaReceived = false, uint64_t timeLimit = 0);
    void seek(uint64_t seekTime, size_t idx = INVALID_TRACK_ID);
    bool bufferFrame(size_t idx, uint32_t keyNum);

    HTTP::URIReader F;

    Socket::Connection srcConn;

    bool lockCache{false};
    bool lockNeeded{false};

    std::set<seekPos> currentPositions;

    uint64_t lastReadPos{0};
    uint64_t lastRecvTime{0};
    uint64_t lastPktTime{0};
    uint64_t pageFillStart{0};
    HTTP::Downloader balanceDl;

    char buffer[8];
    Util::ResizeablePointer pktBuf;

    void seekNext(uint64_t ms, size_t trackIdx, bool forceSeek = false);
    void sendCmd(const JSON::Value & data);
    DTSC::packType handleCommandPacket();
    DTSC::packType handleCommandPacket(bool & clearMeta);
    DTSC::packType handlePacket(bool & clearMeta);

    bool cachedHeader{false};
    bool isSyncReceiver{false};
    bool isLive{false};
    bool isVod{false};
    bool infoReceived{false};
    bool headerReceived{false};
    bool gotPong{false};
    std::string uuid;
    HTTP::URL sourceUri;
    IPC::sharedPage page;
    std::map<std::string, uint64_t> requested;
  };
}// namespace Mist

typedef Mist::InputDTSC mistIn;
