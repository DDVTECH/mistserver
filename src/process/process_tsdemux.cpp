#include "../input/input.h"
#include "../output/output.h"
#include "process.hpp"

#include <mist/defines.h>
#include <mist/h264.h>
#include <mist/http_parser.h>
#include <mist/json.h>
#include <mist/mp4_generic.h>
#include <mist/nal.h>
#include <mist/procs.h>
#include <mist/ts_stream.h>
#include <mist/util.h>

#include <mutex>
#include <thread>

// Stat related stuff
JSON::Value pStat;
JSON::Value & pData = pStat["proc_status_update"]["status"];
std::mutex statsMutex;
uint64_t statSinkMs = 0;
uint64_t statSourceMs = 0;
uint64_t inFpks = 0;

namespace Mist {
  bool sendFirst = false;
  int64_t bootMsOffset = 0;
  bool hasPacket = false;

  JSON::Value opt; /// Options
  std::mutex streamMutex;
  TS::Stream tsStream; ///< Used for parsing the incoming ts stream
  bool checkDelay = false;
  uint64_t firstCheck = Util::bootSecs();

  class ProcessSink : public Input {
    public:
      ProcessSink(Util::Config *cfg) : Input(cfg) {
        capa["name"] = "TSDemux";
        streamName = opt["sink"].asString();
        if (!streamName.size()) { streamName = opt["source"].asString(); }
        Util::streamVariables(streamName, opt["source"].asString());
        {
          std::lock_guard<std::mutex> guard(statsMutex);
          pStat["proc_status_update"]["sink"] = streamName;
          pStat["proc_status_update"]["source"] = opt["source"];
        }
        Util::setStreamName(opt["source"].asString() + "→" + streamName);
        if (opt.isMember("target_mask") && !opt["target_mask"].isNull() && opt["target_mask"].asString() != "") {
          DTSC::trackValidDefault = opt["target_mask"].asInt();
        }
      }
      void getNext(size_t idx = INVALID_TRACK_ID) {
        thisPacket.null();
        {
          std::lock_guard<std::mutex> guard(statsMutex);
          if (pData["sink_tracks"].size() != userSelect.size()) {
            pData["sink_tracks"].null();
            for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end(); it++) {
              pData["sink_tracks"].append((uint64_t)it->first);
            }
          }
        }

        while (!hasPacket && config->is_active) { Util::sleep(5); }
        if (!config->is_active || !hasPacket) { return; }

        {
          std::lock_guard<std::mutex> guard(streamMutex);
          tsStream.getEarliestPacket(thisPacket);
          if (!thisPacket) {
            Util::logExitReason(ER_FORMAT_SPECIFIC, "Could not getNext TS packet!");
            return;
          }
          hasPacket = tsStream.hasPacket();
          tsStream.initializeMetadata(meta);
          if (checkDelay) {
            static size_t lastTrks = 0;
            static size_t lastPids = 0;
            size_t pid = getpid();
            std::set<size_t> pids = tsStream.getActiveTracks(true);
            std::set<size_t> trks;
            for (std::set<size_t>::iterator it = pids.begin(); it != pids.end(); ++it) {
              size_t trIdx = M.trackIDToIndex(*it, pid);
              if (trIdx != INVALID_TRACK_ID) { trks.insert(trIdx); }
            }
            if (pids.size() == trks.size()) {
              INFO_MSG("All %zu tracks from TS mux found; activating all tracks", pids.size());
              for (std::set<size_t>::iterator it = trks.begin(); it != trks.end(); ++it) {
                meta.validateTrack(*it, DTSC::trackValidDefault);
              }
              checkDelay = false;
              tsStream.delay(false);
            } else if (trks.size() != lastTrks || pids.size() != lastPids) {
              WARN_MSG("Waiting for all tracks: have %zu out of %zu...", trks.size(), pids.size());
              lastTrks = trks.size();
              lastPids = pids.size();
              if (!firstCheck) {
                firstCheck = Util::bootSecs();
              } else if (Util::bootSecs() >= firstCheck + config->getInteger("waitforalltracks")) {
                INFO_MSG("Timeout! Activating all tracks despite not all found yet");
                for (std::set<size_t>::iterator it = trks.begin(); it != trks.end(); ++it) {
                  meta.validateTrack(*it, DTSC::trackValidDefault);
                }
                checkDelay = false;
                tsStream.delay(false);
              }
            }
          }
        }

