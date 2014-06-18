#include OUTPUTTYPE
#include <mist/config.h>
#include <mist/socket.h>

int main(int argc, char * argv[]) {
  Util::Config conf(argv[0], PACKAGE_VERSION);
  mistOut::init(&conf);

  mistOut::capa["forward"]["streamname"]["name"] = "Stream";
  mistOut::capa["forward"]["streamname"]["help"] = "What streamname to serve.";
  mistOut::capa["forward"]["streamname"]["type"] = "str";
  mistOut::capa["forward"]["streamname"]["option"] = "--stream";
  mistOut::capa["forward"]["ip"]["name"] = "IP";
  mistOut::capa["forward"]["ip"]["help"] = "IP of forwarded connection.";
  mistOut::capa["forward"]["ip"]["type"] = "str";
  mistOut::capa["forward"]["ip"]["option"] = "--ip";
  
  conf.addOption("streamname",
                   JSON::fromString("{\"arg\":\"string\",\"short\":\"s\",\"long\":\"stream\",\"help\":\"The name of the stream that this connector will transmit.\"}"));
  conf.addOption("ip",
                   JSON::fromString("{\"arg\":\"string\",\"short\":\"i\",\"long\":\"ip\",\"help\":\"Ip addr of connection.\"}"));
  if (conf.parseArgs(argc, argv)) {
    if (conf.getBool("json")) {
      std::cout << mistOut::capa.toString() << std::endl;
      return -1;
    }
    Socket::Connection S(fileno(stdout),fileno(stdin) );
    mistOut tmp(S);
    return tmp.run();
  }
  return 0;
}
