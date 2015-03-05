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
    capa["codecs"][0u][0u].append("HEVC");
    capa["codecs"][0u][1u].append("AAC");
    capa["codecs"][0u][1u].append("MP3");
    capa["codecs"][0u][1u].append("AC3");
    capa["methods"][0u]["handler"] = "http";
    capa["methods"][0u]["type"] = "html5/video/mp2t";
    capa["methods"][0u]["priority"] = 1ll;
  }
  
  void OutHTTPTS::onHTTP(){
    initialize();
    H.Clean();
    H.SetHeader("Content-Type", "video/mp2t");
    H.StartResponse(H, myConn);    
    parseData = true;
    wantRequest = false;
    H.Clean(); //clean for any possible next requests
  }

  void OutHTTPTS::sendTS(const char * tsData, unsigned int len){
    H.Chunkify(tsData, len, myConn);
  }
  
}
