#include "output_aac.h"
#include <mist/ts_packet.h>

namespace Mist{
  OutAAC::OutAAC(Socket::Connection &conn) : HTTPOutput(conn){}

  void OutAAC::init(Util::Config *cfg){
    HTTPOutput::init(cfg);
    capa["name"] = "AAC";
    capa["friendly"] = "AAC over HTTP";
    capa["desc"] = "Pseudostreaming in AAC format over HTTP";
    capa["url_rel"] = "/$.aac";
    capa["url_match"] = "/$.aac";
    capa["codecs"][0u][0u].append("AAC");
    capa["methods"][0u]["handler"] = "http";
    capa["methods"][0u]["type"] = "html5/audio/aac";
    capa["methods"][0u]["hrn"] = "AAC progressive";
    capa["methods"][0u]["priority"] = 8;
    capa["push_urls"].append("/*.aac");

    JSON::Value opt;
    opt["arg"] = "string";
    opt["default"] = "";
    opt["arg_num"] = 1;
    opt["help"] = "Target filename to store AAC file as, or - for stdout.";
    cfg->addOption("target", opt);
  }

  void OutAAC::initialSeek(){
    if (!meta){return;}
    maxSkipAhead = 30000;
    if (targetParams.count("buffer")){
      maxSkipAhead = atof(targetParams["buffer"].c_str())*1000;
    }
    Output::initialSeek();
    uint64_t cTime = currentTime();
    if (M.getLive() && cTime > maxSkipAhead){
      seek(cTime-maxSkipAhead);
    }
  }

  void OutAAC::sendNext(){
    char *dataPointer = 0;
    size_t len = 0;
    thisPacket.getString("data", dataPointer, len);
    std::string head = TS::getAudioHeader(len, M.getInit(thisIdx));
    myConn.SendNow(head);
    myConn.SendNow(dataPointer, len);
  }

  void OutAAC::respondHTTP(const HTTP::Parser & req, bool headersOnly){
    HTTPOutput::respondHTTP(req, headersOnly);
    H.protocol = "HTTP/1.0";
    H.SendResponse("200", "OK", myConn);
    if (!headersOnly){
      parseData = true;
      wantRequest = false;
    }
  }

}// namespace Mist
