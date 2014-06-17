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
  ///\brief Debugging tool for FLV data.
  ///
  /// Expects FLV data through stdin, outputs human-readable information to stderr.
  ///\return The return code of the analyser.
  int analyseTS(bool validate, bool analyse){
    std::string tsString;
    unsigned int pmt = 0;
    //std::map<unsigned int, TS::pmtinfo> PMT;
    TS::Packet packet;
    long long int upTime = Util::bootSecs();
    int64_t pcr = 0;
    unsigned int bytes = 0;
    char packetPtr[188];
    while (std::cin.good()){
      /*for(int i = 0; i < 188; i++){
        tsString += std::cin.r();
        bytes++;
      }*/
      std::cin.read(packetPtr,188);

      bytes += 188;
      if(std::cin.gcount() != 188){
        break;
      }
      if(packet.FromPointer(packetPtr)){
        if(analyse){
           std::cout << packet.toPrettyString();
          if(packet.PID() == 0x00){
            pmt = ((TS::ProgramAssociationTable&)packet).getProgramPID(0);              
          }else if(pmt > 0 && packet.PID() == pmt){
            std::cout << ((TS::ProgramMappingTable&)packet).toPrettyString(2);
            //PMT = TS::getPMTTable(packet);
            /*for(std::map<unsigned int, unsigned int>::iterator it = PMT.begin(); it != PMT.end();it++){
              std::cout << "PID: " << it->first << " - stype: " << it->second << std::endl;
            }*/
            
          }
        }
        if(packet.getSyncByte() == 0x47 && packet.AdaptationField() > 1 && packet.PCRFlag()){
          pcr = packet.PCR();
        }
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
  conf.addOption("analyse",
   JSON::fromString(
        "{\"long\":\"analyse\", \"short\":\"a\", \"default\":1, \"long_off\":\"notanalyse\", \"short_off\":\"b\", \"help\":\"Analyse a file's contents (-a), or don't (-b) returning false on error. Default is analyse.\"}"));
  
  conf.addOption("validate",
    JSON::fromString(
        "{\"long\":\"validate\", \"short\":\"V\", \"default\":0, \"long_off\":\"notvalidate\", \"short_off\":\"X\", \"help\":\"Validate (-V) the file contents or don't validate (-X) its integrity, returning false on error. Default is don't validate.\"}"));
  
  
  
  conf.parseArgs(argc, argv);
  return Analysers::analyseTS(conf.getBool("validate"),conf.getBool("analyse"));
}
