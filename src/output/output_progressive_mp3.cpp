#include "output_progressive_mp3.h"

namespace Mist {
  OutProgressiveMP3::OutProgressiveMP3(Socket::Connection & conn) : HTTPOutput(conn){}
  
  void OutProgressiveMP3::init(Util::Config * cfg){
    HTTPOutput::init(cfg);
    capa["name"] = "MP3";
    capa["friendly"] = "MP3 over HTTP";
    capa["desc"] = "Pseudostreaming in MP3 format over HTTP";
    capa["url_rel"] = "/$.mp3";
    capa["url_match"] = "/$.mp3";
    capa["codecs"][0u][0u].append("MP3");
    capa["methods"][0u]["handler"] = "http";
    capa["methods"][0u]["type"] = "html5/audio/mp3";
    capa["methods"][0u]["priority"] = 8;
  }
  
  void OutProgressiveMP3::sendNext(){
    char * dataPointer = 0;
    size_t len = 0;
    thisPacket.getString("data", dataPointer, len);
    myConn.SendNow(dataPointer, len);
  }

  void OutProgressiveMP3::sendHeader(){
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
