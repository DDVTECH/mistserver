#include "output_http.h"

namespace Mist{
  class OutH264 : public HTTPOutput{
  public:
    OutH264(Socket::Connection &conn);
    static void init(Util::Config *cfg);
    void respondHTTP(const HTTP::Parser & req, bool headersOnly);
    void sendNext();
    void sendHeader();
    bool doesWebsockets() { return true; }
    void onWebsocketConnect();
    void onWebsocketFrame();
    void onIdle();
    virtual bool onFinish();

  protected:
    void sendWebsocketCodecData(const std::string& type);
    bool handleWebsocketSeek(JSON::Value& command);
    bool handleWebsocketSetSpeed(JSON::Value& command);
    bool stayLive;
    uint64_t forwardTo;
    double target_rate; ///< Target playback speed rate (1.0 = normal, 0 = auto)
    size_t prevVidTrack;
    Util::ResizeablePointer webBuf;

  private:
    bool isRecording();
    bool keysOnly;
  };
}// namespace Mist

#ifndef ONE_BINARY
typedef Mist::OutH264 mistOut;
#endif
