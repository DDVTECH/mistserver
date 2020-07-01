#include "output_aac.h"
#include <mist/ts_packet.h>

namespace Mist{
  OutAAC::OutAAC(Socket::Connection &conn) : HTTPOutput(conn){}

  void OutAAC::init(Util::Config *cfg){
    HTTPOutput::init(cfg);
    capa["name"] = "AAC";
    capa["friendly"] = "AAC over HTTP";
    capa["desc"] = "Pseudostreaming in AAC format over HTTP";
    capa["url_rel"] = "/$.aac";
    capa["url_match"] = "/$.aac";
    capa["codecs"][0u][0u].append("AAC");
    capa["methods"][0u]["handler"] = "http";
    capa["methods"][0u]["type"] = "html5/audio/aac";
    capa["methods"][0u]["priority"] = 8;

    JSON::Value opt;
    opt["arg"] = "string";
    opt["default"] = "";
    opt["arg_num"] = 1;
    opt["help"] = "Target filename to store AAC file as, or - for stdout.";
    cfg->addOption("target", opt);
  }

  bool OutAAC::isRecording(){return config->getString("target").size();}

  void OutAAC::initialSeek(){
    if (!meta){return;}
    maxSkipAhead = 30000;
    if (targetParams.count("buffer")){
      maxSkipAhead = atof(targetParams["buffer"].c_str())*1000;
    }
    Output::initialSeek();
    uint64_t cTime = currentTime();
    if (M.getLive() && cTime > maxSkipAhead){
      seek(cTime-maxSkipAhead);
    }
  }

  void OutAAC::sendNext(){
    char *dataPointer = 0;
    size_t len = 0;

    thisPacket.getString("data", dataPointer, len);
    std::string head = TS::getAudioHeader(len, M.getInit(thisIdx));
    myConn.SendNow(head);
    myConn.SendNow(dataPointer, len);
  }

  void OutAAC::sendHeader(){
    if (!isRecording()){
      H.Clean();
      H.SetHeader("Content-Type", "audio/aac");
      H.SetHeader("Accept-Ranges", "none");
      H.protocol = "HTTP/1.0";
      H.setCORSHeaders();
      H.SendResponse("200", "OK", myConn);
    }
    sentHeader = true;
  }

  void OutAAC::onHTTP(){
    std::string method = H.method;
    if (method == "OPTIONS" || method == "HEAD"){
      H.Clean();
      H.SetHeader("Content-Type", "audio/aac");
      H.SetHeader("Accept-Ranges", "none");
      H.protocol = "HTTP/1.0";
      H.setCORSHeaders();
      H.SendResponse("200", "OK", myConn);
      H.Clean();
      return;
    }

    parseData = true;
    wantRequest = false;
  }

}// namespace Mist
