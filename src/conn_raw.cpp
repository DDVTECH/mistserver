/// \file conn_raw.cpp
/// Contains the main code for the RAW connector.

#include <iostream>
#include <mist/config.h>
#include <mist/socket.h>

/// Contains the main code for the RAW connector.
/// Expects a single commandline argument telling it which stream to connect to,
/// then outputs the raw stream to stdout.
int main(int argc, char  ** argv) {
  Util::Config conf(argv[0], PACKAGE_VERSION);
  conf.addOption("stream_name", JSON::fromString("{\"arg_num\":1, \"help\":\"Name of the stream to write to stdout.\"}"));
  conf.parseArgs(argc, argv);

  std::string input = "/tmp/shared_socket_" + conf.getString("stream_name");
  //connect to the proper stream
  Socket::Connection S(input);
  if (!S.connected()){
    std::cout << "Could not open stream " << conf.getString("stream_name") << std::endl;
    return 1;
  }
  //transport ~50kb at a time
  //this is a nice tradeoff between CPU usage and speed
  const char buffer[50000] = {0};
  while(std::cout.good() && S.read(buffer,50000)){std::cout.write(buffer,50000);}
  S.close();
  return 0;
}
