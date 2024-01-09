#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mist/defines.h>
#include <mist/stream.h>
#include <string>

#include <mist/bitfields.h>
#include <mist/util.h>

#include "input_dtsc.h"

namespace Mist{
  inputDTSC::inputDTSC(Util::Config *cfg) : Input(cfg){
    capa["name"] = "DTSC";
    capa["desc"] = "Load DTSC files as Video on Demand sources, or dtsc:// URLs from other "
                   "instances for live sources. This is the optimal method to pull live "
                   "sources from MistServer compatible instances.";
    capa["priority"] = 9;
    capa["source_match"].append("/*.dtsc");
    capa["source_match"].append("dtsc://*");
    capa["always_match"].append("dtsc://*"); // can be said to always-on mode
    capa["source_file"] = "$source";
    capa["codecs"]["video"].append("H264");
    capa["codecs"]["video"].append("H263");
    capa["codecs"]["video"].append("VP6");
    capa["codecs"]["video"].append("theora");
    capa["codecs"]["audio"].append("AAC");
    capa["codecs"]["audio"].append("MP3");
    capa["codecs"]["audio"].append("vorbis");

    JSON::Value option;
    option["arg"] = "integer";
    option["long"] = "buffer";
    option["short"] = "b";
    option["help"] = "Live stream DVR buffer time in ms";
    option["value"].append(50000);
    config->addOption("bufferTime", option);
    capa["optional"]["DVR"]["name"] = "Buffer time (ms)";
    capa["optional"]["DVR"]["help"] =
        "The target available buffer time for this live stream, in milliseconds. This is the time "
        "available to seek around in, and will automatically be extended to fit whole keyframes as "
        "well as the minimum duration needed for stable playback.";
    capa["optional"]["DVR"]["option"] = "--buffer";
    capa["optional"]["DVR"]["type"] = "uint";
    capa["optional"]["DVR"]["default"] = 50000;
    /*LTS-START*/
    option.null();
    option["arg"] = "integer";
    option["long"] = "segment-size";
    option["short"] = "S";
    option["help"] = "Target time duration in milliseconds for segments";
    option["value"].append(DEFAULT_FRAGMENT_DURATION);
    config->addOption("segmentsize", option);
    capa["optional"]["segmentsize"]["name"] = "Segment size (ms)";
    capa["optional"]["segmentsize"]["help"] = "Target time duration in milliseconds for segments.";
    capa["optional"]["segmentsize"]["option"] = "--segment-size";
    capa["optional"]["segmentsize"]["type"] = "uint";
    capa["optional"]["segmentsize"]["default"] = DEFAULT_FRAGMENT_DURATION;
    /*LTS-END*/

    F = NULL;
    lockCache = false;
    lockNeeded = false;
  }

  bool inputDTSC::needsLock(){
    if (!lockCache){
      lockNeeded =
          config->getString("input").substr(0, 7) != "dtsc://" && config->getString("input") != "-";
      lockCache = true;
    }
    return lockNeeded;
  }

  void parseDTSCURI(const std::string &src, std::string &host, uint16_t &port,
                    std::string &password, std::string &streamName){
    host = "";
    port = 4200;
    password = "";
    streamName = "";
    std::deque<std::string> matches;
    if (Util::stringScan(src, "%s:%s@%s/%s", matches)){
      host = matches[0];
      port = atoi(matches[1].c_str());
      password = matches[2];
      streamName = matches[3];
      return;
    }
    // Using default streamname
    if (Util::stringScan(src, "%s:%s@%s", matches)){
      host = matches[0];
      port = atoi(matches[1].c_str());
      password = matches[2];
      return;
    }
    // Without password
    if (Util::stringScan(src, "%s:%s/%s", matches)){
      host = matches[0];
      port = atoi(matches[1].c_str());
      streamName = matches[2];
      return;
    }
    // Using default port
    if (Util::stringScan(src, "%s@%s/%s", matches)){
      host = matches[0];
      password = matches[1];
      streamName = matches[2];
      return;
    }
    // Default port, no password
    if (Util::stringScan(src, "%s/%s", matches)){
      host = matches[0];
      streamName = matches[1];
      return;
    }
    // No password, default streamname
    if (Util::stringScan(src, "%s:%s", matches)){
      host = matches[0];
      port = atoi(matches[1].c_str());
      return;
    }
    // Default port and streamname
    if (Util::stringScan(src, "%s@%s", matches)){
      host = matches[0];
      password = matches[1];
      return;
    }
    // Default port and streamname, no password
    if (Util::stringScan(src, "%s", matches)){
      host = matches[0];
      return;
    }
  }

