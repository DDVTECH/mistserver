#pragma once
#include "../io.h"
#include <cstdlib>
#include <map>
#include <mist/comms.h>
#include <mist/config.h>
#include <mist/dtsc.h>
#include <mist/flv_tag.h>
#include <mist/json.h>
#include <mist/shared_memory.h>
#include <mist/socket.h>
#include <mist/timing.h>
#include <mist/stream.h>
#include <mist/url.h>
#include <mist/ev.h>
#include <set>

namespace Mist{

  /// The output class is intended to be inherited by MistOut process classes.
  /// It contains all generic code and logic, while the child classes implement
  /// anything specific to particular protocols or containers.
  /// It contains several virtual functions, that may be overridden to "hook" into
  /// the streaming process at those particular points, simplifying child class
  /// logic and implementation details.
  class Output : public InOutBase{
  public:
    // constructor and destructor
    Output(Socket::Connection &conn);
    // static members for initialization and capabilities
    static void init(Util::Config *cfg);
    static JSON::Value capa;
    /*LTS-START*/
    std::string reqUrl;
    /*LTS-END*/
    // non-virtual generic functions
    virtual int run();
    virtual void stats(bool force = false);
    bool seek(uint64_t pos, bool toKey = false);
    bool seek(size_t tid, uint64_t pos, bool getNextKey);
    void seekKeyframesIn(unsigned long long pos, unsigned long long maxDelta);
    void stop();
    uint64_t currentTime();
    uint64_t startTime();
    uint64_t endTime();
    uint64_t targetTime();
    void setBlocking(bool blocking);
    bool selectDefaultTracks();
    static bool listenMode(){return true;}
    uint32_t currTrackCount() const;
    virtual bool isReadyForPlay();
    virtual bool reachedPlannedStop();
    // virtuals. The optional virtuals have default implementations that do as little as possible.
    /// This function is called whenever a packet is ready for sending.
    /// Inside it, thisPacket is guaranteed to contain a valid packet.
    virtual void sendNext(){}// REQUIRED! Others are optional.
    virtual bool dropPushTrack(uint32_t trackId, const std::string &dropReason);
    bool getKeyFrame();
    size_t prepareNext();
    virtual void dropTrack(size_t trackId, const std::string &reason, bool probablyBad = true);
    virtual void onRequest();
    static void listener(Util::Config & conf,
                         std::function<void(Socket::Connection &, Socket::Server &)> callback);
    virtual uint64_t getInitialSeekPosition();
    virtual void initialSeek(bool dryRun = false);
    uint64_t getMinKeepAway();
    virtual bool liveSeek(bool rateOnly = false);
    virtual bool onFinish(){return false;}
    void reconnect();
    void disconnect();
    virtual void initialize();
    virtual void sendHeader();
    virtual void onFail(const std::string &msg, bool critical = false);
    virtual void requestHandler(bool readable);
    static Util::Config *config;
    void playbackSleep(uint64_t millis);

    void selectAllTracks();

    /// Accessors for buffer SyncMode.
    void setSyncMode(bool synced){buffer.setSyncMode(synced);}
    bool getSyncMode() { return buffer.getSyncMode(); }

  private: // these *should* not be messed with in child classes.
    std::map<size_t, uint32_t> currentPage;
    void loadPageForKey(size_t trackId, size_t keyNum);
    uint64_t pageNumForKey(size_t trackId, size_t keyNum);
    uint64_t pageNumMax(size_t trackId);
    bool isRecordingToFile;
    uint64_t lastStats; ///< Time of last sending of stats.
    void reinitPlaylist(std::string &playlistBuffer, uint64_t &maxAge, uint64_t &maxEntries,
                        uint64_t &segmentCount, uint64_t &segmentsRemoved, uint64_t &curTime,
                        std::string targetDuration, HTTP::URL &playlistLocation);

    Util::packetSorter buffer; ///< A sorted list of next-to-be-loaded packets.
    bool sought;          ///< If a seek has been done, this is set to true. Used for seeking on
                          ///< prepareNext().
    std::string prevHost; ///< Old value for getConnectedBinHost, for caching
    uint64_t lastReceive;
    bool recursingSync;
    uint32_t seekCount;
    bool firstData;
    uint64_t lastPushUpdate;
    uint64_t outputStartMs; ///< bootMS() at time of output start (unrelated to media start)
    uint64_t lastSeekPos; ///< Last seek target position
    uint64_t totalPlaytime; ///< Appended to with current playback time every seek
    bool newUA;
    size_t maxWait; /// Maximum wait in milliseconds (scheduled next packet timing)
    
  protected:              // these are to be messed with by child classes
    std::string currentTarget;
    virtual bool inlineRestartCapable() const{
      return false;
    }///< True if the output is capable of restarting mid-stream. This is used for swapping recording files
    void closeMyConn();
    bool pushing;
    std::map<std::string, std::string> targetParams; /*LTS*/
    std::string UA;                                  ///< User Agent string, if known.
    uint64_t uaDelay;                                ///< Seconds to wait before setting the UA.
    uint64_t lastRecv;
    uint64_t dataWaitTimeout; ///< How long to wait for new packets before dropping a track, in milliseconds.

    // Playback timing related
    uint64_t timingBootMs; ///< System boot time of the last playback speed change.
    uint64_t timingMdiaMs; ///< Media time of the last playback speed change.
    uint64_t realTime; ///< Playback speed in ms of wallclock time per data-second. (0 is uncapped)
    uint64_t maxSkipAhead; ///< Maximum ms that we will go ahead of the intended timestamps.
    uint64_t needsLookAhead; ///< Amount of millis we need to be able to look ahead in the metadata
    void resetTiming(uint64_t currTime);

    virtual std::string getConnectedHost();
    virtual std::string getConnectedBinHost();
    virtual std::string getStatsName();
    virtual void determineExitReason() {}

    virtual void connStats(uint64_t now, Comms::Connections &statComm);

    std::set<size_t> getSupportedTracks(const std::string &type = "") const;

    inline virtual bool keepGoing(){return config->is_active && myConn;}

    Comms::Connections statComm;
    bool isBlocking; ///< If true, indicates that myConn is blocking.
    std::string tkn;    ///< Random identifier used to split connections into sessions
    uint64_t nextKeyTime();


    // Read/write status variables
    Socket::Connection &myConn; ///< Connection to the client.

    bool wantRequest; ///< If true, waits for a request.
    bool parseData; ///< If true, triggers initalization if not already done, sending of header, sending of packets.
    bool isInitialized; ///< If false, triggers initialization if parseData is true.
    bool sentHeader;    ///< If false, triggers sendHeader if parseData is true.

    virtual bool isRecording();
    virtual bool isFileTarget();
    virtual bool isPushing(){return pushing;};
    std::string getExitTriggerPayload();
    void recEndTrigger();
    void outputEndTrigger();
    bool allowPush(const std::string &passwd);
    bool checkStreamKey();
    void waitForStreamPushReady();

    uint64_t firstPacketTime;
    uint64_t lastPacketTime;
    uint64_t lastPacketBootMs;
    uint64_t interPacketTimes[10]; ///< Needed to calculate avgBetweenPackets
    uint64_t avgBetweenPackets; ///< Used to determine an appropriate wait time when unknown waiting is required.
    uint64_t packetCounter;
    uint64_t thisBootMs;

    std::map<size_t, IPC::sharedPage> curPage; ///< For each track, holds the page that is currently being written.
    
    bool liveSeekDisabled;

    Event::Loop evLp;

  };

}// namespace Mist
