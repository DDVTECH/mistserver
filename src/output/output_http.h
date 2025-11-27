#pragma once
#include "output.h"
#include <mist/defines.h>
#include <mist/http_parser.h>
#include <mist/websocket.h>

namespace Mist{

  class HTTPOutput : public Output{
  public:
    HTTPOutput(Socket::Connection & conn, Util::Config & cfg, JSON::Value & capa);
    virtual ~HTTPOutput();
    static void init(Util::Config *cfg, JSON::Value & capa);
    virtual void onFail(const std::string &msg, bool critical = false);
    virtual void onHTTP();
    virtual void respondHTTP(const HTTP::Parser & req, bool headersOnly);
    virtual void onIdle(){};
    virtual void requestHandler(bool readable);
    virtual void preHTTP();
    virtual bool onFinish();
    virtual void sendNext();
    virtual void initialSeek(bool dryRun = false);
    static bool listenMode(Util::Config *config) { return false; }
    void reConnector(std::string &connector);
    std::string getHandler();
    bool parseRange(std::string header, uint64_t &byteStart, uint64_t &byteEnd);

    //WebSocket related
    virtual bool doesWebsockets(){return false;}
    virtual void onWebsocketFrame(){};
    virtual void onWebsocketConnect(){};
    virtual void preWebsocketConnect(){};
    virtual void onCommandSend(const std::string & data);
    virtual void atLivePoint();
    virtual void atDeadPoint();
    bool handleWebsocketCommands();
    bool handleCommand(const JSON::Value & command);
    void handleWebsocketIdle();
    bool handleWebsocketSeek(const JSON::Value & command);
    bool possiblyReselectTracks(uint64_t seekTarget);
  protected:
    //WebSocket related
    bool wsCmds; ///< If true, implements all our standard websocket-based seek/play/etc commands
    bool wsCmdForce; ///< If true, forces all our standard commands regardless of websocket connection status
    double target_rate; ///< Target playback speed rate (1.0 = normal, 0 = auto)
    uint64_t forwardTo; ///< Playback position we're fast-forwarding towards
    size_t prevVidTrack; ///< Previously selected main video track
    bool stayLive; ///< Whether or not we're trying to stay on the live-most point, for live streams

    std::string fwdHostStr; ///< Forwarded string IP, if non-empty
    std::string fwdHostBin; ///< Forwarded binary IP, if non-empty
    bool responded;
    HTTP::Parser H;
    HTTP::Websocket *webSock;
    uint32_t idleInterval; ///< Interval for the onIdle handler in milliseconds
    uint64_t idleLast; ///< Last time the onIdle handler was ran in BootMs
    uint64_t lastHTTPRequest; ///< BootSecs time of last parsed HTTP request
    std::string getConnectedHost();
    std::string getConnectedBinHost();
    bool isTrustedProxy(const std::string & ip);
  };
}// namespace Mist