        size_t pid = thisPacket.getTrackId();
        thisIdx = M.trackIDToIndex(pid, getpid());
        if (thisIdx == INVALID_TRACK_ID) { return getNext(idx); }

        static int64_t tOff = 0;
        thisTime = thisPacket.getTime() + tOff;
        if (thisTime + 15000 < statSourceMs || statSourceMs + 15000 < thisTime) {
          INFO_MSG("Rewriting track %zu timestamp %" PRIu64 " to %" PRIu64, thisIdx, thisTime, statSourceMs);
          tOff += statSourceMs - thisTime;
          thisTime = statSourceMs;
        }

        if (pid < 5000) {
          for (size_t tPid = pid + 5000; tPid <= pid + 10000; tPid += 5000) {
            size_t subIdx = M.trackIDToIndex(tPid, getpid());
            if (subIdx != INVALID_TRACK_ID) {
              uint64_t nowMs = tsStream.getLastMs(tPid) + tOff;
              if (meta.getNowms(subIdx) < nowMs) { meta.setNowms(subIdx, nowMs); }
            }
          }
        }

        statSinkMs = thisTime;
      }
      void streamMainLoop() {
        uint64_t statTimer = 0;
        uint64_t startTime = Util::bootSecs();
        Comms::Connections statComm;
        getNext();
        if (thisPacket && !userSelect.count(thisIdx)) {
          userSelect[thisIdx].reload(streamName, thisIdx, COMM_STATUS_ACTIVE | COMM_STATUS_SOURCE | COMM_STATUS_DONOTTRACK);
        }
        while (thisPacket && config->is_active && userSelect[thisIdx]) {
          if (userSelect[thisIdx].getStatus() & COMM_STATUS_REQDISCONNECT) {
            Util::logExitReason(ER_CLEAN_LIVE_BUFFER_REQ, "buffer requested shutdown");
            break;
          }
          if (isSingular() && !bufferActive()) {
            Util::logExitReason(ER_SHM_LOST, "Buffer shut down");
            return;
          }
          char *data;
          size_t dataLen;
          thisPacket.getString("data", data, dataLen);
          bufferLivePacket(thisTime, thisPacket.getInt("offset"), thisIdx, data, dataLen, 0, thisPacket.getFlag("keyframe"));
          getNext();
          if (!thisPacket) {
            Util::logExitReason(ER_CLEAN_EOF, "no more data");
            break;
          }
          if (thisPacket && !userSelect.count(thisIdx)) {
            userSelect[thisIdx].reload(streamName, thisIdx, COMM_STATUS_ACTIVE | COMM_STATUS_SOURCE | COMM_STATUS_DONOTTRACK);
          }

          if (Util::bootSecs() - statTimer > 1) {
            // Connect to stats for INPUT detection
            if (!statComm) {
              statComm.reload(streamName, getConnectedBinHost(), JSON::Value(getpid()).asString(),
                              "INPUT:" + capa["name"].asStringRef(), "");
            }
            if (statComm) {
              if (statComm.getStatus() & COMM_STATUS_REQDISCONNECT) {
                config->is_active = false;
                Util::logExitReason(ER_CLEAN_CONTROLLER_REQ, "received shutdown request from controller");
                return;
              }
              uint64_t now = Util::bootSecs();
              statComm.setNow(now);
              statComm.setStream(streamName);
              statComm.setTime(now - startTime);
              statComm.setLastSecond(0);
              connStats(statComm);
            }

            statTimer = Util::bootSecs();
          }
        }
      }

