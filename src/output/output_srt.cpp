#include "output_srt.h"
#include <mist/http_parser.h>
#include <mist/defines.h>
#include <iomanip>

namespace Mist {
  OutProgressiveSRT::OutProgressiveSRT(Socket::Connection & conn) : Output(conn) {
    realTime = 0;
  }

  void OutProgressiveSRT::onFail(){
    HTTP::Parser HTTP_S;
    HTTP_S.Clean(); //make sure no parts of old requests are left in any buffers
    HTTP_S.SetBody("Stream not found. Sorry, we tried.");
    HTTP_S.SendResponse("404", "Stream not found", myConn);
    Output::onFail();
  }
  
  OutProgressiveSRT::~OutProgressiveSRT() {}
  
  void OutProgressiveSRT::init(Util::Config * cfg){
    Output::init(cfg);
    capa["desc"] = "Enables HTTP protocol subtitle streaming.";
    capa["deps"] = "HTTP";
    capa["url_rel"] = "/$.srt";
    capa["url_match"] = "/$.srt";
    capa["url_handler"] = "http";
    capa["url_type"] = "subtitle";
    capa["socket"] = "http_srt";

    cfg->addBasicConnectorOptions(capa);
    config = cfg;
  }
  
  void OutProgressiveSRT::sendNext(){
    char * dataPointer = 0;
    unsigned int len = 0;
    currentPacket.getString("data", dataPointer, len);
    std::stringstream tmp;
    if(!webVTT) {
      tmp << lastNum++ << std::endl;
    }
    long long unsigned int time = currentPacket.getTime();
    char tmpBuf[50];
    int tmpLen = sprintf(tmpBuf, "%0.2llu:%0.2llu:%0.2llu,%0.3llu", (time / 3600000), ((time % 3600000) / 60000), (((time % 3600000) % 60000) / 1000), time % 1000);
    tmp.write(tmpBuf, tmpLen);
    tmp << " --> ";
    time += currentPacket.getInt("duration");
    tmpLen = sprintf(tmpBuf, "%0.2llu:%0.2llu:%0.2llu,%0.3llu", (time / 3600000), ((time % 3600000) / 60000), (((time % 3600000) % 60000) / 1000), time % 1000);
    tmp.write(tmpBuf, tmpLen);
    tmp << std::endl;
    myConn.SendNow(tmp.str());
    myConn.SendNow(dataPointer, len);
    myConn.SendNow("\n");
  }

  void OutProgressiveSRT::sendHeader(){
    HTTP::Parser HTTP_S;
    FLV::Tag tag;
    HTTP_S.SetHeader("Content-Type", "text/plain");
    HTTP_S.protocol = "HTTP/1.0";
    myConn.SendNow(HTTP_S.BuildResponse("200", "OK"));
    sentHeader = true;
  }

  void OutProgressiveSRT::onRequest(){
    HTTP::Parser HTTP_R;
    while (HTTP_R.Read(myConn)){
      DEBUG_MSG(DLVL_DEVEL, "Received request %s", HTTP_R.getUrl().c_str());
      lastNum = 0;
      webVTT = (HTTP_R.url.find(".webvtt") != std::string::npos);
      if (HTTP_R.GetVar("track") != ""){
        selectedTracks.insert(JSON::Value(HTTP_R.GetVar("track")).asInt());
      }
      myConn.setHost(HTTP_R.GetHeader("X-Origin"));
      streamName = HTTP_R.GetHeader("X-Stream");
      parseData = true;
      wantRequest = false;
      HTTP_R.Clean();
    }
  }

}
