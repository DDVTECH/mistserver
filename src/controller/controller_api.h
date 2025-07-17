#include <mist/ev.h>
#include <mist/http_parser.h>
#include <mist/json.h>
#include <mist/socket.h>
#include <mist/websocket.h>

class APIConn {
  public:
    Socket::Connection C;
    HTTP::Parser H;

    // Auth related
    bool authorized;
    size_t attempts;
    bool isLocal;

    // WebSocket related
    bool isWebSocket;
    HTTP::Websocket *W;
    std::string logArg;
    std::string accsArg;
    std::string strmsArg;
    uint64_t authTime;

    void log(uint64_t time, const std::string & kind, const std::string & message, const std::string & stream,
             uint64_t progPid, const std::string & exe, const std::string & line);
    void access(uint64_t time, const std::string & session, const std::string & stream, const std::string & connector,
                const std::string & host, uint64_t duration, uint64_t up, uint64_t down, const std::string & tags);
    void stream(const std::string & stream, uint8_t status, uint64_t viewers, uint64_t inputs, uint64_t outputs,
                const std::string & tags);

    APIConn(Event::Loop & evLp, Socket::Server & srv);
    ~APIConn();

  private:
    Event::Loop & E;
    int sock;
};

namespace Controller{
  void registerLogger(APIConn *aConn);
  void registerAccess(APIConn *aConn);
  void registerStreams(APIConn *aConn);
  void deregister(APIConn *aConn);

  void callLogger(uint64_t time, const std::string & kind, const std::string & message, const std::string & stream,
                  uint64_t progPid, const std::string & exe, const std::string & line);
  void callAccess(uint64_t time, const std::string & session, const std::string & stream, const std::string & connector,
                  const std::string & host, uint64_t duration, uint64_t up, uint64_t down, const std::string & tags);
  void callStreams(const std::string & stream, uint8_t status, uint64_t viewers, uint64_t inputs, uint64_t outputs,
                   const std::string & tags);

  bool authorize(JSON::Value &Request, JSON::Value &Response, Socket::Connection &conn);
  bool handleAPIConnection(APIConn *aConn);
  void handleAPICommands(JSON::Value &Request, JSON::Value &Response);
  void handleWebSocket(APIConn *aConn);
}// namespace Controller
