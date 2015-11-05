#include <fcntl.h>
#include <iostream>
#include <map>
#include <iomanip>
#include <string>
#include <cstdlib>
#include <cstdio>
#include <string.h>
#include <fstream>
#include <unistd.h>
#include <sstream>
#include <signal.h>
#include <mist/ts_packet.h>
#include <mist/ts_stream.h>
#include <mist/config.h>


namespace Analysers {
  /// Debugging tool for TS data.
  /// Expects TS data through stdin, outputs human-readable information to stderr.
  /// \return The return code of the analyser.
  int analyseTS(bool validate, bool analyse, int detailLevel){
    TS::Stream tsStream;
    std::map<unsigned long long, std::string> payloads;
    TS::Packet packet;
    long long int upTime = Util::bootSecs();
    int64_t pcr = 0;
    unsigned int bytes = 0;
    char packetPtr[188];
    while (std::cin.good()){
      std::cin.read(packetPtr,188);
      if(std::cin.gcount() != 188){break;}
      bytes += 188;
      if(packet.FromPointer(packetPtr)){
        //std::cout << packet.toPrettyString();
        tsStream.parse(packet, bytes);
        if (tsStream.hasPacket(packet.getPID())){
          DTSC::Packet dtscPack;
          tsStream.getPacket(packet.getPID(), dtscPack);
          std::cout << dtscPack.toJSON().toPrettyString();
        }
      }
    }
    return 0;
  }
}

int main(int argc, char ** argv){
  Util::Config conf = Util::Config(argv[0]);
  conf.addOption("analyse", JSON::fromString("{\"long\":\"analyse\", \"short\":\"a\", \"default\":1, \"long_off\":\"notanalyse\", \"short_off\":\"b\", \"help\":\"Analyse a file's contents (-a), or don't (-b) returning false on error. Default is analyse.\"}"));
  conf.addOption("validate", JSON::fromString("{\"long\":\"validate\", \"short\":\"V\", \"default\":0, \"long_off\":\"notvalidate\", \"short_off\":\"X\", \"help\":\"Validate (-V) the file contents or don't validate (-X) its integrity, returning false on error. Default is don't validate.\"}"));
  conf.addOption("detail", JSON::fromString("{\"long\":\"detail\", \"short\":\"D\", \"arg\":\"num\", \"default\":3, \"help\":\"Detail level of analysis.\"}"));
  conf.parseArgs(argc, argv);
  return Analysers::analyseTS(conf.getBool("validate"),conf.getBool("analyse"),conf.getInteger("detail"));
}
