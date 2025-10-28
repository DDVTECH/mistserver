#include "input_tsrist.h"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mist/defines.h>
#include <mist/downloader.h>
#include <mist/flv_tag.h>
#include <mist/http_parser.h>
#include <mist/mp4_generic.h>
#include <mist/stream.h>
#include <mist/timing.h>
#include <mist/ts_packet.h>
#include <mist/util.h>
#include <string>

#include <mist/procs.h>
#include <sys/stat.h>

Mist::InputTSRIST *connPtr = 0;
Util::Config *cnfPtr = 0;


struct rist_logging_settings log_settings;
int rist_log_callback(void *, enum rist_log_level llvl, const char *msg){
  switch (llvl){
  case RIST_LOG_WARN: WARN_MSG("RIST: %s", msg); break;
  case RIST_LOG_ERROR: ERROR_MSG("RIST: %s", msg); break;
  case RIST_LOG_DEBUG:
  case RIST_LOG_SIMULATE: DONTEVEN_MSG("RIST: %s", msg); break;
  default: INFO_MSG("RIST: %s", msg);
  }
  return 0;
}

uint64_t pktReceived = 0;
uint64_t pktLost = 0;
uint64_t pktRetransmitted = 0;
uint64_t downBytes = 0;

int cb_stats(void *arg, const struct rist_stats *stats_container){
  JSON::Value stats = JSON::fromString(stats_container->stats_json, stats_container->json_size);
  JSON::Value & sObj = stats["receiver-stats"]["flowinstant"]["stats"];
  pktReceived += sObj["received"].asInt();
  pktLost += sObj["lost"].asInt();
  pktRetransmitted += sObj["retries"].asInt();
  rist_stats_free(stats_container);
  return 0;
}

static int cb_recv(void *arg, struct rist_data_block *b){
  downBytes += b->payload_len;
  if (cnfPtr && cnfPtr->is_active){
    connPtr->addData((const char*)b->payload, b->payload_len);
  }
  rist_receiver_data_block_free2(&b);
  return 0;
}


namespace Mist{
  /// Constructor of TS Input
  /// \arg cfg Util::Config that contains all current configurations.
  InputTSRIST::InputTSRIST(Util::Config *cfg) : Input(cfg){
    rawMode = false;
    rawIdx = INVALID_TRACK_ID;
    lastRawPacket = 0;
    hasRaw = false;
    connPtr = this;
    cnfPtr = config;

    //Setup logger
    log_settings.log_cb = &rist_log_callback;
    log_settings.log_cb_arg = 0;
    log_settings.log_socket = -1;
    log_settings.log_stream = 0;
    if (Util::printDebugLevel >= 10){
      log_settings.log_level = RIST_LOG_SIMULATE;
    }else if(Util::printDebugLevel >= 4){
      log_settings.log_level = RIST_LOG_INFO;
    }else{
      log_settings.log_level = RIST_LOG_WARN;
    }

    capa["name"] = "TSRIST";
    capa["desc"] = "This input allows for processing MPEG2-TS-based RIST streams using librist " + std::string(librist_version()) +".";
    capa["source_match"].append("rist://*");
    capa["source_prefill"] = "rist://";
    capa["source_syntax"] = "rist://[@][address][:port]";
    capa["source_help"] = "Set up a listening RIST socket on one of your interfaces by adding the @. Do not include the @ if you want to pull from another location. Due to backwards compatibility with Simple profile EVEN ports are enforced.";
    // These can/may be set to always-on mode
    capa["always_match"].append("rist://*");
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
    option["long"] = "profile";
    option["short"] = "p";
    option["help"] = "RIST profile (0=Simple, 1=Main)";
    option["value"].append(1);
    config->addOption("profile", option);
    option.null();
    capa["optional"]["profile"]["name"] = "RIST profile";
    capa["optional"]["profile"]["help"] = "RIST profile to use";
    capa["optional"]["profile"]["type"] = "select";
    capa["optional"]["profile"]["default"] = 1;
    capa["optional"]["profile"]["select"][0u][0u] = 0;
    capa["optional"]["profile"]["select"][0u][1u] = "Simple";
    capa["optional"]["profile"]["select"][1u][0u] = 1;
    capa["optional"]["profile"]["select"][1u][1u] = "Main";
    capa["optional"]["profile"]["type"] = "select";
    capa["optional"]["profile"]["option"] = "--profile";

    capa["optional"]["raw"]["name"] = "Raw input mode";
    capa["optional"]["raw"]["help"] = "Enable raw MPEG-TS passthrough mode";
    capa["optional"]["raw"]["option"] = "--raw";

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

    lastTimeStamp = 0;
    timeStampOffset = 0;
    receiver_ctx = 0;
  }

