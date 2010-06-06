#include <iostream>
#include "sockets/SocketW.h"
#include <string>
#include <vector>
#include <cstdlib>

int main() {
  SWUnixSocket mySocket;
  mySocket.connect("/tmp/socketfile");
  char buffer[500000];
  int msg;
  // do something with mySocket...
  std::cout << "HTTP/1.1 200 OK\nConnection: close\nContent-Type: video/x-flv\n\n";
  while(true) {
    msg = mySocket.recv(&buffer[0],10000);
    std::cout.write(buffer,msg);
  }
  // disconnect
  mySocket.disconnect();
  return 0;
}
