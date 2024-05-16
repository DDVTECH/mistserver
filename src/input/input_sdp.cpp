#include "input_sdp.h"

// Will point to current InputSDP obj after constructor is called
Mist::InputSDP *classPointerSDP = 0;
size_t bytesUp = 0;
// CB used to receive DTSC packets back from RTP sorter
void incomingPacketSDP(const DTSC::Packet &pkt){
  classPointerSDP->incoming(pkt);
}
void insertRTP(const uint64_t track, const RTP::Packet &p){
  classPointerSDP->incomingRTP(track, p);
}

/// Function used to send RTCP packets over UDP
///\param socket A UDP Connection pointer, sent as a void*, to keep portability.
///\param data The RTP Packet that needs to be sent
///\param len The size of data
///\param channel Not used here, but is kept for compatibility with sendTCP
void sendUDPSDP(void *socket, const char *data, size_t len, uint8_t channel){
  ((Socket::UDPConnection *)socket)->SendNow(data, len);
  bytesUp += len;
}

namespace Mist{
  void InputSDP::incomingRTP(const uint64_t track, const RTP::Packet &p){
    sdpState.handleIncomingRTP(track, p);
  }

  InputSDP::InputSDP(Util::Config *cfg) : Input(cfg){
    setPacketOffset = false;
    packetOffset = 0;
    sdpState.myMeta = &meta;
    sdpState.incomingPacketCallback = incomingPacketSDP;
    classPointerSDP = this;
    standAlone = false;
    hasBork = false;
    bytesRead = 0;
    count = 0;
    capa["name"] = "SDP";
    capa["desc"] = "This input allows pulling of RTP packets using a provided SDP file";
    capa["source_match"].append("/*.sdp");
    capa["always_match"].append("/*.sdp");
    capa["priority"] = 9;
    capa["codecs"]["video"].append("H264");
    capa["codecs"]["video"].append("HEVC");
    capa["codecs"]["video"].append("MPEG2");
    capa["codecs"]["video"].append("VP8");
    capa["codecs"]["video"].append("VP9");
    capa["codecs"]["audio"].append("AAC");
    capa["codecs"]["audio"].append("MP3");
    capa["codecs"]["audio"].append("AC3");
    capa["codecs"]["audio"].append("ALAW");
    capa["codecs"]["audio"].append("ULAW");
    capa["codecs"]["audio"].append("PCM");
    capa["codecs"]["audio"].append("opus");
    capa["codecs"]["audio"].append("MP2");

    JSON::Value option;
    option["arg"] = "integer";
    option["long"] = "buffer";
    option["short"] = "b";
    option["help"] = "DVR buffer time in ms";
    option["value"].append(50000);
    config->addOption("bufferTime", option);
    capa["optional"]["DVR"]["name"] = "Buffer time (ms)";
    capa["optional"]["DVR"]["help"] = "The target available buffer time for this live stream, in "
                                      "milliseconds. This is the time available to seek around in, "
                                      "and will automatically be extended to fit whole keyframes "
                                      "as well as the minimum duration needed for stable playback.";
    capa["optional"]["DVR"]["option"] = "--buffer";
    capa["optional"]["DVR"]["type"] = "uint";
    capa["optional"]["DVR"]["default"] = 50000;
    option.null();
  }

  /// Checks whether the input string ends with .sdp
  bool InputSDP::checkArguments(){
    const std::string &inpt = config->getString("input");
    if (inpt.substr(inpt.length() - 4) != ".sdp"){
      Util::logExitReason(ER_FORMAT_SPECIFIC, "Expected a SDP file but received: '%s'", inpt.c_str());
      return false;
    }
    return true;
  }

  /// Lets URIreader open the SDP file at the requested given location
  bool InputSDP::openStreamSource(){
    const std::string &inpt = config->getString("input");
    reader.open(inpt);
    // Will return false if it cant open file or it is EOF
    if (!reader){
      Util::logExitReason(ER_READ_START_FAILURE, "Opening input '%s' failed", config->getString("input").c_str());
      return false;
    }
    return true;
  }

