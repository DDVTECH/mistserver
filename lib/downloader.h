#include "http_parser.h"
#include "socket.h"

namespace HTTP{
  class Downloader{
  public:
    Downloader();
    std::string &data();
    const std::string &const_data() const;
    void doRequest(const HTTP::URL &link, const std::string &method="", const std::string &body="");
    bool get(const std::string &link);
    bool get(const HTTP::URL &link, uint8_t maxRecursiveDepth = 6);
    bool post(const HTTP::URL &link, const std::string &payload, bool sync = true, uint8_t maxRecursiveDepth = 6);
    std::string getHeader(const std::string &headerName);
    std::string &getStatusText();
    uint32_t getStatusCode();
    bool isOk(); ///< True if the request was successful.
    bool shouldContinue(); ///<True if the request should be followed-up with another. E.g. redirect or authenticate.
    bool canContinue(const HTTP::URL &link);///<True if the request is able to continue, false if there is a state error or some such.
    bool (*progressCallback)(); ///< Called every time the socket stalls, up to 4X per second.
    void setHeader(const std::string &name, const std::string &val);
    void clearHeaders();
    bool canRequest(const HTTP::URL &link);
    Parser &getHTTP();
    Socket::Connection &getSocket();
    uint32_t retryCount, dataTimeout;

  private:
    std::map<std::string, std::string> extraHeaders; ///< Holds extra headers to sent with request
    std::string connectedHost;                       ///< Currently connected host name
    uint32_t connectedPort;                          ///< Currently connected port number
    Parser H;                                        ///< HTTP parser for downloader
    Socket::Connection S;                            ///< TCP socket for downloader
#ifdef SSL
    Socket::SSLConnection S_SSL; ///< SSL socket for downloader
#endif
    bool ssl;                 ///< True if ssl is currently in use.
    std::string authStr;      ///< Most recently seen WWW-Authenticate request
    std::string proxyAuthStr; ///< Most recently seen Proxy-Authenticate request
    bool proxied;             ///< True if proxy server is configured.
    HTTP::URL proxyUrl;       ///< Set to the URL of the configured proxy.
  };
}// namespace HTTP

