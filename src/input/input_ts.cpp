#include "input_ts.h"
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
#include <mist/stream.h>
#include <mist/timing.h>
#include <mist/ts_packet.h>
#include <mist/util.h>
#include <string>

#include <mist/procs.h>
#include <mist/tinythread.h>
#include <sys/stat.h>

tthread::mutex threadClaimMutex;
std::string globalStreamName;
TS::Stream liveStream;
Util::Config *cfgPointer = NULL;

#define THREAD_TIMEOUT 15
std::map<size_t, uint64_t> threadTimer;

std::set<size_t> claimableThreads;

/// Global, so that all tracks stay in sync
int64_t timeStampOffset = 0;

void parseThread(void *mistIn){
  uint64_t lastTimeStamp = 0;
  Mist::inputTS *input = reinterpret_cast<Mist::inputTS *>(mistIn);

  size_t tid = 0;
  {
    tthread::lock_guard<tthread::mutex> guard(threadClaimMutex);
    if (claimableThreads.size()){
      tid = *claimableThreads.begin();
      claimableThreads.erase(claimableThreads.begin());
    }
  }
  if (tid == 0){return;}

  Comms::Users userConn;
  DTSC::Meta meta;
  DTSC::Packet pack;
  bool dataTrack = liveStream.isDataTrack(tid);
  size_t idx = INVALID_TRACK_ID;
  {
    tthread::lock_guard<tthread::mutex> guard(threadClaimMutex);
    threadTimer[tid] = Util::bootSecs();
  }
  while (Util::bootSecs() - threadTimer[tid] < THREAD_TIMEOUT && cfgPointer->is_active &&
         (!dataTrack || (userConn ? userConn : true))){
    liveStream.parse(tid);
    if (!liveStream.hasPacket(tid)){
      Util::sleep(100);
      continue;
    }
    threadTimer[tid] = Util::bootSecs();
    //Non-stream tracks simply flush all packets and continue
    if (!dataTrack){
      while (liveStream.hasPacket(tid)){liveStream.getPacket(tid, pack);}
      continue;
    }
    //If we arrive here, we want the stream data
    //Make sure the track is valid, loaded, etc
    if (!meta || idx == INVALID_TRACK_ID || !meta.trackValid(idx)){
      {//Only lock the mutex for as long as strictly necessary
        tthread::lock_guard<tthread::mutex> guard(threadClaimMutex);
        std::map<std::string, std::string> overrides;
        overrides["singular"] = "";
        if (!Util::streamAlive(globalStreamName) && !Util::startInput(globalStreamName, "push://INTERNAL_ONLY:" + cfgPointer->getString("input"), true, true, overrides)){
          FAIL_MSG("Could not start buffer for %s", globalStreamName.c_str());
          return;
        }
        if (!input->hasMeta()){input->reloadClientMeta();}
      }
      //This meta object is thread local, no mutex needed
      meta.reInit(globalStreamName, false);
      if (!meta){
        //Meta init failure, retry later
        Util::sleep(100);
        continue;
      }
      liveStream.initializeMetadata(meta, tid);
      idx = meta.trackIDToIndex(tid, getpid());
      if (idx != INVALID_TRACK_ID){
        //Successfully assigned a track index! Inform the buffer we're pushing
        userConn.reload(globalStreamName, idx, COMM_STATUS_ACTIVE | COMM_STATUS_SOURCE | COMM_STATUS_DONOTTRACK);
      }
      //Any kind of failure? Retry later.
      if (idx == INVALID_TRACK_ID || !meta.trackValid(idx)){
        Util::sleep(100);
        continue;
      }
    }
    while (liveStream.hasPacket(tid)){
      liveStream.getPacket(tid, pack);
      if (pack){
        char *data;
        size_t dataLen;
        pack.getString("data", data, dataLen);
        uint64_t adjustTime = pack.getTime() + timeStampOffset;
        if (lastTimeStamp || timeStampOffset){
          if (lastTimeStamp + 5000 < adjustTime || lastTimeStamp > adjustTime + 5000){
            INFO_MSG("Timestamp jump " PRETTY_PRINT_MSTIME " -> " PRETTY_PRINT_MSTIME ", compensating.", PRETTY_ARG_MSTIME(lastTimeStamp), PRETTY_ARG_MSTIME(adjustTime));
            timeStampOffset += (lastTimeStamp-adjustTime);
            adjustTime = pack.getTime() + timeStampOffset;
          }
        }
        lastTimeStamp = adjustTime;
        if (!meta.getBootMsOffset()){meta.setBootMsOffset(Util::bootMS() - adjustTime);}
        {
          tthread::lock_guard<tthread::mutex> guard(threadClaimMutex);
          //If the main thread's local metadata doesn't have this track yet, reload metadata
          if (!input->trackLoaded(idx)){
            input->reloadClientMeta();
            if (!input->trackLoaded(idx)){
              FAIL_MSG("Track %zu could not be loaded into main thread - throwing away packet", idx);
              continue;
            }
          }
          input->bufferLivePacket(adjustTime, pack.getInt("offset"), idx, data, dataLen,
                                pack.getInt("bpos"), pack.getFlag("keyframe"));
        }
      }
    }
  }

  //On shutdown, make sure to clean up stream buffer
  if (idx != INVALID_TRACK_ID){
    tthread::lock_guard<tthread::mutex> guard(threadClaimMutex);
    input->liveFinalize(idx);
  }

  std::string reason = "unknown reason";
  if (!(Util::bootSecs() - threadTimer[tid] < THREAD_TIMEOUT)){reason = "thread timeout";}
  if (!cfgPointer->is_active){reason = "input shutting down";}
  if (!(!liveStream.isDataTrack(tid) || userConn)){
    reason = "buffer disconnect";
    cfgPointer->is_active = false;
  }
  INFO_MSG("Shutting down thread for %zu because %s", tid, reason.c_str());
  {
    tthread::lock_guard<tthread::mutex> guard(threadClaimMutex);
    threadTimer.erase(tid);
  }
  liveStream.eraseTrack(tid);
  if (dataTrack && userConn){userConn.setStatus(COMM_STATUS_DISCONNECT | userConn.getStatus());}
}