      ~ProcessSink() {}
      bool checkArguments() { return true; }
      bool needHeader() { return false; }
      bool readHeader() { return true; }
      bool openStreamSource() { return true; }
      void parseStreamHeader() {}
      bool needsLock() { return false; }
      bool isSingular() { return false; }
      virtual bool publishesTracks() { return false; }
      void connStats(Comms::Connections & statComm) {}
  };

  class ProcessSource : public Output {
    public:
      inline virtual bool keepGoing() { return Util::Config::is_active; }
      bool isRecording() { return false; }
      ProcessSource(Socket::Connection & c, Util::Config & cfg, JSON::Value & capa) : Output(c, cfg, capa) {
        realTime = 0;
        initialize();
        wantRequest = false;
        parseData = true;
        if (opt.isMember("waitforalltracks") && opt["waitforalltracks"].asBool()) {
          tsStream.delay(true);
          checkDelay = true;
        }
      }
      ~ProcessSource() {}
      static void init(Util::Config *cfg, JSON::Value & capa) {
        Output::init(cfg, capa);
        capa["name"] = "TSDemux";
        capa["codecs"][0u][0u].append("rawts");
        capa["codecs"][0u][0u].append("rawhls");
        cfg->addOption("streamname", R"-({"short":"s", "long":"stream", "arg":"string"})-");
        cfg->addBasicConnectorOptions(capa);
      }
      virtual bool onFinish() {
        if (opt.isMember("exit_unmask") && opt["exit_unmask"].asBool()) {
          if (userSelect.size()) {
            for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end(); it++) {
              INFO_MSG("Unmasking source track %zu" PRIu64, it->first);
              meta.validateTrack(it->first, TRACK_VALID_ALL);
            }
          }
        }
        return Output::onFinish();
      }
      virtual void dropTrack(size_t trackId, const std::string & reason, bool probablyBad = true) {
        if (opt.isMember("exit_unmask") && opt["exit_unmask"].asBool()) {
          INFO_MSG("Unmasking source track %zu" PRIu64, trackId);
          meta.validateTrack(trackId, TRACK_VALID_ALL);
        }
        Output::dropTrack(trackId, reason, probablyBad);
      }
      void sendHeader() {
        if (opt.isMember("source_mask")) {
          for (std::map<size_t, Comms::Users>::iterator ti = userSelect.begin(); ti != userSelect.end(); ++ti) {
            if (ti->first == INVALID_TRACK_ID) { continue; }
            INFO_MSG("Masking source track %zu with %zu", ti->first, (size_t)opt["source_mask"].asInt());
            meta.validateTrack(ti->first, meta.trackValid(ti->first) & opt["source_mask"].asInt());
          }
        }
        realTime = 0;
        Output::sendHeader();
      }
      void connStats(uint64_t now, Comms::Connections & statComm) {
        for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end(); it++) {
          if (it->second) { it->second.setStatus(COMM_STATUS_DONOTTRACK | it->second.getStatus()); }
        }
      }
      // Ensure we start at the live-most point
      void initialSeek(bool dryRun) {
        if (!meta) { return; }
        uint64_t t = 0;
        for (std::map<size_t, Comms::Users>::iterator ti = userSelect.begin(); ti != userSelect.end(); ++ti) {
          if (!t || t > M.getNowms(ti->first)) { t = M.getNowms(ti->first); }
        }
        seek(t);
      }
      void sendNext() {
        if (!config->is_active) { return; }

        {
          std::lock_guard<std::mutex> guard(statsMutex);
          if (pData["source_tracks"].size() != userSelect.size()) {
            pData["source_tracks"].null();
            for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end(); it++) {
              pData["source_tracks"].append((uint64_t)it->first);
            }
          }
        }

        if (thisTime > statSourceMs) { statSourceMs = thisTime; }
        needsLookAhead = 0;
        maxSkipAhead = 0;
        realTime = 0;
        if (!sendFirst) {
          bootMsOffset = M.getBootMsOffset();
          sendFirst = true;
        }

        if (M.getCodec(thisIdx) == "rawts") {
          size_t dataLen = thisDataLen;
          char *dataPointer = thisData;
          std::lock_guard<std::mutex> guard(streamMutex);
          while (dataLen >= 188) {
            tsStream.parse(dataPointer, 0);
            dataPointer += 188;
            dataLen -= 188;
          }
          hasPacket = tsStream.hasPacket();
        } else if (M.getCodec(thisIdx) == "rawhls") {
          std::string tmpData(thisData, thisDataLen);
          HTTP::Parser H;
          H.headerOnly = true;
          if (!H.Read(tmpData)) { return; }
          if (HTTP::URL(H.url).getExt() != "ts") {
            MEDIUM_MSG("Skipping non-TS HLS file: %s", H.url.c_str());
            return;
          }
          size_t dataLen = tmpData.size();
          char *dataPointer = (char *)tmpData.c_str();
          std::lock_guard<std::mutex> guard(streamMutex);
          while (dataLen >= 188) {
            tsStream.parse(dataPointer, 0);
            dataPointer += 188;
            dataLen -= 188;
          }
          hasPacket = tsStream.hasPacket();
        }
      }
  };

} // namespace Mist

