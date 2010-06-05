#include <iostream>
#include "sockets/SocketW.h"
#include <string>
#include <vector>
#include <cstdlib>

int main() {
  SWUnixSocket mySocket;
  mySocket.connect("../socketfile");
  std::string msg;
  // do something with mySocket...
  while(true) {
    msg = mySocket.recvmsg();
    std::cout << msg;
  }
  // disconnect
  mySocket.disconnect();
  return 0;
}
