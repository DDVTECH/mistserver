#include "../input/input_ebml.h"
#include "../output/output_ebml.h"

namespace Mist{
  class OutENC{
  public:
    OutENC();
    bool isAudio;
    bool isVideo;
    void SetConfig(JSON::Value &config);
    bool CheckConfig();
    void Run();
    std::string getBitrateSetting();
    void setResolution(uint32_t x, uint32_t y);
    void setBitrate(std::string rate, std::string min, std::string max);
    void setCodec(std::string c);
    void setCRF(int crf);
    std::string flags;
    int sample_rate;
    std::string profile;
    std::string preset;

  private:
    JSON::Value opt;
    char ffcmd[10240];
    char *args[1280];
    uint32_t res_x;
    uint32_t res_y;
    std::string codec;
    std::string bitrate;
    std::string min_bitrate;
    std::string max_bitrate;
    int crf;
    bool checkVideoConfig();
    bool checkAudioConfig();
    bool buildVideoCommand();
    bool buildAudioCommand();
    void prepareCommand();
    std::set<std::string> supportedVideoCodecs;
    std::set<std::string> supportedAudioCodecs;
  };

  class EncodeInputEBML : public InputEBML{
  public:
    EncodeInputEBML(Util::Config *cfg) : InputEBML(cfg){};
    void getNext(size_t idx = INVALID_TRACK_ID);
    void setInFile(int stdin_val);
    bool needsLock(){return false;}
    bool isSingular(){return false;}
  };

  class EncodeOutputEBML : public OutEBML{
  public:
    virtual bool onFinish();
    virtual void dropTrack(size_t trackId, const std::string &reason, bool probablyBad = true);
    EncodeOutputEBML(Socket::Connection &c) : OutEBML(c){}; // realTime = 0;};
    bool isRecording(){return false;}
    void setVideoTrack(std::string tid);
    void setAudioTrack(std::string tid);
    void sendNext();
    void sendHeader();
    std::string getTrackType(int tid);
  };
}// namespace Mist