  void inputDTSC::parseStreamHeader(){
    while (srcConn.connected() && config->is_active){
      srcConn.spool();
      if (!srcConn.Received().available(8)){
        Util::sleep(100);
        keepAlive();
        continue;
      }

      if (srcConn.Received().copy(4) != "DTCM" && srcConn.Received().copy(4) != "DTSC"){
        INFO_MSG("Received a wrong type of packet - '%s'", srcConn.Received().copy(4).c_str());
        break;
      }
      // Command message
      std::string toRec = srcConn.Received().copy(8);
      uint32_t rSize = Bit::btohl(toRec.c_str() + 4);
      if (!srcConn.Received().available(8 + rSize)){
        keepAlive();
        Util::sleep(100);
        continue; // abort - not enough data yet
      }
      // Ignore initial DTCM message, as this is a "hi" message from the server
      if (srcConn.Received().copy(4) == "DTCM"){
        srcConn.Received().remove(8 + rSize);
        continue;
      }
      std::string dataPacket = srcConn.Received().remove(8 + rSize);
      DTSC::Packet metaPack(dataPacket.data(), dataPacket.size());
      DTSC::Meta nM("", metaPack.getScan());
      meta.reInit(streamName, false);
      meta.merge(nM, true, false);
      meta.setBootMsOffset(nM.getBootMsOffset());
      std::set<size_t> validTracks = M.getMySourceTracks(getpid());
      userSelect.clear();
      for (std::set<size_t>::iterator it = validTracks.begin(); it != validTracks.end(); ++it){
        userSelect[*it].reload(streamName, *it, COMM_STATUS_ACTIVE | COMM_STATUS_SOURCE | COMM_STATUS_DONOTTRACK);
      }
      break;
    }
  }

  bool inputDTSC::openStreamSource(){
    std::string source = config->getString("input");
    if (source == "-"){
      srcConn.open(fileno(stdout), fileno(stdin));
      return true;
    }
    if (source.find("dtsc://") == 0){source.erase(0, 7);}
    std::string host;
    uint16_t port;
    std::string password;
    std::string streamName;
    parseDTSCURI(source, host, port, password, streamName);
    std::string givenStream = config->getString("streamname");
    if (streamName == ""){streamName = givenStream;}
    srcConn.open(host, port, true);
    if (!srcConn.connected()){return false;}
    JSON::Value prep;
    prep["cmd"] = "play";
    prep["version"] = APPIDENT;
    prep["stream"] = streamName;
    srcConn.SendNow("DTCM");
    char sSize[4] ={0, 0, 0, 0};
    Bit::htobl(sSize, prep.packedSize());
    srcConn.SendNow(sSize, 4);
    prep.sendTo(srcConn);
    return true;
  }

  void inputDTSC::closeStreamSource(){srcConn.close();}

  bool inputDTSC::checkArguments(){
    if (!needsLock()){return true;}
    if (!config->getString("streamname").size()){
      if (config->getString("output") == "-"){
        Util::logExitReason(ER_FORMAT_SPECIFIC, "Output to stdout not yet supported");
        return false;
      }
    }else{
      if (config->getString("output") != "-"){
        Util::logExitReason(ER_FORMAT_SPECIFIC, "File output in player mode not supported");
        return false;
      }
    }

    // open File
    F = fopen(config->getString("input").c_str(), "r+b");
    if (!F){
      Util::logExitReason(ER_READ_START_FAILURE, "Could not open file %s", config->getString("input").c_str());
      return false;
    }
    fseek(F, 0, SEEK_SET);
    return true;
  }

