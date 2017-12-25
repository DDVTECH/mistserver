#include "output_h264.h"
#include <mist/mp4_generic.h>
#include <mist/bitfields.h>

namespace Mist{
  OutH264::OutH264(Socket::Connection &conn) : HTTPOutput(conn){
    if (targetParams.count("keysonly")){
      keysOnly = 1;
    }
    if (config->getString("target").size()){
      if (!streamName.size()){
        WARN_MSG("Recording unconnected H264 output to file! Cancelled.");
        conn.close();
        return;
      }
      if (config->getString("target") == "-"){
        parseData = true;
        wantRequest = false;
        INFO_MSG("Outputting %s to stdout in H264 format", streamName.c_str());
        return;
      }
      if (connectToFile(config->getString("target"))){
        parseData = true;
        wantRequest = false;
        INFO_MSG("Recording %s to %s in H264 format", streamName.c_str(), config->getString("target").c_str());
      }else{
        conn.close();
      }
    }
  }

  void OutH264::init(Util::Config *cfg){
    HTTPOutput::init(cfg);
    capa["name"] = "H264";
    capa["desc"] = "Enables HTTP protocol H264 Annex B streaming";
    capa["url_rel"] = "/$.h264";
    capa["url_match"] = "/$.h264";
    capa["codecs"][0u][0u].append("H264");

    JSON::Value opt;
    opt["arg"] = "string";
    opt["default"] = "";
    opt["arg_num"] = 1ll;
    opt["help"] = "Target filename to store H264 file as, or - for stdout.";
    cfg->addOption("target", opt);
  }

  bool OutH264::isRecording(){return config->getString("target").size();}

  void OutH264::sendNext(){
    if (keysOnly && !thisPacket.getFlag("keyframe")){return;}
    char *dataPointer = 0;
    unsigned int len = 0;
    thisPacket.getString("data", dataPointer, len);

    unsigned int i = 0;
    while (i + 4 < len){
      uint32_t ThisNaluSize = Bit::btohl(dataPointer+i);
      myConn.SendNow("\000\000\000\001", 4);
      myConn.SendNow(dataPointer + i + 4, ThisNaluSize);
      i += ThisNaluSize+4;
    }
  }

  void OutH264::sendHeader(){
    MP4::AVCC avccbox;
    unsigned int mainTrack = getMainSelectedTrack();
    if (mainTrack && myMeta.tracks.count(mainTrack)){
      avccbox.setPayload(myMeta.tracks[mainTrack].init);
      myConn.SendNow(avccbox.asAnnexB());
    }
    sentHeader = true;
  }

  void OutH264::onHTTP(){
    std::string method = H.method;
    //Set mode to key frames only
    keysOnly = (H.GetVar("keysonly") != "");
    H.Clean();
    H.SetHeader("Content-Type", "video/H264");
    H.protocol = "HTTP/1.0";
    H.setCORSHeaders();
    if (method == "OPTIONS" || method == "HEAD"){
      H.SendResponse("200", "OK", myConn);
      return;
    }
    H.SendResponse("200", "OK", myConn);
    parseData = true;
    wantRequest = false;
  }
}

