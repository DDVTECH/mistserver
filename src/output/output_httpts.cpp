#include "output_httpts.h"

#include <mist/defines.h>
#include <mist/http_parser.h>
#include <mist/procs.h>
#include <mist/stream.h>
#include <mist/triggers.h>
#include <mist/ts_packet.h>
#include <mist/ts_stream.h>
#include <mist/url.h>

#include <dirent.h>
#include <unistd.h>

namespace Mist{
  OutHTTPTS::OutHTTPTS(Socket::Connection & conn, Util::Config & _cfg, JSON::Value & _capa)
    : TSOutput(conn, _cfg, _capa) {
    seenTime = false;
    timeOffset = 0;
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

  void OutHTTPTS::init(Util::Config *cfg, JSON::Value & capa) {
    HTTPOutput::init(cfg, capa);
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
    { // playback method
      JSON::Value & M = capa["methods"].append();
      M["hrn"] = "TS HTTP progressive";
      M["handler"] = "http";
      M["type"] = "html5/video/mpeg";
      M["ttff"] = "ms";
      M["ttff_ms"] = 4000;
      M["latency"] = 2000;
      M["bw"].fromString("[4, 184, 16, true]");
      M["control"] = 0;
      M["stability"] = 5;
      M["cpu_server"] = 2;
      M["permissibility"] = 10;
    }
    cfg->addStandardPushCapabilities(capa);
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

  void OutHTTPTS::dataCallback(const char *ptr, size_t size) {
    if (!pushing) { return; }
    static TS::Stream tsIn;
    char *tmpPtr = (char *)ptr;
    while (size + bodyData.size() >= 188) {
      while (bodyData.size()) {
        if (bodyData.size() < 188) {
          size_t diff = 188 - bodyData.size();
          bodyData.append(tmpPtr, diff);
          tmpPtr += diff;
          size -= diff;
        }
        tsIn.parse(bodyData, 0);
        bodyData.shift(188);
      }
      if (size >= 188) {
        tsIn.parse(tmpPtr, 0);
        tmpPtr += 188;
        size -= 188;
      }
      while (tsIn.hasPacketOnEachTrack()) {
        tsIn.getEarliestPacket(thisPacket);
        if (!thisPacket) {
          FAIL_MSG("Could not getNext TS packet!");
          return;
        }
        thisIdx = M.trackIDToIndex(thisPacket.getTrackId(), getpid());
        if (M.trackIDToIndex(thisIdx == INVALID_TRACK_ID) || !M.getCodec(thisIdx).size()) {
          tsIn.initializeMetadata(meta);
          thisIdx = M.trackIDToIndex(thisPacket.getTrackId(), getpid());
        }
        if (thisIdx == INVALID_TRACK_ID) { continue; }
        if (!userSelect.count(thisIdx)) {
          userSelect[thisIdx].reload(streamName, thisIdx, COMM_STATUS_SOURCE | COMM_STATUS_DONOTTRACK);
        }
        thisTime = thisPacket.getTime();
        if (!seenTime) {
          if (!M.getBootMsOffset()) {
            meta.setBootMsOffset(Util::bootMS() - thisTime);
          } else {
            timeOffset = (Util::bootMS() - thisTime) - M.getBootMsOffset();
          }
          seenTime = true;
        }
        bufferLivePacket(DTSC::RetimedPacket(thisTime + timeOffset, thisPacket));
        stats();
      }
    }
    if (size) { bodyData.append(tmpPtr, size); }
  }

  void OutHTTPTS::preHTTP() {
    if (H.method != "PUT" && H.method != "POST") {
      HTTPOutput::preHTTP();
      return;
    }

    char *rawStream = getenv("stream_raw");
    if (rawStream) { streamName = rawStream; }
    if (checkStreamKey()) {
      if (!streamName.size()) {
        onFail("Stream key rejected for push");
        return;
      }
    } else {
      if (Triggers::shouldTrigger("PUSH_REWRITE")) {
        std::string payload = reqUrl + "\n" + getConnectedHost() + "\n" + streamName;
        std::string newStream = streamName;
        Triggers::doTrigger("PUSH_REWRITE", payload, "", false, newStream);
        if (!newStream.size()) {
          FAIL_MSG("Push from %s to URL %s rejected - PUSH_REWRITE trigger blanked the URL", getConnectedHost().c_str(),
                   reqUrl.c_str());
          onFail("Pushing not allowed");
        } else {
          streamName = newStream;
        }
      }
      if (!allowPush(H.GetVar("password"))) {
        onFail("Pushing not allowed");
        return;
      }
    }
    H.headerOnly = false;
  }

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
