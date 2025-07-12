#include "input_dtsc.h"

#include <mist/bitfields.h>
#include <mist/defines.h>
#include <mist/http_parser.h>
#include <mist/stream.h>
#include <mist/util.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

namespace Mist{
  InputDTSC::InputDTSC(Util::Config *cfg) : Input(cfg){
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

    capa["optional"]["maxkeepaway"]["name"] = "Maximum live keep-away distance";
    capa["optional"]["maxkeepaway"]["help"] = "Maximum distance in milliseconds to fall behind the live point for stable playback.";
    capa["optional"]["maxkeepaway"]["option"] = "--maxkeepaway";
    capa["optional"]["maxkeepaway"]["type"] = "uint";
    capa["optional"]["maxkeepaway"]["default"] = 7500;
    /*LTS-END*/

    F = NULL;
    lockCache = false;
    lockNeeded = false;
    isSyncReceiver = false;
  }

  bool InputDTSC::needsLock(){
    if (!lockCache){
      lockNeeded =
          config->getString("input").substr(0, 7) != "dtsc://" && config->getString("input") != "-";
      lockCache = true;
    }
    return lockNeeded;
  }

  void InputDTSC::parseStreamHeader() {
    // Open metadata
    meta.reInit(streamName, false);
    if (!meta) {
      FAIL_MSG("Could not open stream metadata to merge in remote tracks; aborting!");
      Util::logExitReason(ER_INTERNAL_ERROR, "Could not open stream metadata to merge in remote tracks");
      config->is_active = false;
      return;
    }

    // Read metadata from stream
    getNextFromStream(INVALID_TRACK_ID, true);

    // Open userSelect for all received tracks
    std::set<size_t> validTracks = M.getMySourceTracks(getpid());
    userSelect.clear();
    for (std::set<size_t>::iterator it = validTracks.begin(); it != validTracks.end(); ++it) {
      userSelect[*it].reload(streamName, *it, COMM_STATUS_ACTSOURCEDNT);
    }
  }

  bool InputDTSC::openStreamSource(){
    std::string source = config->getString("input");
    if (source == "-"){
      srcConn.open(fileno(stdout), fileno(stdin));
      srcConn.Received().splitter.clear();
      return true;
    }
    HTTP::URL url(source);

    std::string host = url.host;
    uint16_t port = url.getPort();
    std::string password;
    if (url.pass.size()) {
      password = url.pass;
    } else if (url.user.size()) {
      password = url.user;
    }
    std::map<std::string, std::string> args;
    HTTP::parseVars(url.args, args);

    std::string streamName = url.path;
    Util::sanitizeName(streamName);
    if (!streamName.size()) { streamName = config->getString("streamname"); }

    srcConn.open(host, port, true, url.protocol == "dtscs");
    srcConn.Received().splitter.clear();
    if (!srcConn) { return false; }
    JSON::Value prep;
    prep["cmd"] = "play";
    prep["version"] = APPIDENT;
    prep["stream"] = streamName;
    if (args.count("sync")) { prep["sync"] = JSON::fromString(args["sync"]); }
    srcConn.SendNow("DTCM");
    char sSize[4] ={0, 0, 0, 0};
    Bit::htobl(sSize, prep.packedSize());
    srcConn.SendNow(sSize, 4);
    prep.sendTo(srcConn);
    return true;
  }

  void InputDTSC::closeStreamSource(){srcConn.close();}

