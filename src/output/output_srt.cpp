#include "output_srt.h"
#include <mist/http_parser.h>
#include <mist/defines.h>
#include <mist/checksum.h>
#include <iomanip>

namespace Mist {
  OutProgressiveSRT::OutProgressiveSRT(Socket::Connection & conn) : HTTPOutput(conn){realTime = 0;}
  OutProgressiveSRT::~OutProgressiveSRT() {}
  
  void OutProgressiveSRT::init(Util::Config * cfg){
    HTTPOutput::init(cfg);
    capa["name"] = "SRT";
    capa["desc"] = "Enables HTTP protocol subtitle streaming.";
    capa["url_rel"] = "/$.srt";
    capa["url_match"] = "/$.srt";
    capa["url_handler"] = "http";
    capa["url_type"] = "subtitle";
    capa["codecs"][0u][0u].append("srt");
    capa["codecs"][0u][0u].append("TTXT");
    capa["methods"][0u]["handler"] = "http";
    capa["methods"][0u]["type"] = "html5/text/plain";
    capa["methods"][0u]["priority"] = 8ll;
  }
  
  void OutProgressiveSRT::sendNext(){
    char * dataPointer = 0;
    unsigned int len = 0;
    thisPacket.getString("data", dataPointer, len);
    std::stringstream tmp;
    if(!webVTT) {
      tmp << lastNum++ << std::endl;
    }
    long long unsigned int time = thisPacket.getTime();
    char tmpBuf[50];
    int tmpLen = sprintf(tmpBuf, "%.2llu:%.2llu:%.2llu,%.3llu", (time / 3600000), ((time % 3600000) / 60000), (((time % 3600000) % 60000) / 1000), time % 1000);
    tmp.write(tmpBuf, tmpLen);
    tmp << " --> ";
    time += thisPacket.getInt("duration");
    if (time == thisPacket.getTime()){
      time += len * 100 + 1000;
    }
    tmpLen = sprintf(tmpBuf, "%.2llu:%.2llu:%.2llu,%.3llu", (time / 3600000), ((time % 3600000) / 60000), (((time % 3600000) % 60000) / 1000), time % 1000);
    tmp.write(tmpBuf, tmpLen);
    tmp << std::endl;
    myConn.SendNow(tmp.str());
    myConn.SendNow(dataPointer, len);
    myConn.SendNow("\n");
  }

  void OutProgressiveSRT::sendHeader(){
    H.SetHeader("Content-Type", "text/plain");
    H.protocol = "HTTP/1.0";
    H.SendResponse("200", "OK", myConn);
    sentHeader = true;
  }

  void OutProgressiveSRT::onHTTP(){
    std::string method = H.method;
    std::string url = H.url;
    if (H.GetVar("track") != ""){
      selectedTracks.insert(JSON::Value(H.GetVar("track")).asInt());
    }
    H.Clean();
    H.setCORSHeaders();
    if(method == "OPTIONS" || method == "HEAD"){
      H.SetHeader("Content-Type", "text/plain");
      H.protocol = "HTTP/1.0";
      H.SendResponse("200", "OK", myConn);
      H.Clean();
      return;
    }
    lastNum = 0;
    parseData = true;
    wantRequest = false;
  }
}

