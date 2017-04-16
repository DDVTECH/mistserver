#include "analyser_dtsc.h"
#include <mist/h264.h>

void AnalyserDTSC::init(Util::Config &conf){
  Analyser::init(conf);
}

AnalyserDTSC::AnalyserDTSC(Util::Config &conf) : Analyser(conf){
  conn = Socket::Connection(0, fileno(stdin));
  totalBytes = 0;
}

bool AnalyserDTSC::parsePacket(){
  P.reInit(conn);
  if (conn && !P){
    FAIL_MSG("Invalid DTSC packet @ byte %llu", totalBytes)
    return false;
  }
  if (!conn && !P){
    stop();
    return false;
  }

  switch (P.getVersion()){
  case DTSC::DTSC_V1:{
    if (detail >= 2){
      std::cout << "DTSCv1 packet: " << P.getScan().toPrettyString() << std::endl;
    }
    break;
  }
  case DTSC::DTSC_V2:{
    mediaTime = P.getTime();
    if (detail >= 2){
      std::cout << "DTSCv2 packet (Track " << P.getTrackId() << ", time " << P.getTime()
                << "): " << P.getScan().toPrettyString() << std::endl;
    }
    break;
  }
  case DTSC::DTSC_HEAD:{
    if (detail >= 2){std::cout << "DTSC header: " << P.getScan().toPrettyString() << std::endl;}
    if (detail == 1){
      bool hasH264 = false;
      bool hasAAC = false;
      JSON::Value result;
      std::stringstream issues;
      DTSC::Meta M(P);
      for (std::map<unsigned int, DTSC::Track>::iterator it = M.tracks.begin();
           it != M.tracks.end(); it++){
        JSON::Value track;
        track["kbits"] = (long long)((double)it->second.bps * 8 / 1024);
        track["codec"] = it->second.codec;
        uint32_t shrtest_key = 0xFFFFFFFFul;
        uint32_t longest_key = 0;
        uint32_t shrtest_prt = 0xFFFFFFFFul;
        uint32_t longest_prt = 0;
        uint32_t shrtest_cnt = 0xFFFFFFFFul;
        uint32_t longest_cnt = 0;
        for (std::deque<DTSC::Key>::iterator k = it->second.keys.begin();
             k != it->second.keys.end(); k++){
          if (!k->getLength()){continue;}
          if (k->getLength() > longest_key){longest_key = k->getLength();}
          if (k->getLength() < shrtest_key){shrtest_key = k->getLength();}
          if (k->getParts() > longest_cnt){longest_cnt = k->getParts();}
          if (k->getParts() < shrtest_cnt){shrtest_cnt = k->getParts();}
          if (k->getParts()){
            if ((k->getLength() / k->getParts()) > longest_prt){
              longest_prt = (k->getLength() / k->getParts());
            }
            if ((k->getLength() / k->getParts()) < shrtest_prt){
              shrtest_prt = (k->getLength() / k->getParts());
            }
          }
        }
        track["keys"]["ms_min"] = (long long)shrtest_key;
        track["keys"]["ms_max"] = (long long)longest_key;
        track["keys"]["frame_ms_min"] = (long long)shrtest_prt;
        track["keys"]["frame_ms_max"] = (long long)longest_prt;
        track["keys"]["frames_min"] = (long long)shrtest_cnt;
        track["keys"]["frames_max"] = (long long)longest_cnt;
        if (longest_prt > 500){
          issues << "unstable connection (" << longest_prt << "ms " << it->second.codec
                 << " frame)! ";
        }
        if (shrtest_cnt < 6){
          issues << "unstable connection (" << shrtest_cnt << " " << it->second.codec
                 << " frames in key)! ";
        }
        if (it->second.codec == "AAC"){hasAAC = true;}
        if (it->second.codec == "H264"){hasH264 = true;}
        if (it->second.type == "video"){
          track["width"] = (long long)it->second.width;
          track["height"] = (long long)it->second.height;
          track["fpks"] = it->second.fpks;
          if (it->second.codec == "H264"){
            h264::sequenceParameterSet sps;
            sps.fromDTSCInit(it->second.init);
            h264::SPSMeta spsData = sps.getCharacteristics();
            track["h264"]["profile"] = spsData.profile;
            track["h264"]["level"] = spsData.level;
          }
        }
        result[it->second.getWritableIdentifier()] = track;
      }
      if ((hasAAC || hasH264) && M.tracks.size() > 1){
        if (!hasAAC){issues << "HLS no audio!";}
        if (!hasH264){issues << "HLS no video!";}
      }
      if (issues.str().size()){result["issues"] = issues.str();}
      std::cout << result.toString() << std::endl;
      stop();
    }
    break;
  }
  case DTSC::DTCM:{
    if (detail >= 2){std::cout << "DTCM command: " << P.getScan().toPrettyString() << std::endl;}
    break;
  }
  default: FAIL_MSG("Invalid DTSC packet @ byte %llu", totalBytes); break;
  }

  totalBytes += P.getDataLen();
  return true;
}

