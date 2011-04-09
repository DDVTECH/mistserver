#include <iostream>
#include "../util/ddv_socket.h"
#include <string>
#include <vector>
#include <cstdlib>
#include <cstdio>
#include <unistd.h>
#include <fcntl.h>

int main(int argc, char  ** argv) {
  if (argc < 2){
    std::cout << "Usage: " << argv[0] << " stream_name" << std::endl;
    return 1;
  }
  std::string input;
  input = "/tmp/shared_socket_";
  input += argv[1];
  DDV::Socket S(input);
  if (!S.connected()){
    std::cout << "Could not open stream " << argv[1] << std::endl;
    return 1;
  }
  char buffer[50000];
  int msg;
  while(std::cout.good() && S.read(buffer,50000)){
    std::cout.write(buffer,50000);
  }
  S.close();
  return 0;
}
