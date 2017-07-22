#include <iostream>
#include <cstring>
#include <cerrno>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <mist/stream.h>
#include <mist/defines.h>

#include <mist/util.h>
#include <mist/bitfields.h>

#include "input_dtsc.h"

namespace Mist {
  inputDTSC::inputDTSC(Util::Config * cfg) : Input(cfg) {
    capa["name"] = "DTSC";
    capa["desc"] = "Enables DTSC Input";
    capa["priority"] = 9ll;
    capa["source_match"].append("/*.dtsc");
    capa["source_match"].append("dtsc://*");
    capa["codecs"][0u][0u].append("H264");
    capa["codecs"][0u][0u].append("H263");
    capa["codecs"][0u][0u].append("VP6");
    capa["codecs"][0u][0u].append("theora");
    capa["codecs"][0u][1u].append("AAC");
    capa["codecs"][0u][1u].append("MP3");
    capa["codecs"][0u][1u].append("vorbis");


    JSON::Value option;
    option["arg"] = "integer";
    option["long"] = "buffer";
    option["short"] = "b";
    option["help"] = "Live stream DVR buffer time in ms";
    option["value"].append(50000LL);
    config->addOption("bufferTime", option);
    capa["optional"]["DVR"]["name"] = "Buffer time (ms)";
    capa["optional"]["DVR"]["help"] = "The target available buffer time for this live stream, in milliseconds. This is the time available to seek around in, and will automatically be extended to fit whole keyframes as well as the minimum duration needed for stable playback.";
    capa["optional"]["DVR"]["option"] = "--buffer";
    capa["optional"]["DVR"]["type"] = "uint";
    capa["optional"]["DVR"]["default"] = 50000LL;
    /*LTS-START*/
    option.null();
    option["arg"] = "integer";
    option["long"] = "segment-size";
    option["short"] = "S";
    option["help"] = "Target time duration in milliseconds for segments";
    option["value"].append(5000LL);
    config->addOption("segmentsize", option);
    capa["optional"]["segmentsize"]["name"] = "Segment size (ms)";
    capa["optional"]["segmentsize"]["help"] = "Target time duration in milliseconds for segments.";
    capa["optional"]["segmentsize"]["option"] = "--segment-size";
    capa["optional"]["segmentsize"]["type"] = "uint";
    capa["optional"]["segmentsize"]["default"] = 5000LL;
    /*LTS-END*/

  }

  bool inputDTSC::needsLock(){
    return config->getString("input").substr(0, 7) != "dtsc://" && config->getString("input") != "-";
  }

  void parseDTSCURI(const std::string & src, std::string & host, uint16_t & port, std::string & password, std::string & streamName) {
    host = "";
    port = 4200;
    password = "";
    streamName = "";
    std::deque<std::string> matches;
    if (Util::stringScan(src, "%s:%s@%s/%s", matches)) {
      host = matches[0];
      port = atoi(matches[1].c_str());
      password = matches[2];
      streamName = matches[3];
      return;
    }
    //Using default streamname
    if (Util::stringScan(src, "%s:%s@%s", matches)) {
      host = matches[0];
      port = atoi(matches[1].c_str());
      password = matches[2];
      return;
    }
    //Without password
    if (Util::stringScan(src, "%s:%s/%s", matches)) {
      host = matches[0];
      port = atoi(matches[1].c_str());
      streamName = matches[2];
      return;
    }
    //Using default port
    if (Util::stringScan(src, "%s@%s/%s", matches)) {
      host = matches[0];
      password = matches[1];
      streamName = matches[2];
      return;
    }
    //Default port, no password
    if (Util::stringScan(src, "%s/%s", matches)) {
      host = matches[0];
      streamName = matches[1];
      return;
    }
    //No password, default streamname
    if (Util::stringScan(src, "%s:%s", matches)) {
      host = matches[0];
      port = atoi(matches[1].c_str());
      return;
    }
    //Default port and streamname
    if (Util::stringScan(src, "%s@%s", matches)) {
      host = matches[0];
      password = matches[1];
      return;
    }
    //Default port and streamname, no password
    if (Util::stringScan(src, "%s", matches)) {
      host = matches[0];
      return;
    }
  }

