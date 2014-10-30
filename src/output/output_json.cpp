#include "output_json.h"
#include <iomanip>

namespace Mist {
  OutJSON::OutJSON(Socket::Connection & conn) : HTTPOutput(conn){realTime = 0;}
  OutJSON::~OutJSON() {}
  
  void OutJSON::init(Util::Config * cfg){
    HTTPOutput::init(cfg);
    capa["name"] = "JSON";
    capa["desc"] = "Enables HTTP protocol JSON streaming.";
    capa["url_rel"] = "/$.json";
    capa["url_match"] = "/$.json";
    capa["url_handler"] = "http";
    capa["url_type"] = "json";
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
    H.SetHeader("Content-Type", "text/javascript");
    H.protocol = "HTTP/1.0";
    H.SendResponse("200", "OK", myConn);
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

  void OutJSON::onHTTP(){
    first = true;
    jsonp = "";
    if (H.GetVar("callback") != ""){jsonp = H.GetVar("callback");}
    if (H.GetVar("jsonp") != ""){jsonp = H.GetVar("jsonp");}
    initialize();
    for (std::map<int,DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++){
      if (it->second.type == "meta" ){
        selectedTracks.insert(it->first);
      }
    }
    seek(0);
    parseData = true;
    wantRequest = false;
  }

}