  bool inputDTSC::needHeader(){
    if (!needsLock()){return false;}
    return Input::needHeader();
  }

  bool inputDTSC::readHeader(){
    if (!F){
      Util::logExitReason(ER_READ_START_FAILURE, "Reading header for '%s' failed: Could not open input stream", config->getString("input").c_str());
      return false;
    }
    size_t moreHeader = 0;
    do{
      char hdr[8];
      fseek(F, moreHeader, SEEK_SET);
      if (fread(hdr, 8, 1, F) != 1){
        Util::logExitReason(ER_READ_START_FAILURE, "Reading header for '%s' failed: Could not read header @ bpos %zu", config->getString("input").c_str(), moreHeader);
        return false;
      }
      if (memcmp(hdr, DTSC::Magic_Header, 4)){
        Util::logExitReason(ER_FORMAT_SPECIFIC, "Reading header for '%s' failed: File does not have a DTSC header @ bpos %zu", config->getString("input").c_str(), moreHeader);
        return false;
      }
      size_t pktLen = Bit::btohl(hdr + 4);
      char *pkt = (char *)malloc(8 + pktLen * sizeof(char));
      fseek(F, moreHeader, SEEK_SET);
      if (fread(pkt, 8 + pktLen, 1, F) != 1){
        free(pkt);
        FAIL_MSG("Could not read packet @ bpos %zu", moreHeader);
      }
      DTSC::Scan S(pkt + 8, pktLen);
      if (S.hasMember("moreheader") && S.getMember("moreheader").asInt()){
        moreHeader = S.getMember("moreheader").asInt();
      }else{
        moreHeader = 0;
        meta.reInit(isSingular() ? streamName : "", S);
      }

      free(pkt);
    }while (moreHeader);
    return meta;
  }

  void inputDTSC::getNext(size_t idx){
    if (!needsLock()){
      getNextFromStream(idx);
      return;
    }
    if (!currentPositions.size()){
      WARN_MSG("No seek positions set - returning empty packet.");
      thisPacket.null();
      return;
    }
    seekPos thisPos = *currentPositions.begin();
    fseek(F, thisPos.bytePos, SEEK_SET);
    if (feof(F)){
      thisPacket.null();
      Util::logExitReason(ER_CLEAN_EOF, "End of file reached");
      return;
    }
    clearerr(F);
    currentPositions.erase(currentPositions.begin());
    lastreadpos = ftell(F);
    if (fread(buffer, 4, 1, F) != 1){
      if (feof(F)){
        Util::logExitReason(ER_CLEAN_EOF, "End of file reached while seeking @ %" PRIu64, lastreadpos);
      }else{
        Util::logExitReason(ER_UNKNOWN, "Could not seek to next @ %" PRIu64, lastreadpos);
      }
      thisPacket.null();
      return;
    }
    if (memcmp(buffer, DTSC::Magic_Header, 4) == 0){
      seekNext(thisPacket.getTime(), thisPacket.getTrackId(), true);
      getNext(idx);
      return;
    }
    uint8_t version = 0;
    if (memcmp(buffer, DTSC::Magic_Packet, 4) == 0){version = 1;}
    if (memcmp(buffer, DTSC::Magic_Packet2, 4) == 0){version = 2;}
    if (version == 0){
      Util::logExitReason(ER_FORMAT_SPECIFIC, "Invalid packet header @ %#" PRIx64 " - %.4s != %.4s @ %" PRIu64, lastreadpos,
                buffer, DTSC::Magic_Packet2, lastreadpos);
      thisPacket.null();
      return;
    }
    if (fread(buffer + 4, 4, 1, F) != 1){
      Util::logExitReason(ER_FORMAT_SPECIFIC, "Could not read packet size @ %" PRIu64, lastreadpos);
      thisPacket.null();
      return;
    }
    std::string pBuf;
    uint32_t packSize = Bit::btohl(buffer + 4);
    pBuf.resize(8 + packSize);
    memcpy((char *)pBuf.data(), buffer, 8);
    if (fread((void *)(pBuf.data() + 8), packSize, 1, F) != 1){
      Util::logExitReason(ER_FORMAT_SPECIFIC, "Could not read packet @ %" PRIu64, lastreadpos);
      thisPacket.null();
      return;
    }
    thisPacket.reInit(pBuf.data(), pBuf.size());
    thisTime = thisPacket.getTime();
    thisIdx = thisPacket.getTrackId();
    seekNext(thisPos.seekTime, thisPos.trackID);
    fseek(F, thisPos.bytePos, SEEK_SET);
  }