namespace Mist{

  /// Constructor of TS Input
  /// \arg cfg Util::Config that contains all current configurations.
  inputTS::inputTS(Util::Config *cfg) : Input(cfg){
    rawMode = false;
    rawIdx = INVALID_TRACK_ID;
    lastRawPacket = 0;
    capa["name"] = "TS";
    capa["desc"] =
        "This input allows you to stream MPEG2-TS data from static files (/*.ts), streamed files "
        "or named pipes (stream://*.ts), streamed over HTTP (http(s)://*.ts, http(s)-ts://*), "
        "standard input (ts-exec:*), or multicast/unicast UDP sockets (tsudp://*).";
    capa["source_match"].append("/*.ts");
    capa["source_file"] = "$source";
    capa["source_match"].append("/*.m2ts");
    capa["source_match"].append("stream://*.ts");
    capa["source_match"].append("tsudp://*");
    capa["source_match"].append("ts-exec:*");
    capa["source_match"].append("http://*.ts");
    capa["source_match"].append("http-ts://*");
    capa["source_match"].append("https://*.ts");
    capa["source_match"].append("https-ts://*");
    // These can/may be set to always-on mode
    capa["always_match"].append("stream://*.ts");
    capa["always_match"].append("tsudp://*");
    capa["always_match"].append("ts-exec:*");
    capa["always_match"].append("http://*.ts");
    capa["always_match"].append("http-ts://*");
    capa["always_match"].append("https://*.ts");
    capa["always_match"].append("https-ts://*");
    capa["incoming_push_url"] = "udp://$host:$port";
    capa["incoming_push_url_match"] = "tsudp://*";
    capa["priority"] = 9;
    capa["codecs"]["video"].append("H264");
    capa["codecs"]["video"].append("HEVC");
    capa["codecs"]["video"].append("MPEG2");
    capa["codecs"]["audio"].append("AAC");
    capa["codecs"]["audio"].append("AC3");
    capa["codecs"]["audio"].append("MP2");
    capa["codecs"]["audio"].append("opus");
    capa["codecs"]["passthrough"].append("rawts");
    inFile = NULL;
    inputProcess = 0;
    isFinished = false;

#ifndef WITH_SRT
    {
      pid_t srt_tx = -1;
      const char *args[] ={"srt-live-transmit", 0};
      srt_tx = Util::Procs::StartPiped(args, 0, 0, 0);
      if (srt_tx > 1){
        capa["source_match"].append("srt://*");
        capa["always_match"].append("srt://*");
        capa["desc"] =
            capa["desc"].asStringRef() + " Non-native SRT support (srt://*) is installed and available.";
      }else{
        capa["desc"] = capa["desc"].asStringRef() +
                       " To enable non-native SRT support, please install the srt-live-transmit binary.";
      }
    }
#endif

    capa["optional"]["DVR"]["name"] = "Buffer time (ms)";
    capa["optional"]["DVR"]["help"] =
        "The target available buffer time for this live stream, in milliseconds. This is the time "
        "available to seek around in, and will automatically be extended to fit whole keyframes as "
        "well as the minimum duration needed for stable playback.";
    capa["optional"]["DVR"]["type"] = "uint";
    capa["optional"]["DVR"]["default"] = 50000;

    capa["optional"]["maxkeepaway"]["name"] = "Maximum live keep-away distance";
    capa["optional"]["maxkeepaway"]["help"] = "Maximum distance in milliseconds to fall behind the live point for stable playback.";
    capa["optional"]["maxkeepaway"]["type"] = "uint";
    capa["optional"]["maxkeepaway"]["default"] = 45000;

    capa["optional"]["segmentsize"]["name"] = "Segment size (ms)";
    capa["optional"]["segmentsize"]["help"] = "Target time duration in milliseconds for segments.";
    capa["optional"]["segmentsize"]["type"] = "uint";
    capa["optional"]["segmentsize"]["default"] = 1900;

    capa["optional"]["fallback_stream"]["name"] = "Fallback stream";
    capa["optional"]["fallback_stream"]["help"] =
        "Alternative stream to load for playback when there is no active broadcast";
    capa["optional"]["fallback_stream"]["type"] = "str";
    capa["optional"]["fallback_stream"]["default"] = "";

    capa["optional"]["raw"]["name"] = "Raw input mode";
    capa["optional"]["raw"]["help"] = "Enable raw MPEG-TS passthrough mode";
    capa["optional"]["raw"]["option"] = "--raw";

    JSON::Value option;
    option["long"] = "raw";
    option["short"] = "R";
    option["help"] = "Enable raw MPEG-TS passthrough mode";
    config->addOption("raw", option);
  }

