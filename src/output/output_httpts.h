#include "output_http.h"
#include "output_ts_base.h"

namespace Mist{
  class OutHTTPTS : public TSOutput{
  public:
    OutHTTPTS(Socket::Connection &conn);
    ~OutHTTPTS();
    static void init(Util::Config *cfg);
    void onHTTP();
    void sendTS(const char *tsData, size_t len = 188);
    void initialSeek();

  private:
    bool isRecording();
    bool isFileTarget(){
      HTTP::URL target(config->getString("target"));
      if (isRecording() && (target.getExt() == "ts" || config->getString("target").substr(0, 8) == "ts-exec:")){return true;}
      return false;
    }
    virtual bool inlineRestartCapable() const{return true;}
    void sendHeader();
    float calculateSegmentDuration(std::string filepath, uint64_t & firstTime);
    // Location of playlist file which we need to keep updated
    std::string playlistLocation;
    std::string tsFilePath;
    // Subfolder name (based on playlist name) which gets prepended to each entry in the playlist file
    std::string prepend;
    // Defaults to True. When exporting to .m3u8 & TS, it will overwrite the existing playlist file and remove existing .TS files
    bool removeOldPlaylistFiles;
    // Amount of segments written to the playlist since the last 'EXT-X-PROGRAM-DATE-TIME' tag
    // uint32_t previousTimestamp;
  };
}// namespace Mist

typedef Mist::OutHTTPTS mistOut;
