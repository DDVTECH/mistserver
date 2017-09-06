#include <mist/util.h>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <cstring>
#include <cerrno>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <mist/stream.h>
#include <mist/flv_tag.h>
#include <mist/defines.h>
#include <mist/ts_packet.h>
#include <mist/timing.h>
#include <mist/mp4_generic.h>
#include <mist/http_parser.h>
#include <mist/downloader.h>
#include "input_ts.h"

#include <mist/tinythread.h>
#include <mist/procs.h>
#include <sys/stat.h>

tthread::mutex threadClaimMutex;
std::string globalStreamName;
TS::Stream liveStream(true);
Util::Config * cfgPointer = NULL;

#define THREAD_TIMEOUT 15
std::map<unsigned long long, unsigned long long> threadTimer;

std::set<unsigned long> claimableThreads;

void parseThread(void * ignored) {

  int tid = -1;
  {
    tthread::lock_guard<tthread::mutex> guard(threadClaimMutex);
    if (claimableThreads.size()) {
      tid = *claimableThreads.begin();
      claimableThreads.erase(claimableThreads.begin());
    }
    if (tid == -1) {
      return;
    }
  }

  Mist::negotiationProxy myProxy;
  myProxy.streamName = globalStreamName;
  DTSC::Meta myMeta;

  if (liveStream.isDataTrack(tid)){
    if (!Util::streamAlive(globalStreamName) && !Util::startInput(globalStreamName, "push://INTERNAL_ONLY:"+cfgPointer->getString("input"), true, true)) {
      FAIL_MSG("Could not start buffer for %s", globalStreamName.c_str());
      return;
    }

    char userPageName[NAME_BUFFER_SIZE];
    snprintf(userPageName, NAME_BUFFER_SIZE, SHM_USERS, globalStreamName.c_str());
    myProxy.userClient = IPC::sharedClient(userPageName, PLAY_EX_SIZE, true);
    myProxy.userClient.countAsViewer = false;
  }

  threadTimer[tid] = Util::bootSecs();
  while (Util::bootSecs() - threadTimer[tid] < THREAD_TIMEOUT && cfgPointer->is_active && (!liveStream.isDataTrack(tid) || myProxy.userClient.isAlive())) {
    liveStream.parse(tid);
    if (!liveStream.hasPacket(tid)){
      if (liveStream.isDataTrack(tid)){
        myProxy.userClient.keepAlive();
      }
      Util::sleep(100);
      continue;
    }
    while (liveStream.hasPacket(tid)){
      liveStream.initializeMetadata(myMeta, tid);
      DTSC::Packet pack;
      liveStream.getPacket(tid, pack);
      if (pack && myMeta.tracks.count(tid)){
        myProxy.continueNegotiate(tid, myMeta, true);
        myProxy.bufferLivePacket(pack, myMeta);
      }
    }
    {
      tthread::lock_guard<tthread::mutex> guard(threadClaimMutex);
      threadTimer[tid] = Util::bootSecs();
    }
  }
  std::string reason = "unknown reason";
  if (!(Util::bootSecs() - threadTimer[tid] < THREAD_TIMEOUT)){reason = "thread timeout";}
  if (!cfgPointer->is_active){reason = "input shutting down";}
  if (!(!liveStream.isDataTrack(tid) || myProxy.userClient.isAlive())){
    reason = "buffer disconnect";
    cfgPointer->is_active = false;
  }
  INFO_MSG("Shutting down thread for %d because %s", tid, reason.c_str());
  {
    tthread::lock_guard<tthread::mutex> guard(threadClaimMutex);
    threadTimer.erase(tid);
  }
  liveStream.eraseTrack(tid);
  myProxy.userClient.finish();
}

namespace Mist {

