#include "input_h264.h"
#include <mist/h264.h>
#include <mist/mp4_generic.h>

namespace Mist{
  InputH264::InputH264(Util::Config *cfg) : Input(cfg){
    capa["name"] = "H264";
    capa["desc"] = "H264 Annex B input";
    capa["source_match"] = "h264-exec:*";
    //May be set to always-on mode
    capa["always_match"].append("h264-exec:*");
    capa["priority"] = 0ll;
    capa["codecs"][0u][0u].append("H264");
    frameCount = 0;
    startTime = Util::bootMS();
    inputProcess = 0;
  }

  bool InputH264::preRun(){
    if (config->getString("input") != "-"){
      std::string input = config->getString("input");
      const char *argv[2];
      input = input.substr(10);

      char *args[128];
      uint8_t argCnt = 0;
      char *startCh = 0;
      for (char *i = (char*)input.c_str(); i <= input.data() + input.size(); ++i){
        if (!*i){
          if (startCh){args[argCnt++] = startCh;}
          break;
        }
        if (*i == ' '){
          if (startCh){
            args[argCnt++] = startCh;
            startCh = 0;
            *i = 0;
          }
        }else{
          if (!startCh){startCh = i;}
        }
      }
      args[argCnt] = 0;

      int fin = -1, fout = -1, ferr = -1;
      inputProcess = Util::Procs::StartPiped(args, &fin, &fout, &ferr);
      myConn = Socket::Connection(-1, fout);
    }else{
      myConn = Socket::Connection(fileno(stdout), fileno(stdin));
    }
    myConn.Received().splitter.assign("\000\000\001", 3);
    myMeta.vod = false;
    myMeta.live = true;
    myMeta.tracks[1].type = "video";
    myMeta.tracks[1].codec = "H264";
    myMeta.tracks[1].trackID = 1;
    waitsSinceData = 0;
    return true;
  }

  bool InputH264::checkArguments(){
    std::string input = config->getString("input");
    if (input != "-" && input.substr(0, 10) != "h264-exec:"){
      FAIL_MSG("Unsupported input type: %s", input.c_str());
      return false;
    }
    return true;
  }

  void InputH264::getNext(bool smart){
    do{
      if (!myConn.spool()){
        Util::sleep(25);
        ++waitsSinceData;
        if (waitsSinceData > 5000 / 25 && (waitsSinceData % 40) == 0){
          WARN_MSG("No H264 data received for > 5s, killing source process");
          Util::Procs::Stop(inputProcess);
        }
        continue;
      }
      waitsSinceData = 0;
      uint32_t bytesToRead = myConn.Received().bytesToSplit();
      if (!bytesToRead){continue;}
      std::string NAL = myConn.Received().remove(bytesToRead);
      uint32_t nalSize = NAL.size() - 3;
      while (nalSize && NAL.data()[nalSize - 1] == 0){--nalSize;}
      if (!nalSize){continue;}
      uint8_t nalType = NAL.data()[0] & 0x1F;
      INSANE_MSG("NAL unit, type %u, size %lu", nalType, nalSize);
      if (nalType == 7 || nalType == 8){
        if (nalType == 7){spsInfo = NAL.substr(0, nalSize);}
        if (nalType == 8){ppsInfo = NAL.substr(0, nalSize);}
        if (!myMeta.tracks[1].init.size() && spsInfo.size() && ppsInfo.size()){
          h264::sequenceParameterSet sps(spsInfo.data(), spsInfo.size());
          h264::SPSMeta spsChar = sps.getCharacteristics();
          myMeta.tracks[1].width = spsChar.width;
          myMeta.tracks[1].height = spsChar.height;
          myMeta.tracks[1].fpks = spsChar.fps * 1000;
          if (myMeta.tracks[1].fpks < 100 || myMeta.tracks[1].fpks > 1000000){
            myMeta.tracks[1].fpks = 0;
          }
          MP4::AVCC avccBox;
          avccBox.setVersion(1);
          avccBox.setProfile(spsInfo[1]);
          avccBox.setCompatibleProfiles(spsInfo[2]);
          avccBox.setLevel(spsInfo[3]);
          avccBox.setSPSNumber(1);
          avccBox.setSPS(spsInfo);
          avccBox.setPPSNumber(1);
          avccBox.setPPS(ppsInfo);
          myMeta.tracks[1].init = std::string(avccBox.payload(), avccBox.payloadSize());
        }
        continue;
      }
      if (myMeta.tracks[1].init.size()){
        uint64_t ts = Util::bootMS() - startTime;
        if (myMeta.tracks[1].fpks){ts = frameCount * (1000000 / myMeta.tracks[1].fpks);}
        thisPacket.genericFill(ts, 0, 1, 0, 0, 0, h264::isKeyframe(NAL.data(), nalSize));
        thisPacket.appendNal(NAL.data(), nalSize, nalSize);
        ++frameCount;
        return;
      }
    }while (myConn && (inputProcess == 0 || Util::Procs::childRunning(inputProcess)));
    if (inputProcess){myConn.close();}
  }
}

