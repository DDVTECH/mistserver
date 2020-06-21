#include "output_dtsc.h"
#include <cstdlib>
#include <cstring>
#include <mist/auth.h>
#include <mist/bitfields.h>
#include <mist/defines.h>
#include <mist/stream.h>
#include <mist/triggers.h>
#include <sys/stat.h>

namespace Mist{
  OutDTSC::OutDTSC(Socket::Connection &conn) : Output(conn){
    setBlocking(true);
    JSON::Value prep;
    prep["cmd"] = "hi";
    prep["version"] = APPIDENT;
    prep["pack_method"] = 2;
    salt = Secure::md5("mehstuff" + JSON::Value((uint64_t)time(0)).asString());
    prep["salt"] = salt;
    /// \todo Make this securererer.
    sendCmd(prep);
    lastActive = Util::epoch();
  }

  OutDTSC::~OutDTSC(){}

  void OutDTSC::stats(bool force){
    unsigned long long int now = Util::epoch();
    if (now - lastActive > 1 && !pushing){
      JSON::Value prep;
      prep["cmd"] = "ping";
      sendCmd(prep);
      lastActive = now;
    }
    Output::stats(force);
  }

  void OutDTSC::sendCmd(const JSON::Value &data){
    MEDIUM_MSG("Sending DTCM: %s", data.toString().c_str());
    myConn.SendNow("DTCM");
    char sSize[4] ={0, 0, 0, 0};
    Bit::htobl(sSize, data.packedSize());
    myConn.SendNow(sSize, 4);
    data.sendTo(myConn);
  }

  void OutDTSC::sendOk(const std::string &msg){
    JSON::Value err;
    err["cmd"] = "ok";
    err["msg"] = msg;
    sendCmd(err);
  }

  void OutDTSC::init(Util::Config *cfg){
    Output::init(cfg);
    capa["name"] = "DTSC";
    capa["friendly"] = "DTSC";
    capa["desc"] = "Real time streaming over DTSC (proprietary protocol for efficient inter-server streaming)";
    capa["deps"] = "";
    capa["codecs"][0u][0u].append("+*");
    cfg->addConnectorOptions(4200, capa);
    config = cfg;
  }

  std::string OutDTSC::getStatsName(){return (pushing ? "INPUT:DTSC" : "OUTPUT:DTSC");}

  /// Seeks to the first sync'ed keyframe of the main track.
  /// Aborts if there is no main track or it has no keyframes.
  void OutDTSC::initialSeek(){
    uint64_t seekPos = 0;
    if (M.getLive()){
      size_t mainTrack = getMainSelectedTrack();
      // cancel if there are no keys in the main track
      if (mainTrack == INVALID_TRACK_ID){return;}

      DTSC::Keys keys(M.keys(mainTrack));
      if (!keys.getValidCount()){return;}
      // seek to the oldest keyframe
      std::set<size_t> validTracks = M.getValidTracks();
      for (size_t i = keys.getFirstValid(); i < keys.getEndValid(); ++i){
        seekPos = keys.getTime(i);
        bool good = true;
        // check if all tracks have data for this point in time
        for (std::map<size_t, Comms::Users>::iterator ti = userSelect.begin(); ti != userSelect.end(); ++ti){
          if (mainTrack == ti->first){continue;}// skip self
          if (!validTracks.count(ti->first)){
            HIGH_MSG("Skipping track %zu, not in tracks", ti->first);
            continue;
          }// ignore missing tracks
          if (M.getLastms(ti->first) == M.getFirstms(ti->first)){
            HIGH_MSG("Skipping track %zu, last equals first", ti->first);
            continue;
          }// ignore point-tracks
          if (M.getFirstms(ti->first) > seekPos){
            good = false;
            break;
          }
          HIGH_MSG("Track %zu is good", ti->first);
        }
        // if yes, seek here
        if (good){break;}
      }
    }
    MEDIUM_MSG("Initial seek to %" PRIu64 "ms", seekPos);
    seek(seekPos);
  }

  void OutDTSC::sendNext(){
    // If selectable tracks changed, set sentHeader to false to force it to send init data
    static uint64_t lastMeta = 0;
    if (Util::epoch() > lastMeta + 5){
      lastMeta = Util::epoch();
      if (selectDefaultTracks()){
        INFO_MSG("Track selection changed - resending headers and continuing");
        sentHeader = false;
        return;
      }
    }
    DTSC::Packet p(thisPacket, thisIdx + 1);
    myConn.SendNow(p.getData(), p.getDataLen());
    lastActive = Util::epoch();
  }

