#include "output_wsraw.h"

#include <mist/bitfields.h>
#include <mist/mp4_generic.h>
#include <mist/stream.h>

namespace Mist {
  OutWSRaw::OutWSRaw(Socket::Connection & conn, Util::Config & _cfg, JSON::Value & _capa)
    : HTTPOutput(conn, _cfg, _capa) {
    wsCmds = true;
    keysOnly = targetParams.count("keysonly") ? 1 : 0;
  }

  void OutWSRaw::onWebsocketConnect() {
    capa["name"] = "Raw/WS";
    idleInterval = 1000;
    maxSkipAhead = 0;
  }

  void OutWSRaw::onWebsocketFrame() {
    JSON::Value command(PARSEJSON, webSock->data, webSock->data.size());
    if (!command.isMember("type")) {
      JSON::Value r;
      r["type"] = "error";
      r["data"] = "type field missing from command";
      webSock->sendFrame(r.toString());
      return;
    }

    if (command["type"] == "request_codec_data") {
      // If no supported codecs are passed, assume autodetected capabilities
      if (command.isMember("supported_codecs")) {
        capa.removeMember("exceptions");
        capa["codecs"].null();
        std::set<std::string> dupes;
        jsonForEach (command["supported_codecs"], i) {
          if (dupes.count(i->asStringRef())) { continue; }
          dupes.insert(i->asStringRef());
          JSON::Value arr;
          arr.append(i->asStringRef());
          capa["codecs"][0u].append(arr);
        }
      }
      if (command.isMember("supported_combinations")) {
        capa.removeMember("exceptions");
        capa["codecs"] = command["supported_combinations"];
      }
      selectDefaultTracks();
      sendWebsocketCodecData("codec_data");
      initialSeek();
      return;
    }
  }

  void OutWSRaw::sendWebsocketCodecData(const std::string & type) {
    JSON::Value r;
    r["type"] = type;
    r["data"]["current"] = currentTime();
    for (const auto & it : userSelect) {
      // Skip future tracks
      if (prevVidTrack != INVALID_TRACK_ID && M.getType(it.first) == "video" && it.first != prevVidTrack) { continue; }
      const std::string & mistCodec = M.getCodec(it.first);
      std::string codec = Util::codecString(mistCodec, M.getInit(it.first), true);
      r["data"]["codecs"].append(codec.size() ? codec : ("!" + mistCodec));
      r["data"]["tracks"].append((uint64_t)it.first);
    }
    webSock->sendFrame(r.toString());
  }

  void OutWSRaw::init(Util::Config *cfg, JSON::Value & capa) {
    HTTPOutput::init(cfg, capa);
    capa["name"] = "WSRaw";
    capa["friendly"] = "Raw WebSocket";
    capa["desc"] = "Raw codec data over WebSocket";
    capa["url_rel"] = "/$.raw";
    capa["url_match"] = "/$.raw";
    capa["codecs"][0u][0u].append("+*");
    capa["codecs"][0u][1u].append("+*");
    capa["codecs"][0u][2u].append("+*");

    { // playback method
      JSON::Value & M = capa["methods"].append();
      M["hrn"] = "Raw WebSocket";
      M["handler"] = "ws";
      M["type"] = "ws/video/raw";
      M["ttff"] = "ms";
      M["ttff_ms"] = 0;
      M["latency"] = 100;
      M["bw"].fromString("[0, 0, 12]");
      M["control"] = 10;
      M["stability"] = 6;
      M["cpu_server"] = 10;
      M["permissibility"] = 10;
      M["abr"] = true;
      M["url_rel"] = "/$.raw";
    }
  }