  /// Constructor of TS Input
  /// \arg cfg Util::Config that contains all current configurations.
  inputTS::inputTS(Util::Config * cfg) : Input(cfg) {
    capa["name"] = "TS";
    capa["decs"] = "MPEG2-TS input from static files, streamed files, or multicast/unicast UDP socket";
    capa["source_match"].append("/*.ts");
    capa["source_match"].append("stream://*.ts");
    capa["source_match"].append("tsudp://*");
    capa["source_match"].append("ts-exec:*");
    capa["source_match"].append("http://*.ts");
    capa["source_match"].append("http-ts://*");
    //These can/may be set to always-on mode
    capa["always_match"].append("stream://*.ts");
    capa["always_match"].append("tsudp://*");
    capa["always_match"].append("ts-exec:*");
    capa["always_match"].append("http://*.ts");
    capa["always_match"].append("http-ts://*");
    capa["priority"] = 9ll;
    capa["codecs"][0u][0u].append("H264");
    capa["codecs"][0u][0u].append("HEVC");
    capa["codecs"][0u][0u].append("MPEG2");
    capa["codecs"][0u][1u].append("AAC");
    capa["codecs"][0u][1u].append("AC3");
    capa["codecs"][0u][1u].append("MP2");
    inFile = NULL;
    inputProcess = 0;

    JSON::Value option;
    option["arg"] = "integer";
    option["long"] = "buffer";
    option["short"] = "b";
    option["help"] = "DVR buffer time in ms";
    option["value"].append(50000LL);
    config->addOption("bufferTime", option);
    capa["optional"]["DVR"]["name"] = "Buffer time (ms)";
    capa["optional"]["DVR"]["help"] = "The target available buffer time for this live stream, in milliseconds. This is the time available to seek around in, and will automatically be extended to fit whole keyframes as well as the minimum duration needed for stable playback.";
    capa["optional"]["DVR"]["option"] = "--buffer";
    capa["optional"]["DVR"]["type"] = "uint";
    capa["optional"]["DVR"]["default"] = 50000LL;
  }

  inputTS::~inputTS() {
    if (inFile) {
      fclose(inFile);
    }
    if (tcpCon){
      tcpCon.close();
    }
    if (!standAlone){
      tthread::lock_guard<tthread::mutex> guard(threadClaimMutex);
      threadTimer.clear();
      claimableThreads.clear();
    }
  }

  ///Live Setup of TS Input
  bool inputTS::preRun() {
    const std::string & inpt = config->getString("input");
    //streamed standard input
    if (inpt == "-") {
      standAlone = false;
      tcpCon = Socket::Connection(fileno(stdout), fileno(stdin));
      return true;
    }
    if (inpt.substr(0, 7) == "http://" || inpt.substr(0, 10) == "http-ts://"){
      standAlone = false;
      HTTP::URL url(inpt);
      url.protocol = "http";
      HTTP::Downloader DL;
      DL.getHTTP().headerOnly = true;
      if (!DL.get(url)){
        return false;
      }
      tcpCon = DL.getSocket();
      return true;
    }
    if (inpt.substr(0, 8) == "ts-exec:") {
      standAlone = false;
      std::string input = inpt.substr(8);
      char *args[128];
      uint8_t argCnt = 0;
      char *startCh = 0;
      for (char *i = (char*)input.c_str(); i <= input.data() + input.size(); ++i){
        if (!*i){
          if (startCh){args[argCnt++] = startCh;}
          break;
        }
        if (*i == ' '){
          if (startCh){
            args[argCnt++] = startCh;
            startCh = 0;
            *i = 0;
          }
        }else{
          if (!startCh){startCh = i;}
        }
      }
      args[argCnt] = 0;

      int fin = -1, fout = -1, ferr = -1;
      inputProcess = Util::Procs::StartPiped(args, &fin, &fout, &ferr);
      tcpCon = Socket::Connection(-1, fout);
      return true;
    }
    //streamed file
    if (inpt.substr(0,9) == "stream://"){
      inFile = fopen(inpt.c_str()+9, "r");
      tcpCon = Socket::Connection(-1, fileno(inFile));
      standAlone = false;
      return inFile;
    }
    //UDP input (tsudp://[host:]port[/iface[,iface[,...]]])
    if (inpt.substr(0, 8) == "tsudp://"){
      standAlone = false;
      return true;
    }
    //plain VoD file
    inFile = fopen(inpt.c_str(), "r");
    return inFile;
  }


  ///Track selector of TS Input
  ///\arg trackSpec specifies which tracks  are to be selected
  ///\todo test whether selecting a subset of tracks work
  void inputTS::trackSelect(std::string trackSpec) {
    selectedTracks.clear();
    long long int index;
    while (trackSpec != "") {
      index = trackSpec.find(' ');
      selectedTracks.insert(atoi(trackSpec.substr(0, index).c_str()));
      if (index != std::string::npos) {
        trackSpec.erase(0, index + 1);
      } else {
        trackSpec = "";
      }
    }
  }


