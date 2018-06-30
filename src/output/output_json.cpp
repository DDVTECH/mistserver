#include "output_json.h"
#include <mist/stream.h>
#include <iomanip>

namespace Mist {
  OutJSON::OutJSON(Socket::Connection & conn) : HTTPOutput(conn){
    realTime = 0;
    bootMsOffset = 0;
    keepReselecting = false;
    dupcheck = false;
    noReceive = false;
  }

  void OutJSON::init(Util::Config * cfg){
    HTTPOutput::init(cfg);
    capa["name"] = "JSON";
    capa["desc"] = "Enables HTTP protocol JSON streaming.";
    capa["url_match"] = "/$.json";
    capa["codecs"][0u][0u].append("@+meta");
    capa["methods"][0u]["handler"] = "http";
    capa["methods"][0u]["type"] = "html5/text/javascript";
    capa["methods"][0u]["priority"] = 0ll;
    capa["methods"][0u]["url_rel"] = "/$.json";
    capa["methods"][1u]["handler"] = "ws";
    capa["methods"][1u]["type"] = "html5/text/javascript";
    capa["methods"][1u]["priority"] = 0ll;
    capa["methods"][1u]["url_rel"] = "/$.json";
  }

  void OutJSON::sendNext(){
    if (keepReselecting){
      //If we can select more tracks, do it and continue.
      if (selectDefaultTracks()){
        return;//After a seek, the current packet is invalid. Do nothing and return here.
      }
    }
    JSON::Value jPack;
    if (myMeta.tracks[thisPacket.getTrackId()].codec == "JSON"){
      char * dPtr;
      unsigned int dLen;
      thisPacket.getString("data", dPtr, dLen);
      jPack["data"] = JSON::fromString(dPtr, dLen);
      jPack["time"] = (long long)thisPacket.getTime();
      jPack["track"] = (long long)thisPacket.getTrackId();
    }else{
      jPack = thisPacket.toJSON();
    }
    if (dupcheck){
      if (jPack.compareExcept(lastVal, nodup)){
        return;//skip duplicates
      }
      lastVal = jPack;
    }
    if (webSock){
      webSock->sendFrame(jPack.toString());
      return;
    }
    if (!jsonp.size()){
      if(!first) {
        myConn.SendNow(", ", 2);
      }else{
        myConn.SendNow("[", 1);
        first = false;
      }
    }else{
      myConn.SendNow(jsonp + "(");
    }
    myConn.SendNow(jPack.toString());
    if (jsonp.size()){
      myConn.SendNow(");\n", 3);
    }
  }

  void OutJSON::sendHeader(){
    std::string method = H.method;
    H.Clean();
    H.SetHeader("Content-Type", "text/javascript");
    H.protocol = "HTTP/1.0";
    H.setCORSHeaders();
    H.SendResponse("200", "OK", myConn);
    sentHeader = true;
  }
  
  void OutJSON::onFail(){
    //Only run failure handle if we're not being persistent
    if (!keepReselecting){
      HTTPOutput::onFail();
    }else{
      onFinish();
    }
  }

  bool OutJSON::onFinish(){
    static bool recursive = false;
    if (recursive){return true;}
    recursive = true;
    if (keepReselecting && !isPushing()){
      uint64_t maxTimer = 7200;
      while (--maxTimer && nProxy.userClient.isAlive() && keepGoing()){
        Util::wait(500);
        stats();
        if (Util::getStreamStatus(streamName) != STRMSTAT_READY){
          disconnect();
        }else{
          updateMeta();
          if (isReadyForPlay()){
            recursive = false;
            return true;
          }
        }
      }
      recursive = false;
    }
    if (!jsonp.size() && !first){
      myConn.SendNow("]\n", 2);
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
      while (dupes != "") {
        index = dupes.find(',');
        nodup.insert(dupes.substr(0, index));
        if (index != std::string::npos) {
          dupes.erase(0, index + 1);
        } else {
          dupes = "";
        }
      }
    }
  }

  void OutJSON::onWebsocketFrame(){
    if (!isPushing()){
      if (!allowPush(pushPass)){
        onFinish();
        return;
      }
    }
    if (!bootMsOffset){
      if (myMeta.bootMsOffset){
        bootMsOffset = myMeta.bootMsOffset;
      }else{
        bootMsOffset = Util::bootMS();
      }
    }
    //We now know we're allowed to push. Read a JSON object.
    JSON::Value inJSON = JSON::fromString(webSock->data, webSock->data.size());
    if (!inJSON || !inJSON.isObject()){
      //Ignore empty and/or non-parsable JSON packets
      MEDIUM_MSG("Ignoring non-JSON object: %s", webSock->data);
      return;
    }
    //Let's create a new track for pushing purposes, if needed
    if (!pushTrack){
      pushTrack = 1;
      while (myMeta.tracks.count(pushTrack)){
        ++pushTrack;
      }
    }
    myMeta.tracks[pushTrack].type = "meta";
    myMeta.tracks[pushTrack].codec = "JSON";
    //We have a track set correctly. Let's attempt to buffer a frame.
    lastSendTime = Util::bootMS();
    if (!inJSON.isMember("unix")){
      //Base timestamp on arrival time
      lastOutTime = (lastSendTime - bootMsOffset);
    }else{
      //Base timestamp on unix time
      lastOutTime = (lastSendTime - bootMsOffset) + (inJSON["unix"].asInt() - Util::epoch()) * 1000;
    }
    lastOutData = inJSON.toString();
    static DTSC::Packet newPack;
    newPack.genericFill(lastOutTime, 0, pushTrack, lastOutData.data(), lastOutData.size(), 0, true, bootMsOffset);
    bufferLivePacket(newPack);
    if (!idleInterval){idleInterval = 100;}
    if (isBlocking){setBlocking(false);}
  }

  /// Repeats last JSON packet every 5 seconds to keep stream alive.
  void OutJSON::onIdle(){
    if (nProxy.trackState[pushTrack] != FILL_ACC){
      continueNegotiate(pushTrack);
      if (nProxy.trackState[pushTrack] == FILL_ACC){
        idleInterval = 5000;
      }
      return;
    }
    lastOutTime += (Util::bootMS() - lastSendTime);
    lastSendTime = Util::bootMS();
    static DTSC::Packet newPack;
    newPack.genericFill(lastOutTime, 0, pushTrack, lastOutData.data(), lastOutData.size(), 0, true, bootMsOffset);
    bufferLivePacket(newPack);
  }

  void OutJSON::onHTTP(){
    std::string method = H.method;
    preWebsocketConnect();//Not actually a websocket, but we need to do the same checks
    jsonp = "";
    if (H.GetVar("callback") != ""){jsonp = H.GetVar("callback");}
    if (H.GetVar("jsonp") != ""){jsonp = H.GetVar("jsonp");}
    
    H.Clean();
    H.setCORSHeaders();
    if(method == "OPTIONS" || method == "HEAD"){
      H.SetHeader("Content-Type", "text/javascript");
      H.protocol = "HTTP/1.0";
      H.SendResponse("200", "OK", myConn);
      H.Clean();
      return;
    }
    first = true;
    parseData = true;
    wantRequest = false;
  }

}

