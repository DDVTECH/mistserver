#include "sdp.h"
#include "adts.h"
#include "defines.h"
#include "encode.h"
#include "h264.h"
#include "h265.h"
#include "http_parser.h"
#include "util.h"
#include "url.h"

namespace SDP{

  static State *snglState = 0;
  static void snglStateInitCallback(const uint64_t track, const std::string &initData){
    snglState->updateInit(track, initData);
  }


  Track::Track(){
    rtcpSent = 0;
    channel = -1;
    firstTime = 0;
    packCount = 0;
    fpsTime = 0;
    fpsMeta = 0;
    fps = 0;
    mySSRC = rand();
    portA = portB = 0;
    cPortA = cPortB = 0;
  }

  /// Extracts a particular parameter from the fmtp string. fmtp member must be set before calling.
  std::string Track::getParamString(const std::string &param) const{
    if (!fmtp.size()){return "";}
    size_t pos = fmtp.find(param);
    if (pos == std::string::npos){return "";}
    pos += param.size() + 1;
    size_t ePos = fmtp.find_first_of(" ;", pos);
    return fmtp.substr(pos, ePos - pos);
  }

  /// Extracts a particular parameter from the fmtp string as an integer. fmtp member must be set
  /// before calling.
  uint64_t Track::getParamInt(const std::string &param) const{
    return atoll(getParamString(param).c_str());
  }

