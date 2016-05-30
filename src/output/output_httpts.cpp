#include "output_httpts.h"
#include <mist/defines.h>
#include <mist/http_parser.h>
#include <mist/stream.h>
#include <unistd.h>

namespace Mist {
  OutHTTPTS::OutHTTPTS(Socket::Connection & conn) : TSOutput(conn) {}
  
  OutHTTPTS::~OutHTTPTS() {}

  void OutHTTPTS::init(Util::Config * cfg){
    HTTPOutput::init(cfg);
    capa["name"] = "HTTPTS";
    capa["desc"] = "Enables HTTP protocol MPEG2/TS pseudostreaming.";
    capa["url_rel"] = "/$.ts";
    capa["url_match"] = "/$.ts";
    capa["socket"] = "http_ts";
    capa["codecs"][0u][0u].append("H264");
    capa["codecs"][0u][1u].append("AAC");
    capa["codecs"][0u][1u].append("MP3");
    capa["methods"][0u]["handler"] = "http";
    capa["methods"][0u]["type"] = "html5/video/mp2t";
    capa["methods"][0u]["priority"] = 1ll;
  }
  
  void OutHTTPTS::onHTTP(){
    std::string method = H.method;
    
    initialize();
    H.SetHeader("Content-Type", "video/mp2t");
    H.setCORSHeaders();
    if(method == "OPTIONS" || method == "HEAD"){
      H.SendResponse("200", "OK", myConn);
      H.Clean();
      return;
    }
    H.StartResponse(H, myConn);    
    parseData = true;
    wantRequest = false;
  }

  void OutHTTPTS::sendTS(const char * tsData, unsigned int len){
    H.Chunkify(tsData, len, myConn);
  }
  
}