  bool inputTS::needHeader(){
    if (!standAlone){return false;}
    return Input::needHeader();
  }

  ///Reads headers from a TS stream, and saves them into metadata
  ///It works by going through the entire TS stream, and every time
  ///It encounters a new PES start, it writes the currently found PES data
  ///for a specific track to metadata. After the entire stream has been read,
  ///it writes the remaining metadata.
  ///\todo Find errors, perhaps parts can be made more modular
  bool inputTS::readHeader() {
    if (!inFile){return false;}
    TS::Packet packet;//to analyse and extract data
    DTSC::Packet headerPack;
    fseek(inFile, 0, SEEK_SET);//seek to beginning

    uint64_t lastBpos = 0;
    while (packet.FromFile(inFile) && !feof(inFile)) {
      tsStream.parse(packet, lastBpos);
      lastBpos = Util::ftell(inFile);
      if (packet.getUnitStart()){
        while (tsStream.hasPacketOnEachTrack()) {
          tsStream.getEarliestPacket(headerPack);
          if (!myMeta.tracks.count(headerPack.getTrackId()) || !myMeta.tracks[headerPack.getTrackId()].codec.size()) {
            tsStream.initializeMetadata(myMeta, headerPack.getTrackId());
          }
          myMeta.update(headerPack);
        }
      }
    }
    tsStream.finish();
    INFO_MSG("Reached %s at %llu bytes", feof(inFile)?"EOF":"error", lastBpos);
    while (tsStream.hasPacket()) {
      tsStream.getEarliestPacket(headerPack);
      if (!myMeta.tracks.count(headerPack.getTrackId()) || !myMeta.tracks[headerPack.getTrackId()].codec.size()) {
        tsStream.initializeMetadata(myMeta, headerPack.getTrackId());
      }
      myMeta.update(headerPack);
    }
    
    fseek(inFile, 0, SEEK_SET);
    myMeta.toFile(config->getString("input") + ".dtsh");
    return true;
  }

  ///Gets the next packet that is to be sent
  ///At the moment, the logic of sending the last packet that was finished has been implemented,
  ///but the seeking and finding data is not yet ready.
  ///\todo Finish the implementation
  void inputTS::getNext(bool smart) {
    INSANE_MSG("Getting next");
    thisPacket.null();
    bool hasPacket = (selectedTracks.size() == 1 ? tsStream.hasPacket(*selectedTracks.begin()) : tsStream.hasPacketOnEachTrack());
    while (!hasPacket && !feof(inFile) && (inputProcess == 0 || Util::Procs::childRunning(inputProcess)) && config->is_active) {
      tsBuf.FromFile(inFile);
      if (selectedTracks.count(tsBuf.getPID())) {
        tsStream.parse(tsBuf, 0);//bPos == 0
        if (tsBuf.getUnitStart()){
          hasPacket = (selectedTracks.size() == 1 ? tsStream.hasPacket(*selectedTracks.begin()) : tsStream.hasPacketOnEachTrack());
        }
      }
    }
    if (!hasPacket) {
      return;
    }
    if (selectedTracks.size() == 1) {
      tsStream.getPacket(*selectedTracks.begin(), thisPacket);
    } else {
      tsStream.getEarliestPacket(thisPacket);
    }
    if (!thisPacket){
      FAIL_MSG("Could not getNext TS packet!");
      return;
    }
    tsStream.initializeMetadata(myMeta);
    if (!myMeta.tracks.count(thisPacket.getTrackId())) {
      getNext();
    }
  }

  void inputTS::readPMT() {
    //save current file position
    uint64_t bpos = Util::ftell(inFile);
    if (fseek(inFile, 0, SEEK_SET)) {
      FAIL_MSG("Seek to 0 failed");
      return;
    }

    TS::Packet tsBuffer;
    while (!tsStream.hasPacketOnEachTrack() && tsBuffer.FromFile(inFile)) {
      tsStream.parse(tsBuffer, 0);
    }

    //Clear leaves the PMT in place
    tsStream.partialClear();

    //Restore original file position
    if (Util::fseek(inFile, bpos, SEEK_SET)) {
      return;
    }
  }