  /// Gets the SDP contents for sending out a particular given DTSC::Track.
  std::string mediaDescription(const DTSC::Track &trk){
    std::stringstream mediaDesc;
    if (trk.codec == "H264"){
      MP4::AVCC avccbox;
      avccbox.setPayload(trk.init);
      mediaDesc << "m=video 0 RTP/AVP 97\r\n"
                   "a=rtpmap:97 H264/90000\r\n"
                   "a=cliprect:0,0,"
                << trk.height << "," << trk.width
                << "\r\n"
                   "a=framesize:97 "
                << trk.width << '-' << trk.height
                << "\r\n"
                   "a=fmtp:97 packetization-mode=1;profile-level-id="
                << std::hex << std::setw(2) << std::setfill('0') << (int)trk.init.data()[1]
                << std::dec << "E0" << std::hex << std::setw(2) << std::setfill('0')
                << (int)trk.init.data()[3] << std::dec << ";"
                << "sprop-parameter-sets=";
      size_t count = avccbox.getSPSCount();
      for (size_t i = 0; i < count; ++i){
        mediaDesc << (i ? "," : "")
                  << Encodings::Base64::encode(
                         std::string(avccbox.getSPS(i), avccbox.getSPSLen(i)));
      }
      mediaDesc << ",";
      count = avccbox.getPPSCount();
      for (size_t i = 0; i < count; ++i){
        mediaDesc << (i ? "," : "")
                  << Encodings::Base64::encode(
                         std::string(avccbox.getPPS(i), avccbox.getPPSLen(i)));
      }
      mediaDesc << "\r\n"
                << "a=framerate:" << ((double)trk.fpks) / 1000.0
                << "\r\n"
                   "a=control:track"
                << trk.trackID << "\r\n";
    }else if (trk.codec == "HEVC"){
      h265::initData iData(trk.init);
      mediaDesc << "m=video 0 RTP/AVP 104\r\n"
                   "a=rtpmap:104 H265/90000\r\n"
                   "a=cliprect:0,0,"
                << trk.height << "," << trk.width
                << "\r\n"
                   "a=framesize:104 "
                << trk.width << '-' << trk.height << "\r\n"
                << "a=fmtp:104 sprop-vps=";
      const std::set<std::string> &vps = iData.getVPS();
      if (vps.size()){
        for (std::set<std::string>::iterator it = vps.begin(); it != vps.end(); it++){
          if (it != vps.begin()){mediaDesc << ",";}
          mediaDesc << Encodings::Base64::encode(*it);
        }
      }
      mediaDesc << "; sprop-sps=";
      const std::set<std::string> &sps = iData.getSPS();
      if (sps.size()){
        for (std::set<std::string>::iterator it = sps.begin(); it != sps.end(); it++){
          if (it != sps.begin()){mediaDesc << ",";}
          mediaDesc << Encodings::Base64::encode(*it);
        }
      }
      mediaDesc << "; sprop-pps=";
      const std::set<std::string> &pps = iData.getPPS();
      if (pps.size()){
        for (std::set<std::string>::iterator it = pps.begin(); it != pps.end(); it++){
          if (it != pps.begin()){mediaDesc << ",";}
          mediaDesc << Encodings::Base64::encode(*it);
        }
      }
      mediaDesc << "\r\na=framerate:" << ((double)trk.fpks) / 1000.0
                << "\r\n"
                   "a=control:track"
                << trk.trackID << "\r\n";
    }else if (trk.codec == "MPEG2"){
      mediaDesc << "m=video 0 RTP/AVP 32\r\n"
                   "a=cliprect:0,0,"
                << trk.height << "," << trk.width
                << "\r\n"
                   "a=framesize:32 "
                << trk.width << '-' << trk.height << "\r\n"
                << "a=framerate:" << ((double)trk.fpks) / 1000.0 << "\r\n"
                << "a=control:track" << trk.trackID << "\r\n";
    }else if (trk.codec == "AAC"){
      mediaDesc << "m=audio 0 RTP/AVP 96"
                << "\r\n"
                   "a=rtpmap:96 mpeg4-generic/"
                << trk.rate << "/" << trk.channels
                << "\r\n"
                   "a=fmtp:96 streamtype=5; profile-level-id=15; config=";
      for (unsigned int i = 0; i < trk.init.size(); i++){
        mediaDesc << std::hex << std::setw(2) << std::setfill('0') << (int)trk.init[i] << std::dec;
      }
      // these values are described in RFC 3640
      mediaDesc << "; mode=AAC-hbr; SizeLength=13; IndexLength=3; IndexDeltaLength=3;\r\n"
                   "a=control:track"
                << trk.trackID << "\r\n";
    }else if (trk.codec == "MP3" || trk.codec == "MP2"){
      mediaDesc << "m=" << trk.type << " 0 RTP/AVP 14"
                << "\r\n"
                   "a=rtpmap:14 MPA/90000/"
                << trk.channels
                << "\r\n"
                   "a=control:track"
                << trk.trackID << "\r\n";
    }else if (trk.codec == "AC3"){
      mediaDesc << "m=audio 0 RTP/AVP 100"
                << "\r\n"
                   "a=rtpmap:100 AC3/"
                << trk.rate << "/" << trk.channels
                << "\r\n"
                   "a=control:track"
                << trk.trackID << "\r\n";
    }else if (trk.codec == "ALAW"){
      if (trk.channels == 1 && trk.rate == 8000){
        mediaDesc << "m=audio 0 RTP/AVP 8"
                  << "\r\n";
      }else{
        mediaDesc << "m=audio 0 RTP/AVP 101"
                  << "\r\n";
        mediaDesc << "a=rtpmap:101 PCMA/" << trk.rate << "/" << trk.channels << "\r\n";
      }
      mediaDesc << "a=control:track" << trk.trackID << "\r\n";
    }else if (trk.codec == "ULAW"){
      if (trk.channels == 1 && trk.rate == 8000){
        mediaDesc << "m=audio 0 RTP/AVP 0"
                  << "\r\n";
      }else{
        mediaDesc << "m=audio 0 RTP/AVP 104"
                  << "\r\n";
        mediaDesc << "a=rtpmap:104 PCMU/" << trk.rate << "/" << trk.channels << "\r\n";
      }
      mediaDesc << "a=control:track" << trk.trackID << "\r\n";
    }else if (trk.codec == "PCM"){
      if (trk.size == 16 && trk.channels == 2 && trk.rate == 44100){
        mediaDesc << "m=audio 0 RTP/AVP 10"
                  << "\r\n";
      }else if (trk.size == 16 && trk.channels == 1 && trk.rate == 44100){
        mediaDesc << "m=audio 0 RTP/AVP 11"
                  << "\r\n";
      }else{
        mediaDesc << "m=audio 0 RTP/AVP 103"
                  << "\r\n";
        mediaDesc << "a=rtpmap:103 L" << trk.size << "/" << trk.rate << "/" << trk.channels
                  << "\r\n";
      }
      mediaDesc << "a=control:track" << trk.trackID << "\r\n";
    }else if (trk.codec == "opus"){
      mediaDesc << "m=audio 0 RTP/AVP 102"
                << "\r\n"
                   "a=rtpmap:102 opus/"
                << trk.rate << "/" << trk.channels
                << "\r\n"
                   "a=control:track"
                << trk.trackID << "\r\n";
    }
    return mediaDesc.str();
  }

