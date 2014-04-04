#include "output_json.h"
#include <mist/http_parser.h>
#include <mist/defines.h>
#include <iomanip>

namespace Mist {
  OutJSON::OutJSON(Socket::Connection & conn) : Output(conn){
    realTime = 0;
  }
  
  OutJSON::~OutJSON() {}
  
  void OutJSON::init(Util::Config * cfg){
    capa["desc"] = "Enables HTTP protocol JSON streaming.";
    capa["deps"] = "HTTP";
    capa["url_rel"] = "/$.json";
    capa["url_match"] = "/$.json";
    capa["url_handler"] = "http";
    capa["url_type"] = "json";
    capa["socket"] = "http_json";
    cfg->addBasicConnectorOptions(capa);
    config = cfg;
  }
  
  void OutJSON::sendNext(){
    if(!first) {
      myConn.SendNow(", ", 2);
    }else{
      if (jsonp == ""){
        myConn.SendNow("[", 1);
      }else{
        myConn.SendNow(jsonp + "([");
      }
      first = false;
    }
    myConn.SendNow(currentPacket.toJSON().toString());
  }

  void OutJSON::sendHeader(){
    HTTP::Parser HTTP_S;
    FLV::Tag tag;
    HTTP_S.SetHeader("Content-Type", "text/javascript");
    HTTP_S.protocol = "HTTP/1.0";
    myConn.SendNow(HTTP_S.BuildResponse("200", "OK"));
    sentHeader = true;
  }
  
  bool OutJSON::onFinish(){
    if (jsonp == ""){
      myConn.SendNow("]\n\n", 3);
    }else{
      myConn.SendNow("]);\n\n", 5);
    }
    return false;
  }

  void OutJSON::onRequest(){
    HTTP::Parser HTTP_R;
    while (HTTP_R.Read(myConn)){
      DEBUG_MSG(DLVL_DEVEL, "Received request %s", HTTP_R.getUrl().c_str());
      first = true;
      myConn.setHost(HTTP_R.GetHeader("X-Origin"));
      streamName = HTTP_R.GetHeader("X-Stream");
      jsonp = "";
      if (HTTP_R.GetVar("callback") != ""){
        jsonp = HTTP_R.GetVar("callback");
      }
      if (HTTP_R.GetVar("jsonp") != ""){
        jsonp = HTTP_R.GetVar("jsonp");
      }
      initialize();
      for (std::map<int,DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++){
        if (it->second.type == "meta" ){
          selectedTracks.insert(it->first);
        }
      }
      seek(0);
      parseData = true;
      wantRequest = false;
      HTTP_R.Clean();
    }
  }

}
