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
#include "input_ts.h"

#include <mist/tinythread.h>
#include <sys/stat.h>

#define SEM_TS_CLAIM "/MstTSIN%s"


/// \todo Implement this trigger equivalent...
/*
if(Triggers::shouldTrigger("STREAM_PUSH", smp)){
  std::string payload = streamName+"\n" + myConn.getHost() +"\n"+capa["name"].asStringRef()+"\n"+reqUrl;
  if (!Triggers::doTrigger("STREAM_PUSH", payload, smp)){
    DEBUG_MSG(DLVL_FAIL, "Push from %s to %s rejected - STREAM_PUSH trigger denied the push", myConn.getHost().c_str(), streamName.c_str());
    myConn.close();
    configLock.post();
    configLock.close();
    return;
  }
}
*/

#ifdef TSLIVE_INPUT
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
    if (liveStream.isDataTrack(tid)){
      myProxy.userClient.keepAlive();
    }
    if (!liveStream.hasPacket(tid)){
      Util::sleep(100);
    }
  }
  lock.wait();
  threadTimer.erase(tid);
  lock.post();
  liveStream.eraseTrack(tid);
  myProxy.userClient.finish();
}

#endif

namespace Mist {

  /// Constructor of TS Input
  /// \arg cfg Util::Config that contains all current configurations.
  inputTS::inputTS(Util::Config * cfg) : Input(cfg) {
    capa["name"] = "TS";
    capa["decs"] = "Enables TS Input";
    capa["source_match"] = "/*.ts";
    capa["priority"] = 9ll;
    capa["codecs"][0u][0u].append("H264");
    capa["codecs"][0u][0u].append("HEVC");
    capa["codecs"][0u][1u].append("AAC");
    capa["codecs"][0u][1u].append("AC3");

    capa["optional"]["port"]["name"] = "UDP Port";
    capa["optional"]["port"]["help"] = "The UDP port on which to listen for incoming UDP Packets, optionally prefixed by the interface IP.";
    capa["optional"]["port"]["type"] = "string";
    capa["optional"]["port"]["default"] = "9876";
    capa["optional"]["port"]["option"] = "--port";
    cfg->addOption("port",
                   JSON::fromString("{\"arg\":\"string\",\"value\":9876,\"short\":\"p\",\"long\":\"port\",\"help\":\"The UDP port on which to listen for incoming UDP Packets, optionally prefixed by the interface IP.\"}"));

    capa["optional"]["multicastinterface"]["name"] = "TS Multicast interface";
    capa["optional"]["multicastinterface"]["help"] = "The interface(s) on which to listen for UDP Multicast packets, comma separated.";
    capa["optional"]["multicastinterface"]["option"] = "--multicast-interface";
    capa["optional"]["multicastinterface"]["type"] = "str";
    capa["optional"]["multicastinterface"]["default"] = "";
    cfg->addOption("multicastinterface",
                   JSON::fromString("{\"arg\":\"string\",\"value\":\"\",\"short\":\"M\",\"long\":\"multicast-interface\",\"help\":\"The interfaces on which to listen for UDP Multicast packets, space separatered.\"}"));

    pushing = false;
    inFile = NULL;

#ifdef TSLIVE_INPUT
    standAlone = false;
#endif
  }

  inputTS::~inputTS() {
    if (inFile) {
      fclose(inFile);
    }
#ifdef TSLIVE_INPUT
    char semName[NAME_BUFFER_SIZE];
    snprintf(semName, NAME_BUFFER_SIZE, SEM_TS_CLAIM, globalStreamName.c_str());
    IPC::semaphore lock(semName, O_CREAT | O_RDWR, ACCESSPERMS, 1);
    lock.wait();
    threadTimer.clear();
    claimableThreads.clear();
    lock.post();
    lock.unlink();
#endif
  }

#ifdef TSLIVE_INPUT

  ///Live Setup of TS Input
  bool inputTS::setup() {
    INFO_MSG("Setup start");
    if (config->getString("input") == "-") {
      inFile = stdin;
    } else {
      pushing = true;
      udpCon.setBlocking(false);
      std::string ipPort = config->getString("port");
      size_t colon = ipPort.rfind(':');
      if (colon != std::string::npos) {
        udpCon.bind(JSON::Value(ipPort.substr(colon + 1)).asInt(), ipPort.substr(0, colon), config->getString("multicastinterface"));
      } else {
        udpCon.bind(JSON::Value(ipPort).asInt(), "", config->getString("multicastinterface"));
      }
    }
    INFO_MSG("Setup complete");
    return true;
  }

#else

