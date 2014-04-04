#include OUTPUTTYPE
#include <mist/config.h>
#include <mist/socket.h>

int spawnForked(Socket::Connection & S){
  mistOut tmp(S);
  return tmp.run();
}

int main(int argc, char * argv[]) {
  Util::Config conf(argv[0], PACKAGE_VERSION);
  mistOut::init(&conf);
  if (conf.parseArgs(argc, argv)) {
    if (conf.getBool("json")) {
      std::cout << mistOut::capa.toString() << std::endl;
      return -1;
    }
    conf.serveForkedSocket(spawnForked);
  }
  return 0;
}
