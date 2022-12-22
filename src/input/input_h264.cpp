#include "input_h264.h"
#include <mist/h264.h>
#include <mist/mp4_generic.h>

namespace Mist{
  InputH264::InputH264(Util::Config *cfg) : Input(cfg){
    capa["name"] = "H264";
    capa["desc"] = "This input allows you to take raw H264 Annex B data over a standard input "
                   "pipe, and turn it into a live stream.";
    capa["source_match"] = "h264-exec:*";
    // May be set to always-on mode
    capa["always_match"].append("h264-exec:*");
    capa["priority"] = 0;
    capa["codecs"]["video"].append("H264");
    frameCount = 0;
    startTime = Util::bootMS();
    inputProcess = 0;
  }

  bool InputH264::openStreamSource(){
    if (config->getString("input") != "-"){
      std::string input = config->getString("input");
      input = input.substr(10);

      char *args[128];
      uint8_t argCnt = 0;
      char *startCh = 0;
      for (char *i = (char *)input.c_str(); i <= input.data() + input.size(); ++i){
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

      int fin = -1, fout = -1;
      inputProcess = Util::Procs::StartPiped(args, &fin, &fout, 0);
      myConn.open(-1, fout);
    }else{
      myConn.open(fileno(stdout), fileno(stdin));
    }
    myConn.Received().splitter.assign("\000\000\001", 3);
    return true;
  }

  void InputH264::parseStreamHeader(){
    tNumber = meta.addTrack();
    meta.setType(tNumber, "video");
    meta.setCodec(tNumber, "H264");
    meta.setID(tNumber, tNumber);
    waitsSinceData = 0;
    INFO_MSG("Waiting for init data...");
    while (myConn && !M.getInit(tNumber).size()){getNext();}
    INFO_MSG("Init data received!");
  }

  bool InputH264::checkArguments(){
    std::string input = config->getString("input");
    if (input != "-" && input.substr(0, 10) != "h264-exec:"){
      Util::logExitReason(ER_FORMAT_SPECIFIC, "Unsupported input type: %s", input.c_str());
      return false;
    }
    return true;
  }

  void InputH264::getNext(size_t idx){
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
      INSANE_MSG("NAL unit, type %u, size %" PRIu32, nalType, nalSize);
      if (nalType == 7 || nalType == 8){
        if (nalType == 7){spsInfo = NAL.substr(0, nalSize);}
        if (nalType == 8){ppsInfo = NAL.substr(0, nalSize);}
        if (!meta.getInit(tNumber).size() && spsInfo.size() && ppsInfo.size()){
          h264::sequenceParameterSet sps(spsInfo.data(), spsInfo.size());
          h264::SPSMeta spsChar = sps.getCharacteristics();
          meta.setWidth(tNumber, spsChar.width);
          meta.setHeight(tNumber, spsChar.height);
          meta.setFpks(tNumber, spsChar.fps * 1000);
          if (M.getFpks(tNumber) < 100 || M.getFpks(tNumber) > 1000000){
            meta.setFpks(tNumber, 0);
          }
          MP4::AVCC avccBox;
          avccBox.setVersion(1);
          avccBox.setProfile(spsInfo[1]);
          avccBox.setCompatibleProfiles(spsInfo[2]);
          avccBox.setLevel(spsInfo[3]);
          avccBox.setSPSCount(1);
          avccBox.setSPS(spsInfo);
          avccBox.setPPSCount(1);
          avccBox.setPPS(ppsInfo);
          meta.setInit(tNumber, avccBox.payload(), avccBox.payloadSize());
        }
        continue;
      }
      if (M.getInit(tNumber).size()){
        uint64_t ts = Util::bootMS() - startTime;
        if (M.getFpks(tNumber)){ts = frameCount * (1000000 / M.getFpks(tNumber));}
        thisPacket.genericFill(ts, 0, tNumber, 0, 0, 0, h264::isKeyframe(NAL.data(), nalSize));
        thisPacket.appendNal(NAL.data(), nalSize);
        thisTime = ts;
        thisIdx = tNumber;
        ++frameCount;
        return;
      }
    }while (myConn && (inputProcess == 0 || Util::Procs::childRunning(inputProcess)));
    if (inputProcess){myConn.close();}
  }
}// namespace Mist