  /// Generates a transport string suitable for in a SETUP request.
  /// By default generates a TCP mode string.
  /// Expects parseTransport to be called with the response from the server.
  std::string Track::generateTransport(uint32_t trackNo, const std::string &dest, bool TCPmode){
    if (TCPmode){
      // We simply request interleaved delivery over a trackNo-based identifier.
      // No need to set any internal state, parseTransport will handle it all.
      std::stringstream tStr;
      tStr << "RTP/AVP/TCP;unicast;interleaved=" << ((trackNo - 1) * 2) << "-"
           << ((trackNo - 1) * 2 + 1);
      return tStr.str();
    }else{
      // A little more tricky: we need to find free ports and remember them.
      data.SetDestination(dest, 1337);
      rtcp.SetDestination(dest, 1337);
      portA = portB = 0;
      int retries = 0;
      while (portB != portA + 1 && retries < 10){
        portA = data.bind(0);
        portB = rtcp.bind(portA + 1);
      }
      std::stringstream tStr;
      tStr << "RTP/AVP/UDP;unicast;client_port=" << portA << "-" << portB;
      return tStr.str();
    }
  }

  /// Sets the TCP/UDP connection details from a given transport string.
  /// Sets the transportString member to the current transport string on success.
  /// \param host The host connecting to us.
  /// \source The source identifier.
  /// \return True if successful, false otherwise.
  bool Track::parseTransport(const std::string &transport, const std::string &host,
                             const std::string &source, const DTSC::Track &trk){
    if (trk.codec == "H264"){
      pack = RTP::Packet(97, 1, 0, mySSRC);
    }else if (trk.codec == "HEVC"){
      pack = RTP::Packet(104, 1, 0, mySSRC);
    }else if (trk.codec == "MPEG2"){
      pack = RTP::Packet(32, 1, 0, mySSRC);
    }else if (trk.codec == "AAC"){
      pack = RTP::Packet(96, 1, 0, mySSRC);
    }else if (trk.codec == "AC3"){
      pack = RTP::Packet(100, 1, 0, mySSRC);
    }else if (trk.codec == "MP3" || trk.codec == "MP2"){
      pack = RTP::Packet(14, 1, 0, mySSRC);
    }else if (trk.codec == "ALAW"){
      if (trk.channels == 1 && trk.rate == 8000){
        pack = RTP::Packet(8, 1, 0, mySSRC);
      }else{
        pack = RTP::Packet(101, 1, 0, mySSRC);
      }
    }else if (trk.codec == "ULAW"){
      if (trk.channels == 1 && trk.rate == 8000){
        pack = RTP::Packet(0, 1, 0, mySSRC);
      }else{
        pack = RTP::Packet(104, 1, 0, mySSRC);
      }
    }else if (trk.codec == "PCM"){
      if (trk.size == 16 && trk.channels == 2 && trk.rate == 44100){
        pack = RTP::Packet(10, 1, 0, mySSRC);
      }else if (trk.size == 16 && trk.channels == 1 && trk.rate == 44100){
        pack = RTP::Packet(11, 1, 0, mySSRC);
      }else{
        pack = RTP::Packet(103, 1, 0, mySSRC);
      }
    }else if (trk.codec == "opus"){
      pack = RTP::Packet(102, 1, 0, mySSRC);
    }else{
      ERROR_MSG("Unsupported codec %s for RTSP on track %u", trk.codec.c_str(), trk.trackID);
      return false;
    }
    if (transport.find("TCP") != std::string::npos){
      std::string chanE =
          transport.substr(transport.find("interleaved=") + 12,
                           (transport.size() - transport.rfind('-') - 1)); // extract channel ID
      channel = atol(chanE.c_str());
      rtcpSent = 0;
      transportString = transport;
    }else{
      channel = -1;
      uint32_t sPortA = 0, sPortB = 0;
      cPortA = cPortB = 0;
      size_t sPort_loc = transport.rfind("server_port=") + 12;
      if (sPort_loc != std::string::npos){
        sPortA =
            atol(transport.substr(sPort_loc, transport.find('-', sPort_loc) - sPort_loc).c_str());
        sPortB = atol(transport.substr(transport.find('-', sPort_loc) + 1).c_str());
      }
      size_t port_loc = transport.rfind("client_port=") + 12;
      if (port_loc != std::string::npos){
        cPortA = atol(transport.substr(port_loc, transport.find('-', port_loc) - port_loc).c_str());
        cPortB = atol(transport.substr(transport.find('-', port_loc) + 1).c_str());
      }
      INFO_MSG("UDP ports: server %d/%d, client %d/%d", sPortA, sPortB, cPortA, cPortB);
      int sendbuff = 4 * 1024 * 1024;
      if (!sPortA || !sPortB){
        // Server mode - find server ports
        data.SetDestination(host, cPortA);
        setsockopt(data.getSock(), SOL_SOCKET, SO_SNDBUF, &sendbuff, sizeof(sendbuff));
        rtcp.SetDestination(host, cPortB);
        setsockopt(rtcp.getSock(), SOL_SOCKET, SO_SNDBUF, &sendbuff, sizeof(sendbuff));
        portA = data.bind(0);
        portB = rtcp.bind(0);
        std::stringstream tStr;
        tStr << "RTP/AVP/UDP;unicast;client_port=" << cPortA << '-' << cPortB << ";";
        if (source.size()){tStr << "source=" << source << ";";}
        tStr << "server_port=" << portA << "-" << portB << ";ssrc=" << std::hex << mySSRC
             << std::dec;
        transportString = tStr.str();
      }else{
        // Client mode - check ports and/or obey given ports if possible
        data.SetDestination(host, sPortA);
        setsockopt(data.getSock(), SOL_SOCKET, SO_SNDBUF, &sendbuff, sizeof(sendbuff));
        rtcp.SetDestination(host, sPortB);
        setsockopt(rtcp.getSock(), SOL_SOCKET, SO_SNDBUF, &sendbuff, sizeof(sendbuff));
        if (portA != cPortA){
          portA = data.bind(cPortA);
          if (portA != cPortA){
            FAIL_MSG("Server requested port %d, which we couldn't bind", cPortA);
            return false;
          }
        }
        if (portB != cPortB){
          portB = data.bind(cPortB);
          if (portB != cPortB){
            FAIL_MSG("Server requested port %d, which we couldn't bind", cPortB);
            return false;
          }
        }
        transportString = transport;
      }
      INFO_MSG("Transport string: %s", transportString.c_str());
    }
    return true;
  }

