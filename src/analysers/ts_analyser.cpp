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
  std::string printPES(const std::string & d, unsigned long PID, int detailLevel){    
    unsigned int headSize = 0;
    std::stringstream res;
    bool known = false;
    res << "[PES " << PID << "]";
    if ((d[3] & 0xF0) == 0xE0){
      res << " [Video " << (int)(d[3] & 0xF) << "]";
      known = true;
    }
    if (!known && (d[3] & 0xE0) == 0xC0){
      res << " [Audio " << (int)(d[3] & 0x1F) << "]";
      known = true;
    }
    if (!known){
      res << " [Unknown stream ID]";
    }
    if (d[0] != 0 || d[1] != 0 || d[2] != 1){
      res << " [!INVALID START CODE!]";
    }
    if (known){
      if ((d[6] & 0xC0) != 0x80){
        res << " [!INVALID FIRST BITS!]";
      }
      if (d[6] & 0x30){
        res << " [SCRAMBLED]";
      }
      if (d[6] & 0x08){
        res << " [Priority]";
      }
      if (d[6] & 0x04){
        res << " [Aligned]";
      }
      if (d[6] & 0x02){
        res << " [Copyrighted]";
      }
      if (d[6] & 0x01){
        res << " [Original]";
      }else{
        res << " [Copy]";
      }
      
      if (d[7] & 0x20){
        res << " [ESCR present, not decoded!]";
        headSize += 6;
      }
      if (d[7] & 0x10){
        res << " [ESR present, not decoded!]";
        headSize += 3;
      }
      if (d[7] & 0x08){
        res << " [Trick mode present, not decoded!]";
        headSize += 1;
      }
      if (d[7] & 0x04){
        res << " [Add. copy present, not decoded!]";
        headSize += 1;
      }
      if (d[7] & 0x02){
        res << " [CRC present, not decoded!]";
        headSize += 2;
      }
      if (d[7] & 0x01){
        res << " [Extension present, not decoded!]";
        headSize += 0; /// \todo Implement this. Complicated field, bah.
      }
      int timeFlags = ((d[7] & 0xC0) >> 6);
      if (timeFlags == 2){
        headSize += 5;
      }
      if (timeFlags == 3){
        headSize += 10;
      }
      if (d[8] != headSize){
        res << " [Padding: " << ((int)d[8] - headSize) << "b]";
      }
      if (timeFlags & 0x02){
        long long unsigned int time = (((unsigned int)d[9] & 0xE) >> 1);
        time <<= 15;
        time |= ((unsigned int)d[10] << 7) | (((unsigned int)d[11] >> 1) & 0x7F);
        time <<= 15;
        time |= ((unsigned int)d[12] << 7) | (((unsigned int)d[13] >> 1) & 0x7F);
        res << " [PTS " << ((double)time / 90000) << "s]";
      }
      if (timeFlags & 0x01){
        long long unsigned int time = ((d[14] >> 1) & 0x07);
        time <<= 15;
        time |= ((int)d[15] << 7) | (d[16] >> 1);
        time <<= 15;
        time |= ((int)d[17] << 7) | (d[18] >> 1);
        res << " [DTS " << ((double)time/90000) << "s]";
      }
    }
    if ((((int)d[4]) << 8 | d[5]) != (d.size() - 6)){
      res << " [Size " << (((int)d[4]) << 8 | d[5]) << " => " << (d.size() - 6) << "]";
    }
    res << std::endl;
    
    if(detailLevel==1){
      unsigned int counter = 0;
      for (unsigned int i = 9+headSize; i<d.size(); ++i){
        if ((i < d.size() - 4) && d[i] == 0 && d[i+1] == 0 && d[i+2] == 0 && d[i+3] == 1){res << std::endl; counter = 0;}
        res << std::hex << std::setw(2) << std::setfill('0') << (int)(d[i]&0xff) << " ";
        counter++;
        if ((counter) % 32 == 31){res << std::endl;}
      }
      res << std::endl;
    }
    return res.str();
  }

  /// Debugging tool for TS data.
  /// Expects TS data through stdin, outputs human-readable information to stderr.
  /// \return The return code of the analyser.
  int analyseTS(bool validate, bool analyse, int detailLevel){
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
          if (packet.getUnitStart() && payloads[packet.getPID()] != ""){
            std::cout << printPES(payloads[packet.getPID()], packet.getPID(), detailLevel);
            payloads.erase(packet.getPID());
          }
          if (detailLevel < 2){
            std::stringstream nul;
            nul << packet.toPrettyString(0, detailLevel);
          }else{
            std::cout << packet.toPrettyString(0, detailLevel);
          }
          if (packet.getPID() && !packet.isPMT()){
            payloads[packet.getPID()].append(packet.getPayload(), packet.getPayloadLength());
          }
        }
        if(packet && packet.getAdaptationField() > 1 && packet.hasPCR()){pcr = packet.getPCR();}
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
      std::cout << printPES(it->second, it->first, detailLevel);
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
  conf.addOption("detail", JSON::fromString("{\"long\":\"detail\", \"short\":\"D\", \"arg\":\"num\", \"default\":3, \"help\":\"Detail level of analysis.\"}"));
  conf.parseArgs(argc, argv);
  return Analysers::analyseTS(conf.getBool("validate"),conf.getBool("analyse"),conf.getInteger("detail"));
}
