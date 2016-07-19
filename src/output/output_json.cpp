#include "output_json.h"
#include <iomanip>

namespace Mist {
  OutJSON::OutJSON(Socket::Connection & conn) : HTTPOutput(conn){realTime = 0;}
  OutJSON::~OutJSON() {}
  
  void OutJSON::init(Util::Config * cfg){
    HTTPOutput::init(cfg);
    capa["name"] = "JSON";
    capa["desc"] = "Enables HTTP protocol JSON streaming.";
    capa["url_match"] = "/$.json";
    capa["codecs"][0u][0u].append("srt");
    capa["codecs"][0u][0u].append("TTXT");
    capa["methods"][0u]["handler"] = "http";
    capa["methods"][0u]["type"] = "html5/text/javascript";
    capa["methods"][0u]["priority"] = 0ll;
    capa["methods"][0u]["url_rel"] = "/$.json";
  }
  
  void OutJSON::sendNext(){
    if (!jsonp.size()){
      if(!first) {
        myConn.SendNow(", ", 2);
      }else{
        myConn.SendNow("[", 1);
        first = false;
      }
    }else{
      myConn.SendNow(jsonp + "(");
    }
    myConn.SendNow(thisPacket.toJSON().toString());
    if (jsonp.size()){
      myConn.SendNow(");\n", 3);
    }
  }

  void OutJSON::sendHeader(){
    std::string method = H.method;
    H.Clean();
    H.SetHeader("Content-Type", "text/javascript");
    H.protocol = "HTTP/1.0";
    H.setCORSHeaders();
    H.SendResponse("200", "OK", myConn);
    sentHeader = true;
  }
  
  bool OutJSON::onFinish(){
    if (!jsonp.size()){
      myConn.SendNow("]);\n\n", 5);
    }
    myConn.close();
    return false;
  }

  void OutJSON::onHTTP(){
    std::string method = H.method;
    jsonp = "";
    if (H.GetVar("callback") != ""){jsonp = H.GetVar("callback");}
    if (H.GetVar("jsonp") != ""){jsonp = H.GetVar("jsonp");}
    if (H.GetVar("track") != ""){
      selectedTracks.clear();
      selectedTracks.insert(JSON::Value(H.GetVar("track")).asInt());
    }
    
    H.Clean();
    H.setCORSHeaders();
    if(method == "OPTIONS" || method == "HEAD"){
      H.SetHeader("Content-Type", "text/javascript");
      H.protocol = "HTTP/1.0";
      H.SendResponse("200", "OK", myConn);
      H.Clean();
      return;
    }
    
    first = true;
    initialize();
    if (!selectedTracks.size()){
      for (std::map<unsigned int,DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++){
        if (it->second.type == "meta" ){
          selectedTracks.insert(it->first);
        }
      }
    }
    parseData = true;
    wantRequest = false;
  }

}

