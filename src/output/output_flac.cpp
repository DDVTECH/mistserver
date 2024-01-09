#include "output_flac.h"

namespace Mist{

  OutFLAC::OutFLAC(Socket::Connection &conn) : HTTPOutput(conn){}

  void OutFLAC::init(Util::Config *cfg){
    HTTPOutput::init(cfg);
    capa["name"] = "FLAC";
    capa["friendly"] = "Free Lossless Audio Codec";
    capa["desc"] = "Pseudostreaming in FLAC format over HTTP";
    capa["url_rel"] = "/$.flac";
    capa["url_match"] = "/$.flac";
    capa["codecs"][0u][0u].append("FLAC");
    capa["methods"][0u]["handler"] = "http";
    capa["methods"][0u]["type"] = "html5/audio/flac";
    capa["methods"][0u]["hrn"] = "FLAC progressive";
    capa["methods"][0u]["priority"] = 8;

    JSON::Value opt;
    opt["arg"] = "string";
    opt["default"] = "";
    opt["arg_num"] = 1;
    opt["help"] = "Target filename to store FLAC file as, or - for stdout.";
    cfg->addOption("target", opt);
  }

  void OutFLAC::sendNext(){
    char *dataPointer = 0;
    size_t len = 0;
    thisPacket.getString("data", dataPointer, len);
    myConn.SendNow(dataPointer, len);
  }

  void OutFLAC::sendHeader(){
    myConn.SendNow(M.getInit(M.mainTrack()));
    sentHeader = true;
  }

  void OutFLAC::respondHTTP(const HTTP::Parser &req, bool headersOnly){
    // Set global defaults
    HTTPOutput::respondHTTP(req, headersOnly);

    H.StartResponse("200", "OK", req, myConn);
    if (headersOnly){return;}
    parseData = true;
    wantRequest = false;
  }

}// namespace Mist