  inputTS::~inputTS(){
    if (inFile){fclose(inFile);}
    if (tcpCon){tcpCon.close();}
    if (!standAlone){
      tthread::lock_guard<tthread::mutex> guard(threadClaimMutex);
      threadTimer.clear();
      claimableThreads.clear();
    }
  }

  bool inputTS::checkArguments(){
    if (config->getString("input").substr(0, 6) == "srt://"){
      std::string source = config->getString("input");
      HTTP::URL srtUrl(source);
      config->getOption("input", true).append("ts-exec:srt-live-transmit " + srtUrl.getUrl() + " file://con");
      INFO_MSG("Rewriting SRT source '%s' to '%s'", source.c_str(), config->getString("input").c_str());
    }
    return true;
  }

  /// Live Setup of TS Input
  bool inputTS::preRun(){
    INFO_MSG("Prerun: %s", config->getString("input").c_str());

    rawMode = config->getBool("raw");
    if (rawMode){INFO_MSG("Entering raw mode");}

    // streamed standard input
    if (config->getString("input") == "-"){
      standAlone = false;
      tcpCon.open(fileno(stdout), fileno(stdin));
      return true;
    }
    if (config->getString("input").substr(0, 7) == "http://" ||
        config->getString("input").substr(0, 10) == "http-ts://" ||
        config->getString("input").substr(0, 8) == "https://" ||
        config->getString("input").substr(0, 11) == "https-ts://"){
      standAlone = false;
      HTTP::URL url(config->getString("input"));
      if (url.protocol == "http-ts"){url.protocol = "http";}
      if (url.protocol == "https-ts"){url.protocol = "https";}
      HTTP::Downloader DL;
      DL.getHTTP().headerOnly = true;
      if (!DL.get(url)){return false;}
      tcpCon = DL.getSocket();
      DL.getSocket().drop(); // Prevent shutdown of connection, keeping copy of socket open
      return true;
    }
    if (config->getString("input").substr(0, 8) == "ts-exec:"){
      standAlone = false;
      std::string input = config->getString("input").substr(8);
      int fin = -1, fout = -1;
      inputProcess = Util::Procs::StartPipedShell(input.c_str(), &fin, &fout, 0);
      tcpCon.open(-1, fout);
      return true;
    }
    // streamed file
    if (config->getString("input").substr(0, 9) == "stream://"){
      inFile = fopen(config->getString("input").c_str() + 9, "r");
      tcpCon.open(-1, fileno(inFile));
      standAlone = false;
      return inFile;
    }
    //file descriptor input
    if (config->getString("input").substr(0, 5) == "fd://"){
      int fd = atoi(config->getString("input").c_str() + 5);
      INFO_MSG("Opening file descriptor %s (%d)", config->getString("input").c_str(), fd);
      tcpCon.open(-1, fd);
      standAlone = false;
      return tcpCon;
    }
    // UDP input (tsudp://[host:]port[/iface[,iface[,...]]])
    if (config->getString("input").substr(0, 8) == "tsudp://"){
      standAlone = false;
      return true;
    }
    // plain VoD file
    inFile = fopen(config->getString("input").c_str(), "r");
    return inFile;
  }

