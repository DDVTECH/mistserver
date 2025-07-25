#include "input_tssrt.h"

#include <mist/auth.h>
#include <mist/defines.h>
#include <mist/downloader.h>
#include <mist/flv_tag.h>
#include <mist/http_parser.h>
#include <mist/mp4_generic.h>
#include <mist/procs.h>
#include <mist/socket_srt.h>
#include <mist/stream.h>
#include <mist/timing.h>
#include <mist/ts_packet.h>
#include <mist/util.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <signal.h>
#include <string>
#include <sys/stat.h>
#include <thread>

Util::Config *cfgPointer = NULL;
std::string baseStreamName;
Socket::SRTServer sSock;
bool rawMode = false;

void (*oldSignal)(int, siginfo_t *,void *) = 0;

void signal_handler(int signum, siginfo_t *sigInfo, void *ignore){
  sSock.close();
  if (oldSignal){
    oldSignal(signum, sigInfo, ignore);
  }
}

namespace Mist{
  /// Constructor of TS Input
  /// \arg cfg Util::Config that contains all current configurations.
  InputTSSRT::InputTSSRT(Util::Config *cfg, Socket::SRTConnection s) : Input(cfg){
    rawIdx = INVALID_TRACK_ID;
    udpInit = 0;
    srtConn = 0;
    lastRawPacket = 0;
    bootMSOffsetCalculated = false;
    assembler.setLive();
    capa["name"] = "TSSRT";
    capa["desc"] = "This input allows for processing MPEG2-TS-based SRT streams using libsrt " SRT_VERSION_STRING ".";
    capa["source_match"].append("srt://*");
    // These can/may be set to always-on mode
    capa["always_match"].append("srt://*");
    capa["incoming_push_url"] = "srt://$host:$port";
    capa["incoming_push_url_match"] = "srt://*";
    capa["priority"] = 9;
    capa["codecs"]["video"].append("H264");
    capa["codecs"]["video"].append("HEVC");
    capa["codecs"]["video"].append("MPEG2");
    capa["codecs"]["audio"].append("AAC");
    capa["codecs"]["audio"].append("MP3");
    capa["codecs"]["audio"].append("AC3");
    capa["codecs"]["audio"].append("MP2");
    capa["codecs"]["audio"].append("opus");
    capa["codecs"]["passthrough"].append("rawts");

    JSON::Value option;
    option["arg"] = "integer";
    option["long"] = "buffer";
    option["short"] = "b";
    option["help"] = "DVR buffer time in ms";
    option["value"].append(50000);
    config->addOption("bufferTime", option);
    option.null();
    capa["optional"]["DVR"]["name"] = "Buffer time (ms)";
    capa["optional"]["DVR"]["help"] =
        "The target available buffer time for this live stream, in milliseconds. This is the time "
        "available to seek around in, and will automatically be extended to fit whole keyframes as "
        "well as the minimum duration needed for stable playback.";
    capa["optional"]["DVR"]["option"] = "--buffer";
    capa["optional"]["DVR"]["type"] = "uint";
    capa["optional"]["DVR"]["default"] = 50000;

    option["arg"] = "integer";
    option["long"] = "acceptable";
    option["short"] = "T";
    option["help"] = "Acceptable pushed streamids (0 = use streamid as wildcard, 1 = ignore all streamids, 2 = disallow non-matching streamids)";
    option["value"].append(0);
    config->addOption("acceptable", option);
    capa["optional"]["acceptable"]["name"] = "Acceptable pushed streamids";
    capa["optional"]["acceptable"]["help"] = "What to do with the streamids for incoming pushes, if this is a listener SRT connection";
    capa["optional"]["acceptable"]["option"] = "--acceptable";
    capa["optional"]["acceptable"]["short"] = "T";
    capa["optional"]["acceptable"]["default"] = 0;
    capa["optional"]["acceptable"]["type"] = "select";
    capa["optional"]["acceptable"]["select"][0u][0u] = 0;
    capa["optional"]["acceptable"]["select"][0u][1u] = "Set streamid as wildcard";
    capa["optional"]["acceptable"]["select"][1u][0u] = 1;
    capa["optional"]["acceptable"]["select"][1u][1u] = "Ignore all streamids";
    capa["optional"]["acceptable"]["select"][2u][0u] = 2;
    capa["optional"]["acceptable"]["select"][2u][1u] = "Disallow non-matching streamid";

    capa["optional"]["raw"]["name"] = "Raw input mode";
    capa["optional"]["raw"]["help"] = "Enable raw MPEG-TS passthrough mode";
    capa["optional"]["raw"]["option"] = "--raw";

    option.null();
    option["long"] = "raw";
    option["short"] = "R";
    option["help"] = "Enable raw MPEG-TS passthrough mode";
    config->addOption("raw", option);
    
    capa["optional"]["datatrack"]["name"] = "MPEG Data track parser";
    capa["optional"]["datatrack"]["help"] = "Which parser to use for data tracks";
    capa["optional"]["datatrack"]["type"] = "select";
    capa["optional"]["datatrack"]["option"] = "--datatrack";
    capa["optional"]["datatrack"]["short"] = "D";
    capa["optional"]["datatrack"]["default"] = "";
    capa["optional"]["datatrack"]["select"][0u][0u] = "";
    capa["optional"]["datatrack"]["select"][0u][1u] = "None / disabled";
    capa["optional"]["datatrack"]["select"][1u][0u] = "json";
    capa["optional"]["datatrack"]["select"][1u][1u] = "2b size-prepended JSON";

    option.null();
    option["long"] = "datatrack";
    option["short"] = "D";
    option["arg"] = "string";
    option["default"] = "";
    option["help"] = "Which parser to use for data tracks";
    config->addOption("datatrack", option);

    // Setup if we are called form with a thread for push-based input.
    if (s.connected()){
      srtConn = new Socket::SRTConnection(s);
      streamName = baseStreamName;
      std::string streamid = srtConn->getStreamName();
      int64_t acc = config->getInteger("acceptable");
      if (acc == 0){
        if (streamid.size()){streamName += "+" + streamid;}
      }else if(acc == 2){
        if (streamName != streamid){
          FAIL_MSG("Stream ID '%s' does not match stream name, push blocked", streamid.c_str());
          srtConn->close();
          srtConn = 0;
        }
      }
      Util::setStreamName(streamName);
    }
    lastTimeStamp = 0;
    timeStampOffset = 0;
    singularFlag = true;
  }