  /// Gets the rtpInfo for a given DTSC::Track, source identifier and timestamp (in millis).
  std::string Track::rtpInfo(const DTSC::Track &trk, const std::string &source,
                             uint64_t currentTime){
    std::stringstream rInfo;
    rInfo << "url=" << source << "/track" << trk.trackID
          << ";"; // get the current url, not localhost
    rInfo << "sequence=" << pack.getSequence() << ";rtptime=" << currentTime * getMultiplier(trk);
    return rInfo.str();
  }

  State::State(){
    incomingPacketCallback = 0;
    myMeta = 0;
    snglState = this;
  }

  void State::parseSDP(const std::string &sdp){
    DONTEVEN_MSG("Parsing %llu-byte SDP", sdp.size());
    std::stringstream ss(sdp);
    std::string to;
    uint64_t trackNo = 0;
    bool nope = true; // true if we have no valid track to fill
    DTSC::Track *thisTrack = 0;
    while (std::getline(ss, to, '\n')){
      if (!to.empty() && *to.rbegin() == '\r'){to.erase(to.size() - 1, 1);}
      if (to.empty()){continue;}
      DONTEVEN_MSG("Parsing SDP line: %s", to.c_str());

      // All tracks start with a media line
      if (to.substr(0, 2) == "m="){
        nope = true;
        ++trackNo;
        thisTrack = &(myMeta->tracks[trackNo]);
        std::stringstream words(to.substr(2));
        std::string item;
        if (getline(words, item, ' ') && (item == "audio" || item == "video")){
          thisTrack->type = item;
          thisTrack->trackID = trackNo;
        }else{
          WARN_MSG("Media type not supported: %s", item.c_str());
          myMeta->tracks.erase(trackNo);
          tracks.erase(trackNo);
          continue;
        }
        getline(words, item, ' ');
        if (!getline(words, item, ' ') || item.substr(0, 7) != "RTP/AVP"){
          WARN_MSG("Media transport not supported: %s", item.c_str());
          myMeta->tracks.erase(trackNo);
          tracks.erase(trackNo);
          continue;
        }
        if (getline(words, item, ' ')){
          uint64_t avp_type = JSON::Value(item).asInt();
          switch (avp_type){
          case 0: // PCM Mu-law
            INFO_MSG("PCM Mu-law payload type");
            nope = false;
            thisTrack->codec = "ULAW";
            thisTrack->rate = 8000;
            thisTrack->channels = 1;
            break;
          case 8: // PCM A-law
            INFO_MSG("PCM A-law payload type");
            nope = false;
            thisTrack->codec = "ALAW";
            thisTrack->rate = 8000;
            thisTrack->channels = 1;
            break;
          case 10: // PCM Stereo, 44.1kHz
            INFO_MSG("Linear PCM stereo 44.1kHz payload type");
            nope = false;
            thisTrack->codec = "PCM";
            thisTrack->size = 16;
            thisTrack->rate = 44100;
            thisTrack->channels = 2;
            break;
          case 11: // PCM Mono, 44.1kHz
            INFO_MSG("Linear PCM mono 44.1kHz payload type");
            nope = false;
            thisTrack->codec = "PCM";
            thisTrack->rate = 44100;
            thisTrack->size = 16;
            thisTrack->channels = 1;
            break;
          case 14: // MPA
            INFO_MSG("MPA payload type");
            nope = false;
            thisTrack->codec = "MP3";
            thisTrack->rate = 0;
            thisTrack->size = 0;
            thisTrack->channels = 0;
            break;
          case 32: // MPV
            INFO_MSG("MPV payload type");
            nope = false;
            thisTrack->codec = "MPEG2";
            break;
          default:
            // dynamic type
            if (avp_type >= 96 && avp_type <= 127){
              HIGH_MSG("Dynamic payload type (%llu) detected", avp_type);
              nope = false;
              continue;
            }else{
              FAIL_MSG("Payload type %llu not supported!", avp_type);
              myMeta->tracks.erase(trackNo);
              tracks.erase(trackNo);
              continue;
            }
          }
        }
        tConv[trackNo].setProperties(*thisTrack);
        HIGH_MSG("Incoming track %s", thisTrack->getIdentifier().c_str());
        continue;
      }

      if (nope){continue;}// ignore lines if we have no valid track
      // RTP mapping
      if (to.substr(0, 8) == "a=rtpmap"){
        std::string mediaType = to.substr(to.find(' ', 8) + 1);
        std::string trCodec = mediaType.substr(0, mediaType.find('/'));
        // convert to fullcaps
        for (unsigned int i = 0; i < trCodec.size(); ++i){
          if (trCodec[i] <= 122 && trCodec[i] >= 97){trCodec[i] -= 32;}
        }
        if (thisTrack->type == "audio"){
          std::string extraInfo = mediaType.substr(mediaType.find('/') + 1);
          if (extraInfo.find('/') != std::string::npos){
            size_t lastSlash = extraInfo.find('/');
            thisTrack->rate = atoll(extraInfo.substr(0, lastSlash).c_str());
            thisTrack->channels = atoll(extraInfo.substr(lastSlash + 1).c_str());
          }else{
            thisTrack->rate = atoll(extraInfo.c_str());
            thisTrack->channels = 1;
          }
        }
        if (trCodec == "H264"){
          thisTrack->codec = "H264";
          thisTrack->rate = 90000;
        }
        if (trCodec == "H265"){
          thisTrack->codec = "HEVC";
          thisTrack->rate = 90000;
        }
        if (trCodec == "OPUS"){
          thisTrack->codec = "opus";
          thisTrack->init = std::string("OpusHead\001\002\170\000\200\273\000\000\000\000\000", 19);
        }
        if (trCodec == "PCMA"){thisTrack->codec = "ALAW";}
        if (trCodec == "PCMU"){thisTrack->codec = "ULAW";}
        if (trCodec == "L8"){
          thisTrack->codec = "PCM";
          thisTrack->size = 8;
        }
        if (trCodec == "L16"){
          thisTrack->codec = "PCM";
          thisTrack->size = 16;
        }
        if (trCodec == "L20"){
          thisTrack->codec = "PCM";
          thisTrack->size = 20;
        }
        if (trCodec == "L24"|| trCodec == "PCM"){
          thisTrack->codec = "PCM";
          thisTrack->size = 24;
        }
        if (trCodec == "MPEG4-GENERIC"){thisTrack->codec = "AAC";}
        if (!thisTrack->codec.size()){
          ERROR_MSG("Unsupported RTP mapping: %s", mediaType.c_str());
        }else{
          tConv[trackNo].setProperties(*thisTrack);
          HIGH_MSG("Incoming track %s", thisTrack->getIdentifier().c_str());
        }
        continue;
      }
      if (to.substr(0, 10) == "a=control:"){
        tracks[trackNo].control = to.substr(10);
        continue;
      }
      if (to.substr(0, 12) == "a=framerate:"){
        if (!thisTrack->rate){thisTrack->rate = atof(to.c_str() + 12) * 1000;}
        continue;
      }
      if (to.substr(0, 12) == "a=framesize:"){
        // Ignored for now.
        /// \TODO Maybe implement?
        continue;
      }
      if (to.substr(0, 11) == "a=cliprect:"){
        // Ignored for now.
        /// \TODO Maybe implement?
        continue;
      }
      if (to.substr(0, 7) == "a=fmtp:"){
        tracks[trackNo].fmtp = to.substr(7);
        if (thisTrack->codec == "AAC"){
          if (tracks[trackNo].getParamString("mode") != "AAC-hbr"){
            // a=fmtp:97
            // profile-level-id=1;mode=AAC-hbr;sizelength=13;indexlength=3;indexdeltalength=3;
            // config=120856E500
            FAIL_MSG("AAC transport mode not supported: %s",
                     tracks[trackNo].getParamString("mode").c_str());
            nope = true;
            myMeta->tracks.erase(trackNo);
            tracks.erase(trackNo);
            continue;
          }
          thisTrack->init = Encodings::Hex::decode(tracks[trackNo].getParamString("config"));
          // myMeta.tracks[trackNo].rate = aac::AudSpecConf::rate(myMeta.tracks[trackNo].init);
        }
        if (thisTrack->codec == "H264"){
          // a=fmtp:96 packetization-mode=1;
          // sprop-parameter-sets=Z0LAHtkA2D3m//AUABqxAAADAAEAAAMAMg8WLkg=,aMuDyyA=;
          // profile-level-id=42C01E
          std::string sprop = tracks[trackNo].getParamString("sprop-parameter-sets");
          size_t comma = sprop.find(',');
          tracks[trackNo].spsData = Encodings::Base64::decode(sprop.substr(0, comma));
          tracks[trackNo].ppsData = Encodings::Base64::decode(sprop.substr(comma + 1));
          updateH264Init(trackNo);
        }
        if (thisTrack->codec == "HEVC"){
          tracks[trackNo].hevcInfo.addUnit(
              Encodings::Base64::decode(tracks[trackNo].getParamString("sprop-vps")));
          tracks[trackNo].hevcInfo.addUnit(
              Encodings::Base64::decode(tracks[trackNo].getParamString("sprop-sps")));
          tracks[trackNo].hevcInfo.addUnit(
              Encodings::Base64::decode(tracks[trackNo].getParamString("sprop-pps")));
          updateH265Init(trackNo);
        }
        continue;
      }
      // We ignore bandwidth lines
      if (to.substr(0, 2) == "b="){continue;}
      // we ignore everything before the first media line.
      if (!trackNo){continue;}
      // at this point, the data is definitely for a track
      INFO_MSG("Unhandled SDP line for track %llu: %s", trackNo, to.c_str());
    }
    for (std::map<unsigned int, DTSC::Track>::iterator it = myMeta->tracks.begin();
         it != myMeta->tracks.end(); ++it){
      INFO_MSG("Detected track %s", it->second.getIdentifier().c_str());
    }
  }

