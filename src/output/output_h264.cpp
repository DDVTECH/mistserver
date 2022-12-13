#include "output_h264.h"
#include <mist/bitfields.h>
#include <mist/mp4_generic.h>
#include <mist/stream.h>

namespace Mist{
  OutH264::OutH264(Socket::Connection &conn) : HTTPOutput(conn){
    prevVidTrack = INVALID_TRACK_ID;
    keysOnly = targetParams.count("keysonly")?1:0;
    stayLive = true;
    target_rate = 0.0;
    forwardTo = 0;
    if (config->getString("target").size()){
      if (!streamName.size()){
        WARN_MSG("Recording unconnected H264 output to file! Cancelled.");
        conn.close();
        return;
      }
      if (config->getString("target") == "-"){
        parseData = true;
        wantRequest = false;
        INFO_MSG("Outputting %s to stdout in H264 format", streamName.c_str());
        return;
      }
      if (connectToFile(config->getString("target"))){
        parseData = true;
        wantRequest = false;
        INFO_MSG("Recording %s to %s in H264 format", streamName.c_str(),
                 config->getString("target").c_str());
      }else{
        conn.close();
      }
    }
  }

  void OutH264::onWebsocketConnect() {
    capa["name"] = "Raw/WS";
    idleInterval = 1000;
    maxSkipAhead = 0;
  }

