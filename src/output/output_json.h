#include "output_http.h"
#include <mist/websocket.h>

namespace Mist{
  class OutJSON : public HTTPOutput{
  public:
    OutJSON(Socket::Connection &conn);
    static void init(Util::Config *cfg);
    void onHTTP();
    void onIdle();
    virtual void onWebsocketFrame();
    virtual void onWebsocketConnect();
    virtual void preWebsocketConnect();
    bool onFinish();
    void onFail(const std::string &msg, bool critical = false);
    void sendNext();
    void sendHeader();
    bool doesWebsockets(){return true;}

  protected:
    JSON::Value lastVal;
    std::string lastOutData;
    uint64_t lastOutTime;
    uint64_t lastSendTime;
    bool keepReselecting;
    std::string jsonp;
    std::string pushPass;
    uint64_t pushTrack;
    int64_t bootMsOffset;
    bool dupcheck;
    std::set<std::string> nodup;
    bool first;
    bool noReceive;
  };
}// namespace Mist

#ifndef ONE_BINARY
typedef Mist::OutJSON mistOut;
#endif
