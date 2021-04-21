#include "output_http.h"
#include <list>
#include <mist/http_parser.h>

namespace Mist{
  class keyPart{
  public:
    bool operator<(const keyPart &rhs) const{
      if (time < rhs.time){return true;}
      if (time > rhs.time){return false;}
      if (trackID < rhs.trackID){return true;}
      return (trackID == rhs.trackID && index < rhs.index);
    }
    size_t trackID;
    uint64_t time;
    uint64_t byteOffset; // Stores relative bpos for fragmented MP4
    uint64_t index;
  };

  struct fragSet{
    uint64_t firstPart;
    uint64_t lastPart;
    uint64_t firstTime;
    uint64_t lastTime;
  };

  class OutMP4 : public HTTPOutput{
  public:
    OutMP4(Socket::Connection &conn);
    ~OutMP4();
    static void init(Util::Config *cfg);

    uint64_t mp4HeaderSize(uint64_t &fileSize, int fragmented = 0) const;
    std::string mp4Header(uint64_t &size, int fragmented = 0);

    uint64_t mp4moofSize(uint64_t startFragmentTime, uint64_t endFragmentTime, uint64_t &mdatSize) const;
    virtual void sendFragmentHeaderTime(uint64_t startFragmentTime,
                                        uint64_t endFragmentTime); // this builds the moof box for fragmented MP4

    void findSeekPoint(uint64_t byteStart, uint64_t &seekPoint, uint64_t headerSize);

    void onHTTP();
    void sendNext();
    void sendHeader();

  protected:
    uint64_t fileSize;
    uint64_t byteStart;
    uint64_t byteEnd;
    int64_t leftOver;
    uint64_t currPos;
    uint64_t seekPoint;

    uint64_t nextHeaderTime;
    uint64_t headerSize;

    // variables for standard MP4
    std::set<keyPart> sortSet; // needed for unfragmented MP4, remembers the order of keyparts

    // variables for fragmented
    size_t fragSeqNum;       // the sequence number of the next keyframe/fragment when producing
                             // fragmented MP4's
    size_t vidTrack;         // the video track we use as fragmenting base
    uint64_t realBaseOffset; // base offset for every moof packet
    // from sendnext

    bool sending3GP;

    uint64_t startTime;
    uint64_t endTime;

    bool chromeWorkaround;
    int keysOnly;
    uint64_t estimateFileSize() const;

    // This is a dirty solution... but it prevents copying and copying and copying again
    std::map<size_t, fragSet> currentPartSet;

    std::string protectionHeader(size_t idx);
  };
}// namespace Mist

typedef Mist::OutMP4 mistOut;
