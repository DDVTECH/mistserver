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
    capa["desc"] = "Enables HTTP protocol subtitle streaming in subrip and WebVTT formats.";
    capa["url_match"].append("/$.srt");
    capa["url_match"].append("/$.vtt");
    capa["codecs"][0u][0u].append("srt");
    capa["codecs"][0u][0u].append("TTXT");
    capa["methods"][0u]["handler"] = "http";
    capa["methods"][0u]["type"] = "html5/text/plain";
    capa["methods"][0u]["priority"] = 8ll;
    capa["methods"][0u]["url_rel"] = "/$.srt";
    capa["methods"][1u]["handler"] = "http";
    capa["methods"][1u]["type"] = "html5/text/vtt";
    capa["methods"][1u]["priority"] = 9ll;
    capa["methods"][1u]["url_rel"] = "/$.vtt";
  }
  
  void OutProgressiveSRT::sendNext(){
    char * dataPointer = 0;
    unsigned int len = 0;
    thisPacket.getString("data", dataPointer, len);
    //ignore empty subs
    if (len == 0 || (len == 1 && dataPointer[0] == ' ')){
      return;
    }
    std::stringstream tmp;
    if(!webVTT) {
      tmp << lastNum++ << std::endl;
    }
    long long unsigned int time = thisPacket.getTime();
    char tmpBuf[50];
    int tmpLen = sprintf(tmpBuf, "%.2llu:%.2llu:%.2llu.%.3llu", (time / 3600000), ((time % 3600000) / 60000), (((time % 3600000) % 60000) / 1000), time % 1000);
    tmp.write(tmpBuf, tmpLen);
    tmp << " --> ";
    time += thisPacket.getInt("duration");
    if (time == thisPacket.getTime()){
      time += len * 75 + 800;
    }
    tmpLen = sprintf(tmpBuf, "%.2llu:%.2llu:%.2llu.%.3llu", (time / 3600000), ((time % 3600000) / 60000), (((time % 3600000) % 60000) / 1000), time % 1000);
    tmp.write(tmpBuf, tmpLen);
    tmp << std::endl;
    myConn.SendNow(tmp.str());
    //prevent double newlines
    if (dataPointer[len-1] == '\n'){--dataPointer;}
    myConn.SendNow(dataPointer, len);
    myConn.SendNow("\n\n");
  }

  void OutProgressiveSRT::sendHeader(){
    H.setCORSHeaders();
    if (webVTT){
      H.SetHeader("Content-Type", "text/vtt; charset=utf-8");
    }else{
      H.SetHeader("Content-Type", "text/plain; charset=utf-8");
    }
    H.protocol = "HTTP/1.0";
    H.SendResponse("200", "OK", myConn);
    if (webVTT){
      myConn.SendNow("WEBVTT\n\n");
    }
    sentHeader = true;
  }

  void OutProgressiveSRT::onHTTP(){
    std::string method = H.method;
    webVTT = (H.url.find(".vtt") != std::string::npos);
    if (H.GetVar("track") != ""){
      selectedTracks.clear();
      selectedTracks.insert(JSON::Value(H.GetVar("track")).asInt());
    }
    H.Clean();
    H.setCORSHeaders();
    if(method == "OPTIONS" || method == "HEAD"){
      if (webVTT){
        H.SetHeader("Content-Type", "text/vtt; charset=utf-8");
      }else{
        H.SetHeader("Content-Type", "text/plain; charset=utf-8");
      }
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

