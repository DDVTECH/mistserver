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
#include "input_ts.h"

#include <mist/tinythread.h>
#include <sys/stat.h>

#define SEM_TS_CLAIM "/MstTSIN%s"

std::string globalStreamName;
TS::Stream liveStream(true);
Util::Config * cfgPointer = NULL;

#define THREAD_TIMEOUT 15
std::map<unsigned long long, unsigned long long> threadTimer;

std::set<unsigned long> claimableThreads;

void parseThread(void * ignored) {

  char semName[NAME_BUFFER_SIZE];
  snprintf(semName, NAME_BUFFER_SIZE, SEM_TS_CLAIM, globalStreamName.c_str());
  IPC::semaphore lock(semName, O_CREAT | O_RDWR, ACCESSPERMS, 1);

  int tid = -1;
  lock.wait();
  if (claimableThreads.size()) {
    tid = *claimableThreads.begin();
    claimableThreads.erase(claimableThreads.begin());
  }
  lock.post();
  if (tid == -1) {
    return;
  }

  if (liveStream.isDataTrack(tid)){
    if (!Util::startInput(globalStreamName)) {
      return;
    }
  }

  Mist::negotiationProxy myProxy;
  myProxy.streamName = globalStreamName;
  DTSC::Meta myMeta;

  if (liveStream.isDataTrack(tid)){
    char userPageName[NAME_BUFFER_SIZE];
    snprintf(userPageName, NAME_BUFFER_SIZE, SHM_USERS, globalStreamName.c_str());
    myProxy.userClient = IPC::sharedClient(userPageName, PLAY_EX_SIZE, true);
  }


  threadTimer[tid] = Util::bootSecs();
  while (Util::bootSecs() - threadTimer[tid] < THREAD_TIMEOUT && cfgPointer->is_active) {
    liveStream.parse(tid);
    if (liveStream.hasPacket(tid)){
      liveStream.initializeMetadata(myMeta, tid);
      DTSC::Packet pack;
      liveStream.getPacket(tid, pack);
      if (pack && myMeta.tracks.count(tid)){
        myProxy.continueNegotiate(tid, myMeta, true);
        myProxy.bufferLivePacket(pack, myMeta);
      }

      lock.wait();
      threadTimer[tid] = Util::bootSecs();
      lock.post();
    }
    if (!liveStream.hasPacket(tid)){
      if (liveStream.isDataTrack(tid)){
        myProxy.userClient.keepAlive();
      }
      Util::sleep(100);
    }
  }
  lock.wait();
  threadTimer.erase(tid);
  lock.post();
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
    //These two can/may be set to always-on mode
    capa["always_match"].append("stream://*.ts");
    capa["always_match"].append("tsudp://*");
    capa["priority"] = 9ll;
    capa["codecs"][0u][0u].append("H264");
    capa["codecs"][0u][0u].append("HEVC");
    capa["codecs"][0u][1u].append("AAC");
    capa["codecs"][0u][1u].append("AC3");
    inFile = NULL;
  }

  inputTS::~inputTS() {
    if (inFile) {
      fclose(inFile);
    }
    if (!standAlone){
      char semName[NAME_BUFFER_SIZE];
      snprintf(semName, NAME_BUFFER_SIZE, SEM_TS_CLAIM, globalStreamName.c_str());
      IPC::semaphore lock(semName, O_CREAT | O_RDWR, ACCESSPERMS, 1);
      lock.wait();
      threadTimer.clear();
      claimableThreads.clear();
      lock.post();
      lock.unlink();
    }
  }


  ///Live Setup of TS Input
  bool inputTS::setup() {
    const std::string & inpt = config->getString("input");
    //streamed standard input
    if (inpt == "-") {
      standAlone = false;
      inFile = stdin;
      return true;
    }
    //streamed file
    if (inpt.substr(0,9) == "stream://"){
      inFile = fopen(inpt.c_str()+9, "r");
      standAlone = false;
      return inFile;
    }
    //UDP input (tsudp://[host:]port[/iface[,iface[,...]]])
    if (inpt.substr(0, 8) == "tsudp://"){
      HTTP::URL input_url(inpt);
      standAlone = false;
      udpCon.setBlocking(false);
      udpCon.bind(input_url.getPort(), input_url.host, input_url.path);
      return udpCon.getSock() != -1;
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


  ///Reads headers from a TS stream, and saves them into metadata
  ///It works by going through the entire TS stream, and every time
  ///It encounters a new PES start, it writes the currently found PES data
  ///for a specific track to metadata. After the entire stream has been read,
  ///it writes the remaining metadata.
  ///\todo Find errors, perhaps parts can be made more modular
  bool inputTS::readHeader() {
    if (!standAlone){return true;}
    if (!inFile){return false;}
    //See whether a separate header file exists.
    if (readExistingHeader()){return true;}

    TS::Packet packet;//to analyse and extract data
    fseek(inFile, 0, SEEK_SET);//seek to beginning

    long long int lastBpos = 0;
    while (packet.FromFile(inFile) && !feof(inFile)) {
      tsStream.parse(packet, lastBpos);
      lastBpos = ftell(inFile);
      while (tsStream.hasPacketOnEachTrack()) {
        DTSC::Packet headerPack;
        tsStream.getEarliestPacket(headerPack);
        if (!myMeta.tracks.count(headerPack.getTrackId()) || !myMeta.tracks[headerPack.getTrackId()].codec.size()) {
          tsStream.initializeMetadata(myMeta, headerPack.getTrackId());
        }
        myMeta.update(headerPack);
      }

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
    while (!hasPacket && !feof(inFile) && config->is_active) {
      unsigned int bPos = ftell(inFile);
      tsBuf.FromFile(inFile);
      if (selectedTracks.count(tsBuf.getPID())) {
        tsStream.parse(tsBuf, bPos);
      }
      hasPacket = (selectedTracks.size() == 1 ? tsStream.hasPacket(*selectedTracks.begin()) : tsStream.hasPacketOnEachTrack());
    }
    if (!hasPacket) {
      if (!feof(inFile)) {
        getNext();
      }
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
    int bpos = ftell(inFile);
    if (fseek(inFile, 0, SEEK_SET)) {
      FAIL_MSG("Seek to 0 failed");
      return;
    }

    TS::Packet tsBuffer;
    while (!tsStream.hasPacketOnEachTrack() && tsBuffer.FromFile(inFile)) {
      tsStream.parse(tsBuffer, 0);
    }

    //Clear leaves the PMT in place
    tsStream.clear();


    //Restore original file position
    if (fseek(inFile, bpos, SEEK_SET)) {
      return;
    }

  }

  ///Seeks to a specific time
  void inputTS::seek(int seekTime) {
    tsStream.clear();
    readPMT();
    unsigned long seekPos = 0xFFFFFFFFull;
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
    fseek(inFile, seekPos, SEEK_SET);//seek to the correct position
  }

  void inputTS::stream() {
    if (!Util::startInput(streamName, "push://")) {//manually override stream url to start the buffer
      FAIL_MSG("Could not start buffer for %s", streamName.c_str());
      return;
    }
    IPC::sharedClient statsPage = IPC::sharedClient(SHM_STATISTICS, STAT_EX_SIZE, true);
    uint64_t downCounter = 0;
    uint64_t startTime = Util::epoch();
    cfgPointer = config;
    globalStreamName = streamName;
    unsigned long long threadCheckTimer = Util::bootSecs();
    while (config->is_active) {
      if (inFile) {
        if (feof(inFile)){
          config->is_active = false;
          INFO_MSG("Reached end of file on streamed input");
        }
        int ctr = 0;
        while (ctr < 20 && tsBuf.FromFile(inFile) && !feof(inFile)){
          liveStream.add(tsBuf);
          downCounter += 188;
          ctr++;
        }
      } else {
        std::string leftData;
        while (udpCon.Receive()) {
          int offset = 0;
          //Try to read full TS Packets
          //Watch out! We push here to a global, in order for threads to be able to access it.
          while (offset < udpCon.data_len) {
            if (udpCon.data[0] == 0x47){//check for sync byte
              if (offset + 188 <= udpCon.data_len){
                liveStream.add(udpCon.data + offset);
                downCounter += 188;
              }else{
                leftData.append(udpCon.data + offset, udpCon.data_len - offset);
              }
              offset += 188;
            }else{
              if (leftData.size()){
                leftData.append(udpCon.data + offset, 1);
                if (leftData.size() >= 188){
                  liveStream.add((char*)leftData.data());
                  downCounter += 188;
                  leftData.erase(0, 188);
                }
              }
              ++offset;
            }
          }
        }
      }
      //Check for and spawn threads here.
      if (Util::bootSecs() - threadCheckTimer > 2) {
        //Connect to stats for INPUT detection
        uint64_t now = Util::epoch();
        if (!statsPage.getData()){
          statsPage = IPC::sharedClient(SHM_STATISTICS, STAT_EX_SIZE, true);
        }
        if (statsPage.getData()){
          if (!statsPage.isAlive()){
            config->is_active = false;
            return;
          }
          IPC::statExchange tmpEx(statsPage.getData());
          tmpEx.now(now);
          tmpEx.crc(getpid());
          tmpEx.streamName(streamName);
          tmpEx.connector("INPUT");
          tmpEx.up(0);
          tmpEx.down(downCounter);
          tmpEx.time(now - startTime);
          tmpEx.lastSecond(0);
          statsPage.keepAlive();
        }

        std::set<unsigned long> activeTracks = liveStream.getActiveTracks();
        char semName[NAME_BUFFER_SIZE];
        snprintf(semName, NAME_BUFFER_SIZE, SEM_TS_CLAIM, globalStreamName.c_str());
        IPC::semaphore lock(semName, O_CREAT | O_RDWR, ACCESSPERMS, 1);
        lock.wait();
        for (std::set<unsigned long>::iterator it = activeTracks.begin(); it != activeTracks.end(); it++) {
          if (threadTimer.count(*it) && ((Util::bootSecs() - threadTimer[*it]) > (2 * THREAD_TIMEOUT))) {
            WARN_MSG("Thread for track %d timed out %d seconds ago without a clean shutdown.", *it, Util::bootSecs() - threadTimer[*it]);
            threadTimer.erase(*it);
          }
          if (!threadTimer.count(*it)) {

            //Add to list of unclaimed threads
            claimableThreads.insert(*it);

            //Spawn thread here.
            tthread::thread thisThread(parseThread, 0);
            thisThread.detach();
          }
        }
        lock.post();
        threadCheckTimer = Util::bootSecs();
      }
      if (!inFile){
        Util::sleep(100);
      }
    }
    finish();
    INFO_MSG("Input for stream %s closing clean", streamName.c_str());
  }

  void inputTS::finish() {
    if (standAlone){
      Input::finish();
      return;
    }
    char semName[NAME_BUFFER_SIZE];
    snprintf(semName, NAME_BUFFER_SIZE, SEM_TS_CLAIM, globalStreamName.c_str());
    IPC::semaphore lock(semName, O_CREAT | O_RDWR, ACCESSPERMS, 1);


    int threadCount = 0;
    do {
      lock.wait();
      threadCount = threadTimer.size();
      lock.post();
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
    if (inpt.size() && inpt != "-" && inpt.substr(0,9) != "stream://" && inpt.substr(0,8) != "tsudp://"){
      return true;
    }else{
      return false;
    }
  }

}

