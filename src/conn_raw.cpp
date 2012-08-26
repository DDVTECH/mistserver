/// \file conn_raw.cpp
/// Contains the main code for the RAW connector.

#include <iostream>
#include <sstream>
#include <mist/config.h>
#include <mist/socket.h>

/// Contains the main code for the RAW connector.
/// Expects a single commandline argument telling it which stream to connect to,
/// then outputs the raw stream to stdout.
int main(int argc, char  ** argv) {
  Util::Config conf(argv[0], PACKAGE_VERSION);
  conf.addOption("stream_name", JSON::fromString("{\"arg_num\":1, \"help\":\"Name of the stream to write to stdout.\"}"));
  conf.parseArgs(argc, argv);

  //connect to the proper stream
  Socket::Connection S = Socket::getStream(conf.getString("stream_name"));
  S.setBlocking(false);
  if (!S.connected()){
    std::cout << "Could not open stream " << conf.getString("stream_name") << std::endl;
    return 1;
  }
  unsigned int lastStats = 0;
  unsigned int started = time(0);
  while(std::cout.good()){
    if (S.spool()){
      std::cout.write(S.Received().c_str(),S.Received().size());
      S.Received().clear();
    }else{
      usleep(10000);//sleep 10ms if no data
    }
    unsigned int now = time(0);
    if (now != lastStats){
      lastStats = now;
      std::stringstream st;
      st << "S localhost RAW " << (time(0) - started) << " " << S.dataDown() << " " << S.dataUp() << "\n";
      S.Send(st.str());
    }
  }
  std::stringstream st;
  st << "S localhost RAW " << (time(0) - started) << " " << S.dataDown() << " " << S.dataUp() << "\n";
  S.Send(st.str());
  S.flush();
  S.close();
  return 0;
}
