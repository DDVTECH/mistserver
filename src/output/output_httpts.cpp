#include "output_httpts.h"
#include <mist/defines.h>
#include <mist/http_parser.h>
#include <mist/stream.h>
#include <unistd.h>
#include <mist/procs.h>

namespace Mist{
  OutHTTPTS::OutHTTPTS(Socket::Connection & conn) : TSOutput(conn){
    sendRepeatingHeaders = 500;//PAT/PMT every 500ms (DVB spec)
    
    if(config->getString("target").substr(0,8) == "ts-exec:"){
        std::string input = config->getString("target").substr(8);
        char *args[128];
        uint8_t argCnt = 0;
        char *startCh = 0;
        for (char *i = (char *)input.c_str(); i <= input.data() + input.size(); ++i){
          if (!*i){
            if (startCh){args[argCnt++] = startCh;}
            break;
          }
          if (*i == ' '){
            if (startCh){
              args[argCnt++] = startCh;
              startCh = 0;
              *i = 0;
            }
          }else{
            if (!startCh){startCh = i;}
          }
        }
        args[argCnt] = 0;

        int fin = -1;
        Util::Procs::StartPiped(args, &fin, 0, 0);
        myConn = Socket::Connection(fin, -1);

        wantRequest = false;
        parseData = true;
      }
  }
  
  OutHTTPTS::~OutHTTPTS(){}

  void OutHTTPTS::initialSeek(){
    //Adds passthrough support to the regular initialSeek function
    if (targetParams.count("passthrough")){
      selectedTracks.clear();
      for (std::map<unsigned int, DTSC::Track>::iterator it = myMeta.tracks.begin();
           it != myMeta.tracks.end(); it++){
        selectedTracks.insert(it->first);
      }
    }
    Output::initialSeek();
  }

  void OutHTTPTS::init(Util::Config * cfg){
    HTTPOutput::init(cfg);
    capa["name"] = "HTTPTS";
    capa["desc"] = "Enables HTTP protocol MPEG2/TS pseudostreaming.";
    capa["url_rel"] = "/$.ts";
    capa["url_match"] = "/$.ts";
    capa["socket"] = "http_ts";
    capa["codecs"][0u][0u].append("H264");
    capa["codecs"][0u][0u].append("HEVC");
    capa["codecs"][0u][0u].append("MPEG2");
    capa["codecs"][0u][1u].append("AAC");
    capa["codecs"][0u][1u].append("MP3");
    capa["codecs"][0u][1u].append("AC3");
    capa["codecs"][0u][1u].append("MP2");
    capa["methods"][0u]["handler"] = "http";
    capa["methods"][0u]["type"] = "html5/video/mpeg";
    capa["methods"][0u]["priority"] = 1ll;
    capa["push_urls"].append("/*.ts");
    capa["push_urls"].append("ts-exec:*");

    JSON::Value opt;
    opt["arg"] = "string";
    opt["default"] = "";
    opt["arg_num"] = 1ll;
    opt["help"] = "Target filename to store TS file as, or - for stdout.";
    cfg->addOption("target", opt);
  }

  bool OutHTTPTS::isRecording(){
    return config->getString("target").size();
  }
  
  void OutHTTPTS::onHTTP(){
    std::string method = H.method;
    initialize();
    H.clearHeader("Range");
    H.clearHeader("Icy-MetaData");
    H.clearHeader("User-Agent");
    H.clearHeader("Host");
    H.clearHeader("Accept-Ranges");
    H.clearHeader("transferMode.dlna.org");
    H.SetHeader("Content-Type", "video/mpeg");
    H.setCORSHeaders();
    if(method == "OPTIONS" || method == "HEAD"){
      H.SendResponse("200", "OK", myConn);
      H.Clean();
      return;
    }
    H.protocol = "HTTP/1.0";//Force HTTP/1.0 because some devices just don't understand chunked replies
    H.StartResponse(H, myConn);
    parseData = true;
    wantRequest = false;
  }

  void OutHTTPTS::sendTS(const char * tsData, unsigned int len){
    if (!isRecording()){
      H.Chunkify(tsData, len, myConn);
    }else{
      myConn.SendNow(tsData, len);
    }
  }
}

