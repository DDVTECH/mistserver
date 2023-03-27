#include "analyser_dtsc.h"
#include <iomanip>
#include <mist/h264.h>

void AnalyserDTSC::init(Util::Config &conf){
  Analyser::init(conf);
  JSON::Value opt;
  opt["long"] = "headless";
  opt["short"] = "H";
  opt["help"] = "Parse entire file or streams as a single headless DTSC packet";
  conf.addOption("headless", opt);
  opt["long"] = "sizeprepended";
  opt["short"] = "s";
  opt["help"] = "If set, data of packets is considered to be size-prepended";
  conf.addOption("sizeprepended", opt);
  opt.null();
}

bool AnalyserDTSC::open(const std::string &filename){
  if (!Analyser::open(filename)){return false;}
  conn.open(1, 0);
  totalBytes = 0;
  return true;
}

AnalyserDTSC::AnalyserDTSC(Util::Config &conf) : Analyser(conf){
  headLess = conf.getBool("headless");
  sizePrepended = conf.getBool("sizeprepended");
}

bool AnalyserDTSC::parsePacket(){
  if (headLess){
    Util::ResizeablePointer dataBuf;
    while (conn){
      if (conn.spool()){
        conn.Received().remove(dataBuf, conn.Received().bytes(0xFFFFFFFFul));
      }else{
        Util::sleep(50);
      }
    }
    DTSC::Scan S(dataBuf, dataBuf.size());
    std::cout << S.toPrettyString() << std::endl;
    return false;
  }
  P.reInit(conn);
  if (conn && !P){
    FAIL_MSG("Invalid DTSC packet @ byte %" PRIu64, totalBytes)
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
    if (detail >= 8){
      char *payDat;
      size_t payLen;
      P.getString("data", payDat, payLen);
      uint32_t currLen = 0;
      uint64_t byteCounter = 0;
      for (uint64_t i = 0; i < payLen; ++i){
        if (sizePrepended && !currLen){
          currLen = 4+Bit::btohl(payDat+i);
          byteCounter = 0;
        }
        if ((byteCounter % 32) == 0){std::cout << std::endl;}
        std::cout << std::hex << std::setw(2) << std::setfill('0') << (unsigned int)payDat[i];
        ++byteCounter;
        --currLen;
      }
      std::cout << std::dec << std::endl;
    }
    break;
  }
  case DTSC::DTSC_HEAD:{
    if (detail >= 3){
      std::cout << "DTSC header: ";
      std::cout << DTSC::Meta("", P.getScan()).toPrettyString();
    }
    if (detail == 2){std::cout << "DTSC header: " << P.getScan().toPrettyString() << std::endl;}
    if (detail == 1){
      bool hasH264 = false;
      bool hasAAC = false;
      JSON::Value result;
      std::stringstream issues;
      DTSC::Meta M("", P.getScan());
      std::set<size_t> validTracks = M.getValidTracks();
      for (std::set<size_t>::iterator it = validTracks.begin(); it != validTracks.end(); it++){
        std::string codec = M.getCodec(*it);
        JSON::Value track;
        track["kbits"] = M.getBps(*it) * 8 / 1024;
        track["codec"] = codec;
        uint32_t shrtest_key = 0xFFFFFFFFul;
        uint32_t longest_key = 0;
        uint32_t shrtest_prt = 0xFFFFFFFFul;
        uint32_t longest_prt = 0;
        uint32_t shrtest_cnt = 0xFFFFFFFFul;
        uint32_t longest_cnt = 0;

        DTSC::Keys keys(M.keys(*it));
        uint32_t firstKey = keys.getFirstValid();
        uint32_t endKey = keys.getEndValid();
        for (int i = firstKey; i < endKey; i++){
          uint64_t keyDuration = keys.getDuration(i);
          uint64_t keyParts = keys.getParts(i);
          if (!keyDuration){continue;}
          if (keyDuration > longest_key){longest_key = keyDuration;}
          if (keyDuration < shrtest_key){shrtest_key = keyDuration;}
          if (keyParts > longest_cnt){longest_cnt = keyParts;}
          if (keyParts < shrtest_cnt){shrtest_cnt = keyParts;}
          if (keyParts){
            if ((keyDuration / keyParts) > longest_prt){longest_prt = (keyDuration / keyParts);}
            if ((keyDuration / keyParts) < shrtest_prt){shrtest_prt = (keyDuration / keyParts);}
          }
        }
        track["keys"]["ms_min"] = shrtest_key;
        track["keys"]["ms_max"] = longest_key;
        track["keys"]["frame_ms_min"] = shrtest_prt;
        track["keys"]["frame_ms_max"] = longest_prt;
        track["keys"]["frames_min"] = shrtest_cnt;
        track["keys"]["frames_max"] = longest_cnt;
        if (longest_prt > 500){
          issues << "unstable connection (" << longest_prt << "ms " << codec << " frame)! ";
        }
        if (shrtest_cnt < 6){
          issues << "unstable connection (" << shrtest_cnt << " " << codec << " frames in key)! ";
        }
        if (codec == "AAC"){hasAAC = true;}
        if (codec == "H264"){hasH264 = true;}
        if (M.getType(*it) == "video"){
          track["width"] = M.getWidth(*it);
          track["height"] = M.getHeight(*it);
          track["fpks"] = M.getFpks(*it);
          if (codec == "H264"){
            h264::sequenceParameterSet sps;
            sps.fromDTSCInit(M.getInit(*it));
            h264::SPSMeta spsData = sps.getCharacteristics();
            track["h264"]["profile"] = spsData.profile;
            track["h264"]["level"] = spsData.level;
          }
        }
        result[M.getTrackIdentifier(*it)] = track;
      }
      if (hasAAC || hasH264){
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
  default: FAIL_MSG("Invalid DTSC packet @ byte %" PRIu64, totalBytes); break;
  }

  totalBytes += P.getDataLen();
  return true;
}