  void inputDTSC::parseStreamHeader() {
    while (srcConn.connected() && config->is_active){
      srcConn.spool();
      if (srcConn.Received().available(8)){
        if (srcConn.Received().copy(4) == "DTCM" || srcConn.Received().copy(4) == "DTSC") {
          // Command message
          std::string toRec = srcConn.Received().copy(8);
          unsigned long rSize = Bit::btohl(toRec.c_str() + 4);
          if (!srcConn.Received().available(8 + rSize)) {
            nProxy.userClient.keepAlive();
            Util::sleep(100);
            continue; //abort - not enough data yet
          }
          //Ignore initial DTCM message, as this is a "hi" message from the server
          if (srcConn.Received().copy(4) == "DTCM"){
            srcConn.Received().remove(8 + rSize);
          }else{
            std::string dataPacket = srcConn.Received().remove(8+rSize);
            DTSC::Packet metaPack(dataPacket.data(), dataPacket.size());
            myMeta.reinit(metaPack);
            for (std::map<unsigned int, DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++){
              continueNegotiate(it->first, true);
            }
            break;
          }
        }else{
          INFO_MSG("Received a wrong type of packet - '%s'", srcConn.Received().copy(4).c_str());
          break;
        }
      }else{
        Util::sleep(100);
        nProxy.userClient.keepAlive();
      }
    }
  }

  bool inputDTSC::openStreamSource() {
    std::string source = config->getString("input");
    if (source == "-"){
      srcConn = Socket::Connection(fileno(stdout),fileno(stdin));
      return true;
    }
    if (source.find("dtsc://") == 0) {
      source.erase(0, 7);
    }
    std::string host;
    uint16_t port;
    std::string password;
    std::string streamName;
    parseDTSCURI(source, host, port, password, streamName);
    std::string givenStream = config->getString("streamname");
    if (streamName == "") {
      streamName = givenStream;
    }else{
      if (givenStream.find("+") != std::string::npos){
        streamName += givenStream.substr(givenStream.find("+"));
      }
    }
    srcConn = Socket::Connection(host, port, true);
    if (!srcConn.connected()){
      return false;
    }
    JSON::Value prep;
    prep["cmd"] = "play";
    prep["version"] = "MistServer " PACKAGE_VERSION;
    prep["stream"] = streamName;
    srcConn.SendNow("DTCM");
    char sSize[4] = {0, 0, 0, 0};
    Bit::htobl(sSize, prep.packedSize());
    srcConn.SendNow(sSize, 4);
    prep.sendTo(srcConn);
    return true;
  }

  void inputDTSC::closeStreamSource(){
    srcConn.close();
  }

  bool inputDTSC::checkArguments() {
    if (!needsLock()) {
      return true;
    } else {
      if (!config->getString("streamname").size()) {
        if (config->getString("output") == "-") {
          std::cerr << "Output to stdout not yet supported" << std::endl;
          return false;
        }
      } else {
        if (config->getString("output") != "-") {
          std::cerr << "File output in player mode not supported" << std::endl;
          return false;
        }
      }

      //open File
      inFile = DTSC::File(config->getString("input"));
      if (!inFile) {
        return false;
      }
    }
    return true;
  }

  bool inputDTSC::readHeader() {
    if (!needsLock()) {
      return true;
    }
    if (!inFile) {
      return false;
    }
    DTSC::File tmp(config->getString("input") + ".dtsh");
    if (tmp) {
      myMeta = tmp.getMeta();
      DEBUG_MSG(DLVL_HIGH, "Meta read in with %lu tracks", myMeta.tracks.size());
      return true;
    }
    if (inFile.getMeta().moreheader < 0 || inFile.getMeta().tracks.size() == 0) {
      DEBUG_MSG(DLVL_FAIL, "Missing external header file");
      return false;
    }
    myMeta = DTSC::Meta(inFile.getMeta());
    DEBUG_MSG(DLVL_DEVEL, "Meta read in with %lu tracks", myMeta.tracks.size());
    return true;
  }