  void inputDTSC::getNextFromStream(size_t idx){
    thisPacket.reInit(srcConn);
    while (config->is_active){
      if (thisPacket.getVersion() == DTSC::DTCM){
        // userClient.keepAlive();
        std::string cmd;
        thisPacket.getString("cmd", cmd);
        if (cmd == "reset"){
          // Read next packet
          thisPacket.reInit(srcConn);
          if (thisPacket.getVersion() != DTSC::DTSC_HEAD){
            meta.clear();
            continue;
          }
          DTSC::Meta nM("", thisPacket.getScan());
          meta.merge(nM, true, false);
          thisPacket.reInit(srcConn); // read the next packet before continuing
          continue;                   // parse the next packet before returning
        }
        if (cmd == "error"){
          thisPacket.getString("msg", cmd);
          Util::logExitReason(ER_FORMAT_SPECIFIC, "%s", cmd.c_str());
          thisPacket.null();
          return;
        }
        if (cmd == "ping"){
          thisPacket.reInit(srcConn);
          JSON::Value prep;
          prep["cmd"] = "ok";
          prep["msg"] = "Pong!";
          srcConn.SendNow("DTCM");
          char sSize[4] ={0, 0, 0, 0};
          Bit::htobl(sSize, prep.packedSize());
          srcConn.SendNow(sSize, 4);
          prep.sendTo(srcConn);
          continue;
        }
        INFO_MSG("Unhandled command: %s", cmd.c_str());
        thisPacket.reInit(srcConn);
        continue;
      }
      if (thisPacket.getVersion() == DTSC::DTSC_HEAD){
        DTSC::Meta nM("", thisPacket.getScan());
        meta.merge(nM, false, false);
        thisPacket.reInit(srcConn); // read the next packet before continuing
        continue;                   // parse the next packet before returning
      }
      thisTime = thisPacket.getTime();
      thisIdx = M.trackIDToIndex(thisPacket.getTrackId());
      if (thisPacket.getFlag("keyframe") && M.trackValid(thisIdx)){
        uint32_t shrtest_key = 0xFFFFFFFFul;
        uint32_t longest_key = 0;
        DTSC::Keys Mkeys(M.keys(thisIdx));
        uint32_t firstKey = Mkeys.getFirstValid();
        uint32_t endKey = Mkeys.getEndValid();
        uint32_t checkKey = (endKey-firstKey <= 3)?firstKey:endKey-3;
        for (uint32_t k = firstKey; k+1 < endKey; k++){
          uint64_t kDur = Mkeys.getDuration(k);
          if (!kDur){continue;}
          if (kDur > longest_key && k >= checkKey){longest_key = kDur;}
          if (kDur < shrtest_key){shrtest_key = kDur;}
        }
        if (longest_key > shrtest_key*2){
          JSON::Value prep;
          prep["cmd"] = "check_key_duration";
          prep["id"] = (uint64_t)thisPacket.getTrackId();
          prep["duration"] = longest_key;
          srcConn.SendNow("DTCM");
          char sSize[4] ={0, 0, 0, 0};
          Bit::htobl(sSize, prep.packedSize());
          srcConn.SendNow(sSize, 4);
          prep.sendTo(srcConn);
          INFO_MSG("Key duration %" PRIu32 " is quite long - confirming with upstream source", longest_key);
        }
      }
      return; // we have a packet
    }
  }

