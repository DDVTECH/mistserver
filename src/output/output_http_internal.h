#include "output_http.h"

namespace Mist{
  class OutHTTP : public HTTPOutput{
  public:
    OutHTTP(Socket::Connection &conn);
    ~OutHTTP();
    static void init(Util::Config *cfg);
    static bool listenMode();
    virtual void onFail(const std::string &msg, bool critical = false);
    /// preHTTP is disabled in the internal HTTP output, since most don't need the stream alive to work
    virtual void preHTTP(){};
    void HTMLResponse(const HTTP::Parser & req, bool headersOnly);
    void respondHTTP(const HTTP::Parser & req, bool headersOnly);
    void sendIcon(bool headersOnly);
    bool websocketHandler(const HTTP::Parser & req, bool headersOnly);
    JSON::Value getStatusJSON(std::string &reqHost, const std::string &useragent = "", bool metaEverywhere = false);
    bool stayConnected;
    virtual bool onFinish(){return stayConnected;}

  private:
    std::string origStreamName;
    std::string mistPath;
    std::string thisError;
  };
}// namespace Mist

#ifndef ONE_BINARY
typedef Mist::OutHTTP mistOut;
#endif