  ///Seeks to a specific time
  void inputTS::seek(int seekTime) {
    tsStream.clear();
    readPMT();
    uint64_t seekPos = 0xFFFFFFFFFFFFFFFFull;
    for (std::set<unsigned long>::iterator it = selectedTracks.begin(); it != selectedTracks.end(); it++) {
      unsigned long thisBPos = 0;
      for (std::deque<DTSC::Key>::iterator keyIt = myMeta.tracks[*it].keys.begin(); keyIt != myMeta.tracks[*it].keys.end(); keyIt++) {
        if (keyIt->getTime() > seekTime) {
          break;
        }
        thisBPos = keyIt->getBpos();
      }
      if (thisBPos < seekPos) {
        seekPos = thisBPos;
      }
    }
    Util::fseek(inFile, seekPos, SEEK_SET);//seek to the correct position
  }

  void inputTS::stream() {
    IPC::semaphore pullLock;
    pullLock.open(std::string("/MstPull_" + streamName).c_str(), O_CREAT | O_RDWR, ACCESSPERMS, 1);
    if (!pullLock){
      FAIL_MSG("Could not open pull lock for stream '%s' - aborting!", streamName.c_str());
      return;
    }
    if (!pullLock.tryWait()){
      WARN_MSG("A pull process for stream %s is already running", streamName.c_str());
      pullLock.close();
      return;
    }
    const std::string & inpt = config->getString("input");
    if (inpt.substr(0, 8) == "tsudp://"){
      HTTP::URL input_url(inpt);
      udpCon.setBlocking(false);
      udpCon.bind(input_url.getPort(), input_url.host, input_url.path);
      if (udpCon.getSock() == -1){
        FAIL_MSG("Could not open UDP socket. Aborting.");
        pullLock.post();
        pullLock.close();
        pullLock.unlink();
        return;
      }
    }
    IPC::sharedClient statsPage = IPC::sharedClient(SHM_STATISTICS, STAT_EX_SIZE, true);
    uint64_t downCounter = 0;
    uint64_t startTime = Util::epoch();
    uint64_t noDataSince = Util::bootSecs();
    bool gettingData = false;
    bool hasStarted = false;
    cfgPointer = config;
    globalStreamName = streamName;
    unsigned long long threadCheckTimer = Util::bootSecs();
    while (config->is_active) {
      if (tcpCon) {
        if (tcpCon.spool()){
          while (tcpCon.Received().available(188)){
            while (tcpCon.Received().get()[0] != 0x47 && tcpCon.Received().available(188)){
              tcpCon.Received().remove(1);
            }
            if (tcpCon.Received().available(188) && tcpCon.Received().get()[0] == 0x47){
              std::string newData = tcpCon.Received().remove(188);
              tsBuf.FromPointer(newData.data());
              liveStream.add(tsBuf);
              if (!liveStream.isDataTrack(tsBuf.getPID())){
                liveStream.parse(tsBuf.getPID());
              }
            }
          }
          noDataSince = Util::bootSecs();
        }else{
          Util::sleep(100);
        }
        if (!tcpCon){
          config->is_active = false;
          INFO_MSG("End of streamed input");
        }
      } else {
        std::string leftData;
        bool received = false;
        while (udpCon.Receive()) {
          downCounter += udpCon.data_len;
          received = true;
          if (!gettingData){
            gettingData = true;
            INFO_MSG("Now receiving UDP data...");
          }
          int offset = 0;
          //Try to read full TS Packets
          //Watch out! We push here to a global, in order for threads to be able to access it.
          while (offset < udpCon.data_len) {
            if (udpCon.data[offset] == 0x47){//check for sync byte
              if (offset + 188 <= udpCon.data_len){
                tsBuf.FromPointer(udpCon.data + offset);
                liveStream.add(tsBuf);
                if (!liveStream.isDataTrack(tsBuf.getPID())){
                  liveStream.parse(tsBuf.getPID());
                }
                leftData.clear();
              }else{
                leftData.append(udpCon.data + offset, udpCon.data_len - offset);
              }
              offset += 188;
            }else{
              uint32_t maxBytes = std::min((uint32_t)(188 - leftData.size()), (uint32_t)(udpCon.data_len - offset));
              uint32_t numBytes = maxBytes;
              VERYHIGH_MSG("%lu bytes of non-sync-byte data received", numBytes);
              if (leftData.size()){
                leftData.append(udpCon.data + offset, numBytes);
                while (leftData.size() >= 188){
                  VERYHIGH_MSG("Assembled scrap packet");
                  tsBuf.FromPointer((char*)leftData.data());
                  liveStream.add(tsBuf);
                  if (!liveStream.isDataTrack(tsBuf.getPID())){
                    liveStream.parse(tsBuf.getPID());
                  }
                  leftData.erase(0, 188);
                }
              }
              offset += numBytes;
            }
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
      //Check for and spawn threads here.
      if (Util::bootSecs() - threadCheckTimer > 1) {
        //Connect to stats for INPUT detection
        uint64_t now = Util::epoch();
        if (!statsPage.getData()){
          statsPage = IPC::sharedClient(SHM_STATISTICS, STAT_EX_SIZE, true);
        }
        if (statsPage.getData()){
          if (!statsPage.isAlive()){
            config->is_active = false;
            pullLock.post();
            pullLock.close();
            pullLock.unlink();
            return;
          }
          IPC::statExchange tmpEx(statsPage.getData());
          tmpEx.now(now);
          tmpEx.crc(getpid());
          tmpEx.streamName(streamName);
          tmpEx.connector("INPUT");
          tmpEx.up(0);
          tmpEx.down(downCounter + tcpCon.dataDown());
          tmpEx.time(now - startTime);
          tmpEx.lastSecond(0);
          statsPage.keepAlive();
        }

        std::set<unsigned long> activeTracks = liveStream.getActiveTracks();
        {
          tthread::lock_guard<tthread::mutex> guard(threadClaimMutex);
          if (hasStarted && !threadTimer.size()){
            if (!isAlwaysOn()){
              INFO_MSG("Shutting down because no active threads and we had input in the past");
              config->is_active = false;
            }else{
              hasStarted = false;
            }
          }
          for (std::set<unsigned long>::iterator it = activeTracks.begin(); it != activeTracks.end(); it++) {
            if (!liveStream.isDataTrack(*it)){continue;}
            if (threadTimer.count(*it) && ((Util::bootSecs() - threadTimer[*it]) > (2 * THREAD_TIMEOUT))) {
              WARN_MSG("Thread for track %d timed out %d seconds ago without a clean shutdown.", *it, Util::bootSecs() - threadTimer[*it]);
              threadTimer.erase(*it);
            }
            if (!hasStarted){
              hasStarted = true;
            }
            if (!threadTimer.count(*it)) {

              //Add to list of unclaimed threads
              claimableThreads.insert(*it);

              //Spawn thread here.
              tthread::thread thisThread(parseThread, 0);
              thisThread.detach();
            }
          }
        }
        threadCheckTimer = Util::bootSecs();
      }
      if (Util::bootSecs() - noDataSince > 20){
        if (!isAlwaysOn()){
          WARN_MSG("No packets received for 20 seconds - terminating");
          config->is_active = false;
        }else{
          noDataSince = Util::bootSecs();
        }
      }
    }
    finish();
    pullLock.post();
    pullLock.close();
    pullLock.unlink();
    INFO_MSG("Input for stream %s closing clean", streamName.c_str());
  }

  void inputTS::finish() {
    if (standAlone){
      Input::finish();
      return;
    }
    int threadCount = 0;
    do {
      {
        tthread::lock_guard<tthread::mutex> guard(threadClaimMutex);
        threadCount = threadTimer.size();
      }
      if (threadCount){
        Util::sleep(100);
      }
    } while (threadCount);
  }

  bool inputTS::needsLock() {
    //we already know no lock will be needed
    if (!standAlone){return false;}
    //otherwise, check input param
    const std::string & inpt = config->getString("input");
    if (inpt.size() && inpt != "-" && inpt.substr(0,9) != "stream://" && inpt.substr(0,8) != "tsudp://" && inpt.substr(0, 8) != "ts-exec:" && inpt.substr(0, 7) != "http://" && inpt.substr(0, 10) != "http-ts://"){
      return true;
    }else{
      return false;
    }
  }

}