  void OutWSRaw::sendNext() {
    // Call parent handler for generic websocket handling
    HTTPOutput::sendNext();
    if (!thisPacket) { return; }

    if (keysOnly && !thisPacket.getFlag("keyframe")) { return; }
    if (!webSock) { return; }

    webBuf.truncate(0);
    webBuf.append("\000\000\000\000\000\000\000\000\000\000\000\000", 12);
    webBuf[0] = thisIdx;
    webBuf[1] = thisPacket.getFlag("keyframe") ? 1 : 0;
    Bit::htobll(webBuf + 2, thisTime);
    if (thisPacket.hasMember("offset")) {
      Bit::htobs(webBuf + 10, thisPacket.getInt("offset"));
    } else {
      Bit::htobs(webBuf + 10, 0);
    }

    /*
    if (M.getCodec(thisIdx) == "UYVY"){
      webBuf[1] = 1;
      // Convert to I422: first all Y samples, then all U samples, then all V samples.
      webBuf.allocate(12+thisDataLen);
      webBuf.append(0, thisDataLen);
      // Y samples
      size_t offset = 12;
      for (size_t i = 0; i < thisDataLen/2; ++i){
        webBuf[offset+i] = thisData[i*2+1];
      }
      offset += thisDataLen/2;
      for (size_t i = 0; i < thisDataLen/4; ++i){
        webBuf[offset+i] = thisData[i*4];
      }
      offset += thisDataLen/4;
      for (size_t i = 0; i < thisDataLen/4; ++i){
        webBuf[offset+i] = thisData[i*4+2];
      }
    } else if (M.getCodec(thisIdx) == "YUYV"){
      webBuf[1] = 1;
      // Convert to I422: first all Y samples, then all U samples, then all V samples.
      webBuf.allocate(12+thisDataLen);
      webBuf.append(0, thisDataLen);
      // Y samples
      size_t offset = 12;
      for (size_t i = 0; i < thisDataLen/2; ++i){
        webBuf[offset+i] = thisData[i*2];
      }
      offset += thisDataLen/2;
      for (size_t i = 0; i < thisDataLen/4; ++i){
        webBuf[offset+i] = thisData[i*4+1];
      }
      offset += thisDataLen/4;
      for (size_t i = 0; i < thisDataLen/4; ++i){
        webBuf[offset+i] = thisData[i*4+3];
      }
    */
    if (M.getCodec(thisIdx) == "UYVY") {
      size_t w = M.getWidth(thisIdx);
      size_t h = M.getHeight(thisIdx);
      size_t a = w * h;
      webBuf[1] = 1;
      // Convert to NV12: first all Y samples, then U/V samples interleaved, 4:2:0 (2x2 blocks)
      webBuf.allocate(12 + thisDataLen);
      webBuf.append(0, thisDataLen);
      // Y samples
      size_t offset = 12;
      for (size_t i = 0; i < a; ++i) { webBuf[offset + i] = thisData[i * 2 + 1]; }
      // U/V samples, only even rows
      offset += thisDataLen / 2;
      for (size_t y = 0; y < h; y += 2) {
        for (size_t x = 0; x < w; x += 2) {
          size_t p = (x + w * y) * 2;
          webBuf[offset++] = thisData[p]; // U
          webBuf[offset++] = thisData[p + 2]; // V
        }
      }
    } else if (M.getCodec(thisIdx) == "YUYV") {
      size_t w = M.getWidth(thisIdx);
      size_t h = M.getHeight(thisIdx);
      size_t a = w * h;
      webBuf[1] = 1;
      // Convert to NV12: first all Y samples, then U/V samples interleaved, 4:2:0 (2x2 blocks)
      webBuf.allocate(12 + thisDataLen);
      webBuf.append(0, thisDataLen);
      // Y samples
      size_t offset = 12;
      for (size_t i = 0; i < a; ++i) { webBuf[offset + i] = thisData[i * 2]; }
      // U/V samples, only even rows
      offset += thisDataLen / 2;
      for (size_t y = 0; y < h; y += 2) {
        for (size_t x = 0; x < w; x += 2) {
          size_t p = (x + w * y) * 2;
          webBuf[offset++] = thisData[p + 1]; // U
          webBuf[offset++] = thisData[p + 3]; // V
        }
      }
    } else {
      webBuf.append(thisData, thisDataLen);
    }
    webSock->sendFrame(webBuf, webBuf.size(), 2);
  }

  void OutWSRaw::sendHeader() {
    if (!webSock) { return; }

    JSON::Value r;
    r["type"] = "info";
    r["data"]["msg"] = "Sending header";
    for (const auto & it : userSelect) { r["data"]["tracks"].append((uint64_t)it.first); }
    webSock->sendFrame(r.toString());

    for (const auto & it : userSelect) {
      const std::string init = M.getInit(it.first);
      if (init.size()) {
        Util::ResizeablePointer headerData;
        headerData.append("\000\000\000\000\000\000\000\000\000\000\000\000", 12);
        headerData[0] = it.first;
        headerData[1] = 2;
        headerData.append(init);
        webSock->sendFrame(headerData, headerData.size(), 2);
      }
    }
    handleWebsocketIdle();

    sentHeader = true;
  }

  void OutWSRaw::respondHTTP(const HTTP::Parser & req, bool headersOnly) {
    // Set global defaults
    HTTPOutput::respondHTTP(req, headersOnly);
    H.SendResponse("406", "Not acceptable", myConn);
  }

} // namespace Mist