  bool InputDTSC::checkArguments(){
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

  bool InputDTSC::needHeader(){
    if (!needsLock()){return false;}
    return Input::needHeader();
  }

  bool InputDTSC::readHeader(){
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

  void InputDTSC::getNext(size_t idx){
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

  void InputDTSC::getNextFromStream(size_t idx, bool returnAfterMetaReceived) {
    bool clearMeta = returnAfterMetaReceived;
    while (config->is_active && srcConn) {
      thisPacket.reInit(srcConn);

      // Handle command packets
      if (thisPacket.getVersion() == DTSC::DTCM) {
        std::string cmd;
        thisPacket.getString("cmd", cmd);
        // "reset" indicates the metadata will be replaced
        if (cmd == "reset"){
          // set clearMeta to true so that if we get no new header, we wipe the metadata entirely
          // Also ensures that the next header will wipe old tracks instead of only being an addition
          clearMeta = true;
          continue;
        }
        // "error" indicates a fatal error
        if (cmd == "error"){
          // abort the input process and set the message as the exit reason
          thisPacket.getString("msg", cmd);
          Util::logExitReason(ER_FORMAT_SPECIFIC, "%s", cmd.c_str());
          thisPacket.null();
          return;
        }
        // "ping" is a request for a "pong"
        if (cmd == "ping") {
          JSON::Value prep;
          prep["cmd"] = "ok";
          prep["msg"] = "Pong!";
          char sSize[8] = {'D', 'T', 'C', 'M', 0, 0, 0, 0};
          Bit::htobl(sSize + 4, prep.packedSize());
          srcConn.SendNow(sSize, 8);
          prep.sendTo(srcConn);
          continue;
        }
        // "sync" indicates a switch between async/sync modes
        if (cmd == "sync") {
          isSyncReceiver = thisPacket.getInt("sync");
          INFO_MSG("Switching over to receiving in %s mode", isSyncReceiver ? "sync" : "async");
          continue;
        }
        // "hi" is the server hello message
        if (cmd == "hi") {
          // let's log the version at INFO level
          thisPacket.getString("version", cmd);
          if (cmd.size()) { INFO_MSG("Connected to remote server version %s", cmd.c_str()); }
          continue;
        }
        // Ignore all other commands
        INFO_MSG("Unhandled command: %s", cmd.c_str());
        continue;
      }

      // Handle metadata update/replacement packets
      if (thisPacket.getVersion() == DTSC::DTSC_HEAD){
        DTSC::Meta nM("", thisPacket.getScan());
        meta.merge(nM, clearMeta, false);
        if (clearMeta) { meta.setBootMsOffset(nM.getBootMsOffset()); }
        clearMeta = false;
        if (returnAfterMetaReceived) { return; }
        continue;
      }

      // Track data packet
      if (thisPacket.getVersion() == DTSC::DTSC_V1 || thisPacket.getVersion() == DTSC::DTSC_V2) {
        thisTime = thisPacket.getTime();
        thisIdx = M.trackIDToIndex(thisPacket.getTrackId());
        if (thisPacket.getFlag("keyframe") && M.trackValid(thisIdx)) {
          uint32_t shrtest_key = 0xFFFFFFFFul;
          uint32_t longest_key = 0;
          DTSC::Keys Mkeys(M.keys(thisIdx));
          uint32_t firstKey = Mkeys.getFirstValid();
          uint32_t endKey = Mkeys.getEndValid();
          uint32_t checkKey = (endKey - firstKey <= 3) ? firstKey : endKey - 3;
          for (uint32_t k = firstKey; k + 1 < endKey; k++) {
            uint64_t kDur = Mkeys.getDuration(k);
            if (!kDur) { continue; }
            if (kDur > longest_key && k >= checkKey) { longest_key = kDur; }
            if (kDur < shrtest_key) { shrtest_key = kDur; }
          }
          if (longest_key > shrtest_key * 2) {
            JSON::Value prep;
            prep["cmd"] = "check_key_duration";
            prep["id"] = (uint64_t)thisPacket.getTrackId();
            prep["duration"] = longest_key;
            srcConn.SendNow("DTCM");
            char sSize[4] = {0, 0, 0, 0};
            Bit::htobl(sSize, prep.packedSize());
            srcConn.SendNow(sSize, 4);
            prep.sendTo(srcConn);
            INFO_MSG("Key duration %" PRIu32 " is quite long - confirming with upstream source", longest_key);
          }
        }

        // If we're receiving packets in sync, we now know until 1ms ago all tracks are up-to-date
        if (isSyncReceiver && thisTime) {
          std::map<size_t, Comms::Users>::iterator uIt;
          for (uIt = userSelect.begin(); uIt != userSelect.end(); ++uIt) {
            if (uIt->first == thisIdx) { continue; }
            if (M.getNowms(uIt->first) < thisTime - 1) { meta.setNowms(uIt->first, thisTime - 1); }
          }
        }

        // If we're not trying to get metadata, break. Otherwise keep going to try and get metadata.
        if (!returnAfterMetaReceived) { break; }
      }

      WARN_MSG("Unhandled DTSC packet type encountered, ignoring...");
    }
    if (clearMeta) { meta.clear(); }
  }

  void InputDTSC::seek(uint64_t seekTime, size_t idx){
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

  void InputDTSC::seekNext(uint64_t ms, size_t trackIdx, bool forceSeek){
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
