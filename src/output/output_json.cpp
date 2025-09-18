#include "output_json.h"
#include <iomanip>
#include <mist/stream.h>
#include <mist/triggers.h>

namespace Mist{
  OutJSON::OutJSON(Socket::Connection &conn) : HTTPOutput(conn){
    wsCmds = true;
    realTime = 0;
    bootMsOffset = 0;
    keepReselecting = false;
    dupcheck = false;
    noReceive = false;
    pushTrack = INVALID_TRACK_ID;
  }

  void OutJSON::init(Util::Config *cfg){
    HTTPOutput::init(cfg);
    capa["name"] = "JSON";
    capa["friendly"] = "JSON over HTTP";
    capa["desc"] = "Pseudostreaming in JSON format over HTTP";
    capa["url_match"] = "/$.json";
    capa["codecs"][0u][0u].append("@meta");
    capa["codecs"][0u][0u].append("subtitle");
    capa["methods"][0u]["handler"] = "http";
    capa["methods"][0u]["type"] = "html5/text/javascript";
    capa["methods"][0u]["hrn"] = "JSON progressive";
    capa["methods"][0u]["priority"] = 0;
    capa["methods"][0u]["url_rel"] = "/$.json";
    capa["methods"][1u]["handler"] = "ws";
    capa["methods"][1u]["type"] = "html5/text/javascript";
    capa["methods"][1u]["hrn"] = "JSON WebSocket";
    capa["methods"][1u]["priority"] = 0;
    capa["methods"][1u]["url_rel"] = "/$.json";
  }

  void OutJSON::sendNext(){
    //Call parent handler for generic websocket handling
    HTTPOutput::sendNext();
    if (!thisPacket) { return; }

    if (keepReselecting){
      // If we can select more tracks, do it and continue.
      if (selectDefaultTracks()){
        return; // After a seek, the current packet is invalid. Do nothing and return here.
      }
    }
    JSON::Value jPack;
    if (M.getCodec(thisIdx) == "JSON"){
      char *dPtr;
      size_t dLen;
      thisPacket.getString("data", dPtr, dLen);
      if (dLen == 0 || (dLen == 1 && dPtr[0] == ' ')){return;}
      jPack["data"] = JSON::fromString(dPtr, dLen);
      jPack["time"] = thisTime;
      jPack["track"] = (uint64_t)thisIdx;
    }else if (M.getCodec(thisIdx) == "subtitle"){
      char *dPtr;
      size_t dLen;
      thisPacket.getString("data", dPtr, dLen);

      //Ignore blank subtitles
      if (dLen == 0 || (dLen == 1 && dPtr[0] == ' ')){return;}

      //Get duration, or calculate if missing
      uint64_t duration = thisPacket.getInt("duration");
      if (!duration){duration = dLen * 75 + 800;}

      //Build JSON data to transmit
      jPack["duration"] = duration;
      jPack["time"] = thisTime;
      jPack["track"] = (uint64_t)thisIdx;
      jPack["data"] = std::string(dPtr, dLen);
    }else{
      jPack = thisPacket.toJSON();
      jPack.removeMember("bpos");
      jPack["generic_converter_used"] = true;
    }
    if (dupcheck){
      if (jPack.compareExcept(lastVal, nodup)){
        return; // skip duplicates
      }
      lastVal = jPack;
    }
    if (webSock){
      webSock->sendFrame(jPack.toString());
      return;
    }
    if (!jsonp.size()){
      if (!first){
        myConn.SendNow(", ", 2);
      }else{
        myConn.SendNow("[", 1);
        first = false;
      }
    }else{
      myConn.SendNow(jsonp + "(");
    }
    myConn.SendNow(jPack.toString());
    if (jsonp.size()){myConn.SendNow(");\n", 3);}
  }

  void OutJSON::sendHeader(){
    sentHeader = true;
    if (webSock){return;}
    std::string method = H.method;
    H.Clean();
    H.SetHeader("Content-Type", "text/javascript");
    H.protocol = "HTTP/1.0";
    H.setCORSHeaders();
    H.SendResponse("200", "OK", myConn);
  }

  void OutJSON::onFail(const std::string &msg, bool critical){
    // Only run failure handle if we're not being persistent
    if (!keepReselecting){
      HTTPOutput::onFail(msg, critical);
    }else{
      onFinish();
    }
  }

  bool OutJSON::onFinish(){
    static bool recursive = false;
    if (recursive){return true;}
    recursive = true;
    if (keepReselecting && !isPushing() && !M.getVod()){
      uint64_t maxTimer = 7200;
      while (--maxTimer && keepGoing()){
        if (!isBlocking){myConn.spool();}
        Util::wait(500);
        thisBootMs = Util::bootMS();
        stats();
        if (Util::getStreamStatus(streamName) != STRMSTAT_READY){
          if (isInitialized){
            INFO_MSG("Disconnecting from offline stream");
            disconnect();
            stop();
          }
        }else{
          if (isReadyForPlay()){
            INFO_MSG("Resuming playback!");
            recursive = false;
            parseData = true;
            return true;
          }
        }
      }
      recursive = false;
    }
    if (!webSock && !jsonp.size() && !first){myConn.SendNow("]\n", 2);}
    if (isPushing() && pushTrack != INVALID_TRACK_ID) {
      meta.abandonTrack(pushTrack);
      pushTrack = INVALID_TRACK_ID;
      pushing = false;
    }
    myConn.close();
    return false;
  }

  void OutJSON::onWebsocketConnect(){
    sentHeader = true;
    parseData = !noReceive;
  }

