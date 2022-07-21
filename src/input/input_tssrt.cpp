#include "input_tssrt.h"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mist/defines.h>
#include <mist/downloader.h>
#include <mist/flv_tag.h>
#include <mist/http_parser.h>
#include <mist/mp4_generic.h>
#include <mist/socket_srt.h>
#include <mist/stream.h>
#include <mist/timing.h>
#include <mist/ts_packet.h>
#include <mist/util.h>
#include <string>

#include <mist/procs.h>
#include <mist/tinythread.h>
#include <sys/stat.h>

Util::Config *cfgPointer = NULL;
std::string baseStreamName;
Socket::SRTServer sSock;

void (*oldSignal)(int, siginfo_t *,void *) = 0;

void signal_handler(int signum, siginfo_t *sigInfo, void *ignore){
  sSock.close();
  if (oldSignal){
    oldSignal(signum, sigInfo, ignore);
  }
}


// We use threads here for multiple input pushes, because of the internals of the SRT Library
static void callThreadCallbackSRT(void *socknum){
  SRTSOCKET sock = *((SRTSOCKET *)socknum);
  // use the accepted socket as the second parameter
  Mist::inputTSSRT inp(cfgPointer, sock);
  inp.setSingular(false);
  inp.run();
}

namespace Mist{
  /// Constructor of TS Input
  /// \arg cfg Util::Config that contains all current configurations.
  inputTSSRT::inputTSSRT(Util::Config *cfg, SRTSOCKET s) : Input(cfg){
    capa["name"] = "TSSRT";
    capa["desc"] = "This input allows for processing MPEG2-TS-based SRT streams. Use mode=listener "
                   "for push input.";
    capa["source_match"].append("srt://*");
    // These can/may be set to always-on mode
    capa["always_match"].append("srt://*");
    capa["incoming_push_url"] = "srt://$host:$port";
    capa["incoming_push_url_match"] = "srt://*";
    capa["priority"] = 9;
    capa["codecs"][0u][0u].append("H264");
    capa["codecs"][0u][0u].append("HEVC");
    capa["codecs"][0u][0u].append("MPEG2");
    capa["codecs"][0u][1u].append("AAC");
    capa["codecs"][0u][1u].append("MP3");
    capa["codecs"][0u][1u].append("AC3");
    capa["codecs"][0u][1u].append("MP2");
    capa["codecs"][0u][1u].append("opus");

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


    // Setup if we are called form with a thread for push-based input.
    if (s != -1){
      srtConn = Socket::SRTConnection(s);
      streamName = baseStreamName;
      std::string streamid = srtConn.getStreamName();
      int64_t acc = config->getInteger("acceptable");
      if (acc == 0){
        if (streamid.size()){streamName += "+" + streamid;}
      }else if(acc == 2){
        if (streamName != streamid){
          FAIL_MSG("Stream ID '%s' does not match stream name, push blocked", streamid.c_str());
          srtConn.close();
        }
      }
      Util::setStreamName(streamName);
    }
    lastTimeStamp = 0;
    timeStampOffset = 0;
    singularFlag = true;
  }

  inputTSSRT::~inputTSSRT(){}

  bool inputTSSRT::checkArguments(){return true;}

  /// Live Setup of SRT Input. Runs only if we are the "main" thread
  bool inputTSSRT::preRun(){
    if (srtConn.getSocket() == -1){
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
        size_t connectCnt = 0;
        do{
          srtConn.connect(u.host, u.getPort(), "input", arguments);
          if (!srtConn){Util::sleep(1000);}
          ++connectCnt;
        }while (!srtConn && connectCnt < 10);
        if (!srtConn){WARN_MSG("Connecting to %s timed out", u.getUrl().c_str());}
      }
    }
    return true;
  }

  // Retrieve the next packet to be played from the srt connection.
  void inputTSSRT::getNext(size_t idx){
    thisPacket.null();
    bool hasPacket = tsStream.hasPacket();
    while (!hasPacket && srtConn && config->is_active){

      size_t recvSize = srtConn.RecvNow();
      if (recvSize){
        if (assembler.assemble(tsStream, srtConn.recvbuf, recvSize, true)){hasPacket = tsStream.hasPacket();}
      }else if (srtConn){
        // This should not happen as the SRT socket is read blocking and won't return until there is
        // data. But if it does, wait before retry
        Util::sleep(10);
      }
    }
    if (hasPacket){tsStream.getEarliestPacket(thisPacket);}

    if (!thisPacket){
      if (srtConn){
        INFO_MSG("Could not getNext TS packet!");
        Util::logExitReason("internal TS parser error");
      }else{
        Util::logExitReason("SRT connection close");
      }
      return;
    }

    tsStream.initializeMetadata(meta);
    thisIdx = M.trackIDToIndex(thisPacket.getTrackId(), getpid());
    if (thisIdx == INVALID_TRACK_ID){getNext(idx);}

    uint64_t adjustTime = thisPacket.getTime() + timeStampOffset;
    if (lastTimeStamp || timeStampOffset){
      if (lastTimeStamp + 5000 < adjustTime || lastTimeStamp > adjustTime + 5000){
        INFO_MSG("Timestamp jump " PRETTY_PRINT_MSTIME " -> " PRETTY_PRINT_MSTIME ", compensating.",
                 PRETTY_ARG_MSTIME(lastTimeStamp), PRETTY_ARG_MSTIME(adjustTime));
        timeStampOffset += (lastTimeStamp - adjustTime);
        adjustTime = thisPacket.getTime() + timeStampOffset;
      }
    }
    lastTimeStamp = adjustTime;
    thisPacket.setTime(adjustTime);
  }

  bool inputTSSRT::openStreamSource(){return true;}

  void inputTSSRT::streamMainLoop(){
    // If we do not have a srtConn here, we are the main thread and should start accepting pushes.
    if (srtConn.getSocket() == -1){
      cfgPointer = config;
      baseStreamName = streamName;
      while (config->is_active && sSock.connected()){
        Socket::SRTConnection S = sSock.accept();
        if (S.connected()){// check if the new connection is valid
          SRTSOCKET sock = S.getSocket();
          // spawn a new thread for this connection
          tthread::thread T(callThreadCallbackSRT, (void *)&sock);
          // detach it, no need to keep track of it anymore
          T.detach();
          HIGH_MSG("Spawned new thread for socket %i", S.getSocket());
        }
      }
      return;
    }
    // If we are here: we have a proper connection (either accepted or pull input) and should start parsing it as such
    Input::streamMainLoop();
  }

  bool inputTSSRT::needsLock(){return false;}

  void inputTSSRT::setSingular(bool newSingular){singularFlag = newSingular;}

  void inputTSSRT::connStats(Comms::Statistics &statComm){
    statComm.setUp(srtConn.dataUp());
    statComm.setDown(srtConn.dataDown());
    statComm.setHost(getConnectedBinHost());
    statComm.setPacketCount(srtConn.packetCount());
    statComm.setPacketLostCount(srtConn.packetLostCount());
    statComm.setPacketRetransmitCount(srtConn.packetRetransmitCount());
  }

}// namespace Mist
