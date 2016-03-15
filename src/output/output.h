#pragma once
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
/*LTS-START*/
#ifdef GEOIP
#include <GeoIP.h>
#endif
/*LTS-END*/
#include "../io.h"

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
    unsigned int tid;
    long long unsigned int time;
    unsigned int offset;
  };

  /// The output class is intended to be inherited by MistOut process classes.
  /// It contains all generic code and logic, while the child classes implement
  /// anything specific to particular protocols or containers.
  /// It contains several virtual functions, that may be overridden to "hook" into
  /// the streaming process at those particular points, simplifying child class
  /// logic and implementation details.
  class Output : public InOutBase {
    public:
      //constructor and destructor
      Output(Socket::Connection & conn);
      virtual ~Output();
      //static members for initialization and capabilities
      static void init(Util::Config * cfg);
      static JSON::Value capa;
      /*LTS-START*/
      #ifdef GEOIP
      static GeoIP * geoIP4;
      static GeoIP * geoIP6;
      #endif
      std::string reqUrl;
      /*LTS-END*/
      //non-virtual generic functions
      int run();
      void stats();
      void seek(unsigned long long pos);
      bool seek(unsigned int tid, unsigned long long pos, bool getNextKey = false);
      void stop();
      void setBlocking(bool blocking);
      long unsigned int getMainSelectedTrack();
      void updateMeta();
      void selectDefaultTracks();
      /*begin-roxlu*/
      bool openOutputFileForRecording(); // Opens the output file and uses dup2() to make sure that all stdout is written into a file.
      bool closeOutputFileForRecording(); // Closes the output file into which we're writing and resets the file descriptor. 
      /*end-roxlu*/
      static bool listenMode(){return true;}
      //virtuals. The optional virtuals have default implementations that do as little as possible.
      virtual void sendNext() {}//REQUIRED! Others are optional.
      virtual void prepareNext();
      virtual void onRequest();
      virtual bool onFinish() {
        return false;
      }
      void reconnect();
      virtual void initialize();
      virtual void sendHeader();
      virtual void onFail();
      virtual void requestHandler();
      virtual void onRecord(){
        wantRequest = false;
        parseData = true;
        realTime = 1000;

        seek(0);
      }
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
      int pageNumForKey(long unsigned int trackId, long long int keyNum);
      unsigned int lastStats;///<Time of last sending of stats.
      long long unsigned int firstTime;///< Time of first packet after last seek. Used for real-time sending.
      std::map<unsigned long, unsigned long> nxtKeyNum;///< Contains the number of the next key, for page seeking purposes.
      std::set<sortedPageInfo> buffer;///< A sorted list of next-to-be-loaded packets.
      bool sought;///<If a seek has been done, this is set to true. Used for seeking on prepareNext().
      bool completeKeyReadyTimeOut;//a bool to see if there has been a keyframe TimeOut for complete keys in Live
    protected://these are to be messed with by child classes
      std::string source;
      
      virtual std::string getConnectedHost();
      virtual std::string getConnectedBinHost();


      IPC::sharedClient statsPage;///< Shared memory used for statistics reporting.
      bool isBlocking;///< If true, indicates that myConn is blocking.
      unsigned int crc;///< Checksum, if any, for usage in the stats.
      unsigned int getKeyForTime(long unsigned int trackId, long long timeStamp);
      
      //stream delaying variables
      unsigned int maxSkipAhead;///< Maximum ms that we will go ahead of the intended timestamps.
      unsigned int minSkipAhead;///< Minimum ms that we will go ahead of the intended timestamps.
      unsigned int realTime;///< Playback speed in ms of data per second. eg: 0 is infinite, 1000 real-time, 5000 is 0.2X speed, 500 = 2X speed.
      bool completeKeysOnly;///< Bool if we send whole keys only, so the metadata is complete and the output knows in advance what will be sent.

      //Read/write status variables
      Socket::Connection & myConn;///< Connection to the client.

      bool wantRequest;///< If true, waits for a request.
      bool parseData;///< If true, triggers initalization if not already done, sending of header, sending of packets.
      bool isInitialized;///< If false, triggers initialization if parseData is true.
      bool sentHeader;///< If false, triggers sendHeader if parseData is true.

      std::map<int,DTSCPageData> bookKeeping;
      /*begin-roxlu*/
      int outputFileDescriptor; // Write output into this file.
      /*end-roxlu*/
      bool recording();
  };

}

