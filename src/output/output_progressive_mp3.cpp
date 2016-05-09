#include "output_progressive_mp3.h"

namespace Mist {
  OutProgressiveMP3::OutProgressiveMP3(Socket::Connection & conn) : HTTPOutput(conn){
    if (config->getString("target").size()){
      if (!streamName.size()){
        WARN_MSG("Recording unconnected MP3 output to file! Cancelled.");
        conn.close();
        return;
      }
      if (config->getString("target") == "-"){
        parseData = true;
        wantRequest = false;
        INFO_MSG("Outputting %s to stdout in MP3 format", streamName.c_str());
        return;
      }
      if (connectToFile(config->getString("target"))){
        parseData = true;
        wantRequest = false;
        INFO_MSG("Recording %s to %s in MP3 format", streamName.c_str(), config->getString("target").c_str());
      }else{
        conn.close();
      }
    }
  }

  void OutProgressiveMP3::init(Util::Config * cfg){
    HTTPOutput::init(cfg);
    capa["name"] = "MP3";
    capa["desc"] = "Enables HTTP protocol progressive streaming.";
    capa["url_rel"] = "/$.mp3";
    capa["url_match"] = "/$.mp3";
    capa["codecs"][0u][0u].append("MP3");
    capa["methods"][0u]["handler"] = "http";
    capa["methods"][0u]["type"] = "html5/audio/mp3";
    capa["methods"][0u]["priority"] = 8ll;

    JSON::Value opt;
    opt["arg"] = "string";
    opt["default"] = "";
    opt["arg_num"] = 1ll;
    opt["help"] = "Target filename to store MP3 file as, or - for stdout.";
    cfg->addOption("target", opt);
  }
  
  bool OutProgressiveMP3::isRecording(){
    return config->getString("target").size();
  }
  
  void OutProgressiveMP3::sendNext(){
    char * dataPointer = 0;
    unsigned int len = 0;
    thisPacket.getString("data", dataPointer, len);
    myConn.SendNow(dataPointer, len);
  }

  void OutProgressiveMP3::sendHeader(){
    if (!isRecording()){
      std::string method = H.method;
      H.Clean();
      H.SetHeader("Content-Type", "audio/mpeg");
      H.protocol = "HTTP/1.0";
      H.setCORSHeaders();
      if(method == "OPTIONS" || method == "HEAD"){
        H.SendResponse("200", "OK", myConn);
        return;
      }
      H.SendResponse("200", "OK", myConn);
    }
    sentHeader = true;
  }

  void OutProgressiveMP3::onHTTP(){
    std::string method = H.method;
    
    H.Clean();
    H.setCORSHeaders();
    if(method == "OPTIONS" || method == "HEAD"){
      H.SetHeader("Content-Type", "audio/mpeg");
      H.protocol = "HTTP/1.0";
      H.SendResponse("200", "OK", myConn);
      H.Clean();
      return;
    }
    
    parseData = true;
    wantRequest = false;
  }

}
