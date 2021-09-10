#include <mist/http_parser.h>
#include <mist/json.h>
#include <mist/socket.h>
#include <mist/websocket.h>

namespace Controller{
  bool authorize(JSON::Value &Request, JSON::Value &Response, Socket::Connection &conn);
  int handleAPIConnection(Socket::Connection &conn);
  void handleAPICommands(JSON::Value &Request, JSON::Value &Response);
  void handleWebSocket(HTTP::Parser &H, Socket::Connection &C);
  void handleUDPAPI(void *np);
}// namespace Controller
