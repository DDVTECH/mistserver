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
#include <signal.h>
#include <mist/ts_packet.h>
#include <mist/config.h>


namespace Analysers {
  /// Debugging tool for TS data.
  /// Expects TS data through stdin, outputs human-readable information to stderr.
  /// \return The return code of the analyser.
  int analyseTS(bool validate, bool analyse){
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
        if(analyse){std::cout << packet.toPrettyString();}
        if(packet.getSyncByte() == 0x47 && packet.AdaptationField() > 1 && packet.PCRFlag()){pcr = packet.PCR();}
      }
      if(bytes > 1024){
        long long int tTime = Util::bootSecs();
        if(validate && tTime - upTime > 5 && tTime - upTime > pcr/27000000){
          std::cerr << "data received too slowly" << std::endl;
          return 1;
        }
        bytes = 0;
      }
    }
    long long int finTime = Util::bootSecs();
    if(validate){
      fprintf(stdout,"time since boot,time at completion,real time duration of data receival,video duration\n");
      fprintf(stdout, "%lli000,%lli000,%lli000,%li \n",upTime,finTime,finTime-upTime,pcr/27000);
    }
    return 0;
  }
}

int main(int argc, char ** argv){
  Util::Config conf = Util::Config(argv[0], PACKAGE_VERSION);
  conf.addOption("analyse", JSON::fromString("{\"long\":\"analyse\", \"short\":\"a\", \"default\":1, \"long_off\":\"notanalyse\", \"short_off\":\"b\", \"help\":\"Analyse a file's contents (-a), or don't (-b) returning false on error. Default is analyse.\"}"));
  conf.addOption("validate", JSON::fromString("{\"long\":\"validate\", \"short\":\"V\", \"default\":0, \"long_off\":\"notvalidate\", \"short_off\":\"X\", \"help\":\"Validate (-V) the file contents or don't validate (-X) its integrity, returning false on error. Default is don't validate.\"}"));
  conf.parseArgs(argc, argv);
  return Analysers::analyseTS(conf.getBool("validate"),conf.getBool("analyse"));
}