  /// Gets and parses the SDP file
  void InputSDP::parseStreamHeader(){
    if (!reader){
      Util::logExitReason(ER_READ_START_FAILURE, "Connection lost with input. Could not get stream description!");
      return;
    }

    reader.readAll(buffer, bytesRead);
    HIGH_MSG("Downloaded SDP file (%zu B)", bytesRead);

    // Save old buffer in order to identify changes
    oldBuffer = strdup(buffer);

    sdpState.reinitSDP();
    sdpState.parseSDP(buffer);

    INFO_MSG("Stream contains %zu tracks", M.getValidTracks().size());

    if (reader){
      reader.close();
    }
  }

  void InputSDP::closeStreamSource(){
    if (reader){
      reader.close();
    }
    return;
  }

  /// Compare two c strings char by char
  /// \return false if not equals (or different in size), else true
  bool InputSDP::compareStrings(char* str1, char* str2){
    size_t strlen1 = strlen(str1);
    size_t strlen2 = strlen(str2);

    if (strlen1 != strlen2){
      return false;
    }

    for (int k = 0; k < strlen1; k++){
      if(str1[k] != str2[k]){
        return false;
      }
    }
    return true;
  }

  // Checks if there are updates available to the SDP file
  // and updates the SDP file accordingly
  bool InputSDP::updateSDP(){
    // Reset error flag
    hasBork = false;
    // Reopen the file if necessary
    if (!reader){
      const std::string &inpt = config->getString("input");
      reader.open(inpt);
    }
    // If the file has dissappeared the stream must have stopped
    if (!reader){     
      WARN_MSG("SDP file no longer available. Cannot update SDP info.");
      return false;
    }
    // Re-read SDP file
    reader.readAll(buffer, bytesRead);
    // Re-init SPD state iff contents have changed
    INFO_MSG("Downloaded SDP file (%zu B)", bytesRead);
    if (bytesRead != 0){
      if (!compareStrings(oldBuffer, buffer)){
        INFO_MSG("SDP contents have changed. Reparsing SDP file");
        // Save old buffer in order to identify changes
        oldBuffer = strdup(buffer);

        sdpState.reinitSDP();
        sdpState.parseSDP(buffer);

        INFO_MSG("Stream contains %zu tracks", M.getValidTracks().size());
      }
      else{
        FAIL_MSG("Unable to parse stream data for current SDP file. Quitting...");
        return false;
      }
    }
    else{
      FAIL_MSG("SDP file no longer available. Quitting...");
      return false;
    }


    // Close the file so that we can reopen it on err
    if (reader){
      reader.close();
    }

    // Notify Meta of changes to tracks
    meta.refresh();

    return true;
  }

  // Updates stats and quits if parsePacket returns false
  void InputSDP::streamMainLoop(){
    Comms::Connections statComm;
    uint64_t startTime = Util::epoch();
    uint64_t lastSecs = 0;
    // Get RTP packets from UDP socket and stop if this fails
    while (keepAlive() && parsePacket()){
      uint64_t currSecs = Util::bootSecs();
      if (lastSecs != currSecs){
        lastSecs = currSecs;
        // Connect to stats for INPUT detection
        statComm.reload(streamName, getConnectedBinHost(), JSON::Value(getpid()).asString(), "INPUT:" + capa["name"].asStringRef(), "");
        if (statComm){
          if (statComm.getStatus() == COMM_STATUS_REQDISCONNECT){
            config->is_active = false;
            Util::logExitReason(ER_CLEAN_CONTROLLER_REQ, "received shutdown request from controller");
            return;
          }
          uint64_t now = Util::bootSecs();
          statComm.setNow(now);
          statComm.setStream(streamName);
          statComm.setConnector("INPUT:" + capa["name"].asStringRef());
          statComm.setDown(bytesRead);
          statComm.setUp(bytesUp);
          statComm.setTime(now - startTime);
          statComm.setLastSecond(0);
        }
      }
      // If the error flag is raised or we are lacking data, try to recover
      if (count > 5 || hasBork) {
        if (!updateSDP()){
          return;
        }
      }
    }
  }

