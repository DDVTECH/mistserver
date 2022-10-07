#include "output_h264.h"
#include <mist/bitfields.h>
#include <mist/mp4_generic.h>

namespace Mist{
  OutH264::OutH264(Socket::Connection &conn) : HTTPOutput(conn){
    if (targetParams.count("keysonly")){keysOnly = 1;}
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
      if (genericWriter(config->getString("target"))){
        parseData = true;
        wantRequest = false;
        INFO_MSG("Recording %s to %s in H264 format", streamName.c_str(),
                 config->getString("target").c_str());
      }else{
        conn.close();
      }
    }
  }

  void OutH264::init(Util::Config *cfg){
    HTTPOutput::init(cfg);
    capa["name"] = "H264";
    capa["friendly"] = "H264 over HTTP";
    capa["desc"] = "Pseudostreaming in raw H264 Annex B format over HTTP";
    capa["url_rel"] = "/$.h264";
    capa["url_match"] = "/$.h264";
    capa["codecs"][0u][0u].append("H264");

    JSON::Value opt;
    opt["arg"] = "string";
    opt["default"] = "";
    opt["arg_num"] = 1;
    opt["help"] = "Target filename to store H264 file as, or - for stdout.";
    cfg->addOption("target", opt);
  }

  bool OutH264::isRecording(){return config->getString("target").size();}

  void OutH264::sendNext(){
    if (keysOnly && !thisPacket.getFlag("keyframe")){return;}
    char *dataPointer = 0;
    size_t len = 0;
    thisPacket.getString("data", dataPointer, len);

    unsigned int i = 0;
    while (i + 4 < len){
      uint32_t ThisNaluSize = Bit::btohl(dataPointer + i);
      myConn.SendNow("\000\000\000\001", 4);
      myConn.SendNow(dataPointer + i + 4, ThisNaluSize);
      i += ThisNaluSize + 4;
    }
  }

  void OutH264::sendHeader(){
    MP4::AVCC avccbox;
    size_t mainTrack = getMainSelectedTrack();
    if (mainTrack != INVALID_TRACK_ID){
      avccbox.setPayload(M.getInit(mainTrack));
      myConn.SendNow(avccbox.asAnnexB());
    }
    sentHeader = true;
  }

  void OutH264::onHTTP(){
    std::string method = H.method;
    // Set mode to key frames only
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
}// namespace Mist
