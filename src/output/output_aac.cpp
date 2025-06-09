#include "output_aac.h"

#include <mist/ts_packet.h>

namespace Mist {
  OutAAC::OutAAC(Socket::Connection & conn, Util::Config & _cfg, JSON::Value & _capa) : HTTPOutput(conn, _cfg, _capa) {}

  void OutAAC::init(Util::Config *cfg, JSON::Value & capa) {
    HTTPOutput::init(cfg, capa);
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

  void OutAAC::initialSeek(bool dryRun) {
    if (!meta) { return; }
    maxSkipAhead = 30000;
    if (targetParams.count("buffer")) { maxSkipAhead = atof(targetParams["buffer"].c_str()) * 1000; }
    Output::initialSeek(dryRun);
    if (dryRun) { return; }
    uint64_t cTime = currentTime();
    if (M.getLive() && cTime > maxSkipAhead) { seek(cTime - maxSkipAhead); }
  }

  void OutAAC::sendNext() {
    myConn.SendNow(TS::getAudioHeader(thisDataLen, M.getInit(thisIdx)));
    myConn.SendNow(thisData, thisDataLen);
  }

  void OutAAC::respondHTTP(const HTTP::Parser & req, bool headersOnly) {
    HTTPOutput::respondHTTP(req, headersOnly);
    H.protocol = "HTTP/1.0";
    H.SendResponse("200", "OK", myConn);
    if (!headersOnly) {
      parseData = true;
      wantRequest = false;
    }
  }

} // namespace Mist
