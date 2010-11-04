#include <iostream>
#include "../sockets/SocketW.h"
#include <string>
#include <vector>
#include <cstdlib>
#include <cstdio>
#include <unistd.h>
#include <fcntl.h>

int main() {
  SWUnixSocket mySocket;
  mySocket.connect("/tmp/shared_socket");
  char buffer[500000];
  int msg;
  std::string input;
  // do something with mySocket...
  while( std::cin >> input && input != "") {}
  std::cout << "HTTP/1.1 200 OK\nConnection: close\nContent-Type: video/x-flv\n\n";
  while(std::cout.good()) {
    msg = mySocket.recv(&buffer[0],10000);
    std::cout.write(buffer,msg);
  }
  // disconnect
  mySocket.disconnect();
  return 0;
}