  void inputDTSC::seek(uint64_t seekTime, size_t idx){
    currentPositions.clear();
    if (idx != INVALID_TRACK_ID){
      seekNext(seekTime, idx, true);
    }else{
      std::set<size_t> tracks = M.getValidTracks();
      for (std::set<size_t>::iterator it = tracks.begin(); it != tracks.end(); it++){
        seekNext(seekTime, *it, true);
      }
    }
  }

  void inputDTSC::seekNext(uint64_t ms, size_t trackIdx, bool forceSeek){
    seekPos tmpPos;
    tmpPos.trackID = trackIdx;
    if (!forceSeek && thisPacket && ms >= thisPacket.getTime() && trackIdx >= thisPacket.getTrackId()){
      tmpPos.seekTime = thisPacket.getTime();
      tmpPos.bytePos = ftell(F);
    }else{
      tmpPos.seekTime = 0;
      tmpPos.bytePos = 0;
    }
    if (feof(F)){
      clearerr(F);
      fseek(F, 0, SEEK_SET);
      tmpPos.bytePos = 0;
      tmpPos.seekTime = 0;
    }
    DTSC::Keys keys(M.keys(trackIdx));
    uint32_t keyNum = M.getKeyNumForTime(trackIdx, ms);
    if (keys.getTime(keyNum) > tmpPos.seekTime){
      tmpPos.seekTime = keys.getTime(keyNum);
      tmpPos.bytePos = keys.getBpos(keyNum);
    }
    bool foundPacket = false;
    while (!foundPacket){
      lastreadpos = ftell(F);
      if (feof(F)){
        WARN_MSG("Reached EOF during seek to %" PRIu64 " in track %zu - aborting @ %" PRIu64, ms,
                 trackIdx, lastreadpos);
        return;
      }
      // Seek to first packet after ms.
      fseek(F, tmpPos.bytePos, SEEK_SET);
      lastreadpos = ftell(F);
      // read the header
      char header[20];
      if (fread((void *)header, 20, 1, F) != 1){
        WARN_MSG("Could not read header from file. Much sadface.");
        return;
      }
      // check if packetID matches, if not, skip size + 8 bytes.
      uint32_t packSize = Bit::btohl(header + 4);
      uint32_t packID = Bit::btohl(header + 8);
      if (memcmp(header, DTSC::Magic_Packet2, 4) != 0 || packID != trackIdx){
        if (memcmp(header, "DT", 2) != 0){
          WARN_MSG("Invalid header during seek to %" PRIu64 " in track %zu @ %" PRIu64
                   " - resetting bytePos from %" PRIu64 " to zero",
                   ms, trackIdx, lastreadpos, tmpPos.bytePos);
          tmpPos.bytePos = 0;
          continue;
        }
        tmpPos.bytePos += 8 + packSize;
        continue;
      }
      // get timestamp of packet, if too large, break, if not, skip size bytes.
      uint64_t myTime = Bit::btohll(header + 12);
      tmpPos.seekTime = myTime;
      if (myTime >= ms){
        foundPacket = true;
      }else{
        tmpPos.bytePos += 8 + packSize;
        continue;
      }
    }
    // HIGH_MSG("Seek to %u:%d resulted in %lli", trackIdx, ms, tmpPos.seekTime);
    if (tmpPos.seekTime > 0xffffffffffffff00ll){tmpPos.seekTime = 0;}
    currentPositions.insert(tmpPos);
    return;
  }
}// namespace Mist