  /// Calculates H265 track metadata from sps and pps data stored in tracks[trackNo]
  void State::updateH265Init(uint64_t trackNo){
    DTSC::Track &Trk = myMeta->tracks[trackNo];
    SDP::Track &RTrk = tracks[trackNo];
    if (!RTrk.hevcInfo.haveRequired()){
      MEDIUM_MSG("Aborted meta fill for hevc track %lu: no info nal unit", trackNo);
      return;
    }
    Trk.init = RTrk.hevcInfo.generateHVCC();

    h265::metaInfo MI = tracks[trackNo].hevcInfo.getMeta();

    RTrk.fpsMeta = MI.fps;
    Trk.width = MI.width;
    Trk.height = MI.height;
    Trk.fpks = RTrk.fpsMeta * 1000;
    tConv[trackNo].setProperties(Trk);
  }

  /// Calculates H264 track metadata from vps, sps and pps data stored in tracks[trackNo]
  void State::updateH264Init(uint64_t trackNo){
    DTSC::Track &Trk = myMeta->tracks[trackNo];
    SDP::Track &RTrk = tracks[trackNo];
    h264::sequenceParameterSet sps(RTrk.spsData.data(), RTrk.spsData.size());
    h264::SPSMeta hMeta = sps.getCharacteristics();
    MP4::AVCC avccBox;
    avccBox.setVersion(1);
    avccBox.setProfile(RTrk.spsData[1]);
    avccBox.setCompatibleProfiles(RTrk.spsData[2]);
    avccBox.setLevel(RTrk.spsData[3]);
    avccBox.setSPSCount(1);
    avccBox.setSPS(RTrk.spsData);
    avccBox.setPPSCount(1);
    avccBox.setPPS(RTrk.ppsData);
    RTrk.fpsMeta = hMeta.fps;
    Trk.width = hMeta.width;
    Trk.height = hMeta.height;
    Trk.fpks = hMeta.fps * 1000;
    Trk.init = std::string(avccBox.payload(), avccBox.payloadSize());
    tConv[trackNo].setProperties(Trk);
  }

