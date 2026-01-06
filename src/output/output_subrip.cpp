#include "output_subrip.h"
#include <sstream>
#include <mist/checksum.h>
#include <mist/defines.h>
#include <mist/http_parser.h>

namespace Mist{
  OutSubRip::OutSubRip(Socket::Connection &conn) : HTTPOutput(conn){realTime = 0;}
  OutSubRip::~OutSubRip(){}

  void OutSubRip::init(Util::Config *cfg){
    HTTPOutput::init(cfg);
    capa["name"] = "SubRip";
    capa["friendly"] = "SubRip (SRT/WebVTT) over HTTP";
    capa["desc"] = "Pseudostreaming in SubRip Text (SRT) and WebVTT formats over HTTP";
    capa["url_match"].append("/$.srt");
    capa["url_match"].append("/$.vtt");
    capa["url_match"].append("/$.webvtt");
    capa["codecs"][0u][0u].append("subtitle");
    capa["methods"][0u]["handler"] = "http";
    capa["methods"][0u]["type"] = "html5/text/plain";
    capa["methods"][0u]["hrn"] = "SRT subtitle progressive";
    capa["methods"][0u]["priority"] = 8;
    capa["methods"][0u]["url_rel"] = "/$.srt";
    capa["methods"][1u]["handler"] = "http";
    capa["methods"][1u]["type"] = "html5/text/vtt";
    capa["methods"][1u]["hrn"] = "WebVTT subtitle progressive";
    capa["methods"][1u]["priority"] = 9;
    capa["methods"][1u]["url_rel"] = "/$.webvtt";
  }

  void OutSubRip::sendNext(){
    // Reached the end we wanted? Stop here.
    if (filter_to > 0 && thisTime > filter_to && filter_to > filter_from){
      config->is_active = false;
      return;
    }

    char *dataPointer = 0;
    size_t len = 0;
    thisPacket.getString("data", dataPointer, len);
    // ignore empty subs
    if (len == 0 || (len == 1 && dataPointer[0] == ' ')){return;}
    std::stringstream tmp;
    if (!webVTT){tmp << lastNum++ << std::endl;}

    char tmpBuf[50];
    size_t tmpLen =
        sprintf(tmpBuf, "%.2" PRIu64 ":%.2" PRIu64 ":%.2" PRIu64 ".%.3" PRIu64, (thisTime / 3600000),
                ((thisTime % 3600000) / 60000), (((thisTime % 3600000) % 60000) / 1000), thisTime % 1000);
    tmp.write(tmpBuf, tmpLen);
    tmp << " --> ";
    uint64_t time = thisTime + thisPacket.getInt("duration");
    if (time == thisTime){time += len * 75 + 800;}
    tmpLen = sprintf(tmpBuf, "%.2" PRIu64 ":%.2" PRIu64 ":%.2" PRIu64 ".%.3" PRIu64, (time / 3600000),
                     ((time % 3600000) / 60000), (((time % 3600000) % 60000) / 1000), time % 1000);
    tmp.write(tmpBuf, tmpLen);
    tmp << std::endl;
    myConn.SendNow(tmp.str());
    // prevent extra newlines
    while (len && dataPointer[len - 1] == '\n'){--len;}
    myConn.SendNow(dataPointer, len);
    myConn.SendNow("\n\n");
  }

  void OutSubRip::sendHeader(){
    H.setCORSHeaders();
    H.SetHeader("Content-Type", (webVTT ? "text/vtt; charset=utf-8" : "text/plain; charset=utf-8"));
    H.protocol = "HTTP/1.0";
    H.SendResponse("200", "OK", myConn);
    if (webVTT){myConn.SendNow("WEBVTT\n\n");}
    sentHeader = true;
  }

  void OutSubRip::onHTTP(){
    std::string method = H.method;
    webVTT = (H.url.find(".vtt") != std::string::npos) || (H.url.find(".webvtt") != std::string::npos);
    if (H.GetVar("track").size()){
      size_t tid = atoll(H.GetVar("track").c_str());
      if (M.getValidTracks().count(tid)){
        userSelect.clear();
        userSelect[tid].reload(streamName, tid);
      }
    }

    filter_from = 0;
    filter_to = 0;
    index = 0;

    if (H.GetVar("from") != ""){
      filter_from = JSON::Value(H.GetVar("from")).asInt();
      seek(filter_from);
    }
    if (H.GetVar("to") != ""){filter_to = JSON::Value(H.GetVar("to")).asInt();}
    if (filter_to){realTime = 0;}

    H.Clean();
    H.setCORSHeaders();
    if (method == "OPTIONS" || method == "HEAD"){
      H.SetHeader("Content-Type", (webVTT ? "text/vtt; charset=utf-8" : "text/plain; charset=utf-8"));
      H.protocol = "HTTP/1.0";
      H.SendResponse("200", "OK", myConn);
      H.Clean();
      return;
    }
    lastNum = 0;
    parseData = true;
    wantRequest = false;
  }
}// namespace Mist