int main(int argc, char *argv[]) {
  DTSC::trackValidMask = TRACK_VALID_INT_PROCESS;
  Util::Config config(argv[0]);
  Util::Config::binaryType = Util::PROCESS;
  JSON::Value capa;

  {
    JSON::Value opt;
    opt["arg"] = "string";
    opt["default"] = "-";
    opt["arg_num"] = 1;
    opt["help"] = "JSON configuration, or - (default) to read from stdin";
    config.addOption("configuration", opt);
    opt.null();
    opt["long"] = "json";
    opt["short"] = "j";
    opt["help"] = "Output connector info in JSON format, then exit.";
    opt["value"].append(0);
    config.addOption("json", opt);
  }

  capa["codecs"][0u][0u].append("rawts");
  capa["codecs"][0u][0u].append("rawhls");

  capa["ainfo"]["sinkTime"]["name"] = "Sink timestamp";
  capa["ainfo"]["sourceTime"]["name"] = "Source timestamp";
  capa["ainfo"]["child_pid"]["name"] = "Child process PID";
  capa["ainfo"]["cmd"]["name"] = "Child process command";

  if (!(config.parseArgs(argc, argv))) { return 1; }
  if (config.getBool("json")) {

    addGenericProcessOptions(capa);

    capa["name"] = "TSDemux";
    capa["hrn"] = "Demuxer: MPEG-TS";
    capa["desc"] = "Takes a raw format TS track and demuxes it into tracks";

    capa["optional"]["waitforalltracks"]["name"] = "Wait for all tracks";
    capa["optional"]["waitforalltracks"]["help"] =
      "Delays tracks becoming visible until all supported tracks in all PATs are ready (parameter is max wait time, "
      "zero disables the wait)";
    capa["optional"]["waitforalltracks"]["type"] = "uint";
    capa["optional"]["waitforalltracks"]["default"] = 0;

    capa["optional"]["source_mask"]["name"] = "Source track mask";
    capa["optional"]["source_mask"]["help"] = "What internal processes should have access to the source track(s)";
    capa["optional"]["source_mask"]["type"] = "select";
    capa["optional"]["source_mask"]["select"][0u][0u] = "";
    capa["optional"]["source_mask"]["select"][0u][1u] = "Keep original value";
    capa["optional"]["source_mask"]["select"][1u][0u] = 255;
    capa["optional"]["source_mask"]["select"][1u][1u] = "Everything";
    capa["optional"]["source_mask"]["select"][2u][0u] = 4;
    capa["optional"]["source_mask"]["select"][2u][1u] = "Processing tasks (not viewers, not pushes)";
    capa["optional"]["source_mask"]["select"][3u][0u] = 6;
    capa["optional"]["source_mask"]["select"][3u][1u] = "Processing and pushing tasks (not viewers)";
    capa["optional"]["source_mask"]["select"][4u][0u] = 5;
    capa["optional"]["source_mask"]["select"][4u][1u] = "Processing and viewer tasks (not pushes)";
    capa["optional"]["source_mask"]["default"] = "";

    capa["optional"]["target_mask"]["name"] = "Output track mask";
    capa["optional"]["target_mask"]["help"] = "What internal processes should have access to the ouput track(s)";
    capa["optional"]["target_mask"]["type"] = "select";
    capa["optional"]["target_mask"]["select"][0u][0u] = "";
    capa["optional"]["target_mask"]["select"][0u][1u] = "Keep original value";
    capa["optional"]["target_mask"]["select"][1u][0u] = 255;
    capa["optional"]["target_mask"]["select"][1u][1u] = "Everything";
    capa["optional"]["target_mask"]["select"][2u][0u] = 1;
    capa["optional"]["target_mask"]["select"][2u][1u] = "Viewer tasks (not processing, not pushes)";
    capa["optional"]["target_mask"]["select"][3u][0u] = 2;
    capa["optional"]["target_mask"]["select"][3u][1u] = "Pushing tasks (not processing, not viewers)";
    capa["optional"]["target_mask"]["select"][4u][0u] = 4;
    capa["optional"]["target_mask"]["select"][4u][1u] = "Processing tasks (not pushes, not viewers)";
    capa["optional"]["target_mask"]["select"][5u][0u] = 3;
    capa["optional"]["target_mask"]["select"][5u][1u] = "Viewer and pushing tasks (not processing)";
    capa["optional"]["target_mask"]["select"][6u][0u] = 5;
    capa["optional"]["target_mask"]["select"][6u][1u] = "Viewer and processing tasks (not pushes)";
    capa["optional"]["target_mask"]["select"][7u][0u] = 6;
    capa["optional"]["target_mask"]["select"][7u][1u] = "Pushing and processing tasks (not viewers)";
    capa["optional"]["target_mask"]["select"][8u][0u] = 0;
    capa["optional"]["target_mask"]["select"][8u][1u] = "Nothing";
    capa["optional"]["target_mask"]["default"] = "";

    capa["optional"]["exit_unmask"]["name"] = "Undo masks on process exit/fail";
    capa["optional"]["exit_unmask"]["help"] = "If/when the process exits or fails, the masks for input tracks will be "
                                              "reset to defaults. (NOT to previous value, but to defaults!)";
    capa["optional"]["exit_unmask"]["default"] = false;

    capa["optional"]["sink"]["name"] = "Target stream";
    capa["optional"]["sink"]["help"] = "What stream the encoded track should be added to. Defaults "
                                       "to source stream. May contain variables.";
    capa["optional"]["sink"]["type"] = "string";
    capa["optional"]["sink"]["validate"][0u] = "streamname_with_wildcard_and_variables";

    std::cout << capa.toString() << std::endl;
    return -1;
  }

  Util::redirectLogsIfNeeded();

  // read configuration
  if (config.getString("configuration") != "-") {
    Mist::opt.fromString(config.getString("configuration"));
  } else {
    INFO_MSG("Reading configuration from standard input");
    Mist::opt.fromStream(std::cin);
  }

  if (!Mist::opt.isMember("source") || !Mist::opt["source"] || !Mist::opt["source"].isString()) {
    FAIL_MSG("invalid source in config!");
    return 1;
  }

  if (!Mist::opt.isMember("sink") || !Mist::opt["sink"] || !Mist::opt["sink"].isString()) {
    INFO_MSG("No sink explicitly set, using source as sink");
  }

  Util::Config co;
  Util::Config conf;
  co.is_active = true;
  conf.is_active = true;

  // stream which connects to input
  std::thread source([&conf, &co]() {
    Util::nameThread("source");
    Util::setStreamName(Mist::opt["source"].asStringRef() + "→" + Mist::opt["sink"].asStringRef());
    JSON::Value capa;
    Mist::ProcessSource::init(&conf, capa);
    conf.getOption("streamname", true).append(Mist::opt["source"].asStringRef());
    JSON::Value opt;
    opt["arg"] = "string";
    opt["default"] = "";
    opt["arg_num"] = 1;
    conf.addOption("target", opt);
    conf.getOption("target", true).append("-");
    Socket::Connection S(1, 0);
    Mist::ProcessSource out(S, conf, capa);
    Util::setStreamName(Mist::opt["source"].asStringRef() + "→" + Mist::opt["sink"].asStringRef());
    MEDIUM_MSG("Running source thread...");
    out.run();
    INFO_MSG("Stop source thread...");
    co.is_active = false;
  });

  // needs to pass through encoder to outputEBML
  std::thread sink([&co, &conf]() {
    Util::nameThread("sink");
    Mist::ProcessSink in(&co);
    co.getOption("output", true).append("-");
    MEDIUM_MSG("Running sink thread...");
    in.run();
    INFO_MSG("Stop sink thread...");
    conf.is_active = false;
  });

  // run process
  uint64_t lastProcUpdate = Util::bootSecs();
  {
    std::lock_guard<std::mutex> guard(statsMutex);
    pStat["proc_status_update"]["id"] = getpid();
    pStat["proc_status_update"]["proc"] = "TSDemux";
  }
  uint64_t startTime = Util::bootSecs();
  while (conf.is_active && co.is_active) {
    Util::sleep(200);
    if (lastProcUpdate + 5 <= Util::bootSecs()) {
      std::lock_guard<std::mutex> guard(statsMutex);
      pData["active_seconds"] = (Util::bootSecs() - startTime);
      pData["ainfo"]["sourceTime"] = statSourceMs;
      pData["ainfo"]["sinkTime"] = statSinkMs;
      Util::sendUDPApi(pStat);
      lastProcUpdate = Util::bootSecs();
    }
  }

  co.is_active = false;
  conf.is_active = false;

  source.join();
  HIGH_MSG("source thread joined");

  sink.join();
  HIGH_MSG("sink thread joined");

  return 0;
}
