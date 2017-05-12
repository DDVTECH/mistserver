#pragma once
#include "input.h"
#include <mist/dtsc.h>
#include <mist/nal.h>
#include <mist/ts_packet.h>
#include <mist/ts_stream.h>
#include <fstream>
#include <string>
#include <set>
#include <vector>
//#include <stdint.h>

#define BUFFERTIME 10

namespace Mist {

  enum PlaylistType { VOD, LIVE, EVENT };


  struct playListEntries
  {
    std::string filename;
    uint64_t bytePos;
    float duration;
    unsigned int timestamp;
    unsigned int wait;
  };

  class Playlist {
    public:
      Playlist();
    std::string codecs;
    std::string video;
    std::string audio;
    std::string uri;
    std::string uri_root;

    std::string source;  
    const char *packetPtr;

    int id;
    bool playlistEnd;
    int noChangeCount;
    int version;
    int targetDuration;
    uint64_t media_sequence;
    int lastFileIndex;
    int waitTime;
    bool vodLive;
    PlaylistType playlistType;
    std::deque<playListEntries> entries;
    int entryCount;
    int programId;
    int bandwidth;
    unsigned int lastTimestamp;
  };

  
  struct entryBuffer
  {
    int timestamp;
    playListEntries entry;
    int playlistIndex;
  };

  class inputHLS : public Input {
    public:
      inputHLS(Util::Config * cfg);
      ~inputHLS();
      bool needsLock();
      bool openStreamSource();
    protected:
      //Private Functions
      
      unsigned int startTime;
      PlaylistType playlistType;
      int version;
      int targetDuration;
      int media_sequence;
      bool endPlaylist;
      int currentPlaylist;
     
      bool initDone;
      std::string init_source;  

      //std::vector<playListEntries> entries;
      std::vector<Playlist> playlists;
      //std::vector<int> pidMapping;
      std::map<int,int> pidMapping;
      std::map<int,int> pidMappingR;

      std::string playlistFile;
      std::string playlistRootPath;
      std::vector<int> reloadNext;


      bool liveStream;
      int currentIndex;
      std::string currentFile;
      std::ifstream in;
      bool isUrl;

      TS::Stream tsStream;///<Used for parsing the incoming ts stream
      bool pushing;
      Socket::UDPConnection udpCon;
      std::string udpDataBuffer;
      Socket::Connection conn;
      TS::Packet tsBuf;

      int getFirstPlaylistToReload();

      int firstSegment();
      bool getNextSegment();
      void readPMT();
      bool setup();
      bool preSetup();
      bool readHeader();
      void getNext(bool smart = true);
      void seek(int seekTime);
      void trackSelect(std::string trackSpec);
      FILE * inFile;
      FILE * tsFile;
      bool openURL(std::string urlString, Playlist &p);


      void printContent();
      void printBuffer();
      bool readIndex();
      bool initPlaylist(std::string uri);
      bool readPlaylist(std::string uri);
      bool reloadPlaylist(Playlist &p);
      bool readNextFile();



      void parseStreamHeader();
      void addEntryToPlaylist(Playlist &p, std::string filename, float duration, uint64_t &totalBytes);

      int getMappedTrackId(int id);
      int getMappedTrackPlaylist(int id);
      int getOriginalTrackId(int playlistId, int id);
      int getEntryId(int playlistId, uint64_t bytePos);
      int cleanLine(std::string &s);
  };
}

typedef Mist::inputHLS mistIn;