  void OutH264::onWebsocketFrame() {

    JSON::Value command = JSON::fromString(webSock->data, webSock->data.size());
    if (!command.isMember("type")) {
      JSON::Value r;
      r["type"] = "error";
      r["data"] = "type field missing from command";
      webSock->sendFrame(r.toString());
      return;
    }
    
    if (command["type"] == "request_codec_data") {
      //If no supported codecs are passed, assume autodetected capabilities
      if (command.isMember("supported_codecs")) {
        capa.removeMember("exceptions");
        capa["codecs"].null();
        std::set<std::string> dupes;
        jsonForEach(command["supported_codecs"], i){
          if (dupes.count(i->asStringRef())){continue;}
          dupes.insert(i->asStringRef());
          if (i->asStringRef() == "H264" || i->asStringRef() == "HEVC"){
            capa["codecs"][0u][0u].append(i->asStringRef());
          }else{
            JSON::Value r;
            r["type"] = "error";
            r["data"] = "Unsupported codec: "+i->asStringRef();
          }
        }
      }
      selectDefaultTracks();
      sendWebsocketCodecData("codec_data");
    }else if (command["type"] == "seek") {
      handleWebsocketSeek(command);
    }else if (command["type"] == "pause") {
      parseData = !parseData;
      JSON::Value r;
      r["type"] = "pause";
      r["paused"] = !parseData;
      //Make sure we reset our timing code, too
      if (parseData){
        firstTime = Util::bootMS() - (currentTime() / target_rate);
      }
      webSock->sendFrame(r.toString());
    }else if (command["type"] == "hold") {
      parseData = false;
      webSock->sendFrame("{\"type\":\"pause\",\"paused\":true}");
    }else if (command["type"] == "tracks") {
      if (command.isMember("audio")){
        if (!command["audio"].isNull()){
          targetParams["audio"] = command["audio"].asString();
        }else{
          targetParams.erase("audio");
        }
      }
      if (command.isMember("video")){
        if (!command["video"].isNull()){
          targetParams["video"] = command["video"].asString();
        }else{
          targetParams.erase("video");
        }
      }
      // Remember the previous video track, if any.
      std::set<size_t> prevSelTracks;
      prevVidTrack = INVALID_TRACK_ID;
      for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end(); it++){
        prevSelTracks.insert(it->first);
        if (M.getType(it->first) == "video"){
          prevVidTrack = it->first;
        }
      }
      if (selectDefaultTracks()) {
        uint64_t seekTarget = currentTime();
        if (command.isMember("seek_time")){
          seekTarget = command["seek_time"].asInt();
          prevVidTrack = INVALID_TRACK_ID;
        }
        // Add the previous video track back, if we had one.
        if (prevVidTrack != INVALID_TRACK_ID && !userSelect.count(prevVidTrack)){
          userSelect[prevVidTrack].reload(streamName, prevVidTrack);
          seek(seekTarget);
          std::set<size_t> newSelTracks;
          newSelTracks.insert(prevVidTrack);
          for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end(); it++){
            if (M.getType(it->first) != "video"){
              newSelTracks.insert(it->first);
            }
          }
          if (prevSelTracks != newSelTracks){
            seek(seekTarget, true);
            realTime = 0;
            forwardTo = seekTarget;
            sendWebsocketCodecData(command["type"]);
            sendHeader();
            JSON::Value r;
            r["type"] = "set_speed";
            if (target_rate == 0.0){
              r["data"]["play_rate_prev"] = "auto";
            }else{
              r["data"]["play_rate_prev"] = target_rate;
            }
            r["data"]["play_rate_curr"] = "fast-forward";
            webSock->sendFrame(r.toString());
          }
        }else{
          prevVidTrack = INVALID_TRACK_ID;
          seek(seekTarget, true);
          realTime = 0;
          forwardTo = seekTarget;
          sendWebsocketCodecData(command["type"]);
          sendHeader();
          JSON::Value r;
          r["type"] = "set_speed";
          if (target_rate == 0.0){
            r["data"]["play_rate_prev"] = "auto";
          }else{
            r["data"]["play_rate_prev"] = target_rate;
          }
          r["data"]["play_rate_curr"] = "fast-forward";
          webSock->sendFrame(r.toString());
        }
        onIdle();
        return;
      }else{
        prevVidTrack = INVALID_TRACK_ID;
      }
      onIdle();
      return;
    }else if (command["type"] == "set_speed") {
      handleWebsocketSetSpeed(command);
    }else if (command["type"] == "stop") {
      Util::logExitReason(ER_CLEAN_REMOTE_CLOSE, "User requested stop");
      myConn.close();
    }else if (command["type"] == "play") {
      parseData = true;
      if (command.isMember("seek_time")){handleWebsocketSeek(command);}
    }
  }

  void OutH264::sendWebsocketCodecData(const std::string& type) {
    JSON::Value r;
    r["type"] = type;
    r["data"]["current"] = currentTime();
    std::map<size_t, Comms::Users>::const_iterator it = userSelect.begin();
    while (it != userSelect.end()) {
      if (prevVidTrack != INVALID_TRACK_ID && M.getType(it->first) == "video" && it->first != prevVidTrack){
        //Skip future tracks
        ++it;
        continue;
      }
      std::string codec = Util::codecString(M.getCodec(it->first), M.getInit(it->first));
      if (!codec.size()) {
        FAIL_MSG("Failed to get the codec string for track: %zu.", it->first);
        ++it;
        continue;
      }
      r["data"]["codecs"].append(codec);
      r["data"]["tracks"].append((uint64_t)it->first);
      ++it;
    }
    webSock->sendFrame(r.toString());
  }
  
  bool OutH264::handleWebsocketSeek(JSON::Value& command) {
    JSON::Value r;
    r["type"] = "seek";
    if (!command.isMember("seek_time")){
      r["error"] = "seek_time missing";
      webSock->sendFrame(r.toString());
      return false;
    }

    uint64_t seek_time = command["seek_time"].asInt();
    if (!parseData){
      parseData = true;
      selectDefaultTracks();
    }

    stayLive = (target_rate == 0.0) && (Output::endTime() < seek_time + 5000);
    if (command["seek_time"].asStringRef() == "live"){stayLive = true;}
    if (stayLive){seek_time = Output::endTime();}
    
    if (!seek(seek_time, true)) {
      r["error"] = "seek failed, continuing as-is";
      webSock->sendFrame(r.toString());
      return false;
    }
    if (M.getLive()){r["data"]["live_point"] = stayLive;}
    if (target_rate == 0.0){
      r["data"]["play_rate_curr"] = "auto";
    }else{
      r["data"]["play_rate_curr"] = target_rate;
    }
    if (seek_time >= 250 && currentTime() < seek_time - 250){
      forwardTo = seek_time;
      realTime = 0;
      r["data"]["play_rate_curr"] = "fast-forward";
    }
    onIdle();
    webSock->sendFrame(r.toString());
    return true;
  }

  bool OutH264::handleWebsocketSetSpeed(JSON::Value& command) {
    JSON::Value r;
    r["type"] = "set_speed";
    if (!command.isMember("play_rate")){
      r["error"] = "play_rate missing";
      webSock->sendFrame(r.toString());
      return false;
    }

    double set_rate = command["play_rate"].asDouble();
    if (!parseData){
      parseData = true;
      selectDefaultTracks();
    }
    
    if (target_rate == 0.0){
      r["data"]["play_rate_prev"] = "auto";
    }else{
      r["data"]["play_rate_prev"] = target_rate;
    }
    if (set_rate == 0.0){
      r["data"]["play_rate_curr"] = "auto";
    }else{
      r["data"]["play_rate_curr"] = set_rate;
    }

    if (target_rate != set_rate){
      target_rate = set_rate;
      if (target_rate == 0.0){
        realTime = 1000;//set playback speed to default
        firstTime = Util::bootMS() - currentTime();
        maxSkipAhead = 0;//enabled automatic rate control
      }else{
        stayLive = false;
        //Set new realTime speed
        realTime = 1000 / target_rate;
        firstTime = Util::bootMS() - (currentTime() / target_rate);
        maxSkipAhead = 1;//disable automatic rate control
      }
    }
    if (M.getLive()){r["data"]["live_point"] = stayLive;}
    webSock->sendFrame(r.toString());
    onIdle();
    return true;
  }

  void OutH264::onIdle() {
    if (!webSock){return;}
    if (!parseData){return;}
    JSON::Value r;
    r["type"] = "on_time";
    r["data"]["current"] = currentTime();
    r["data"]["begin"] = Output::startTime();
    r["data"]["end"] = Output::endTime();
    if (realTime == 0){
      r["data"]["play_rate_curr"] = "fast-forward";
    }else{
      if (target_rate == 0.0){
        r["data"]["play_rate_curr"] = "auto";
      }else{
        r["data"]["play_rate_curr"] = target_rate;
      }
    }
    for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end(); it++){
      r["data"]["tracks"].append((uint64_t)it->first);
    }
    webSock->sendFrame(r.toString());
  }

  bool OutH264::onFinish() {
    if (!webSock){
      H.Chunkify(0, 0, myConn);
      wantRequest = true;
      return true;
    }
    JSON::Value r;
    r["type"] = "on_stop";
    r["data"]["current"] = currentTime();
    r["data"]["begin"] = Output::startTime();
    r["data"]["end"] = Output::endTime();
    webSock->sendFrame(r.toString());
    parseData = false;
    return false;
  }

  void OutH264::init(Util::Config *cfg){
    HTTPOutput::init(cfg);
    capa["name"] = "H264";
    capa["friendly"] = "H264/H265 over HTTP";
    capa["desc"] = "Pseudostreaming in raw H264/H265 Annex B format over HTTP";
    capa["url_rel"] = "/$.h264";
    capa["url_match"] = "/$.h264";
    capa["codecs"][0u][0u].append("H264");
    capa["codecs"][0u][0u].append("HEVC");

    capa["methods"][0u]["handler"] = "http";
    capa["methods"][0u]["type"] = "html5/video/raw";
    capa["methods"][0u]["hrn"] = "Raw progressive";
    capa["methods"][0u]["priority"] = 1;
    capa["methods"][0u]["url_rel"] = "/$.h264";
    capa["methods"][1u]["handler"] = "ws";
    capa["methods"][1u]["type"] = "ws/video/raw";
    capa["methods"][1u]["hrn"] = "Raw WebSocket";
    capa["methods"][1u]["priority"] = 2;
    capa["methods"][1u]["url_rel"] = "/$.h264";

    JSON::Value opt;
    opt["arg"] = "string";
    opt["default"] = "";
    opt["arg_num"] = 1;
    opt["help"] = "Target filename to store H264 file as, or - for stdout.";
    cfg->addOption("target", opt);
  }

  bool OutH264::isRecording(){return config->getString("target").size();}

  void OutH264::sendNext(){
    if (keysOnly && !thisPacket.getFlag("keyframe")){return;}
    char *dataPointer = 0;
    size_t len = 0;
    thisPacket.getString("data", dataPointer, len);

    if (webSock) {

      if (forwardTo && currentTime() >= forwardTo){
        forwardTo = 0;
        if (target_rate == 0.0){
          realTime = 1000;//set playback speed to default
          firstTime = Util::bootMS() - currentTime();
          maxSkipAhead = 0;//enabled automatic rate control
        }else{
          stayLive = false;
          //Set new realTime speed
          realTime = 1000 / target_rate;
          firstTime = Util::bootMS() - (currentTime() / target_rate);
          maxSkipAhead = 1;//disable automatic rate control
        }
        JSON::Value r;
        r["type"] = "set_speed";
        r["data"]["play_rate_prev"] = "fast-forward";
        if (target_rate == 0.0){
          r["data"]["play_rate_curr"] = "auto";
        }else{
          r["data"]["play_rate_curr"] = target_rate;
        }
        webSock->sendFrame(r.toString());
      }

      // Handle nice move-over to new track ID
      if (prevVidTrack != INVALID_TRACK_ID && thisIdx != prevVidTrack && M.getType(thisIdx) == "video"){
        if (!thisPacket.getFlag("keyframe")){
          // Ignore the packet if not a keyframe
          return;
        }
        dropTrack(prevVidTrack, "Smoothly switching to new video track", false);
        prevVidTrack = INVALID_TRACK_ID;
        onIdle();
        sendWebsocketCodecData("tracks");
        sendHeader();
      }


      webBuf.truncate(0);
      webBuf.append("\000\000\000\000\000\000\000\000\000\000\000\000", 12);
      webBuf[0] = thisIdx;
      webBuf[1] = thisPacket.getFlag("keyframe")?1:0;
      Bit::htobll(webBuf+2, thisTime);
      if (thisPacket.hasMember("offset")) { 
        Bit::htobs(webBuf+10, thisPacket.getInt("offset"));
      }else{
        Bit::htobs(webBuf+10, 0);
      }

      size_t lenSize = 4;
      if (M.getCodec(thisIdx) == "H264"){lenSize = (M.getInit(thisIdx)[4] & 3) + 1;}
      unsigned int i = 0;
      uint32_t ThisNaluSize;
      while (i + 4 < len){
        if (lenSize == 4){
          ThisNaluSize = Bit::btohl(dataPointer + i);
        }else if (lenSize == 2){
          ThisNaluSize = Bit::btohs(dataPointer + i);
        }else{
          ThisNaluSize = dataPointer[i];
        }
        webBuf.append("\000\000\000\001", 4);
        webBuf.append(dataPointer + i + lenSize, ThisNaluSize);
        i += ThisNaluSize + lenSize;
      }

      webSock->sendFrame(webBuf, webBuf.size(), 2);

      if (stayLive && thisPacket.getFlag("keyframe")){liveSeek();}
      // We must return here, the rest of this function won't work for websockets. 
      return;
    }

    size_t lenSize = 4;
    if (M.getCodec(thisIdx) == "H264"){lenSize = (M.getInit(thisIdx)[4] & 3) + 1;}
    unsigned int i = 0;
    uint32_t ThisNaluSize;
    while (i + 4 < len){
      if (lenSize == 4){
        ThisNaluSize = Bit::btohl(dataPointer + i);
      }else if (lenSize == 2){
        ThisNaluSize = Bit::btohs(dataPointer + i);
      }else{
        ThisNaluSize = dataPointer[i];
      }
      H.Chunkify("\000\000\000\001", 4, myConn);
      H.Chunkify(dataPointer + i + lenSize, ThisNaluSize, myConn);
      i += ThisNaluSize + lenSize;
    }

  }

  void OutH264::sendHeader(){

    size_t mainTrack = getMainSelectedTrack();
    if (mainTrack != INVALID_TRACK_ID){

      if (webSock) {

        JSON::Value r;
        r["type"] = "info";
        r["data"]["msg"] = "Sending header";
        for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end(); it++){
          r["data"]["tracks"].append((uint64_t)it->first);
        }
        webSock->sendFrame(r.toString());

        Util::ResizeablePointer headerData;

        headerData.append("\000\000\000\000\000\000\000\000\000\000\000\000", 12);
        headerData[0] = thisIdx;
        headerData[1] = 2;

        if (M.getCodec(mainTrack) == "H264"){
          MP4::AVCC avccbox;
          avccbox.setPayload(M.getInit(mainTrack));
          headerData.append(avccbox.asAnnexB());
        }
        if (M.getCodec(mainTrack) == "HEVC"){
          MP4::HVCC hvccbox;
          hvccbox.setPayload(M.getInit(mainTrack));
          headerData.append(hvccbox.asAnnexB());
        }
        webSock->sendFrame(headerData, headerData.size(), 2);
        sentHeader = true;
        return;
      }

      if (M.getCodec(mainTrack) == "H264"){
        MP4::AVCC avccbox;
        avccbox.setPayload(M.getInit(mainTrack));
        H.Chunkify(avccbox.asAnnexB(), myConn);
      }
      if (M.getCodec(mainTrack) == "HEVC"){
        MP4::HVCC hvccbox;
        hvccbox.setPayload(M.getInit(mainTrack));
        H.Chunkify(hvccbox.asAnnexB(), myConn);
      }
      sentHeader = true;
    }
  }

  void OutH264::respondHTTP(const HTTP::Parser & req, bool headersOnly){
    //Set global defaults
    HTTPOutput::respondHTTP(req, headersOnly);

    size_t mainTrk = getMainSelectedTrack();
    H.SetHeader("Content-Type", "video/"+M.getCodec(mainTrk));
    H.StartResponse("200", "OK", req, myConn);
    if (headersOnly){return;}
    parseData = true;
    wantRequest = false;
  }

}// namespace Mist