  bool inputTS::needHeader(){
    if (!standAlone){return false;}
    return Input::needHeader();
  }

  /// Reads headers from a TS stream, and saves them into metadata
  /// It works by going through the entire TS stream, and every time
  /// It encounters a new PES start, it writes the currently found PES data
  /// for a specific track to metadata. After the entire stream has been read,
  /// it writes the remaining metadata.
  ///\todo Find errors, perhaps parts can be made more modular
  bool inputTS::readHeader(){
    if (!inFile){return false;}
    meta.reInit(isSingular() ? streamName : "");
    TS::Packet packet; // to analyse and extract data
    DTSC::Packet headerPack;
    fseek(inFile, 0, SEEK_SET); // seek to beginning

    uint64_t lastBpos = 0;
    while (packet.FromFile(inFile) && !feof(inFile)){
      tsStream.parse(packet, lastBpos);
      lastBpos = Util::ftell(inFile);
      if (packet.getUnitStart()){
        while (tsStream.hasPacketOnEachTrack()){
          tsStream.getEarliestPacket(headerPack);
          size_t pid = headerPack.getTrackId();
          size_t idx = M.trackIDToIndex(pid, getpid());
          if (idx == INVALID_TRACK_ID || !M.getCodec(idx).size()){
            tsStream.initializeMetadata(meta, pid);
            idx = M.trackIDToIndex(pid, getpid());
          }
          char *data;
          size_t dataLen;
          headerPack.getString("data", data, dataLen);
          meta.update(headerPack.getTime(), headerPack.getInt("offset"), idx, dataLen,
                      headerPack.getInt("bpos"), headerPack.getFlag("keyframe"), headerPack.getDataLen());
        }
      }
    }
    tsStream.finish();
    INFO_MSG("Reached %s at %" PRIu64 " bytes", feof(inFile) ? "EOF" : "error", lastBpos);
    while (tsStream.hasPacket()){
      tsStream.getEarliestPacket(headerPack);
      size_t pid = headerPack.getTrackId();
      size_t idx = M.trackIDToIndex(pid, getpid());
      if (idx == INVALID_TRACK_ID || !M.getCodec(idx).size()){
        tsStream.initializeMetadata(meta, pid);
        idx = M.trackIDToIndex(pid, getpid());
      }
      char *data;
      size_t dataLen;
      headerPack.getString("data", data, dataLen);
      meta.update(headerPack.getTime(), headerPack.getInt("offset"), idx, dataLen,
                  headerPack.getInt("bpos"), headerPack.getFlag("keyframe"), headerPack.getDataLen());
    }

    fseek(inFile, 0, SEEK_SET);
    meta.toFile(config->getString("input") + ".dtsh");
    return true;
  }

