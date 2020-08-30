#include "output_dtsc.h"
#include <cstdlib>
#include <cstring>
#include <mist/auth.h>
#include <mist/bitfields.h>
#include <mist/defines.h>
#include <mist/stream.h>
#include <mist/triggers.h>
#include <mist/http_parser.h>
#include <sys/stat.h>

namespace Mist{
  OutDTSC::OutDTSC(Socket::Connection &conn) : Output(conn){
    JSON::Value prep;
    if (config->getString("target").size()){
      streamName = config->getString("streamname");
      pushUrl = HTTP::URL(config->getString("target"));
      if (pushUrl.protocol != "dtsc"){
        onFail("Target must start with dtsc://", true);
        return;
      }

      if (!pushUrl.path.size()){pushUrl.path = streamName;}
      INFO_MSG("About to push stream %s out. Host: %s, port: %d, target stream: %s", streamName.c_str(),
               pushUrl.host.c_str(), pushUrl.getPort(), pushUrl.path.c_str());
      myConn.close();
      myConn.Received().clear();
      myConn.open(pushUrl.host, pushUrl.getPort(), true);
      initialize();
      initialSeek();
      if (!myConn){
        onFail("Could not start push, aborting", true);
        return;
      }
      prep["cmd"] = "push";
      prep["version"] = APPIDENT;
      prep["stream"] = pushUrl.path;
      std::map<std::string, std::string> args;
      HTTP::parseVars(pushUrl.args, args);
      if (args.count("pass")){prep["password"] = args["pass"];}
      if (args.count("pw")){prep["password"] = args["pw"];}
      if (args.count("password")){prep["password"] = args["password"];}
      if (pushUrl.pass.size()){prep["password"] = pushUrl.pass;}
      sendCmd(prep);
      wantRequest = true;
      parseData = true;
      return;
    }


    setBlocking(true);
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
    capa["push_urls"].append("dtsc://*");
    capa["incoming_push_url"] = "dtsc://$host:$port/$stream?pass=$password";

    JSON::Value opt;
    opt["arg"] = "string";
    opt["default"] = "";
    opt["arg_num"] = 1;
    opt["help"] = "Target DTSC URL to push out towards.";
    cfg->addOption("target", opt);
    cfg->addOption("streamname", JSON::fromString("{\"arg\":\"string\",\"short\":\"s\",\"long\":"
                                                  "\"stream\",\"help\":\"The name of the stream to "
                                                  "push out, when pushing out.\"}"));

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
    std::set<size_t> selectedTracks;
    for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end(); it++){
      selectedTracks.insert(it->first);
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
        HIGH_MSG("Received DTCM: %s", dScan.asJSON().toString().c_str());
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
        if (dScan.getMember("cmd").asString() == "hi"){
          INFO_MSG("Connected to server running version %s", dScan.getMember("version").asString().c_str());
          continue;
        }
        if (dScan.getMember("cmd").asString() == "error"){
          ERROR_MSG("%s", dScan.getMember("msg").asString().c_str());
          continue;
        }
        if (dScan.getMember("cmd").asString() == "reset"){
          userSelect.clear();
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
        DTSC::Scan metaScan = metaPack.getScan();
        meta.refresh();
        size_t prevTracks = meta.getValidTracks().size();

        size_t tNum = metaScan.getMember("tracks").getSize();
        for (int i = 0; i < tNum; i++){
          DTSC::Scan trk = metaScan.getMember("tracks").getIndice(i);
          size_t trackID = trk.getMember("trackid").asInt();
          if (meta.trackIDToIndex(trackID, getpid()) == INVALID_TRACK_ID){
            MEDIUM_MSG("Adding track: %s", trk.asJSON().toString().c_str());
            meta.addTrackFrom(trk);
          }else{
            HIGH_MSG("Already had track: %s", trk.asJSON().toString().c_str());
          }
        }
        meta.refresh();
        std::stringstream rep;
        rep << "DTSC_HEAD parsed, we went from " << prevTracks << " to " << meta.getValidTracks().size() << " tracks. Bring on those data packets!";
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
        size_t tid = M.trackIDToIndex(inPack.getTrackId(), getpid());
        if (tid == INVALID_TRACK_ID){
          //WARN_MSG("Received data for unknown track: %zu", inPack.getTrackId());
          onFail("DTSC_V2 received for a track that was not announced in a header!", true);
          return;
        }
        if (!userSelect.count(tid)){
          userSelect[tid].reload(streamName, tid, COMM_STATUS_SOURCE);
        }
        char *data;
        size_t dataLen;
        inPack.getString("data", data, dataLen);
        bufferLivePacket(inPack.getTime(), inPack.getInt("offset"), tid, data, dataLen, inPack.getInt("bpos"), inPack.getFlag("keyframe"));
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
    Util::streamName = streamName;
    parseData = true;
    INFO_MSG("Handled play for stream %s", streamName.c_str());
    setBlocking(false);
  }

  void OutDTSC::handlePush(DTSC::Scan &dScan){
    streamName = dScan.getMember("stream").asString();
    std::string passString = dScan.getMember("password").asString();
    Util::sanitizeName(streamName);
    Util::streamName = streamName;
    if (!allowPush(passString)){
      onFail("Push not allowed - stream and/or password incorrect", true);
      return;
    }
    sendOk("You're cleared for pushing! DTSC_HEAD please?");
  }

}// namespace Mist