  ///Setup of TS Input
  bool inputTS::setup() {
    if (config->getString("input") != "-") {
      inFile = fopen(config->getString("input").c_str(), "r");
    }
    if (!inFile) {
      return false;
    }
    return true;
  }

#endif

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


#ifdef TSLIVE_INPUT
  //This implementation in used in the live version of TS input, where no header is available in advance.
  //Reading the header returns true in this case, to continue parsing the actual stream.
  bool inputTS::readHeader() {
    return true;
  }
#else
  ///Reads headers from a TS stream, and saves them into metadata
  ///It works by going through the entire TS stream, and every time
  ///It encounters a new PES start, it writes the currently found PES data
  ///for a specific track to metadata. After the entire stream has been read,
  ///it writes the remaining metadata.
  ///\todo Find errors, perhaps parts can be made more modular
  bool inputTS::readHeader() {
    if (!inFile) {
      return false;
    }
    DTSC::File tmp(config->getString("input") + ".dtsh");
    if (tmp) {
      myMeta = tmp.getMeta();
      return true;
    }

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
    std::ofstream oFile(std::string(config->getString("input") + ".dtsh").c_str());
    oFile << myMeta.toJSON().toNetPacked();
    oFile.close();
    return true;
  }
#endif

  ///Gets the next packet that is to be sent
  ///At the moment, the logic of sending the last packet that was finished has been implemented,
  ///but the seeking and finding data is not yet ready.
  ///\todo Finish the implementation
  void inputTS::getNext(bool smart) {
    INSANE_MSG("Getting next");
    thisPacket.null();
    bool hasPacket = (selectedTracks.size() == 1 ? tsStream.hasPacket(*selectedTracks.begin()) : tsStream.hasPacketOnEachTrack());
    while (!hasPacket && (pushing || !feof(inFile)) && config->is_active) {
      if (!pushing) {
        unsigned int bPos = ftell(inFile);
        tsBuf.FromFile(inFile);
        if (selectedTracks.count(tsBuf.getPID())) {
          tsStream.parse(tsBuf, bPos);
        }
      } else {
        while (udpCon.Receive()) {
          udpDataBuffer.append(udpCon.data, udpCon.data_len);
          while (udpDataBuffer.size() > 188 && (udpDataBuffer[0] != 0x47 || udpDataBuffer[188] != 0x47)) {
            size_t syncPos = udpDataBuffer.find("\107", 1);
            udpDataBuffer.erase(0, syncPos);
          }
          while (udpDataBuffer.size() >= 188) {
            tsBuf.FromPointer(udpDataBuffer.data());
            tsStream.parse(tsBuf, 0);
            udpDataBuffer.erase(0, 188);
          }
        }
        Util::sleep(500);
      }
      hasPacket = (selectedTracks.size() == 1 ? tsStream.hasPacket(*selectedTracks.begin()) : tsStream.hasPacketOnEachTrack());
    }
    if (!hasPacket) {
      if (inFile && !feof(inFile)) {
        getNext();
      }
      if (pushing) {
        sleep(500);
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

#ifdef TSLIVE_INPUT
  void inputTS::stream() {
    cfgPointer = config;
    globalStreamName = streamName;
    unsigned long long threadCheckTimer = Util::bootSecs();
    while (config->is_active) {
      if (!pushing) {
        unsigned int bPos = ftell(inFile);
        int ctr = 0;
        while (ctr < 20 && tsBuf.FromFile(inFile)){
          liveStream.add(tsBuf);
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
              }else{
                leftData.append(udpCon.data + offset, udpCon.data_len - offset);
              }
              offset += 188;
            }else{
              if (leftData.size()){
                leftData.append(udpCon.data + offset, 1);
                if (leftData.size() >= 188){
                  liveStream.add((char*)leftData.data());
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
      if (pushing){
        Util::sleep(100);
      }
    }
    finish();
    INFO_MSG("Input for stream %s closing clean", streamName.c_str());
  }

  void inputTS::finish() {
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
    return false;
  }
#endif

}

