#include "input_dtsc.h"

#include <mist/auth.h>
#include <mist/bitfields.h>
#include <mist/cache.h>
#include <mist/defines.h>
#include <mist/encode.h>
#include <mist/http_parser.h>
#include <mist/stream.h>
#include <mist/util.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
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
    capa["source_match"].append("dtscs://*");
    capa["source_prefill"].append("/");
    capa["source_prefill"].append("dtsc://");
    capa["source_prefill"].append("dtscs://");
#if defined(__CYGWIN__)
    capa["source_syntax"].append("/cygdrive/[DRIVE/path/to/][file_name]");
    capa["source_help"]["/cygdrive/[DRIVE/path/to/][file_name]"] = "Location where MistServer can find the input file.";
#else
    capa["source_syntax"].append("/[path/to/][file_name]");
    capa["source_help"]["/[path/to/][file_name]"] = "Location where MistServer can find the input file.";
#endif
    capa["source_syntax"].append("dtsc://[address][/streamname]");
    capa["source_help"]["default"] = "Location where MistServer can find the input file.";
    capa["source_help"]["dtsc://[address][/streamname]"] = "Where MistServer can find the stream.\nInclude the DTSC port if it is not the default (4200).\nIf the stream name is omitted, the stream name of this stream is used.";
    capa["always_match"].append("dtsc://*"); // can be said to always-on mode
    capa["always_match"].append("dtscs://*"); // can be said to always-on mode
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
  }

  bool InputDTSC::openRemoteSource() {
    if (srcConn) return true;
    srcConn.open(sourceUri.host, sourceUri.getPort(), true, sourceUri.protocol == "dtscs");
    Util::Procs::socketList.insert(srcConn.getSocket());

    srcConn.Received().splitter.clear();
    return srcConn;
  }

  bool InputDTSC::needsLock() {
    if (sourceUri.protocol != "dtsc" && sourceUri.protocol != "dtscs") return Input::needsLock();
    // For dtsc:// we need to know if the remote stream is VOD or live.
    // VOD streams need a lock (serve mode), live streams don't (stream mode).
    if (!infoReceived) { getRemoteInfo(); }
    return !isLive;
  }

  void InputDTSC::parseStreamHeader() {
    lastRecvTime = lastPktTime = Util::bootMS();
    evLp.addInterval([&]() {
      uint64_t nowMs = Util::bootMS();
      if (lastRecvTime + 10000 < nowMs) {
        Util::logExitReason(ER_READ_START_FAILURE, "Receive timeout on DTSC socket");
        config->is_active = false;
        return (size_t)0;
      }
      return (size_t)(lastRecvTime + 10000 - nowMs);
    }, 10000);

    if (getenv("MIST_BALANCE_URL")) {
      HTTP::URL balanceUrl = (std::string)getenv("MIST_BALANCE_URL");
      evLp.addInterval([&, balanceUrl]() {
        if (balanceDl.isEventLooping()) { return 1000; }
        uint64_t nowMs = Util::bootMS();
        if (lastPktTime + 2000 < nowMs) {
          balanceDl.progressCallback = 0;
          balanceDl.getEventLooped(evLp, balanceUrl, 5, [&]() {
            // success
            HTTP::URL localUrl = config->getString("input");
            HTTP::URL wantedUrl = balanceDl.data();
            if (localUrl.protocol != wantedUrl.protocol || localUrl.host != wantedUrl.host){
              Util::logExitReason(ER_CLEAN_EOF, "Shutting down pull due to balancer URL change");
              config->is_active = false;
            }
            lastPktTime = Util::bootMS();
          }, [&]() {
            // failure
            INFO_MSG("Error reading from balancer: %s", balanceDl.getStatusText().c_str());
            lastPktTime = Util::bootMS();
          });
        }
        return 1000;
      }, 1000);
    }

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
    if (!validTracks.size()) { return; }
    userSelect.clear();
    for (std::set<size_t>::iterator it = validTracks.begin(); it != validTracks.end(); ++it) {
      userSelect[*it].reload(streamName, *it, COMM_STATUS_ACTSOURCEDNT);
    }
    {
      JSON::Value APIcall;
      APIcall["tag_stream"][streamName] = "replicated";
      Util::sendUDPApi(APIcall);
    }
  }

  bool InputDTSC::openStreamSource(){
    if (sourceUri.host == "-") {
      srcConn.open(fileno(stdout), fileno(stdin));
      srcConn.Received().splitter.clear();
      return true;
    }

    if (!openRemoteSource()) return false;

    /// \todo future password support for in the play cmd
    // std::string password = sourceUri.pass.size() ? sourceUri.pass : sourceUri.user;
    std::map<std::string, std::string> args;
    HTTP::parseVars(sourceUri.args, args);

    std::string streamName = sourceUri.path;
    Util::sanitizeName(streamName);
    if (!streamName.size()) streamName = config->getString("streamname");
    if (!srcConn) { return false; }

    JSON::Value prep(JSON::object{},
                     {
                       {"cmd", "play"},
                       {"version", APPIDENT},
                       {"stream", streamName},
                     });

    if (!isLive) prep["serve"] = true;
    if (args.count("sync")) prep["sync"].fromString(args["sync"]);
    if (args.count("noinput")) prep["noinput"].fromString(args["noinput"]);
    headerRequested = Util::bootMS();
    sendCmd(prep);
    return true;
  }

  void InputDTSC::closeStreamSource() {
    evLp.remove(srcConn.getSocket());
    Util::Procs::socketList.erase(srcConn.getSocket());
    srcConn.close();
  }

  InputDTSC::~InputDTSC() {
    if (streamName.size()) {
      JSON::Value APIcall;
      APIcall["untag_stream"][streamName] = "replicated";
      Util::sendUDPApi(APIcall);
    }
  }

  bool InputDTSC::checkArguments(){
    sourceUri = config->getString("input");
    if (!sourceUri.isLocalPath()) { return true; }
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

    // Remote: we try and open a connection later in needHeader
    if (sourceUri.protocol == "dtsc" || sourceUri.protocol == "dtscs") return true;

    // File: try and open the file
    if (!F.open(config->getString("input").c_str())) {
      Util::logExitReason(ER_READ_START_FAILURE, "Could not open file %s", config->getString("input").c_str());
      return false;
    }
    F.seek(0);
    return true;
  }

  bool InputDTSC::getRemoteInfo() {
    if (infoReceived) return true;
    if (!openRemoteSource()) return false;

    std::string remoteStreamName = sourceUri.path;
    Util::sanitizeName(remoteStreamName);
    if (!remoteStreamName.size()) remoteStreamName = config->getString("streamname");

    // Send getinfo and immediately ping, messages on the remote are strictly ordered which means
    // new remotes respond with streaminfo and a pong, whereas old remotes respond with just pong
    sendCmd(JSONOBJECT({{"cmd", "getinfo"}, {"version", APPIDENT}, {"stream", remoteStreamName}}));
    sendCmd(JSONOBJECT({{"cmd", "ping"}}));

    gotPong = false;
    uint64_t startTime = Util::bootMS();
    uint64_t nowMs = startTime;
    evLp.addSocket(4242, srcConn.getSocket());
    while (startTime + 5000 > (nowMs = Util::bootMS())) {
      size_t ret = evLp.await(startTime + 5000 - nowMs);
      if (ret == 4242 && srcConn.spool()) {
        while (srcConn.Received().available(8)) {
          if (srcConn.Received().copy(2) != "DT") {
            srcConn.Received().clear();
            break;
          }
          size_t pktSize = Bit::btohl(srcConn.Received().copy(8).data() + 4) + 8;
          if (!srcConn.Received().available(pktSize)) break;
          pktBuf.truncate(0);
          pktBuf.allocate(pktSize);
          srcConn.Received().remove(pktBuf, pktSize);
          thisPacket.reInit(pktBuf, pktBuf.size());
          if (thisPacket.getVersion() == DTSC::DTCM) handleCommandPacket();
        }
      }
      if (infoReceived) break;
      if (gotPong) {
        // Got pong without streaminfo first, the remote is old, fall back to old behaviour
        WARN_MSG("Remote does not support getinfo; assuming live stream");
        isLive = true;
        infoReceived = true;
        break;
      }
    }
    evLp.remove(srcConn.getSocket());

    return infoReceived;
  }

  bool InputDTSC::needHeader() {
    if (sourceUri.protocol != "dtsc" && sourceUri.protocol != "dtscs") return Input::needHeader();
    if (!infoReceived) getRemoteInfo();
    if (infoReceived && isLive) return false;
    if (infoReceived && isVod) {
      cachedHeader = readExistingHeader();
      if (M.getUUID() != uuid) {
        Cache::wipeStreamFromCache(meta.getStreamName(), true);
        meta.clear();
        return true;
      }
      return !cachedHeader;
    }

    // Fallback for old remotes that don't support getinfo: assume live
    if (!infoReceived) {
      isLive = true;
      infoReceived = true;
    }
    return false;
  }

  void InputDTSC::postHeader() {
    if (M && !srcConn && sourceUri.protocol != "dtsc" && sourceUri.protocol != "dtscs") {
      infoReceived = true;
      isLive = M.getLive();
      isVod = M.getVod();
    }

    // Open userSelect for all received tracks if we are in live mode
    if (isLive) {
      std::set<size_t> validTracks = M.getMySourceTracks(getpid());
      if (!validTracks.size()) { return; }
      userSelect.clear();
      for (std::set<size_t>::iterator it = validTracks.begin(); it != validTracks.end(); ++it) {
        userSelect[*it].reload(streamName, *it, COMM_STATUS_ACTSOURCEDNT);
      }
    }

    {
      JSON::Value APIcall;
      APIcall["tag_stream"][streamName] = "replicated";
      Util::sendUDPApi(APIcall);
    }
  }

  bool InputDTSC::readHeader() {
    if (sourceUri.protocol == "dtsc" || sourceUri.protocol == "dtscs") {
      // Re-init meta only if there is no cached header
      if (!cachedHeader) {
        meta.reInit(streamName);
        if (!meta) {
          Util::logExitReason(ER_INTERNAL_ERROR, "Could not open stream metadata to merge in remote tracks");
          config->is_active = false;
          return false;
        }
      }

      // Connect to the remote for a fresh header, fallback to cache if we cannot
      if (!openStreamSource()) {
        if (cachedHeader) WARN_MSG("Remote unavailable; using cached header");
        return cachedHeader;
      }

      // Wait for up to 15 seconds for the remote to send us the header
      uint64_t startTime = Util::bootMS();
      uint64_t nowMs = startTime;
      bool clearMeta = !headerReceived;
      evLp.remove(srcConn.getSocket());
      evLp.addSocket(4242, srcConn.getSocket());
      while (!headerReceived && startTime + 15000 > (nowMs = Util::bootMS())) {
        size_t ret = evLp.await(startTime + 15000 - nowMs);
        if (ret == 4242 && srcConn.spool()) {
          Socket::Buffer & rcv = srcConn.Received();
          while (rcv.available(8) && !headerReceived) {
            if (rcv.copy(2) != "DT") {
              rcv.clear();
              break;
            }
            size_t pktSize = Bit::btohl(rcv.copy(8).data() + 4) + 8;
            if (!rcv.available(pktSize)) break;
            pktBuf.truncate(0);
            pktBuf.allocate(pktSize);
            rcv.remove(pktBuf, pktSize);
            thisPacket.reInit(pktBuf, pktBuf.size());
            handlePacket(clearMeta);
          }
        }
      }
      evLp.remove(srcConn.getSocket());
      if (!headerReceived) {
        if (cachedHeader) {
          WARN_MSG("Timeout waiting for remote DTSH header; using cached header");
        } else {
          ERROR_MSG("Timeout waiting for DTSH header from the remote; no cached header available");
        }
        return cachedHeader;
      }
      // For VOD remotes we add a socket callback to handle start_track_media / data / stop_track_media commands
      if (M) {
        evLp.addSocket(srcConn.getSocket(), [this](void *p) {
          Util::ResizeablePointer ptr;
          bool clearMeta = !headerReceived;
          Socket::Buffer & rcv = srcConn.Received();
          do {
            while (rcv.available(8)) {
              if (rcv.copy(2) != "DT") {
                rcv.clear();
                break;
              }
              size_t pktSize = Bit::btohl(rcv.copy(8).data() + 4) + 8;
              if (!rcv.available(pktSize)) break;
              ptr.truncate(0);
              ptr.allocate(pktSize);
              rcv.remove(ptr, pktSize);
              thisPacket.reInit(ptr, ptr.size());
              handlePacket(clearMeta);
            }
          } while (srcConn.spool());
        }, 0);
        return true;
      }
    }
    if (!F) {
      Util::logExitReason(ER_READ_START_FAILURE, "Reading header for '%s' failed: Could not open input stream", config->getString("input").c_str());
      return false;
    }
    size_t moreHeader = 0;
    do{
      char hdr[8];
      char *ptr = hdr;
      F.seek(moreHeader);
      if (F.readExact(ptr, 8)) {
        Util::logExitReason(ER_READ_START_FAILURE, "Reading header for '%s' failed: Could not read header @ bpos %zu", config->getString("input").c_str(), moreHeader);
        return false;
      }
      if (memcmp(hdr, DTSC::Magic_Header, 4)){
        Util::logExitReason(ER_FORMAT_SPECIFIC, "Reading header for '%s' failed: File does not have a DTSC header @ bpos %zu", config->getString("input").c_str(), moreHeader);
        return false;
      }
      size_t pktLen = Bit::btohl(hdr + 4);
      char *pkt = (char *)malloc(8 + pktLen * sizeof(char));
      F.seek(moreHeader);
      if (F.readExact(pkt, 8 + pktLen)) {
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
    F.seek(thisPos.bytePos);
    if (F.isEOF()) {
      thisPacket.null();
      Util::logExitReason(ER_CLEAN_EOF, "End of file reached");
      return;
    }
    currentPositions.erase(currentPositions.begin());
    lastReadPos = F.getPos();
    char *ptr = buffer;
    if (F.readExact(ptr, 4)) {
      if (F.isEOF()) {
        Util::logExitReason(ER_CLEAN_EOF, "End of file reached while seeking @ %" PRIu64, lastReadPos);
      } else {
        Util::logExitReason(ER_UNKNOWN, "Could not seek to next @ %" PRIu64, lastReadPos);
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
      Util::logExitReason(ER_FORMAT_SPECIFIC, "Invalid packet header @ %#" PRIx64 " - %.4s != %.4s @ %" PRIu64,
                          lastReadPos, buffer, DTSC::Magic_Packet2, lastReadPos);
      thisPacket.null();
      return;
    }
    ptr += 4;
    if (F.readExact(ptr, 4)) {
      Util::logExitReason(ER_FORMAT_SPECIFIC, "Could not read packet size @ %" PRIu64, lastReadPos);
      thisPacket.null();
      return;
    }
    std::string pBuf;
    uint32_t packSize = Bit::btohl(buffer + 4);
    pBuf.resize(8 + packSize);
    memcpy((char *)pBuf.data(), buffer, 8);
    ptr = &pBuf[0] + 8;
    if (F.readExact(ptr, packSize)) {
      Util::logExitReason(ER_FORMAT_SPECIFIC, "Could not read packet @ %" PRIu64, lastReadPos);
      thisPacket.null();
      return;
    }
    thisPacket.reInit(pBuf.data(), pBuf.size());
    thisTime = thisPacket.getTime();
    thisIdx = thisPacket.getTrackId();
    seekNext(thisPos.seekTime, thisPos.trackID);
    F.seek(thisPos.bytePos);
  }

  void InputDTSC::getNextFromStream(size_t idx, bool returnAfterMetaReceived, uint64_t timeLimit) {
    uint64_t endTime = (timeLimit == 0) ? UINT64_MAX : Util::bootMS() + timeLimit;
    bool clearMeta = returnAfterMetaReceived;

    while (config->is_active && srcConn && Util::bootMS() < endTime) {

      pktBuf.truncate(0);
      thisPacket.null();

      // Event-loop until we have a whole header
      if (srcConn && config->is_active && !srcConn.Received().available(8)) {
        evLp.addSocket(4242, srcConn.getSocket());
        while (srcConn && config->is_active && !srcConn.Received().available(8)) {
          size_t ret = evLp.await(10000);
          if (ret == 4242 && srcConn.spool()) { lastRecvTime = Util::bootMS(); }
        }
        evLp.remove(srcConn.getSocket());
      }
      // No log message since the only reasons this could happen is interrupt or disconnect.
      // We log both of those exit reasons elsewhere already
      if (!srcConn.Received().available(8)) { return; }

      // Check if (likely) valid packet header
      if (srcConn.Received().copy(2) != "DT") {
        WARN_MSG("Invalid DTSC Packet header encountered (%s)", Encodings::Hex::encode(srcConn.Received().copy(4)).c_str());
        return;
      }

      // All good so far, let's read the payload size in bytes from the 8-byte header
      size_t paySize = Bit::btohl(srcConn.Received().copy(8).data() + 4);

      if (!pktBuf.allocate(paySize + 8)) {
        FAIL_MSG("Could not allocate packet buffer for size=%zub packet!", paySize + 8);
        return;
      }

      // Read what we have buffered so far
      srcConn.Received().remove(pktBuf, srcConn.Received().bytes(paySize + 8 - pktBuf.size()));

      // Event-loop until we have a whole packet
      if (pktBuf.size() < paySize + 8 && srcConn && config->is_active) {
        evLp.addSocket(4242, srcConn.getSocket());
        while (pktBuf.size() < paySize + 8 && srcConn && config->is_active) {
          size_t ret = evLp.await(10000);
          if (ret == 4242) {
            if (srcConn.spool()) {
              lastRecvTime = Util::bootMS();
              srcConn.Received().remove(pktBuf, srcConn.Received().bytes(paySize + 8 - pktBuf.size()));
            }
          }
        }
        evLp.remove(srcConn.getSocket());
      }

      // No log message since the only reasons this could happen is interrupt or disconnect.
      // We log both of those exit reasons elsewhere already
      if (pktBuf.size() < paySize + 8) { return; }

      // Read success! Initialize thisPacket from our packet buffer
      thisPacket.reInit(pktBuf, pktBuf.size());

      // Handle the packet and return or break based on the returned packet type
      DTSC::packType pType = handlePacket(clearMeta);
      if (pType == DTSC::DTSC_INVALID) { return; }
      if (pType == DTSC::DTSC_HEAD && returnAfterMetaReceived) { return; }
      if ((pType == DTSC::DTSC_V1 || pType == DTSC::DTSC_V2) && !returnAfterMetaReceived) { break; }
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
      tmpPos.bytePos = F.getPos();
    }else{
      tmpPos.seekTime = 0;
      tmpPos.bytePos = 0;
    }
    if (F.isEOF()) {
      F.seek(0);
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
      lastReadPos = F.getPos();
      if (F.isEOF()) {
        WARN_MSG("Reached EOF during seek to %" PRIu64 " in track %zu - aborting @ %" PRIu64, ms, trackIdx, lastReadPos);
        return;
      }
      // Seek to first packet after ms.
      F.seek(tmpPos.bytePos);
      lastReadPos = F.getPos();
      // read the header
      char header[20];
      char *ptr = header;
      if (F.readExact(ptr, 20)) {
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
                   ms, trackIdx, lastReadPos, tmpPos.bytePos);
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

  void InputDTSC::sendCmd(const JSON::Value & data) {
    MEDIUM_MSG("Sending DTCM: %s", data.toString().c_str());
    char sSize[8] = {'D', 'T', 'C', 'M', 0, 0, 0, 0};
    Bit::htobl(sSize + 4, data.packedSize());
    srcConn.SendNow(sSize, 8);
    data.sendTo(srcConn);
  }

  DTSC::packType InputDTSC::handleCommandPacket() {
    bool ignoredResult = false;
    return handleCommandPacket(ignoredResult);
  }

  DTSC::packType InputDTSC::handleCommandPacket(bool & clearMeta) {
    if (thisPacket.getVersion() != DTSC::DTCM) return thisPacket.getVersion();
    std::string cmd;
    thisPacket.getString("cmd", cmd);
    HIGH_MSG("Received DTCM: %s", thisPacket.getScan().asJSON().toString().c_str());
    // "reset" indicates the metadata will be replaced (setting clearMeta also means a next header wipes old tracks)
    if (cmd == "reset") {
      clearMeta = true;
      headerReceived = false;
      return DTSC::DTCM;
    }
    // "error" indicates a fatal error
    if (cmd == "error") {
      thisPacket.getString("msg", cmd);
      Util::logExitReason(ER_FORMAT_SPECIFIC, "%s", cmd.c_str());
      thisPacket.null();
      return DTSC::DTSC_INVALID;
    }
    // "ping" is a request for a "pong"
    if (cmd == "ping") {
      sendCmd(JSONOBJECT({{"cmd", "ok"}, {"msg", "Pong!"}}));
      return DTSC::DTCM;
    }
    // "ok" is an acknowledgement from the remote (e.g. pong for our ping)
    if (cmd == "ok") {
      std::string msg;
      thisPacket.getString("msg", msg);
      if (msg == "Pong!") { gotPong = true; }
      return DTSC::DTCM;
    }
    // "sync" indicates a switch between async/sync modes
    if (cmd == "sync") {
      isSyncReceiver = thisPacket.getInt("sync");
      INFO_MSG("Switching over to receiving in %s mode", isSyncReceiver ? "sync" : "async");
      return DTSC::DTCM;
    }
    // "hi" is the server hello message
    if (cmd == "hi") {
      thisPacket.getString("version", cmd);
      if (cmd.size()) INFO_MSG("Connected to remote server %s version %s", srcConn.getHost().c_str(), cmd.c_str());
      return DTSC::DTCM;
    }
    // "streaminfo" contains the type of stream and the time on the remote
    if (cmd == "streaminfo") {
      isLive = thisPacket.getFlag("live");
      isVod = thisPacket.getFlag("vod");
      thisPacket.getString("uuid", uuid);
      infoReceived = true;
      return DTSC::DTCM;
    }
    // "start_track_media" opens a page to write data to until a stop is received
    if (cmd == "start_track_media") {
      uint64_t trkIdx = M.trackIDToIndex(thisPacket.getInt("id"));
      if (trkIdx == INVALID_TRACK_ID) {
        ERROR_MSG("Cannot find track index for id %" PRIu64, thisPacket.getInt("id"));
        return DTSC::DTCM;
      }

      uint64_t firstKey = thisPacket.getInt("firstkey");
      uint64_t pageIdx = M.getPageIndexForKeyExact(trkIdx, firstKey);
      if (pageIdx == INVALID_KEY_NUM) {
        ERROR_MSG("Cannot find page for track %" PRIu64 " firstkey %" PRIu64, trkIdx, firstKey);
        return DTSC::DTCM;
      }

      uint64_t pageSize = M.pages(trkIdx).getInt("size", pageIdx);
      meta.pages(trkIdx).setInt("avail", 0, pageIdx); // fresh request
      char pageName[NAME_BUFFER_SIZE];
      snprintf(pageName, NAME_BUFFER_SIZE, SHM_TRACK_DATA, streamName.c_str(), trkIdx, (uint32_t)firstKey);

      page.init(pageName, pageSize, true, true, false);
      page.master = false;

      pageFillStart = Util::bootMS();
      pageParts = 0;
      return DTSC::DTCM;
    }
    // "stop_track_media" closes the current page and sets avail to zero if the page was incomplete
    if (cmd == "stop_track_media") {
      uint64_t trkIdx = M.trackIDToIndex(thisPacket.getInt("id"));
      if (trkIdx == INVALID_TRACK_ID) {
        ERROR_MSG("Cannot find track index for id %" PRIu64, thisPacket.getInt("id"));
        return DTSC::DTCM;
      }

      uint64_t firstKey = thisPacket.getInt("firstkey");
      uint64_t pageIdx = M.getPageIndexForKeyExact(trkIdx, firstKey);
      if (pageIdx == INVALID_KEY_NUM) {
        ERROR_MSG("Cannot find page for track %" PRIu64 " firstkey %" PRIu64, trkIdx, firstKey);
        return DTSC::DTCM;
      }

      const uint64_t expectedParts = M.pages(trkIdx).getInt("parts", pageIdx);
      if (isVod && (!pageParts || pageParts < expectedParts)) {
        WARN_MSG("Track %" PRIu64 " page %" PRIu64 " is incomplete (%" PRIu64 "/%" PRIu64 " parts), not caching it",
                 trkIdx, firstKey, pageParts, expectedParts);
        meta.pages(trkIdx).setInt("avail", 0, pageIdx);
        page.master = true; // ensure deletion of the partial page
      } else if (isVod) {
        char pageName[NAME_BUFFER_SIZE];
        snprintf(pageName, NAME_BUFFER_SIZE, SHM_TRACK_DATA, M.getUUID().c_str(), trkIdx, (uint32_t)firstKey);

        if (Cache::cacheWrite(pageName, [&](int fd) {
          Socket::Connection S(Socket::clone{}, fd);
          S.SendNow(page.mapped, meta.pages(trkIdx).getInt("avail", pageIdx));
        }, Util::bootMS() - pageFillStart, streamName)) {
          page.master = true; // ensure deletion of the original
        }
      }

      requested.erase(std::to_string(trkIdx) + '@' + std::to_string(firstKey));
      page.close();
      return DTSC::DTCM;
    }
    // Ignore all other commands
    INFO_MSG("Unhandled command: %s", cmd.c_str());
    return DTSC::DTCM;
  }

  DTSC::packType InputDTSC::handlePacket(bool & clearMeta) {
    DTSC::packType packType = thisPacket.getVersion();
    switch (packType) {
      // Command packet
      case DTSC::DTCM: {
        packType = handleCommandPacket(clearMeta);
        break;
      }
      // Handle metadata update/replacement packet
      case DTSC::DTSC_HEAD: {
        DTSC::Meta nM("", thisPacket.getScan());
        meta.merge(nM, clearMeta, !isLive);
        if (clearMeta) {
          meta.setBootMsOffset(nM.getBootMsOffset());
          meta.setVod(nM.getVod());
          meta.setLive(nM.getLive());
          meta.setUUID(nM.getUUID());
        }
        clearMeta = false;
        headerReceived = true;

        // The remote pushes headers without us asking, so only the first one says how long a header takes to get
        if (!headerGenTime) { headerGenTime = Util::bootMS() - headerRequested; }

        if (isVod) {
          INFO_MSG("Attempting to write header to cache");
          Cache::cacheWrite(config->getString("input") + ".dtsh", [this](int fd) {
            Socket::Connection conn(Socket::clone{}, fd);
            M.send(conn);
          }, headerGenTime, streamName);
        }

        break;
      }
      // Track data packet
      case DTSC::DTSC_V1:
      case DTSC::DTSC_V2: {
        lastPktTime = Util::bootMS();
        thisTime = thisPacket.getTime();
        thisIdx = M.trackIDToIndex(thisPacket.getTrackId());

        if (isLive) {
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
              sendCmd(JSONOBJECT({{"cmd", "check_key_duration"}, {"id", (uint64_t)thisPacket.getTrackId()}, {"duration", longest_key}}));
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
        }

        // Write the data to the relevant page if said page exists
        if (page) {
          char *dataPointer = 0;
          size_t len = 0;
          thisPacket.getString("data", dataPointer, len);
          InOutBase::bufferNext(thisPacket.getTime(), thisPacket.getInt("offset"), thisIdx, dataPointer, len,
                                thisPacket.getInt("bpos"), thisPacket.getFlag("keyframe"), page);
          ++pageParts;
        }
        break;
      }
      // Unknown packet type
      default: {
        WARN_MSG("Unhandled DTSC packet type (%d) encountered, ignoring...", thisPacket.getVersion());
      }
    }
    return packType;
  }

  bool InputDTSC::bufferFrame(size_t idx, uint32_t keyNum) {
    // For non-remotes call the default implementation
    if (sourceUri.protocol != "dtsc" && sourceUri.protocol != "dtscs") return Input::bufferFrame(idx, keyNum);
    if (M.getLive()) { return true; }
    size_t sourceIdx = M.getSourceTrack(idx);
    if (sourceIdx == INVALID_TRACK_ID) { sourceIdx = idx; }

    // Now check whether the remote has the pages available
    Util::RelAccX & tPages = meta.pages(idx);
    DTSC::Keys keys(M.keys(idx));
    uint64_t firstKey = keys.getFirstValid();
    if (keyNum < firstKey) {
      HIGH_MSG("Key %" PRIu32 " on track %zu no longer seekable (earliest requestable key is %" PRIu64
               "). Cancelling buffering.",
               keyNum, idx, firstKey);
      return true;
    }
    uint64_t lastKey = keys.getEndValid();
    if (keyNum > lastKey) {
      if (keyNum > lastKey + 1) {
        WARN_MSG("Key %" PRIu32 " on track %zu is higher than total (latest key is %" PRIu64 "). Cancelling buffering.",
                 keyNum, idx, lastKey);
      }
      return true;
    }
    if (!tPages.getEndPos()) {
      WARN_MSG("No pages for track %zu found! Cancelling bufferFrame", idx);
      return false;
    }
    uint64_t pageIdx = 0;
    for (uint64_t i = tPages.getDeleted(); i < tPages.getEndPos(); i++) {
      if (tPages.getInt("firstkey", i) > keyNum) break;
      pageIdx = i;
    }
    uint32_t pageNumber = tPages.getInt("firstkey", pageIdx);
    pageCounter[idx][pageNumber] = Util::bootSecs();

    // Check whether the requested page is already loaded into memory
    if (isBuffered(idx, pageNumber, meta)) {
      // Mark the page as still actively requested
      DONTEVEN_MSG("Track %zu, key %" PRIu32 " is already buffered in page %" PRIu32 ". Cancelling bufferFrame", idx,
                   keyNum, pageNumber);
      return true;
    }

    // Not already loaded or cached? Return if we already requested from the remote in the past 15 seconds, it's probably in-flight
    std::string trkAtPage = std::to_string(idx) + '@' + std::to_string(pageNumber);
    if (requested.count(trkAtPage) && Util::bootMS() < requested[trkAtPage] + 15000) return true;
    requested[trkAtPage] = Util::bootMS();

    // Perform request
    sendCmd(JSONOBJECT({{"cmd", "get_track_media"}, {"id", M.getID(idx)}, {"page", pageNumber}}));
    return true;
  }
}// namespace Mist
