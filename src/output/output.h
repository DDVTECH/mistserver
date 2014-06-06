#include <set>
#include <cstdlib>
#include <map>
#include <mist/config.h>
#include <mist/json.h>
#include <mist/flv_tag.h>
#include <mist/timing.h>
#include <mist/dtsc.h>
#include <mist/socket.h>
#include <mist/shared_memory.h>
#include <GeoIP.h> /*LTS*/

namespace Mist {

  /// This struct keeps packet information sorted in playback order, so the
  /// Mist::Output class knows when to buffer which packet.
  struct sortedPageInfo {
    bool operator < (const sortedPageInfo & rhs) const {
      if (time < rhs.time) {
        return true;
      }
      return (time == rhs.time && tid < rhs.tid);
    }
    int tid;
    long long unsigned int time;
    unsigned int offset;
  };

  /// The output class is intended to be inherited by MistOut process classes.
  /// It contains all generic code and logic, while the child classes implement
  /// anything specific to particular protocols or containers.
  /// It contains several virtual functions, that may be overridden to "hook" into
  /// the streaming process at those particular points, simplifying child class
  /// logic and implementation details.
  class Output {
    public:
      //constructor and destructor
      Output(Socket::Connection & conn);
      virtual ~Output();
      //static members for initialization and capabilities
      static void init(Util::Config * cfg);
      static JSON::Value capa;
      static GeoIP * geoIP4; /*LTS*/
      static GeoIP * geoIP6; /*LTS*/
      //non-virtual generic functions
      int run();
      void stats();
      void seek(long long pos);
      bool seek(int tid, long long pos, bool getNextKey = false);
      void stop();
      void setBlocking(bool blocking);
      void updateMeta();
      //virtuals. The optional virtuals have default implementations that do as little as possible.
      virtual void sendNext() {}//REQUIRED! Others are optional.
      virtual void prepareNext();
      virtual void onRequest();
      virtual bool onFinish() {
        return false;
      }
      virtual void initialize();
      virtual void sendHeader();
      virtual void onFail();
    private://these *should* not be messed with in child classes.
      /*LTS-START*/
      void Log(std::string type, std::string message);
      bool checkLimits();
      bool isBlacklisted(std::string host, std::string streamName, int timeConnected);
      std::string hostLookup(std::string ip);
      bool onList(std::string ip, std::string list);
      std::string getCountry(std::string ip);
      /*LTS-END*/
      std::map<unsigned long, unsigned int> currKeyOpen;
      void loadPageForKey(long unsigned int trackId, long long int keyNum);
      bool isBlocking;///< If true, indicates that myConn is blocking.
      unsigned int lastStats;///<Time of last sending of stats.
      IPC::sharedClient statsPage;///< Shared memory used for statistics reporting.
      long long unsigned int firstTime;///< Time of first packet after last seek. Used for real-time sending.
      std::map<unsigned long, unsigned long> nxtKeyNum;///< Contains the number of the next key, for page seeking purposes.
      std::set<sortedPageInfo> buffer;///< A sorted list of next-to-be-loaded packets.
      std::map<unsigned long, unsigned long> lastKeyTime;///< Stores the time of the last keyframe, for preventing duplicates
    protected://these are to be messed with by child classes
      unsigned int getKeyForTime(long unsigned int trackId, long long timeStamp);
      IPC::sharedPage streamIndex;///< Shared memory used for metadata
      std::map<int, IPC::sharedPage> indexPages; ///< Maintains index pages of each track, holding information about available pages with DTSC packets.
      std::map<int, IPC::sharedPage> curPages; ///< Holds the currently used pages with DTSC packets for each track.
      /// \todo Privitize keyTimes
      IPC::sharedClient playerConn;///< Shared memory used for connection to MistIn process.
      std::map<int, std::set<int> > keyTimes; ///< Per-track list of keyframe times, for keyframe detection.
      //static member for initialization
      static Util::Config * config;///< Static, global configuration for the MistOut process

      //stream delaying variables
      unsigned int maxSkipAhead;///< Maximum ms that we will go ahead of the intended timestamps.
      unsigned int minSkipAhead;///< Minimum ms that we will go ahead of the intended timestamps.
      unsigned int realTime;///< Playback speed times 1000 (1000 == 1.0X). Zero is infinite.

      //Read/write status variables
      Socket::Connection & myConn;///< Connection to the client.
      std::string streamName;///< Name of the stream that will be opened by initialize()
      std::set<unsigned long> selectedTracks; ///< Tracks that are selected for playback
      bool wantRequest;///< If true, waits for a request.
      bool parseData;///< If true, triggers initalization if not already done, sending of header, sending of packets.
      bool isInitialized;///< If false, triggers initialization if parseData is true.
      bool sentHeader;///< If false, triggers sendHeader if parseData is true.

      //Read-only stream data variables
      DTSC::Packet currentPacket;///< The packet that is ready for sending now.
      DTSC::Meta myMeta;///< Up to date stream metadata
  };

}

