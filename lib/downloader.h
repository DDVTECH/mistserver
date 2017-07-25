#include "http_parser.h"
#include "socket.h"

namespace HTTP{
  class Downloader{
  public:
    Downloader(){progressCallback = 0;}
    std::string &data();
    void doRequest(const HTTP::URL &link);
    bool get(const std::string &link);
    bool get(const HTTP::URL &link, uint8_t maxRecursiveDepth = 6);
    std::string getHeader(const std::string &headerName);
    std::string &getStatusText();
    uint32_t getStatusCode();
    bool isOk();
    bool (*progressCallback)(); ///< Called every time the socket stalls, up to 4X per second.
    void setHeader(const std::string &name, const std::string &val);
    void clearHeaders();
    Parser &getHTTP();
    Socket::Connection &getSocket();

  private:
    std::map<std::string, std::string> extraHeaders; ///< Holds extra headers to sent with request
    std::string connectedHost;                       ///< Currently connected host name
    uint32_t connectedPort;                          ///< Currently connected port number
    Parser H;                                        ///< HTTP parser for downloader
    Socket::Connection S;                            ///< TCP socket for downloader
  };
}

