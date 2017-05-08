#include <mist/socket.h>
#include <mist/json.h>

namespace Controller {
  bool authorize(JSON::Value & Request, JSON::Value & Response, Socket::Connection & conn);
  int handleAPIConnection(Socket::Connection & conn);
  void handleAPICommands(JSON::Value & Request, JSON::Value & Response);
  void handleUDPAPI(void * np);
}