  void OutJSON::preWebsocketConnect(){
    if (H.GetVar("password") != ""){pushPass = H.GetVar("password");}
    if (H.GetVar("password").size() || H.GetVar("push").size()){noReceive = true;}

    if (H.GetVar("persist") != ""){keepReselecting = true;}
    if (H.GetVar("dedupe") != ""){
      dupcheck = true;
      size_t index;
      std::string dupes = H.GetVar("dedupe");
      while (dupes != ""){
        index = dupes.find(',');
        nodup.insert(dupes.substr(0, index));
        if (index != std::string::npos){
          dupes.erase(0, index + 1);
        }else{
          dupes = "";
        }
      }
    }
  }

  void OutJSON::onWebsocketFrame(){
    if (bufferData(webSock->data, webSock->data.size())) {
      if (!idleInterval) { idleInterval = 50; }
      if (isBlocking) { setBlocking(false); }
    }
  }

  bool OutJSON::bufferData(const char *ptr, size_t len) {
    if (!isPushing()){
      if (checkStreamKey()) {
        if (!streamName.size()) {
          onFinish();
          return false;
        }
      } else {
        if (Triggers::shouldTrigger("PUSH_REWRITE")) {
          std::string payload = reqUrl + "\n" + getConnectedHost() + "\n" + streamName;
          std::string newStream = streamName;
          Triggers::doTrigger("PUSH_REWRITE", payload, "", false, newStream);
          if (!newStream.size()) {
            FAIL_MSG("Push from %s to URL %s rejected - PUSH_REWRITE trigger blanked the URL",
                     getConnectedHost().c_str(), reqUrl.c_str());
            onFinish();
            return false;
          } else {
            streamName = newStream;
            Util::sanitizeName(streamName);
            Util::setStreamName(streamName);
          }
        }
        if (!allowPush(pushPass)) {
          onFinish();
          return false;
        }
      }
    }

    std::string tCodec = "JSON";
    if (targetParams.count("meta") && (targetParams["meta"] == "SCTE35" || targetParams["meta"] == "scte35")) {
      tCodec = "SCTE35";
    }

    // We now know we're allowed to push. Read a JSON object.
    JSON::Value inJSON = JSON::fromString(ptr, len);
    // Let's create a new track for pushing purposes, if needed
    if (pushTrack == INVALID_TRACK_ID) {
      // First attempt to resume a track if we can
      std::set<size_t> v = M.getValidTracks();
      for (size_t t : v) {
        if (M.getType(t) == "meta" && M.getCodec(t) == tCodec && !M.isClaimed(t)) {
          if (meta.claimTrack(t)) {
            pushTrack = t;
            break;
          }
        }
      }
      if (pushTrack == INVALID_TRACK_ID) {
        pushTrack = meta.addTrack();
        meta.setType(pushTrack, "meta");
        meta.setCodec(pushTrack, tCodec);
        meta.setID(pushTrack, pushTrack);
      }
    }
    // We have a track set correctly. Let's attempt to buffer a frame.
    lastSendTime = Util::bootMS();
    if (!inJSON.isMember("unix")){
      // Base timestamp on arrival time
      lastOutTime = (lastSendTime - M.getBootMsOffset());
    }else{
      // Base timestamp on unix time
      lastOutTime = (lastSendTime - M.getBootMsOffset()) + (inJSON["unix"].asInt() - Util::epoch()) * 1000;
    }
    lastOutData = inJSON.toString();
    bufferLivePacket(lastOutTime, 0, pushTrack, lastOutData.data(), lastOutData.size(), 0, true);
    return true;
  }

  /// Updates NowMs every 50ms to keep track updated
  void OutJSON::onIdle() {
    if (isPushing()) {
      uint64_t now = Util::bootMS();
      if (now - lastSendTime > 50) {
        lastOutTime += (now - lastSendTime - 50);
        lastSendTime = now;
        meta.upNowms(pushTrack, lastOutTime - 50);
      }
    }
  }

  void OutJSON::onHTTP(){
    std::string method = H.method;
    std::string body = H.body;

    // Force-set push param if POST/PUT method is used
    if (method == "POST" || method == "PUT") { H.SetVar("push", method); }

    preWebsocketConnect(); // Not actually a websocket, but we need to do the same checks

    // Determine if a callback is used, and if yes, what it's named
    jsonp = "";
    if (H.GetVar("callback") != ""){jsonp = H.GetVar("callback");}
    if (H.GetVar("jsonp") != ""){jsonp = H.GetVar("jsonp");}

    H.Clean();
    H.setCORSHeaders();
    if (method == "OPTIONS" || method == "HEAD"){
      H.SetHeader("Content-Type", "text/javascript");
      H.protocol = "HTTP/1.0";
      H.SendResponse("200", "OK", myConn);
      H.Clean();
      return;
    }
    if (method == "POST" || method == "PUT") {
      INFO_MSG("Received method %s!", method.c_str());
      INFO_MSG("Body = %s", body.c_str());
      H.SetHeader("Content-Type", "application/json");
      H.protocol = "HTTP/1.0";
      if (bufferData(body.data(), body.size())) {
        H.body = "Packet inserted into datastream successfully";
        H.SendResponse("200", "OK", myConn);
      } else {
        H.body = "Could not insert packet :-(";
        H.SendResponse("404", "OK", myConn);
      }
      myConn.close();
      H.Clean();
      return;
    }
    first = true;
    parseData = true;
    wantRequest = false;
  }

}// namespace Mist