  uint32_t State::getTrackNoForChannel(uint8_t chan){
    for (std::map<uint32_t, Track>::iterator it = tracks.begin(); it != tracks.end(); ++it){
      if (chan == it->second.channel){return it->first;}
    }
    return 0;
  }

  uint32_t State::parseSetup(HTTP::Parser &H, const std::string &cH, const std::string &src){
    static uint32_t trackCounter = 0;
    if (H.url == "200"){
      ++trackCounter;
      if (!tracks.count(trackCounter)){return 0;}
      if (!tracks[trackCounter].parseTransport(H.GetHeader("Transport"), cH, src,
                                               myMeta->tracks[trackCounter])){
        return 0;
      }
      return trackCounter;
    }

    HTTP::URL url(H.url);
    std::string urlString = url.getBareUrl();
    std::string pw = H.GetVar("pass");
    bool loop = true;

    while (loop){
      if (tracks.size()){
        for (std::map<uint32_t, Track>::iterator it = tracks.begin(); it != tracks.end(); ++it){
          if (!it->second.control.size()){
            it->second.control = "/track" + JSON::Value(it->first).asString();
            INFO_MSG("Control track: %s", it->second.control.c_str());
          }

          if ((urlString.size() >= it->second.control.size() &&
               urlString.substr(urlString.size() - it->second.control.size()) ==
                   it->second.control) ||
              (pw.size() >= it->second.control.size() &&
               pw.substr(pw.size() - it->second.control.size()) == it->second.control)){
            INFO_MSG("Parsing SETUP against track %lu", it->first);
            if (!it->second.parseTransport(H.GetHeader("Transport"), cH, src,
                                           myMeta->tracks[it->first])){
              return 0;
            }
            return it->first;
          }
        }
      }
      if (H.url.find("/track") != std::string::npos){
        uint32_t trackNo = atoi(H.url.c_str() + H.url.find("/track") + 6);
        if (trackNo){
          INFO_MSG("Parsing SETUP against track %lu", trackNo);
          if (!tracks[trackNo].parseTransport(H.GetHeader("Transport"), cH, src,
                                              myMeta->tracks[trackNo])){
            return 0;
          }
          return trackNo;
        }
      }
      if (urlString != url.path){
        urlString = url.path;
      }else{
        loop = false;
      }
    }
    return 0;
  }

  /// Returns the multiplier to use to get milliseconds from the RTP payload type for the given
  /// track
  double getMultiplier(const DTSC::Track &Trk){
    if (Trk.type == "video" || Trk.codec == "MP2" || Trk.codec == "MP3"){return 90.0;}
    return ((double)Trk.rate / 1000.0);
  }
  
  void State::updateInit(const uint64_t trackNo, const std::string &initData){
    if (myMeta->tracks.count(trackNo)){
      myMeta->tracks[trackNo].init = initData;
    }
  }

  /// Handles RTP packets generically, for both TCP and UDP-based connections.
  /// In case of UDP, expects packets to be pre-sorted.
  void State::handleIncomingRTP(const uint64_t track, const RTP::Packet &pkt){
    tConv[track].setCallbacks(incomingPacketCallback, snglStateInitCallback);
    tConv[track].addRTP(pkt);
  }

}// namespace SDP

