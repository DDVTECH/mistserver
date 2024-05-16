#pragma once
#include "http_parser.h"
#include "socket.h"
#include "url.h"
#include "util.h"

namespace HTTP{
  class Downloader{
  public:
    Downloader();
    ~Downloader();
    std::string &data();
    const std::string &const_data() const;
    void prepareRequest(const HTTP::URL &link, const std::string &method = "");
    void doRequest(const HTTP::URL &link, const std::string &method = "", const void *body = 0,
                   const size_t bodyLen = 0);
    void doRequest(const HTTP::URL &link, const std::string &method, const std::string &body);
    bool get(const std::string &link, Util::DataCallback &cb = Util::defaultDataCallback);
    bool get(const HTTP::URL &link, uint8_t maxRecursiveDepth = 6, Util::DataCallback &cb = Util::defaultDataCallback);
    bool head(const HTTP::URL &link, uint8_t maxRecursiveDepth = 6);
    bool getRange(const HTTP::URL &link, size_t byteStart, size_t byteEnd,
                  Util::DataCallback &cb = Util::defaultDataCallback);
    bool getRangeNonBlocking(const HTTP::URL &link, size_t byteStart, size_t byteEnd,
                             Util::DataCallback &cb = Util::defaultDataCallback);
    bool post(const HTTP::URL &link, const void *payload, const size_t payloadLen, bool sync = true,
              uint8_t maxRecursiveDepth = 6);
    bool post(const HTTP::URL &link, const std::string &payload, bool sync = true,
              uint8_t maxRecursiveDepth = 6);

    bool getNonBlocking(const HTTP::URL &link, uint8_t maxRecursiveDepth = 6);
    bool continueNonBlocking(Util::DataCallback &cb);

    std::string getHeader(const std::string &headerName);
    std::string &getStatusText();
    uint32_t getStatusCode();
    bool isOk();           ///< True if the request was successful.
    bool shouldContinue(); ///< True if the request should be followed-up with another. E.g.
                           ///< redirect or authenticate.
    bool canContinue(const HTTP::URL &link); ///< True if the request is able to continue, false if
                                             ///< there is a state error or some such.
    bool (*progressCallback)(); ///< Called every time the socket stalls, up to 4X per second.
    void setHeader(const std::string &name, const std::string &val);
    void clearHeaders();
    bool canRequest(const HTTP::URL &link);
    bool completed() const{return isComplete;}
    Parser &getHTTP();
    Socket::Connection &getSocket();
    const Socket::Connection &getSocket() const;
    void clean();
    void setSocket(Socket::Connection * socketPtr);
    uint32_t retryCount, dataTimeout;
    bool isProxied() const;
    const HTTP::URL &lastURL();

  private:
    bool isComplete;
    std::map<std::string, std::string> extraHeaders; ///< Holds extra headers to sent with request
    std::string connectedHost;                       ///< Currently connected host name
    uint32_t connectedPort;                          ///< Currently connected port number
    Parser H;                                        ///< HTTP parser for downloader
    Socket::Connection S;                            ///< TCP socket for downloader
    Socket::Connection * sPtr;                       ///< TCP socket override, when wanting to use an external socket
    bool ssl;                                        ///< True if ssl is currently in use.
    std::string authStr;      ///< Most recently seen WWW-Authenticate request
    std::string proxyAuthStr; ///< Most recently seen Proxy-Authenticate request
    bool proxied;             ///< True if proxy server is configured.
    HTTP::URL proxyUrl;       ///< Set to the URL of the configured proxy.
    size_t nbLoop;
    HTTP::URL nbLink;
    uint8_t nbMaxRecursiveDepth;
    uint64_t nbReqTime;
    uint64_t nbLastOff;
  };

}// namespace HTTP
