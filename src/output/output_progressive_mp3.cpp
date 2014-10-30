#include "output_progressive_mp3.h"

namespace Mist {
  OutProgressiveMP3::OutProgressiveMP3(Socket::Connection & conn) : HTTPOutput(conn){}
  OutProgressiveMP3::~OutProgressiveMP3(){}
  
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
  }
  
  void OutProgressiveMP3::sendNext(){
    char * dataPointer = 0;
    unsigned int len = 0;
    currentPacket.getString("data", dataPointer, len);
    myConn.SendNow(dataPointer, len);
  }

  void OutProgressiveMP3::sendHeader(){
    H.SetHeader("Content-Type", "audio/mpeg");
    H.protocol = "HTTP/1.0";
    H.SendResponse("200", "OK", myConn);
    sentHeader = true;
  }

  void OutProgressiveMP3::onHTTP(){
    parseData = true;
    wantRequest = false;
  }

}