  InputTSSRT::~InputTSSRT(){}

  bool InputTSSRT::checkArguments(){
    if (config->getString("datatrack") == "json"){
      tsStream.setRawDataParser(TS::JSON);
    }
    return true;
  }

  /// Live Setup of SRT Input. Runs only if we are the "main" thread
  bool InputTSSRT::preRun(){
    Socket::SRT::libraryInit();
    rawMode = config->getBool("raw");
    if (rawMode){INFO_MSG("Entering raw mode");}
    if (srtConn && *srtConn){return true;}
    std::string source = config->getString("input");
    standAlone = false;
    HTTP::URL u(source);
    INFO_MSG("Parsed url: %s", u.getUrl().c_str());
    if (Socket::interpretSRTMode(u) == "listener"){
      std::map<std::string, std::string> arguments;
      HTTP::parseVars(u.args, arguments);
      sSock = Socket::SRTServer(u.getPort(), u.host, arguments, false);
      struct sigaction new_action;
      struct sigaction cur_action;
      new_action.sa_sigaction = signal_handler;
      sigemptyset(&new_action.sa_mask);
      new_action.sa_flags = SA_SIGINFO;
      sigaction(SIGINT, &new_action, &cur_action);
      if (cur_action.sa_sigaction && cur_action.sa_sigaction != oldSignal){
        if (oldSignal){WARN_MSG("Multiple signal handlers! I can't deal with this.");}
        oldSignal = cur_action.sa_sigaction;
      }
      sigaction(SIGHUP, &new_action, &cur_action);
      if (cur_action.sa_sigaction && cur_action.sa_sigaction != oldSignal){
        if (oldSignal){WARN_MSG("Multiple signal handlers! I can't deal with this.");}
        oldSignal = cur_action.sa_sigaction;
      }
      sigaction(SIGTERM, &new_action, &cur_action);
      if (cur_action.sa_sigaction && cur_action.sa_sigaction != oldSignal){
        if (oldSignal){WARN_MSG("Multiple signal handlers! I can't deal with this.");}
        oldSignal = cur_action.sa_sigaction;
      }
    }else{
      std::map<std::string, std::string> arguments;
      HTTP::parseVars(u.args, arguments);

      std::string addData;
      if (arguments.count("streamid")){addData = arguments["streamid"];}
      size_t connectCnt = 0;
      do{
        if (!srtConn){srtConn = new Socket::SRTConnection();}
        srtConn->connect(u.host, u.getPort(), "input", arguments);
        if (!*srtConn){Util::sleep(1000);}
        ++connectCnt;
      }while (!*srtConn && connectCnt < 10);
      if (!*srtConn){WARN_MSG("Connecting to %s timed out", u.getUrl().c_str());}
    }
    return true;
  }

