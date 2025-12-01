#include "output_httpts.h"

#include <mist/defines.h>
#include <mist/http_parser.h>
#include <mist/procs.h>
#include <mist/stream.h>
#include <mist/ts_packet.h>
#include <mist/ts_stream.h>
#include <mist/url.h>

#include <dirent.h>
#include <unistd.h>

namespace Mist{
  OutHTTPTS::OutHTTPTS(Socket::Connection &conn) : TSOutput(conn){
    sendRepeatingHeaders = 500; // PAT/PMT every 500ms (DVB spec)
    HTTP::URL target(config->getString("target"));
    // Detect youtube-style URL
    if (target.path == "http_upload_hls" && target.args.size() >= 5 && target.args.find("file=") == target.args.size() - 5) {
      targetParams["segment"] = target.path + "?" + target.args + "$segmentCounter.ts";
      targetParams["m3u8"] = target.path + "?" + target.args + "index.m3u8";
      targetParams["split"] = "1";
      targetParams["maxEntries"] = "3";
      targetParams["nounlink"] = "";
      INFO_MSG("Youtube-style HLS push -> setting appropriate segmenting options");
    }
    if (target.protocol == "srt"){
      std::string newTarget = "ts-exec:srt-live-transmit file://con " + target.getUrl();
      INFO_MSG("Rewriting SRT target '%s' to '%s'", config->getString("target").c_str(), newTarget.c_str());
      config->getOption("target", true).append(newTarget);
    }
    if (config->getString("target").substr(0, 8) == "ts-exec:"){
      std::deque<std::string> args;
      Util::shellSplit(config->getString("target").substr(8), args);

      int fin = -1;
      pid_t outProc = Util::Procs::StartPiped(args, &fin, 0, 0);
      myConn.open(fin, -1);
      INFO_MSG("Sending to process %d: %s", outProc, config->getString("target").substr(8).c_str());

      wantRequest = false;
      parseData = true;
    }
  }

  OutHTTPTS::~OutHTTPTS(){}

  void OutHTTPTS::initialSeek(bool dryRun){
    // Adds passthrough support to the regular initialSeek function
    if (targetParams.count("passthrough")){selectAllTracks();}
    Output::initialSeek(dryRun);
  }

  void OutHTTPTS::init(Util::Config *cfg){
    HTTPOutput::init(cfg);
    capa["name"] = "HTTPTS";
    capa["friendly"] = "TS over HTTP";
    capa["desc"] = "Pseudostreaming in MPEG2/TS format over HTTP";
    capa["url_rel"] = "/$.ts";
    capa["url_match"] = "/$.ts";
    capa["socket"] = "http_ts";
    capa["codecs"][0u][0u].append("+H264");
    capa["codecs"][0u][0u].append("+HEVC");
    capa["codecs"][0u][0u].append("+MPEG2");
    capa["codecs"][0u][0u].append("+AV1");
    capa["codecs"][0u][1u].append("+AAC");
    capa["codecs"][0u][1u].append("+MP3");
    capa["codecs"][0u][1u].append("+AC3");
    capa["codecs"][0u][1u].append("+MP2");
    capa["codecs"][0u][1u].append("+opus");
    capa["codecs"][0u][2u].append("+JSON");
    capa["codecs"][0u][2u].append("+SCTE35");
    capa["codecs"][1u][0u].append("rawts");
    capa["methods"][0u]["handler"] = "http";
    capa["methods"][0u]["type"] = "html5/video/mpeg";
    capa["methods"][0u]["hrn"] = "TS HTTP progressive";
    capa["methods"][0u]["priority"] = 1;
    config->addStandardPushCapabilities(capa);
    capa["push_urls"].append("/*.ts");
    capa["push_urls"].append("https://*/http_upload_hls?"); // Youtube-specific HLS push URL
    capa["push_urls"].append("ts-exec:*");

#ifndef WITH_SRT
    {
      pid_t srt_tx = -1;
      const char *args[] ={"srt-live-transmit", 0};
      srt_tx = Util::Procs::StartPiped(args, 0, 0, 0);
      if (srt_tx > 1){
        capa["push_urls"].append("srt://*");
        capa["desc"] += ". Non-native SRT push output support (srt://*) is installed and available.";
      } else {
        capa["desc"] += ". To enable non-native SRT push output support, please install the srt-live-transmit binary.";
      }
    }
#endif

    JSON::Value opt;
    opt["arg"] = "string";
    opt["default"] = "";
    opt["arg_num"] = 1;
    opt["help"] = "Target filename to store TS file as or - for stdout.";
    cfg->addOption("target", opt);
  }

  bool OutHTTPTS::isRecording(){return config->getString("target").size();}

  void OutHTTPTS::respondHTTP(const HTTP::Parser & req, bool headersOnly){
    HTTPOutput::respondHTTP(req, headersOnly);
    H.protocol = "HTTP/1.0";
    H.SendResponse("200", "OK", myConn);
    if (!headersOnly){
      parseData = true;
      wantRequest = false;
    }
  }

  void OutHTTPTS::sendTS(const char *tsData, size_t len){
    if (isRecording()){
      myConn.SendNow(tsData, len);
      return;
    }
    H.Chunkify(tsData, len, myConn);
    if (targetParams.count("passthrough")){selectAllTracks();}
  }
}// namespace Mist
