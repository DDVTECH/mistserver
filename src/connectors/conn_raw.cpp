/// \file conn_raw.cpp
/// Contains the main code for the RAW connector.

#include <iostream>
#include <sstream>
#include <mist/config.h>
#include <mist/socket.h>
#include <mist/stream.h>
#include <mist/timing.h>

///\brief Contains the main code for the RAW connector.
///
///Expects a single commandline argument telling it which stream to connect to,
///then outputs the raw stream to stdout.
int main(int argc, char ** argv){
  Util::Config conf(argv[0], PACKAGE_VERSION);
  conf.addOption("stream_name", JSON::fromString("{\"arg_num\":1, \"help\":\"Name of the stream to write to stdout.\"}"));
  conf.parseArgs(argc, argv);

  //connect to the proper stream
  Socket::Connection S = Util::Stream::getStream(conf.getString("stream_name"));
  S.setBlocking(false);
  if ( !S.connected()){
    std::cout << "Could not open stream " << conf.getString("stream_name") << std::endl;
    return 1;
  }
  long long int lastStats = 0;
  long long int started = Util::epoch();
  while (std::cout.good()){
    if (S.spool()){
      while (S.Received().size()){
        std::cout.write(S.Received().get().c_str(), S.Received().get().size());
        S.Received().get().clear();
      }
    }else{
      Util::sleep(10); //sleep 10ms if no data
    }
    unsigned int now = Util::epoch();
    if (now != lastStats){
      lastStats = now;
      std::stringstream st;
      st << "S localhost RAW " << (Util::epoch() - started) << " " << S.dataDown() << " " << S.dataUp() << "\n";
      S.SendNow(st.str().c_str());
    }
  }
  std::stringstream st;
  st << "S localhost RAW " << (Util::epoch() - started) << " " << S.dataDown() << " " << S.dataUp() << "\n";
  S.SendNow(st.str().c_str());
  S.close();
  return 0;
}
