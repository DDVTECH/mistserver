#include <mist/socket.h>
#include <mist/json.h>
#include <mist/websocket.h>
#include <mist/http_parser.h>

namespace Controller {
  bool authorize(JSON::Value & Request, JSON::Value & Response, Socket::Connection & conn);
  int handleAPIConnection(Socket::Connection & conn);
  void handleAPICommands(JSON::Value & Request, JSON::Value & Response);
  void handleWebSocket(HTTP::Parser & H, Socket::Connection & C);
  void handleUDPAPI(void * np);
}