  void inputDTSC::getNext(bool smart) {
    if (!needsLock()){
      thisPacket.reInit(srcConn);
      while (config->is_active){
        if (thisPacket.getVersion() == DTSC::DTCM){
          nProxy.userClient.keepAlive();
          std::string cmd;
          thisPacket.getString("cmd", cmd);
          if (cmd == "reset"){
            //Read next packet
            thisPacket.reInit(srcConn);
            if (thisPacket.getVersion() == DTSC::DTSC_HEAD){
              DTSC::Meta newMeta;
              newMeta.reinit(thisPacket);
              //Detect new tracks
              std::set<unsigned int> newTracks;
              for (std::map<unsigned int, DTSC::Track>::iterator it = newMeta.tracks.begin(); it != newMeta.tracks.end(); it++){
                if (!myMeta.tracks.count(it->first)){
                  newTracks.insert(it->first);
                }
              }

              for (std::set<unsigned int>::iterator it = newTracks.begin(); it != newTracks.end(); it++){
                INFO_MSG("Reset: adding track %d", *it);
                myMeta.tracks[*it] = newMeta.tracks[*it];
                continueNegotiate(*it, true);
              }

              //Detect removed tracks
              std::set<unsigned int> deletedTracks;
              for (std::map<unsigned int, DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++){
                if (!newMeta.tracks.count(it->first)){
                  deletedTracks.insert(it->first);
                }
              }

              for(std::set<unsigned int>::iterator it = deletedTracks.begin(); it != deletedTracks.end(); it++){
                INFO_MSG("Reset: deleting track %d", *it);
                myMeta.tracks.erase(*it);
              }
              thisPacket.reInit(srcConn);//read the next packet before continuing
            }else{
              myMeta = DTSC::Meta();
            }
          }else{
            thisPacket.reInit(srcConn);//read the next packet before continuing
          }
          continue;//parse the next packet before returning
        }else if (thisPacket.getVersion() == DTSC::DTSC_HEAD){
          DTSC::Meta newMeta;
          newMeta.reinit(thisPacket);
          std::set<unsigned int> newTracks;
          for (std::map<unsigned int, DTSC::Track>::iterator it = newMeta.tracks.begin(); it != newMeta.tracks.end(); it++){
            if (!myMeta.tracks.count(it->first)){
              newTracks.insert(it->first);
            }
          }

          for (std::set<unsigned int>::iterator it = newTracks.begin(); it != newTracks.end(); it++){
            INFO_MSG("New header: adding track %d (%s)", *it, newMeta.tracks[*it].type.c_str());
            myMeta.tracks[*it] = newMeta.tracks[*it];
            continueNegotiate(*it, true);
          }
          thisPacket.reInit(srcConn);//read the next packet before continuing
          continue;//parse the next packet before returning
        }
        //We now know we have either a data packet, or an error.
        if (!thisPacket.getTrackId()){
          if (thisPacket.getVersion() == DTSC::DTSC_V2){
            WARN_MSG("Received bad packet for stream %s: %llu@%llu", streamName.c_str(), thisPacket.getTrackId(), thisPacket.getTime());
          }else{
            //All types except data packets are handled above, so if it's not a V2 data packet, we assume corruption
            WARN_MSG("Invalid packet header for stream %s", streamName.c_str());
          }
        }
        return;//we have a packet
      }
    }else{
      if (smart) {
        inFile.seekNext();
      } else {
        inFile.parseNext();
      }
      thisPacket = inFile.getPacket();
    }
  }

  void inputDTSC::seek(int seekTime) {
    inFile.seek_time(seekTime);
    initialTime = 0;
    playUntil = 0;
  }

  void inputDTSC::trackSelect(std::string trackSpec) {
    selectedTracks.clear();
    long long unsigned int index;
    while (trackSpec != "") {
      index = trackSpec.find(' ');
      selectedTracks.insert(atoi(trackSpec.substr(0, index).c_str()));
      if (index != std::string::npos) {
        trackSpec.erase(0, index + 1);
      } else {
        trackSpec = "";
      }
    }
    inFile.selectTracks(selectedTracks);
  }
}

