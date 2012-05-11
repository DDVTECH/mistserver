/// \file conn_raw.cpp
/// Contains the main code for the RAW connector.

#include <iostream>
#include "../lib/socket.h"

/// Contains the main code for the RAW connector.
/// Expects a single commandline argument telling it which stream to connect to,
/// then outputs the raw stream to stdout.
int main(int argc, char  ** argv) {
  if (argc < 2){
    std::cout << "Usage: " << argv[0] << " stream_name" << std::endl;
    return 1;
  }
  std::string input = "/tmp/shared_socket_";
  input += argv[1];
  //connect to the proper stream
  Socket::Connection S(input);
  if (!S.connected()){
    std::cout << "Could not open stream " << argv[1] << std::endl;
    return 1;
  }
  //transport ~50kb at a time
  //this is a nice tradeoff between CPU usage and speed
  const char buffer[50000] = {0};
  while(std::cout.good() && S.read(buffer,50000)){std::cout.write(buffer,50000);}
  S.close();
  return 0;
}