  /// Gets the next packet that is to be sent
  /// At the moment, the logic of sending the last packet that was finished has been implemented,
  /// but the seeking and finding data is not yet ready.
  ///\todo Finish the implementation
  void inputTS::getNext(size_t idx){
    size_t pid = (idx == INVALID_TRACK_ID ? 0 : M.getID(idx));
    INSANE_MSG("Getting next on track %zu", idx);
    thisPacket.null();
    bool hasPacket = (idx == INVALID_TRACK_ID ? tsStream.hasPacket() : tsStream.hasPacket(pid));
    while (!hasPacket && !feof(inFile) &&
           (inputProcess == 0 || Util::Procs::childRunning(inputProcess)) && config->is_active){
      tsBuf.FromFile(inFile);
      if (idx == INVALID_TRACK_ID || pid == tsBuf.getPID()){
        tsStream.parse(tsBuf, 0); // bPos == 0
        if (tsBuf.getUnitStart()){
          hasPacket = (idx == INVALID_TRACK_ID ? tsStream.hasPacket() : tsStream.hasPacket(pid));
        }
      }
    }
    if (feof(inFile)){
      if (!isFinished){
        tsStream.finish();
        isFinished = true;
      }
      hasPacket = true;
    }
    if (!hasPacket){return;}
    if (idx == INVALID_TRACK_ID){
      if (tsStream.hasPacket()){tsStream.getEarliestPacket(thisPacket);}
    }else{
      if (tsStream.hasPacket(pid)){tsStream.getPacket(pid, thisPacket);}
    }

    if (!thisPacket){
      INFO_MSG("Could not getNext TS packet!");
      return;
    }
    tsStream.initializeMetadata(meta);
    thisIdx = M.trackIDToIndex(thisPacket.getTrackId(), getpid());
    thisTime = thisPacket.getTime();
    if (thisIdx == INVALID_TRACK_ID){getNext(idx);}
  }

  void inputTS::readPMT(){
    // save current file position
    uint64_t bpos = Util::ftell(inFile);
    if (fseek(inFile, 0, SEEK_SET)){
      FAIL_MSG("Seek to 0 failed");
      return;
    }

    TS::Packet tsBuffer;
    while (!tsStream.hasPacketOnEachTrack() && tsBuffer.FromFile(inFile)){
      tsStream.parse(tsBuffer, 0);
    }

    // Clear leaves the PMT in place
    tsStream.partialClear();

    // Restore original file position
    if (Util::fseek(inFile, bpos, SEEK_SET)){
      clearerr(inFile);
      return;
    }
  }

  /// Seeks to a specific time
  void inputTS::seek(uint64_t seekTime, size_t idx){
    tsStream.clear();
    readPMT();
    uint64_t seekPos = 0xFFFFFFFFull;
    if (idx != INVALID_TRACK_ID){
      uint32_t keyNum = M.getKeyNumForTime(idx, seekTime);
      DTSC::Keys keys(M.keys(idx));
      seekPos = keys.getBpos(keyNum);
    }else{
      std::set<size_t> tracks = M.getValidTracks();
      for (std::set<size_t>::iterator it = tracks.begin(); it != tracks.end(); it++){
        uint32_t keyNum = M.getKeyNumForTime(*it, seekTime);
        DTSC::Keys keys(M.keys(*it));
        uint64_t thisBPos = keys.getBpos(keyNum);
        if (thisBPos < seekPos){seekPos = thisBPos;}
      }
    }
    clearerr(inFile);
    Util::fseek(inFile, seekPos, SEEK_SET); // seek to the correct position
  }