  // Retrieve the next packet to be played from the srt connection.
  void InputTSSRT::getNext(size_t idx){
    thisPacket.null();
    bool hasPacket = tsStream.hasPacket();
    while (!hasPacket && srtConn && *srtConn && config->is_active){

      size_t recvSize = srtConn->RecvNow();
      if (recvSize){
        if (rawMode){
          keepAlive();
          rawBuffer.append(srtConn->recvbuf, recvSize);
          if (rawBuffer.size() >= 1316 && (lastRawPacket == 0 || lastRawPacket != Util::bootMS())){
            if (rawIdx == INVALID_TRACK_ID){
              rawIdx = meta.addTrack();
              meta.setType(rawIdx, "meta");
              meta.setCodec(rawIdx, "rawts");
              meta.setID(rawIdx, 1);
              userSelect[rawIdx].reload(streamName, rawIdx, COMM_STATUS_ACTSOURCEDNT);
            }
            uint64_t packetTime = Util::bootMS();
            thisPacket.genericFill(packetTime, 0, 1, rawBuffer, rawBuffer.size(), 0, 0);
            lastRawPacket = packetTime;
            rawBuffer.truncate(0);
            return;
          }
          continue;
        }
        if (assembler.assemble(tsStream, srtConn->recvbuf, recvSize, true)){hasPacket = tsStream.hasPacket();}
      }else if (srtConn && *srtConn){
        // This should not happen as the SRT socket is read blocking and won't return until there is
        // data. But if it does, wait before retry
        Util::sleep(10);
      }
    }
    if (hasPacket){tsStream.getEarliestPacket(thisPacket);}

    if (!thisPacket){
      if (srtConn && *srtConn){
        INFO_MSG("Could not getNext TS packet!");
        Util::logExitReason(ER_FORMAT_SPECIFIC, "internal TS parser error");
      }else{
        Util::logExitReason(ER_CLEAN_REMOTE_CLOSE, "SRT connection close");
      }
      return;
    }

    tsStream.initializeMetadata(meta);
    thisIdx = M.trackIDToIndex(thisPacket.getTrackId(), getpid());
    if (thisIdx == INVALID_TRACK_ID){getNext(idx);}

    uint64_t pktTimeWithOffset = thisPacket.getTime() + timeStampOffset;
    if (lastTimeStamp || timeStampOffset){
      uint64_t targetTime = Util::bootMS() - M.getBootMsOffset();
      if (targetTime + 5000 < pktTimeWithOffset || targetTime > pktTimeWithOffset + 5000){
        INFO_MSG("Timestamp jump " PRETTY_PRINT_MSTIME " -> " PRETTY_PRINT_MSTIME ", compensating.",
                 PRETTY_ARG_MSTIME(targetTime), PRETTY_ARG_MSTIME(pktTimeWithOffset));
        timeStampOffset += (targetTime - pktTimeWithOffset);
        pktTimeWithOffset = thisPacket.getTime() + timeStampOffset;
      }
    }
    if (!bootMSOffsetCalculated){
      meta.setBootMsOffset((int64_t)Util::bootMS() - (int64_t)pktTimeWithOffset);
      bootMSOffsetCalculated = true;
    }
    lastTimeStamp = pktTimeWithOffset;
    thisPacket.setTime(pktTimeWithOffset);
    thisTime = pktTimeWithOffset;
  }

  bool InputTSSRT::openStreamSource(){return true;}

  void InputTSSRT::streamMainLoop(){
    // If we do not have a srtConn here, we are the main thread and should start accepting pushes.
    if (!srtConn || !*srtConn){
      cfgPointer = config;
      baseStreamName = streamName;
      while (config->is_active && sSock.connected()){
        Socket::SRTConnection * S = new Socket::SRTConnection();
        *S = sSock.accept();
        if (S->connected()){// check if the new connection is valid
          // spawn a new thread for this connection
          std::thread T([S](){
            Mist::InputTSSRT inp(cfgPointer, *S);
            inp.setSingular(false);
            inp.run();
            delete S;
          });
          // detach it, no need to keep track of it anymore
          T.detach();
          HIGH_MSG("Spawned new thread for socket %i", S->getSocket());
        }
      }
      Socket::SRT::libraryCleanup();
      return;
    }
    // If we are here: we have a proper connection (either accepted or pull input) and should start parsing it as such
    Input::streamMainLoop();
    srtConn->close();
    Socket::SRT::libraryCleanup();
  }

  bool InputTSSRT::needsLock(){return false;}

  void InputTSSRT::setSingular(bool newSingular){singularFlag = newSingular;}

  void InputTSSRT::connStats(Comms::Connections &statComm){
    statComm.setUp(srtConn->dataUp());
    statComm.setDown(srtConn->dataDown());
    statComm.setPacketCount(srtConn->packetCount());
    statComm.setPacketLostCount(srtConn->packetLostCount());
    statComm.setPacketRetransmitCount(srtConn->packetRetransmitCount());
  }

}// namespace Mist