  void OutDTSC::sendHeader(){
    sentHeader = true;
    userSelect.clear();
    std::set<size_t> validTracks = M.getValidTracks();
    std::set<size_t> selectedTracks;
    for (std::set<size_t>::iterator it = validTracks.begin(); it != validTracks.end(); it++){
      if (M.getType(*it) == "video" || M.getType(*it) == "audio"){
        userSelect[*it].reload(streamName, *it);
        selectedTracks.insert(*it);
      }
    }
    M.send(myConn, true, selectedTracks, true);
    if (M.getLive()){realTime = 0;}
  }

  void OutDTSC::onFail(const std::string &msg, bool critical){
    JSON::Value err;
    err["cmd"] = "error";
    err["msg"] = msg;
    sendCmd(err);
    Output::onFail(msg, critical);
  }

  void OutDTSC::onRequest(){
    while (myConn.Received().available(8)){
      if (myConn.Received().copy(4) == "DTCM"){
        // Command message
        std::string toRec = myConn.Received().copy(8);
        unsigned long rSize = Bit::btohl(toRec.c_str() + 4);
        if (!myConn.Received().available(8 + rSize)){return;}// abort - not enough data yet
        myConn.Received().remove(8);
        std::string dataPacket = myConn.Received().remove(rSize);
        DTSC::Scan dScan((char *)dataPacket.data(), rSize);
        INFO_MSG("Received DTCM: %s", dScan.asJSON().toString().c_str());
        if (dScan.getMember("cmd").asString() == "push"){
          handlePush(dScan);
          continue;
        }
        if (dScan.getMember("cmd").asString() == "play"){
          handlePlay(dScan);
          continue;
        }
        if (dScan.getMember("cmd").asString() == "ping"){
          sendOk("Pong!");
          continue;
        }
        if (dScan.getMember("cmd").asString() == "ok"){
          INFO_MSG("Ok: %s", dScan.getMember("msg").asString().c_str());
          continue;
        }
        if (dScan.getMember("cmd").asString() == "error"){
          ERROR_MSG("%s", dScan.getMember("msg").asString().c_str());
          continue;
        }
        if (dScan.getMember("cmd").asString() == "reset"){
          meta.reInit(streamName);
          sendOk("Internal state reset");
          continue;
        }
        WARN_MSG("Unhandled DTCM command: '%s'", dScan.getMember("cmd").asString().c_str());
      }else if (myConn.Received().copy(4) == "DTSC"){
        // Header packet
        if (!isPushing()){
          onFail("DTSC_HEAD ignored: you are not cleared for pushing data!", true);
          return;
        }
        std::string toRec = myConn.Received().copy(8);
        unsigned long rSize = Bit::btohl(toRec.c_str() + 4);
        if (!myConn.Received().available(8 + rSize)){return;}// abort - not enough data yet
        std::string dataPacket = myConn.Received().remove(8 + rSize);
        DTSC::Packet metaPack(dataPacket.data(), dataPacket.size());
        meta.reInit(streamName, metaPack.getScan());
        std::stringstream rep;
        rep << "DTSC_HEAD received with " << M.getValidTracks().size() << " tracks. Bring on those data packets!";
        sendOk(rep.str());
      }else if (myConn.Received().copy(4) == "DTP2"){
        if (!isPushing()){
          onFail("DTSC_V2 ignored: you are not cleared for pushing data!", true);
          return;
        }
        // Data packet
        std::string toRec = myConn.Received().copy(8);
        unsigned long rSize = Bit::btohl(toRec.c_str() + 4);
        if (!myConn.Received().available(8 + rSize)){return;}// abort - not enough data yet
        std::string dataPacket = myConn.Received().remove(8 + rSize);
        DTSC::Packet inPack(dataPacket.data(), dataPacket.size(), true);
        if (M.trackIDToIndex(inPack.getTrackId(), getpid()) == INVALID_TRACK_ID){
          onFail("DTSC_V2 received for a track that was not announced in the DTSC_HEAD!", true);
          return;
        }
        bufferLivePacket(inPack);
      }else{
        // Invalid
        onFail("Invalid packet header received. Aborting.", true);
        return;
      }
    }
  }

  void OutDTSC::handlePlay(DTSC::Scan &dScan){
    streamName = dScan.getMember("stream").asString();
    Util::sanitizeName(streamName);
    Util::Config::streamName = streamName;
    parseData = true;
    INFO_MSG("Handled play for stream %s", streamName.c_str());
    setBlocking(false);
  }

  void OutDTSC::handlePush(DTSC::Scan &dScan){
    streamName = dScan.getMember("stream").asString();
    std::string passString = dScan.getMember("password").asString();
    Util::sanitizeName(streamName);
    Util::Config::streamName = streamName;
    if (!allowPush(passString)){
      onFail("Push not allowed - stream and/or password incorrect", true);
      return;
    }
    sendOk("You're cleared for pushing! DTSC_HEAD please?");
  }

}// namespace Mist