  bool inputTS::openStreamSource(){
    const std::string &inpt = config->getString("input");
    if (inpt.substr(0, 8) == "tsudp://"){
      HTTP::URL input_url(inpt);
      udpCon.setBlocking(false);
      udpCon.bind(input_url.getPort(), input_url.host, input_url.path);
      if (udpCon.getSock() == -1){
        FAIL_MSG("Could not open UDP socket. Aborting.");
        return false;
      }
    }
    return true;
  }

  void inputTS::parseStreamHeader(){
    // Placeholder empty track to force normal code to continue despite no tracks available
    tmpIdx = meta.addTrack(0, 0, 0, 0);
  }

  void inputTS::streamMainLoop(){
    meta.removeTrack(tmpIdx);
    INFO_MSG("Removed temptrack %zu", tmpIdx);
    Comms::Connections statComm;
    uint64_t downCounter = 0;
    uint64_t startTime = Util::bootSecs();
    uint64_t noDataSince = Util::bootSecs();
    bool gettingData = false;
    bool hasStarted = false;
    cfgPointer = config;
    globalStreamName = streamName;
    unsigned long long threadCheckTimer = Util::bootSecs();
    while (config->is_active){
      if (tcpCon){
        if (tcpCon.spool()){
          while (tcpCon.Received().available(188)){
            while (tcpCon.Received().get()[0] != 0x47 && tcpCon.Received().available(188)){
              tcpCon.Received().remove(1);
            }
            if (tcpCon.Received().available(188) && tcpCon.Received().get()[0] == 0x47){
              std::string newData = tcpCon.Received().remove(188);
              if (rawMode){
                keepAlive();
                rawBuffer.append(newData);
                if (rawBuffer.size() >= 1316 && (lastRawPacket == 0 || lastRawPacket != Util::bootMS())){
                  if (rawIdx == INVALID_TRACK_ID){
                    rawIdx = meta.addTrack();
                    meta.setType(rawIdx, "meta");
                    meta.setCodec(rawIdx, "rawts");
                    meta.setID(rawIdx, 1);
                    userSelect[rawIdx].reload(streamName, rawIdx, COMM_STATUS_SOURCE);
                  }
                  uint64_t packetTime = Util::bootMS();
                  thisPacket.genericFill(packetTime, 0, 1, rawBuffer, rawBuffer.size(), 0, 0);
                  bufferLivePacket(thisPacket);
                  lastRawPacket = packetTime;
                  rawBuffer.truncate(0);
                }
              }else {
                tsBuf.FromPointer(newData.data());
                liveStream.add(tsBuf);
                if (!liveStream.isDataTrack(tsBuf.getPID())){liveStream.parse(tsBuf.getPID());}
              }
            }
          }
          noDataSince = Util::bootSecs();
        }else{
          Util::sleep(100);
        }
        if (!tcpCon){
          config->is_active = false;
          Util::logExitReason("end of streamed input");
          return;
        }
      }else{
        bool received = false;
        while (udpCon.Receive()){
          downCounter += udpCon.data.size();
          received = true;
          if (!gettingData){
            gettingData = true;
            INFO_MSG("Now receiving UDP data...");
          }
          if (rawMode){
            keepAlive();
            rawBuffer.append(udpCon.data, udpCon.data.size());
            if (rawBuffer.size() >= 1316 && (lastRawPacket == 0 || lastRawPacket != Util::bootMS())){
              if (rawIdx == INVALID_TRACK_ID){
                rawIdx = meta.addTrack();
                meta.setType(rawIdx, "meta");
                meta.setCodec(rawIdx, "rawts");
                meta.setID(rawIdx, 1);
                userSelect[rawIdx].reload(streamName, rawIdx, COMM_STATUS_SOURCE);
              }
              uint64_t packetTime = Util::bootMS();
              thisPacket.genericFill(packetTime, 0, 1, rawBuffer, rawBuffer.size(), 0, 0);
              bufferLivePacket(thisPacket);
              lastRawPacket = packetTime;
              rawBuffer.truncate(0);
            }
          }else{
            assembler.assemble(liveStream, udpCon.data, udpCon.data.size());
          }
        }
        if (!received){
          Util::sleep(100);
        }else{
          noDataSince = Util::bootSecs();
        }
      }
      if (gettingData && Util::bootSecs() - noDataSince > 1){
        gettingData = false;
        INFO_MSG("No longer receiving data.");
      }
      // Check for and spawn threads here.
      if (Util::bootSecs() - threadCheckTimer > 1){
        // Connect to stats for INPUT detection
        statComm.reload(streamName, getConnectedBinHost(), JSON::Value(getpid()).asString(), "INPUT:" + capa["name"].asStringRef(), "");
        if (statComm){
          if (statComm.getStatus() & COMM_STATUS_REQDISCONNECT){
            config->is_active = false;
            Util::logExitReason("received shutdown request from controller");
            return;
          }
          uint64_t now = Util::bootSecs();
          statComm.setNow(now);
          statComm.setStream(streamName);
          statComm.setConnector("INPUT:" + capa["name"].asStringRef());
          statComm.setUp(0);
          statComm.setDown(downCounter + tcpCon.dataDown());
          statComm.setTime(now - startTime);
          statComm.setLastSecond(0);
        }

        std::set<size_t> activeTracks = liveStream.getActiveTracks();
        if (!rawMode){
          tthread::lock_guard<tthread::mutex> guard(threadClaimMutex);
          if (hasStarted && !threadTimer.size()){
            if (!isAlwaysOn()){
              config->is_active = false;
              Util::logExitReason("no active threads and we had input in the past");
              return;
            }else{
              liveStream.clear();
              hasStarted = false;
            }
          }
          for (std::set<size_t>::iterator it = activeTracks.begin(); it != activeTracks.end(); it++){
            if (!liveStream.isDataTrack(*it)){continue;}
            if (threadTimer.count(*it) && ((Util::bootSecs() - threadTimer[*it]) > (2 * THREAD_TIMEOUT))){
              WARN_MSG("Thread for track %zu timed out %" PRIu64
                       " seconds ago without a clean shutdown.",
                       *it, Util::bootSecs() - threadTimer[*it]);
              threadTimer.erase(*it);
            }
            if (!hasStarted){hasStarted = true;}
            if (!threadTimer.count(*it)){

              // Add to list of unclaimed threads
              claimableThreads.insert(*it);

              // Spawn thread here.
              tthread::thread thisThread(parseThread, this);
              thisThread.detach();
            }
          }
        }
        threadCheckTimer = Util::bootSecs();
      }
      if (Util::bootSecs() - noDataSince > 20){
        if (!isAlwaysOn()){
          config->is_active = false;
          Util::logExitReason("no packets received for 20 seconds");
          return;
        }else{
          noDataSince = Util::bootSecs();
        }
      }
    }
  }

  void inputTS::finish(){
    if (standAlone){
      Input::finish();
      return;
    }
    int threadCount = 0;
    do{
      {
        tthread::lock_guard<tthread::mutex> guard(threadClaimMutex);
        threadCount = threadTimer.size();
      }
      if (threadCount){Util::sleep(100);}
    }while (threadCount);
  }

  bool inputTS::needsLock(){
    // we already know no lock will be needed
    if (!standAlone){return false;}
    // otherwise, check input param
    const std::string &inpt = config->getString("input");
    if (inpt.size() && inpt != "-" && inpt.substr(0, 9) != "stream://" && inpt.substr(0, 8) != "tsudp://" &&
        inpt.substr(0, 8) != "ts-exec:" && inpt.substr(0, 6) != "srt://" &&
        inpt.substr(0, 7) != "http://" && inpt.substr(0, 10) != "http-ts://" &&
        inpt.substr(0, 8) != "https://" && inpt.substr(0, 11) != "https-ts://"){
      return Input::needsLock();
    }
    return false;
  }

}// namespace Mist
