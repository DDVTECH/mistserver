#include "output_progressive_mp3.h"
#include <mist/http_parser.h>
#include <mist/defines.h>

namespace Mist {
  OutProgressiveMP3::OutProgressiveMP3(Socket::Connection & conn) : Output(conn) {
    myConn.setHost(config->getString("ip"));
    streamName = config->getString("streamname");  
  }
  
  OutProgressiveMP3::~OutProgressiveMP3() {}
  
  void OutProgressiveMP3::init(Util::Config * cfg){
    Output::init(cfg);
    capa["name"] = "MP3";
    capa["desc"] = "Enables HTTP protocol progressive streaming.";
    capa["deps"] = "HTTP";
    capa["url_rel"] = "/$.mp3";
    capa["url_match"] = "/$.mp3";
    capa["socket"] = "http_progressive_mp3";
    capa["codecs"][0u][0u].append("MP3");
    capa["methods"][0u]["handler"] = "http";
    capa["methods"][0u]["type"] = "mp3";
    capa["methods"][0u]["priority"] = 8ll;

    cfg->addBasicConnectorOptions(capa);
    config = cfg;
  }
  
  void OutProgressiveMP3::sendNext(){
    char * dataPointer = 0;
    unsigned int len = 0;
    currentPacket.getString("data", dataPointer, len);
    myConn.SendNow(dataPointer, len);
  }

  void OutProgressiveMP3::sendHeader(){
    HTTP::Parser HTTP_S;
    FLV::Tag tag;
    HTTP_S.SetHeader("Content-Type", "audio/mpeg");
    HTTP_S.protocol = "HTTP/1.0";
    myConn.SendNow(HTTP_S.BuildResponse("200", "OK"));
    sentHeader = true;
  }

  void OutProgressiveMP3::onFail(){
    HTTP::Parser HTTP_S;
    HTTP_S.Clean(); //make sure no parts of old requests are left in any buffers
    HTTP_S.SetBody("Stream not found. Sorry, we tried.");
    HTTP_S.SendResponse("404", "Stream not found", myConn);
    Output::onFail();
  }
  
  void OutProgressiveMP3::onRequest(){
    HTTP::Parser HTTP_R;
    while (HTTP_R.Read(myConn)){
      DEBUG_MSG(DLVL_DEVEL, "Received request %s", HTTP_R.getUrl().c_str());
      if (HTTP_R.GetVar("audio") != ""){
        selectedTracks.insert(JSON::Value(HTTP_R.GetVar("audio")).asInt());
      }
      parseData = true;
      wantRequest = false;
      HTTP_R.Clean();
    }
  }

}
