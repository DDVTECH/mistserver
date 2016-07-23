#include "output_dtsc.h"
#include <mist/defines.h>
#include <mist/stream.h>
#include <mist/triggers.h>
#include <mist/auth.h>
#include <mist/bitfields.h>
#include <sys/stat.h>
#include <cstring>
#include <cstdlib>

namespace Mist {
  OutDTSC::OutDTSC(Socket::Connection & conn) : Output(conn) {
    setBlocking(true);
    JSON::Value prep;
    prep["cmd"] = "hi";
    prep["version"] = "MistServer " PACKAGE_VERSION;
#ifdef BIGMETA
    prep["pack_method"] = 2ll;
#else
    prep["pack_method"] = 1ll;
#endif
    salt = Secure::md5("mehstuff"+JSON::Value((long long)time(0)).asString());
    prep["salt"] = salt;
    /// \todo Make this securererer.
    unsigned long sendSize = prep.packedSize();
    myConn.SendNow("DTCM");
    char sSize[4] = {0, 0, 0, 0};
    Bit::htobl(sSize, prep.packedSize());
    myConn.SendNow(sSize, 4);
    prep.sendTo(myConn);
    pushing = false;
  }

  OutDTSC::~OutDTSC() {}

  void OutDTSC::init(Util::Config * cfg){
    Output::init(cfg);
    capa["name"] = "DTSC";
    capa["desc"] = "Enables the DTSC protocol for efficient inter-server stream exchange.";
    capa["deps"] = "";
    capa["codecs"][0u][0u].append("*");
    cfg->addConnectorOptions(4200, capa);
    config = cfg;
  }
  
  std::string OutDTSC::getStatsName(){
    if (pushing){
      return "INPUT";
    }else{
      return "OUTPUT";
    }
  }

  /// Seeks to the first sync'ed keyframe of the main track.
  /// Aborts if there is no main track or it has no keyframes.
  void OutDTSC::initialSeek(){
    unsigned long long seekPos = 0;
    if (myMeta.live){
      long unsigned int mainTrack = getMainSelectedTrack();
      //cancel if there are no keys in the main track
      if (!myMeta.tracks.count(mainTrack) || !myMeta.tracks[mainTrack].keys.size()){return;}
      //seek to the oldest keyframe
      for (std::deque<DTSC::Key>::iterator it = myMeta.tracks[mainTrack].keys.begin(); it != myMeta.tracks[mainTrack].keys.end(); ++it){
        seekPos = it->getTime();
        bool good = true;
        //check if all tracks have data for this point in time
        for (std::set<unsigned long>::iterator ti = selectedTracks.begin(); ti != selectedTracks.end(); ++ti){
          if (mainTrack == *ti){continue;}//skip self
          if (!myMeta.tracks.count(*ti)){
            HIGH_MSG("Skipping track %lu, not in tracks", *ti);
            continue;
          }//ignore missing tracks
          if (myMeta.tracks[*ti].lastms == myMeta.tracks[*ti].firstms){
            HIGH_MSG("Skipping track %lu, last equals first", *ti);
            continue;
          }//ignore point-tracks
          if (myMeta.tracks[*ti].firstms > seekPos){good = false; break;}
          HIGH_MSG("Track %lu is good", *ti);
        }
        //if yes, seek here
        if (good){break;}
      }
    }
    MEDIUM_MSG("Initial seek to %llums", seekPos);
    seek(seekPos);
  }

  void OutDTSC::sendNext(){
    //If there are now more selectable tracks, select the new track and do a seek to the current timestamp
    //Set sentHeader to false to force it to send init data
    if (selectedTracks.size() < 2){
      static unsigned long long lastMeta = 0;
      if (Util::epoch() > lastMeta + 5){
        lastMeta = Util::epoch();
        updateMeta();
        if (myMeta.tracks.size() > 1){
          size_t prevTrackCount = selectedTracks.size();
          selectDefaultTracks();
          if (selectedTracks.size() > prevTrackCount){
            INFO_MSG("Picked up new track - selecting it and resetting state.");
            sentHeader = false;
            seek(currentTime());
            return;
          }
        }
      }
    }
    myConn.SendNow(thisPacket.getData(), thisPacket.getDataLen());
  }

