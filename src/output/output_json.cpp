#include "output_json.h"
#include <iomanip>

namespace Mist {
  OutJSON::OutJSON(Socket::Connection & conn) : HTTPOutput(conn){
    ws = 0;
    realTime = 0;
  }
  OutJSON::~OutJSON() {
    if (ws){
      delete ws;
      ws = 0;
    }
  }
  
  void OutJSON::init(Util::Config * cfg){
    HTTPOutput::init(cfg);
    capa["name"] = "JSON";
    capa["desc"] = "Enables HTTP protocol JSON streaming.";
    capa["url_match"] = "/$.json";
    capa["codecs"][0u][0u].append("@+meta");
    capa["methods"][0u]["handler"] = "http";
    capa["methods"][0u]["type"] = "html5/text/javascript";
    capa["methods"][0u]["priority"] = 0ll;
    capa["methods"][0u]["url_rel"] = "/$.json";
    capa["methods"][1u]["handler"] = "ws";
    capa["methods"][1u]["type"] = "html5/text/javascript";
    capa["methods"][1u]["priority"] = 0ll;
    capa["methods"][1u]["url_rel"] = "/$.json";
  }
  
  void OutJSON::sendNext(){
    if (ws){
      ws->sendFrame(thisPacket.toJSON().toString());
      return;
    }
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
    if (!jsonp.size() && !first){
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
    
    if (H.GetHeader("Upgrade") == "websocket"){
      ws = new HTTP::Websocket(myConn, H);
      if (!(*ws)){
        delete ws;
        ws = 0;
        return;
      }
      sentHeader = true;
      parseData = true;
      wantRequest = false;
      return;
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
    parseData = true;
    wantRequest = false;
  }

}

