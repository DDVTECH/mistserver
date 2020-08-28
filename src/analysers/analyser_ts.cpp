#include "analyser.h"
#include "analyser_ts.h"
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <mist/bitfields.h>
#include <mist/config.h>
#include <mist/defines.h>
#include <mist/ts_packet.h>
#include <signal.h>
#include <sstream>
#include <string.h>
#include <string>
#include <unistd.h>


std::set<unsigned int> pmtTracks;

void AnalyserTS::init(Util::Config &conf){
  Analyser::init(conf);
  JSON::Value opt;
  opt["long"] = "detail";
  opt["short"] = "D";
  opt["arg"] = "num";
  opt["default"] = 3;
  opt["help"] = "Detail level of analysis bitmask (default=3). 1 = PES, 2 = TS non-stream pkts, 4 "
                "= TS stream pkts, 32 = raw PES packet bytes, 64 = raw TS packet bytes";
  conf.addOption("detail", opt);
  opt.null();

  opt["long"] = "pid";
  opt["short"] = "P";
  opt["arg"] = "num";
  opt["default"] = 0;
  opt["help"] = "Only use the given PID, ignore others";
  conf.addOption("pid", opt);
  opt.null();

  opt["long"] = "raw";
  opt["short"] = "R";
  opt["arg"] = "str";
  opt["default"] = "";
  opt["help"] = "Write raw PES payloads to given file";
  conf.addOption("raw", opt);
  opt.null();
}

AnalyserTS::AnalyserTS(Util::Config &conf) : Analyser(conf){
  pidOnly = conf.getInteger("pid");
  if (conf.getString("raw").size()){
    outFile.open(conf.getString("raw").c_str());
  }
  bytes = 0;
}

bool AnalyserTS::parsePacket(){
  static char packetPtr[188];
  std::cin.read(packetPtr, 188);
  if (std::cin.gcount() != 188){return false;}
  DONTEVEN_MSG("Reading from position %" PRIu64, bytes);
  bytes += 188;
  if (!packet.FromPointer(packetPtr)){return false;}
  if (detail){
    if (packet.getUnitStart() && payloads.count(packet.getPID()) && payloads[packet.getPID()] != ""){
      if ((detail & 1) && (!pidOnly || packet.getPID() == pidOnly)){
        std::cout << printPES(payloads[packet.getPID()], packet.getPID());
      }
      payloads.erase(packet.getPID());
    }
    if (packet.getPID() == 0){((TS::ProgramAssociationTable *)&packet)->parsePIDs(pmtTracks);}
    if (packet.isPMT(pmtTracks)){((TS::ProgramMappingTable *)&packet)->parseStreams();}
    if ((((detail & 2) && !packet.isStream()) || ((detail & 4) && packet.isStream())) &&
        (!pidOnly || packet.getPID() == pidOnly)){
      std::cout << packet.toPrettyString(pmtTracks, 0, detail);
    }
    if (packet.getPID() >= 0x10 && !packet.isPMT(pmtTracks) && packet.getPID() != 17 &&
        (payloads[packet.getPID()].size() || packet.getUnitStart())){
      payloads[packet.getPID()].append(packet.getPayload(), packet.getPayloadLength());
    }
  }
  if (packet && packet.getAdaptationField() > 1 && packet.hasPCR()){
    mediaTime = packet.getPCR() / 27000;
  }
  return true;
}

AnalyserTS::~AnalyserTS(){
  for (std::map<size_t, std::string>::iterator it = payloads.begin(); it != payloads.end(); it++){
    if ((detail & 1) && (!pidOnly || it->first == pidOnly)){
      std::cout << printPES(it->second, it->first);
    }
  }
}

std::string AnalyserTS::printPES(const std::string &d, size_t PID){
  size_t headSize = 0;
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
  if (!known && d[3] == 0xBD){
    res << " [Private Stream 1]";
    known = true;
  }
  if (!known){res << " [Unknown stream ID: " << (int)d[3] << "]";}
  if (d[0] != 0 || d[1] != 0 || d[2] != 1){
    res << " [!!! INVALID START CODE: " << (int)d[0] << " " << (int)d[1] << " " << (int)d[2] << " ]";
  }
  size_t padding = 0;
  if (known){
    if ((d[6] & 0xC0) != 0x80){res << " [!INVALID FIRST BITS!]";}
    if (d[6] & 0x30){res << " [SCRAMBLED]";}
    if (d[6] & 0x08){res << " [Priority]";}
    if (d[6] & 0x04){res << " [Aligned]";}
    if (d[6] & 0x02){res << " [Copyrighted]";}
    if (d[6] & 0x01){
      res << " [Original]";
    }else{
      res << " [Copy]";
    }

    int timeFlags = ((d[7] & 0xC0) >> 6);
    if (timeFlags == 2){headSize += 5;}
    if (timeFlags == 3){headSize += 10;}
    if (d[7] & 0x20){
      res << " [ESCR present, not decoded!]";
      headSize += 6;
    }
    if (d[7] & 0x10){
      uint32_t es_rate = (Bit::btoh24(d.data() + 9 + headSize) & 0x7FFFFF) >> 1;
      res << " [ESR: " << (es_rate * 50) / 1024 << " KiB/s]";
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
    if (d[8] != headSize){
      padding = d[8] - headSize;
      res << " [Padding: " << padding << "b]";
    }
    if (timeFlags & 0x02){
      uint64_t time = ((d[9] & 0xE) >> 1);
      time <<= 15;
      time |= ((uint32_t)d[10] << 7) | ((d[11] >> 1) & 0x7F);
      time <<= 15;
      time |= ((uint32_t)d[12] << 7) | ((d[13] >> 1) & 0x7F);
      res.precision(3);
      res << " [PTS " << std::fixed << (time / 90000.0) << "s]";
    }
    if (timeFlags & 0x01){
      uint64_t time = ((d[14] >> 1) & 0x07);
      time <<= 15;
      time |= ((uint32_t)d[15] << 7) | (d[16] >> 1);
      time <<= 15;
      time |= ((uint32_t)d[17] << 7) | (d[18] >> 1);
      res.precision(3);
      res << " [DTS " << std::fixed << (time / 90000.0) << "s]";
    }
  }
  if ((((int)d[4]) << 8 | d[5]) != (d.size() - 6)){
    res << " [Size " << (((int)d[4]) << 8 | d[5]) + 6 << " => " << (d.size()) << "] [Payload "
        << (d.size() - 9 - headSize) << "]";
  }else{
    res << " [Size " << (d.size()) << "] [Payload " << (d.size() - 9 - headSize) << "]";
  }
  res << std::endl;

  if (outFile){
    outFile.write(d.data() + 9 + headSize + padding, d.size()-(9 + headSize + padding));
  }
  if (detail & 32){
    size_t counter = 0;
    for (size_t i = 9 + headSize + padding; i < d.size(); ++i){
      if ((i < d.size() - 4) && d[i] == 0 && d[i + 1] == 0 && d[i + 2] == 0 && d[i + 3] == 1){
        res << std::endl;
        counter = 0;
      }
      if ((i < d.size() - 3) && d[i] == 0 && d[i + 1] == 0 && d[i + 2] == 1){
        if (counter > 1){
          res << std::endl << "   ";
          counter = 0;
        }
      }
      res << std::hex << std::setw(2) << std::setfill('0') << (int)(d[i] & 0xff) << " ";
      if ((counter) % 32 == 31){res << std::endl;}
      counter++;
    }
    res << std::endl;
  }
  return res.str();
}