  void OutDTSC::sendHeader(){
    sentHeader = true;
    selectedTracks.clear();
    for (std::map<unsigned int, DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++){
      if (it->second.type == "video" || it->second.type == "audio"){
        selectedTracks.insert(it->first);
      }
    }
    myMeta.send(myConn, true, selectedTracks);
    if (myMeta.live){
      realTime = 0;
    }
  }

  void OutDTSC::onRequest(){
    while (myConn.Received().available(8)){
      if (myConn.Received().copy(4) == "DTCM"){
        // Command message
        std::string toRec = myConn.Received().copy(8);
        unsigned long rSize = Bit::btohl(toRec.c_str()+4);
        if (!myConn.Received().available(8+rSize)){return;}//abort - not enough data yet
        myConn.Received().remove(8);
        std::string dataPacket = myConn.Received().remove(rSize);
        DTSC::Scan dScan((char*)dataPacket.data(), rSize);
        if (dScan.getMember("cmd").asString() == "push"){handlePush(dScan); continue;}
        if (dScan.getMember("cmd").asString() == "play"){handlePlay(dScan); continue;}
        WARN_MSG("Unhandled DTCM command: '%s'", dScan.getMember("cmd").asString().c_str());
      }else{
        // Non-command message
        //
      }
    }
  }

  void OutDTSC::handlePlay(DTSC::Scan & dScan){
    streamName = dScan.getMember("stream").asString();
    Util::sanitizeName(streamName);
    parseData = true;
    INFO_MSG("Handled play for stream %s", streamName.c_str());
    setBlocking(false);
  }

  void OutDTSC::handlePush(DTSC::Scan & dScan){
    streamName = dScan.getMember("stream").asString();
    std::string passString = dScan.getMember("password").asString();

    Util::sanitizeName(streamName);
    //pull the server configuration
    std::string smp = streamName.substr(0,(streamName.find_first_of("+ ")));
    IPC::sharedPage serverCfg("!mistConfig", DEFAULT_CONF_PAGE_SIZE); ///< Contains server configuration and capabilities
    IPC::semaphore configLock("!mistConfLock", O_CREAT | O_RDWR, ACCESSPERMS, 1);
    configLock.wait();
    
    DTSC::Scan streamCfg = DTSC::Scan(serverCfg.mapped, serverCfg.len).getMember("streams").getMember(smp);
    if (streamCfg){
      if (streamCfg.getMember("source").asString().substr(0, 7) != "push://"){
        DEBUG_MSG(DLVL_FAIL, "Push rejected - stream %s not a push-able stream. (%s != push://*)", streamName.c_str(), streamCfg.getMember("source").asString().c_str());
        myConn.close();
      }else{
        std::string source = streamCfg.getMember("source").asString().substr(7);
        std::string IP = source.substr(0, source.find('@'));
        /*LTS-START*/
        std::string password;
        if (source.find('@') != std::string::npos){
          password = source.substr(source.find('@')+1);
          if (password != ""){
            if (passString == Secure::md5(salt + password)){
              DEBUG_MSG(DLVL_DEVEL, "Password accepted - ignoring IP settings.");
              IP = "";
            }else{
              DEBUG_MSG(DLVL_DEVEL, "Password rejected - checking IP.");
              if (IP == ""){
                IP = "deny-all.invalid";
              }
            }
          }
        }
        if(Triggers::shouldTrigger("STREAM_PUSH", smp)){
          std::string payload = streamName+"\n" + myConn.getHost() +"\n"+capa["name"].asStringRef()+"\n"+reqUrl;
          if (!Triggers::doTrigger("STREAM_PUSH", payload, smp)){
            DEBUG_MSG(DLVL_FAIL, "Push from %s to %s rejected - STREAM_PUSH trigger denied the push", myConn.getHost().c_str(), streamName.c_str());
            myConn.close();
            configLock.post();
            configLock.close();
            return;
          }
        }
        /*LTS-END*/
        if (IP != ""){
          if (!myConn.isAddress(IP)){
            DEBUG_MSG(DLVL_FAIL, "Push from %s to %s rejected - source host not whitelisted", myConn.getHost().c_str(), streamName.c_str());
            myConn.close();
          }
        }
      }
    }else{
      DEBUG_MSG(DLVL_FAIL, "Push from %s rejected - stream '%s' not configured.", myConn.getHost().c_str(), streamName.c_str());
      myConn.close();
    }
    configLock.post();
    configLock.close();
    if (!myConn){return;}//do not initialize if rejected
    initialize();
    pushing = true;
  }


}