  InputTSRIST::~InputTSRIST(){
    cnfPtr = 0;
    rist_destroy(receiver_ctx);
  }

  bool InputTSRIST::checkArguments(){
    if (config->getString("datatrack") == "json"){
      tsStream.setRawDataParser(TS::JSON);
    }
    return true;
  }

  /// Live Setup of SRT Input. Runs only if we are the "main" thread
  bool InputTSRIST::preRun(){
    rawMode = config->getBool("raw");
    if (rawMode){INFO_MSG("Entering raw mode");}

    std::string source = config->getString("input");
    standAlone = false;
    HTTP::URL u(source);
    if (u.protocol != "rist"){
      Util::logExitReason(ER_FORMAT_SPECIFIC, "Input protocol must begin with rist://");
      return false;
    }
    std::map<std::string, std::string> arguments;
    HTTP::parseVars(u.args, arguments);
    return true;
  }

  // Retrieve the next packet to be played from the srt connection.
  void InputTSRIST::getNext(size_t idx){
    thisPacket.null();
    if (rawMode){
      //Set to false so the other thread knows its safe to fill
      hasRaw = false;
      while (!hasRaw && config->is_active){
        Util::sleep(50);
        if (!bufferActive()){
          Util::logExitReason(ER_SHM_LOST, "Buffer shut down");
          return;
        }
      }
      //if hasRaw, thisPacket has been filled by the other thread
      return;
    }

    while (!thisPacket && config->is_active){
      if (tsStream.hasPacket()){
        tsStream.getEarliestPacket(thisPacket);
      }else{
        Util::sleep(50);
        if (!bufferActive()){
          Util::logExitReason(ER_SHM_LOST, "Buffer shut down");
          return;
        }
      }
    }
    if (!config->is_active){return;}

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

  void InputTSRIST::onFail(const std::string & msg){
    FAIL_MSG("%s", msg.c_str());
    Util::logExitReason(ER_FORMAT_SPECIFIC, msg.c_str());
  }

  bool InputTSRIST::openStreamSource(){
    int profile = config->getInteger("profile");
    INFO_MSG("RIST input starting in %s mode", (profile == 1) ? "main" : "simple");
    if (rist_receiver_create(&receiver_ctx, (rist_profile)profile, &log_settings) != 0) {
      onFail("Failed to create RIST receiver context");
      return false;
    }
    struct rist_peer_config *peer_config_link = 0;
    if (rist_parse_address2(config->getString("input").c_str(), &peer_config_link)){
      onFail("Failed to parse input URL: "+config->getString("input"));
      return false;
    }
    strcpy(peer_config_link->cname, streamName.c_str());
    struct rist_peer *peer;
    if (rist_peer_create(receiver_ctx, &peer, peer_config_link) == -1){
      onFail("Could not create RIST peer");
      return false;
    }
    if (rist_stats_callback_set(receiver_ctx, 1000, cb_stats, 0) == -1){
      onFail("Error setting up RIST stats callback");
      return false;
    }
    if (rist_receiver_data_callback_set2(receiver_ctx, cb_recv, 0)){
      onFail("Error setting up RIST data callback");
      return false;
    }
    if (rist_start(receiver_ctx) == -1){
      onFail("Failed to start RIST connection");
      return false;
    }
    return true;
  }

  void InputTSRIST::addData(const char * ptr, size_t len){
    for (size_t o = 0; o+188 <= len; o += 188){
      if (rawMode){
        rawBuffer.append(ptr+o, 188);
        if (!hasRaw && rawBuffer.size() >= 1316 && (lastRawPacket == 0 || lastRawPacket != Util::bootMS())){
          if (rawIdx == INVALID_TRACK_ID){
            rawIdx = meta.addTrack();
            meta.setType(rawIdx, "meta");
            meta.setCodec(rawIdx, "rawts");
            meta.setID(rawIdx, 1);
            userSelect[rawIdx].reload(streamName, rawIdx, COMM_STATUS_ACTSOURCEDNT);
          }
          thisTime = Util::bootMS();
          thisIdx = rawIdx;
          thisPacket.genericFill(thisTime, 0, 1, rawBuffer, rawBuffer.size(), 0, 0);
          lastRawPacket = thisTime;
          rawBuffer.truncate(0);
          hasRaw = true;
        }
      }else{
        tsStream.parse((char*)ptr+o, 0);
      }
    }
  }


  void InputTSRIST::connStats(Comms::Connections &statComm){
    statComm.setUp(0);
    statComm.setDown(downBytes);
    statComm.setHost(getConnectedBinHost());
    statComm.setPacketCount(pktReceived);
    statComm.setPacketLostCount(pktLost);
    statComm.setPacketRetransmitCount(pktRetransmitted);
  }

}// namespace Mist
