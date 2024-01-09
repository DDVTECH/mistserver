#pragma once
#include "output.h"
#include <mist/defines.h>
#include <mist/http_parser.h>
#include <mist/websocket.h>

namespace Mist{

  class HTTPOutput : public Output{
  public:
    HTTPOutput(Socket::Connection &conn);
    virtual ~HTTPOutput();
    static void init(Util::Config *cfg);
    virtual void onFail(const std::string &msg, bool critical = false);
    virtual void onHTTP();
    virtual void respondHTTP(const HTTP::Parser & req, bool headersOnly);
    virtual void onIdle(){};
    virtual void requestHandler();
    virtual void preHTTP();
    virtual bool onFinish();
    virtual void sendNext();
    static bool listenMode(){return false;}
    void reConnector(std::string &connector);
    std::string getHandler();
    bool parseRange(std::string header, uint64_t &byteStart, uint64_t &byteEnd);

    //WebSocket related
    virtual bool doesWebsockets(){return false;}
    virtual void onWebsocketFrame(){};
    virtual void onWebsocketConnect(){};
    virtual void preWebsocketConnect(){};
    bool handleWebsocketCommands();
    void handleWebsocketIdle();
    bool handleWebsocketSeek(const JSON::Value & command);
    bool possiblyReselectTracks(uint64_t seekTarget);
  protected:
    //WebSocket related
    bool wsCmds; ///< If true, implements all our standard websocket-based seek/play/etc commands
    double target_rate; ///< Target playback speed rate (1.0 = normal, 0 = auto)
    uint64_t forwardTo; ///< Playback position we're fast-forwarding towards
    size_t prevVidTrack; ///< Previously selected main video track
    bool stayLive; ///< Whether or not we're trying to stay on the live-most point, for live streams

    bool responded;
    HTTP::Parser H;
    HTTP::Websocket *webSock;
    uint32_t idleInterval;
    uint64_t idleLast;
    std::string getConnectedHost();             // LTS
    std::string getConnectedBinHost();          // LTS
    bool isTrustedProxy(const std::string &ip); // LTS
  };
}// namespace Mist
