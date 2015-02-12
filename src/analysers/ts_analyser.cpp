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
#include <mist/config.h>


namespace Analysers {
  std::string printPES(const std::string & pesPacket){
    std::stringstream res;
    res << "PES Packet" << std::endl;
    res << "  Packet Start Code Prefix: "
      << std::hex << std::setw(2) << std::setfill('0')
      << (int)pesPacket[0] << (int)pesPacket[1] << (int)pesPacket[2];
    res << std::endl;
    res << "  Stream Id: "
      << std::hex << std::setw(2) << std::setfill('0')
      << (int)pesPacket[3];
    res << std::endl;
    res << "  Packet Length: " << (((int)pesPacket[4]) << 8 | pesPacket[5]) << std::endl;
    res << "  PES Scrambling Control: " << (int)((pesPacket[6] & 0x30) >> 4) << std::endl;
    res << "  PES Priority: " << (int)((pesPacket[6] & 0x08) >> 3) << std::endl;
    res << "  Data Alginment Indicator: " << (int)((pesPacket[6] & 0x04) >> 2) << std::endl;
    res << "  Copyright: " << (int)((pesPacket[6] & 0x02) >> 1) << std::endl;
    res << "  Original or Copy: " << (int)((pesPacket[6] & 0x01)) << std::endl;
    int timeFlags = ((pesPacket[7] & 0xC0) >> 6);
    res << "  PTS/DTS Flags: " << timeFlags << std::endl;
    res << "  ESCR Flag: " << (int)((pesPacket[7] & 0x20) >> 5) << std::endl;
    res << "  ES Rate Flag: " << (int)((pesPacket[6] & 0x10) >> 4) << std::endl;
    res << "  DSM Trick Mode Flag: " << (int)((pesPacket[6] & 0x08) >> 3) << std::endl;
    res << "  Additional Copy Info Flag: " << (int)((pesPacket[6] & 0x04) >> 2) << std::endl;
    res << "  PES CRC Flag: " << (int)((pesPacket[6] & 0x02) >> 1) << std::endl;
    res << "  PES Extension Flag: " << (int)((pesPacket[6] & 0x01)) << std::endl;
    res << "  PES Header Data Length: " << (int)pesPacket[7] << std::endl;
    if (timeFlags & 0x02){
      long long unsigned int time = ((pesPacket[8] >> 1) & 0x07);
      time <<= 15;
      time |= ((int)pesPacket[9] << 7) | ((pesPacket[10] >> 1) & 0x7F);
      time <<= 15;
      time |= ((int)pesPacket[11] << 7) | ((pesPacket[12] >> 1) & 0x7F);
      res << "  PTS: " << std::dec << time << std::endl;
    }
    if (timeFlags & 0x01){
      long long unsigned int time = ((pesPacket[13] >> 1) & 0x07);
      time <<= 15;
      time |= ((int)pesPacket[14] << 7) | (pesPacket[15] >> 1);
      time <<= 15;
      time |= ((int)pesPacket[16] << 7) | (pesPacket[17] >> 1);
      res << "  DTS: " << std::dec << time << std::endl;
    }
    return res.str();
  }

  /// Debugging tool for TS data.
  /// Expects TS data through stdin, outputs human-readable information to stderr.
  /// \return The return code of the analyser.
  int analyseTS(bool validate, bool analyse){
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
        if(analyse){
          std::cout << packet.toPrettyString();
          if (packet.UnitStart() && payloads[packet.PID()] != ""){
            std::cout << printPES(payloads[packet.PID()]);
            payloads.erase(packet.PID());
          }
          payloads[packet.PID()].append(packet.getPayload(), packet.getPayloadLength());
        }
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
    for (std::map<unsigned long long, std::string>::iterator it = payloads.begin(); it != payloads.end(); it++){
      if (!it->first || it->first == 4096){ continue; }
      std::cout << "Remainder of a packet on track " << it->first << ":" << std::endl;
      std::cout << printPES(it->second);
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
