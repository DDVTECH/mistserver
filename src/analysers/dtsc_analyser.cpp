/// \file dtsc_analyser.cpp
/// Reads an DTSC file and prints all readable data about it

#include <string>
#include <iostream>
#include <sstream>

#include <mist/dtsc.h>
#include <mist/json.h>
#include <mist/config.h>
#include <mist/defines.h>
#include <mist/h264.h>

///\brief Holds everything unique to the analysers.  
namespace Analysers {
  ///\brief Debugging tool for DTSC data.
  ///
  /// Expects DTSC data in a file given on the command line, outputs human-readable information to stderr.
  ///\param conf The configuration parsed from the commandline.
  ///\return The return code of the analyser.
  int analyseDTSC(Util::Config conf){
    DTSC::File F(conf.getString("filename"));
    if (!F){
      std::cerr << "Not a valid DTSC file" << std::endl;
      return 1;
    }

    if (conf.getBool("compact")){
      bool hasH264 = false;
      bool hasAAC = false;
      bool unstable_keys = false;
      bool unstable_parts = false;
      JSON::Value result;
      std::stringstream issues;
      for (std::map<unsigned int, DTSC::Track>::iterator it = F.getMeta().tracks.begin(); it != F.getMeta().tracks.end(); it++){
        JSON::Value track;
        track["kbits"] = (long long)((double)it->second.bps * 8 / 1024);
        track["codec"] = it->second.codec;
        uint32_t shrtest_key = 0xFFFFFFFFul;
        uint32_t longest_key = 0;
        uint32_t shrtest_prt = 0xFFFFFFFFul;
        uint32_t longest_prt = 0;
        uint32_t shrtest_cnt = 0xFFFFFFFFul;
        uint32_t longest_cnt = 0;
        for (std::deque<DTSC::Key>::iterator k = it->second.keys.begin(); k != it->second.keys.end(); k++){
          if (!k->getLength()){continue;}
          if (k->getLength() > longest_key){longest_key = k->getLength();}
          if (k->getLength() < shrtest_key){shrtest_key = k->getLength();}
          if (k->getParts() > longest_cnt){longest_cnt = k->getParts();}
          if (k->getParts() < shrtest_cnt){shrtest_cnt = k->getParts();}
          if ((k->getLength()/k->getParts()) > longest_prt){longest_prt = (k->getLength()/k->getParts());}
          if ((k->getLength()/k->getParts()) < shrtest_prt){shrtest_prt = (k->getLength()/k->getParts());}
        }
        track["keys"]["min"] = (long long)shrtest_key;
        track["keys"]["max"] = (long long)longest_key;
        track["prts"]["min"] = (long long)shrtest_prt;
        track["prts"]["max"] = (long long)longest_prt;
        track["count"]["min"] = (long long)shrtest_cnt;
        track["count"]["max"] = (long long)longest_cnt;
        if (shrtest_key < longest_key / 2){issues << it->second.codec << " key duration unstable (variable key interval!) (" << shrtest_key << "-" << longest_key << ")! ";}
        if ((shrtest_prt < longest_prt / 2) && (shrtest_cnt != longest_cnt)){issues << it->second.codec << " part duration unstable (bad connection!) (" << shrtest_prt << "-" << longest_prt << ")! ";}
        if (it->second.codec == "AAC"){hasAAC = true;}
        if (it->second.codec == "H264"){hasH264 = true;}
        if (it->second.type=="video"){
          track["width"] = (long long)it->second.width;
          track["height"] = (long long)it->second.height;
          track["fpks"] = it->second.fpks;
        }
        result[it->second.getWritableIdentifier()] = track;
      }
      if (hasAAC || hasH264){
        if (!hasAAC){issues << "video-only! ";}
        if (!hasH264){issues << "audio-only! ";}
      }
      if (issues.str().size()){result["issues"] = issues.str();}
      std::cout << result.toString();
      return 0;
    }

    if (F.getMeta().vod || F.getMeta().live){
      F.getMeta().toPrettyString(std::cout,0, 0x03);
    }

    int bPos = 0;
    F.seek_bpos(0);
    F.parseNext();
    while (F.getPacket()){
      switch (F.getPacket().getVersion()){
        case DTSC::DTSC_V1: {
          std::cout << "DTSCv1 packet: " << F.getPacket().getScan().toPrettyString() << std::endl;
          break;
        }
        case DTSC::DTSC_V2: {
          std::cout << "DTSCv2 packet (Track " << F.getPacket().getTrackId() << ", time " << F.getPacket().getTime() << "): " << F.getPacket().getScan().toPrettyString() << std::endl;
          break;
        }
        case DTSC::DTSC_HEAD: {
          std::cout << "DTSC header: " << F.getPacket().getScan().toPrettyString() << std::endl;
          break;
        }
        case DTSC::DTCM: {
          std::cout << "DTCM command: " << F.getPacket().getScan().toPrettyString() << std::endl;
          break;
        }
        default:
          DEBUG_MSG(DLVL_WARN,"Invalid dtsc packet @ bpos %d", bPos);
          break;
      }
      bPos = F.getBytePos();
      F.parseNext();
    }
    return 0;
  }
}

/// Reads an DTSC file and prints all readable data about it
int main(int argc, char ** argv){
  Util::Config conf = Util::Config(argv[0]);
  conf.addOption("filename", JSON::fromString("{\"arg_num\":1, \"arg\":\"string\", \"help\":\"Filename of the DTSC file to analyse.\"}"));
  conf.addOption("compact", JSON::fromString("{\"short\": \"c\", \"long\": \"compact\", \"help\":\"Filename of the DTSC file to analyse.\"}"));
  conf.parseArgs(argc, argv);
  return Analysers::analyseDTSC(conf);
} //main