  /// \brief Passes incoming RTP packets to sorter
  /// \return False if we cannot recover and should quit. Else returns True
  bool InputSDP::parsePacket(){
    uint32_t waitTime = 200;
    bool receivedPacket = false;
    // How often to send RTCP receiver requests in seconds
    const uint32_t rtcpInterval = 7;
    for (std::map<uint64_t, SDP::Track>::iterator it = sdpState.tracks.begin();
         it != sdpState.tracks.end(); ++it){

      // Get RTP socket for selected track
      Socket::UDPConnection &s = it->second.data;
      it->second.sorter.setCallback(it->first, insertRTP);

      // Get RTP packets
      while (s.Receive()){
        count = 0;
        receivedPacket = true;
        bytesRead += (s.data.size());
        RTP::Packet pack(s.data, s.data.size());

        // Init local and remote SSRC if it was not set
        if (!it->second.theirSSRC){
          it->second.theirSSRC = pack.getSSRC();
        }
        if (!currentSSRC[it->first]){
          currentSSRC[it->first] = pack.getSSRC();
        }
        // If we still have some packets from the old track in the socket buffer, skip it
        if (oldSSRC[it->first] == pack.getSSRC()){
          continue;
        }
        // Verify if the SSRC has changed: indicating that a new video is being sent
        // Either recover, reload or quit at this point
        if (currentSSRC[it->first] != pack.getSSRC()){
          WARN_MSG("Sorter for the current track has encountered an error: current SSRC has changed from %u to %u. Trying to recover...", currentSSRC[it->first], pack.getSSRC());
          oldSSRC[it->first] = currentSSRC[it->first];
          hasBork = true;
          return true;
        }

        // Let sorter handle RTP specifics
        it->second.sorter.addPacket(pack);
        DONTEVEN_MSG("Added %zu B RTP packet to buffer with start time %u and SSRC %u: %s", bytesRead, pack.getTimeStamp(), pack.getSSRC(), pack.toString().c_str());
      }
      // Send RTCP packet back to host
      if (Util::bootSecs() > it->second.rtcpSent + rtcpInterval){
        it->second.rtcpSent = Util::bootSecs();
        it->second.pack.sendRTCP_RR(it->second, sendUDPSDP);
      }
    }
    if (!receivedPacket){
      Util::sleep(waitTime);
      count++;
    }
    return true;
  }

  // Buffers incoming DTSC packets (from SDP tracks -> RTP sorter)
  void InputSDP::incoming(const DTSC::Packet &pkt){
    if (!M.getBootMsOffset()){
      meta.setBootMsOffset(Util::bootMS() - pkt.getTime());
      packetOffset = 0;
      setPacketOffset = true;
    }else if (!setPacketOffset){
      packetOffset = (Util::bootMS() - pkt.getTime()) - M.getBootMsOffset();
      setPacketOffset = true;
    }
    static DTSC::Packet newPkt;
    char *pktData;
    size_t pktDataLen;
    pkt.getString("data", pktData, pktDataLen);
    size_t idx = M.trackIDToIndex(pkt.getTrackId(), getpid());

    HIGH_MSG("Buffering new pkt for track %zu->%zu at offset %" PRId64 " and time %" PRIu64, pkt.getTrackId(), idx, packetOffset, pkt.getTime());

    if (idx == INVALID_TRACK_ID){
      INFO_MSG("Invalid index for track number %zu", pkt.getTrackId());
    }else{
      if (!userSelect.count(idx)){
        WARN_MSG("Reloading track %zu, index %zu", pkt.getTrackId(), idx);
        userSelect[idx].reload(streamName, idx, COMM_STATUS_ACTIVE | COMM_STATUS_SOURCE | COMM_STATUS_DONOTTRACK);
      }
      if (userSelect[idx].getStatus() == COMM_STATUS_REQDISCONNECT){
        Util::logExitReason(ER_CLEAN_LIVE_BUFFER_REQ, "buffer requested shutdown");
      }
    }

    bufferLivePacket(pkt.getTime() + packetOffset, pkt.getInt("offset"), idx, pktData,
                     pktDataLen, 0, pkt.getFlag("keyframe"));
  }
}// namespace Mist
