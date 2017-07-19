#pragma once
#include "input.h"
#include <fstream>
#include <mist/dtsc.h>
#include <mist/nal.h>
#include <mist/ts_packet.h>
#include <mist/ts_stream.h>
#include <set>
#include <string>
#include <vector>
//#include <stdint.h>

#define BUFFERTIME 10

namespace Mist{

  enum PlaylistType{VOD, LIVE, EVENT};

  struct playListEntries{
    std::string filename;
    uint64_t bytePos;
    float duration;
    unsigned int timestamp;
    unsigned int wait;
  };

  class Playlist{
  public:
    Playlist(const std::string &uriSrc = "");
    bool atEnd() const;
    bool isUrl() const;
    bool reload();
    void addEntry(const std::string &filename, float duration, uint64_t &totalBytes);
    bool loadURL(const std::string &loadUrl);

    std::string uri;
    std::string uri_root;

    std::string source;
    const char *packetPtr;

    int id;
    bool initDone;
    bool playlistEnd;
    int noChangeCount;
    int version;
    uint64_t media_sequence;
    int lastFileIndex;

    int waitTime;
    PlaylistType playlistType;
    std::deque<playListEntries> entries;
    int entryCount;
    unsigned int lastTimestamp;
    unsigned int startTime;
  };

  struct entryBuffer{
    int timestamp;
    playListEntries entry;
    int playlistIndex;
  };

  class inputHLS : public Input{
  public:
    inputHLS(Util::Config *cfg);
    ~inputHLS();
    bool needsLock();
    bool openStreamSource();

  protected:
    // Private Functions

    unsigned int startTime;
    PlaylistType playlistType;
    int version;
    int targetDuration;
    int media_sequence;
    bool endPlaylist;
    int currentPlaylist;

    // std::vector<playListEntries> entries;
    std::vector<Playlist> playlists;
    // std::vector<int> pidMapping;
    std::map<int, int> pidMapping;
    std::map<int, int> pidMappingR;

    std::vector<int> reloadNext;

    int currentIndex;
    std::string currentFile;
    std::ifstream in;

    TS::Stream tsStream; ///<Used for parsing the incoming ts stream

    Socket::Connection conn;
    TS::Packet tsBuf;

    int getFirstPlaylistToReload();

    int firstSegment();
    void waitForNextSegment();
    void readPMT();
    bool checkArguments();
    bool preSetup();
    bool readHeader();
    void getNext(bool smart = true);
    void seek(int seekTime);
    void trackSelect(std::string trackSpec);
    FILE *inFile;
    FILE *tsFile;

    bool readIndex();
    bool initPlaylist(const std::string &uri);
    bool readPlaylist(const std::string &uri);
    bool readNextFile();

    void parseStreamHeader();

    int getMappedTrackId(int id);
    int getMappedTrackPlaylist(int id);
    int getOriginalTrackId(int playlistId, int id);
    int getEntryId(int playlistId, uint64_t bytePos);
  };
}

typedef Mist::inputHLS mistIn;

