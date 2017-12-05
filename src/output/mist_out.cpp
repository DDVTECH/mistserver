#include OUTPUTTYPE
#include <mist/config.h>
#include <mist/socket.h>
#include <mist/defines.h>
#include <mist/util.h>

int spawnForked(Socket::Connection & S){
  mistOut tmp(S);
  return tmp.run();
}

int main(int argc, char * argv[]) {
  Util::redirectLogsIfNeeded();
  Util::Config conf(argv[0]);
  mistOut::init(&conf);
  if (conf.parseArgs(argc, argv)) {
    if (conf.getBool("json")) {
      std::cout << mistOut::capa.toString() << std::endl;
      return -1;
    }
    conf.activate();
    if (mistOut::listenMode()){
      mistOut::listener(conf, spawnForked);
    }else{
      Socket::Connection S(fileno(stdout),fileno(stdin) );
      mistOut tmp(S);
      return tmp.run();
    }
  }
  return 0;
}
