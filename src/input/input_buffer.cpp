#include <fcntl.h>
#include <sys/stat.h>

#include <iostream>
#include <cstring>
#include <cerrno>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <mist/stream.h>
#include <mist/defines.h>
#include <mist/triggers.h>

#include "input_buffer.h"

#ifndef TIMEOUTMULTIPLIER
#define TIMEOUTMULTIPLIER 2
#endif

/*LTS-START*/
//We consider a stream playable when this many fragments are available.
#define FRAG_BOOT 3
/*LTS-END*/

namespace Mist {
  inputBuffer::inputBuffer(Util::Config * cfg) : Input(cfg) {
    liveMeta = 0;
    capa["name"] = "Buffer";
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
    /*LTS-start*/
    option.null();
    option["arg"] = "integer";
    option["long"] = "cut";
    option["short"] = "c";
    option["help"] = "Any timestamps before this will be cut from the live buffer";
    option["value"].append(0LL);
    config->addOption("cut", option);
    capa["optional"]["cut"]["name"] = "Cut time (ms)";
    capa["optional"]["cut"]["help"] = "Any timestamps before this will be cut from the live buffer.";
    capa["optional"]["cut"]["option"] = "--cut";
    capa["optional"]["cut"]["type"] = "uint";
    capa["optional"]["cut"]["default"] = 0LL;
    option.null();

    option["arg"] = "integer";
    option["long"] = "resume";
    option["short"] = "R";
    option["help"] = "Enable resuming support (1) or disable resuming support (0, default)";
    option["value"].append(0LL);
    config->addOption("resume", option);
    capa["optional"]["resume"]["name"] = "Resume support";
    capa["optional"]["resume"]["help"] = "If enabled, the buffer will linger after source disconnect to allow resuming the stream later. If disabled, the buffer will instantly close on source disconnect.";
    capa["optional"]["resume"]["option"] = "--resume";
    capa["optional"]["resume"]["type"] = "select";
    capa["optional"]["resume"]["select"][0u][0u] = "0";
    capa["optional"]["resume"]["select"][0u][1u] = "Disabled";
    capa["optional"]["resume"]["select"][1u][0u] = "1";
    capa["optional"]["resume"]["select"][1u][1u] = "Enabled";
    capa["optional"]["resume"]["default"] = 0LL;
    option.null();

    option["arg"] = "integer";
    option["long"] = "segment-size";
    option["short"] = "S";
    option["help"] = "Target time duration in milliseconds for segments";
    option["value"].append(5000LL);
    config->addOption("segmentsize", option);
    capa["optional"]["segmentsize"]["name"] = "Segment size (ms)";
    capa["optional"]["segmentsize"]["help"] = "Target time duration in milliseconds for segments.";
    capa["optional"]["segmentsize"]["option"] = "--segment-size";
    capa["optional"]["segmentsize"]["type"] = "uint";
    capa["optional"]["segmentsize"]["default"] = 5000LL;
    option.null();
    /*LTS-end*/

    capa["source_match"] = "push://*";
    capa["non-provider"] = true;//Indicates we don't provide data, only collect it
    capa["priority"] = 9ll;
    capa["desc"] = "Provides buffered live input";
    capa["codecs"][0u][0u].append("*");
    capa["codecs"][0u][1u].append("*");
    capa["codecs"][0u][2u].append("*");
    isBuffer = true;
    singleton = this;
    bufferTime = 50000;
    cutTime = 0;
    segmentSize = 5000;
    hasPush = false;
    resumeMode = false;
  }

  inputBuffer::~inputBuffer() {
    config->is_active = false;
    if (myMeta.tracks.size()) {
      /*LTS-START*/
      if (myMeta.bufferWindow) {
        if (Triggers::shouldTrigger("STREAM_BUFFER")) {
          std::string payload = config->getString("streamname") + "\nEMPTY";
          Triggers::doTrigger("STREAM_BUFFER", payload, config->getString("streamname"));
        }
      }
      /*LTS-END*/
      DEBUG_MSG(DLVL_DEVEL, "Cleaning up, removing last keyframes");
      for (std::map<unsigned int, DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++) {
        std::map<unsigned long, DTSCPageData> & locations = bufferLocations[it->first];
        if (!nProxy.metaPages.count(it->first) || !nProxy.metaPages[it->first].mapped) {
          continue;
        }
        //First detect all entries on metaPage
        for (int i = 0; i < 8192; i += 8) {
          int * tmpOffset = (int *)(nProxy.metaPages[it->first].mapped + i);
          if (tmpOffset[0] == 0 && tmpOffset[1] == 0) {
            continue;
          }
          unsigned long keyNum = ntohl(tmpOffset[0]);

          //Add an entry into bufferLocations[tNum] for the pages we haven't handled yet.
          if (!locations.count(keyNum)) {
            locations[keyNum].curOffset = 0;
          }
          locations[keyNum].pageNum = keyNum;
          locations[keyNum].keyNum = ntohl(tmpOffset[1]);
        }
        for (std::map<unsigned long, DTSCPageData>::iterator it2 = locations.begin(); it2 != locations.end(); it2++) {
          char thisPageName[NAME_BUFFER_SIZE];
          snprintf(thisPageName, NAME_BUFFER_SIZE, SHM_TRACK_DATA, config->getString("streamname").c_str(), it->first, it2->first);
          IPC::sharedPage erasePage(thisPageName, 20971520);
          erasePage.master = true;
        }
      }
    }
    if (liveMeta){
      liveMeta->unlink();
      delete liveMeta;
      liveMeta = 0;
    }
  }


  ///Cleans up any left-over data for the current stream
  void inputBuffer::onCrash(){
    WARN_MSG("Buffer crashed. Cleaning.");
    streamName = config->getString("streamname");
    char pageName[NAME_BUFFER_SIZE];

    //Set userpage to all 0xFF's, will disconnect all current clients.
    snprintf(pageName, NAME_BUFFER_SIZE, SHM_USERS, streamName.c_str());
    std::string baseName = pageName;
    for (long unsigned i = 0; i < 15; ++i){
      unsigned int size = std::min(((8192 * 2) << i), (32 * 1024 * 1024));
      IPC::sharedPage tmp(std::string(baseName + (char)(i + (int)'A')), size, false, false);
      if (tmp.mapped){
        tmp.master = true;
        WARN_MSG("Wiping %s", std::string(baseName + (char)(i + (int)'A')).c_str());
        memset(tmp.mapped, 0xFF, size);
      }
    }
    //Delete the live stream semaphore, if any.
    if (liveMeta){liveMeta->unlink();}
    {
      //Delete the stream index metapage.
      snprintf(pageName, NAME_BUFFER_SIZE, SHM_STREAM_INDEX, streamName.c_str());
      IPC::sharedPage erasePage(pageName, DEFAULT_STRM_PAGE_SIZE, false, false);
      erasePage.master = true;
    }
    //Delete most if not all temporary track metadata pages.
    for (long unsigned i = 1001; i <= 1024; ++i){
      snprintf(pageName, NAME_BUFFER_SIZE, SHM_TRACK_META, streamName.c_str(), i);
      IPC::sharedPage erasePage(pageName, 1024, false, false);
      erasePage.master = true;
    }
    //Delete most if not all track indexes and data pages.
    for (long unsigned i = 1; i <= 24; ++i){
      snprintf(pageName, NAME_BUFFER_SIZE, SHM_TRACK_INDEX, streamName.c_str(), i);
      IPC::sharedPage indexPage(pageName, SHM_TRACK_INDEX_SIZE, false, false);
      indexPage.master = true;
      if (indexPage.mapped){
        char * mappedPointer = indexPage.mapped;
        for (int j = 0; j < 8192; j += 8) {
          int * tmpOffset = (int *)(mappedPointer + j);
          if (tmpOffset[0] == 0 && tmpOffset[1] == 0){
            continue;
          }
          unsigned long keyNum = ntohl(tmpOffset[0]);
          snprintf(pageName, NAME_BUFFER_SIZE, SHM_TRACK_DATA, streamName.c_str(), i, keyNum);
          IPC::sharedPage erasePage(pageName, 1024, false, false);
          erasePage.master = true;
        }
      }

    }
  }

  /// Fills the details variable with details about the current buffer contents
  void inputBuffer::fillBufferDetails(JSON::Value & details){
    //clear the reference of old data, first
    details.null();
    bool hasH264 = false;
    bool hasAAC = false;
    std::stringstream issues;
    for (std::map<unsigned int, DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++){
      JSON::Value & track = details[it->second.getWritableIdentifier()];
      track["kbits"] = (long long)((double)it->second.bps * 8 / 1024);
      track["codec"] = it->second.codec;
      uint32_t shrtest_key = 0xFFFFFFFFul;
      uint32_t longest_key = 0;
      uint32_t shrtest_prt = 0xFFFFFFFFul;
      uint32_t longest_prt = 0;
      uint32_t shrtest_cnt = 0xFFFFFFFFul;
      uint32_t longest_cnt = 0;
      for (std::deque<DTSC::Key>::iterator k = it->second.keys.begin(); k != it->second.keys.end(); k++){
        if (!k->getLength()){continue;}
        if (k->getLength() > longest_key){longest_key = k->getLength();}
        if (k->getLength() < shrtest_key){shrtest_key = k->getLength();}
        if (k->getParts() > longest_cnt){longest_cnt = k->getParts();}
        if (k->getParts() < shrtest_cnt){shrtest_cnt = k->getParts();}
        if ((k->getLength()/k->getParts()) > longest_prt){longest_prt = (k->getLength()/k->getParts());}
        if ((k->getLength()/k->getParts()) < shrtest_prt){shrtest_prt = (k->getLength()/k->getParts());}
      }
      track["keys"]["ms_min"] = (long long)shrtest_key;
      track["keys"]["ms_max"] = (long long)longest_key;
      track["keys"]["frame_ms_min"] = (long long)shrtest_prt;
      track["keys"]["frame_ms_max"] = (long long)longest_prt;
      track["keys"]["frames_min"] = (long long)shrtest_cnt;
      track["keys"]["frames_max"] = (long long)longest_cnt;
      if (longest_prt > 500){issues << "unstable connection (" << longest_prt << "ms " << it->second.codec << " frame)! ";}
      if (shrtest_cnt < 6){issues << "unstable connection (" << shrtest_cnt << " " << it->second.codec << " frames in key)! ";}
      if (it->second.codec == "AAC"){hasAAC = true;}
      if (it->second.codec == "H264"){hasH264 = true;}
      if (it->second.type=="video"){
        track["width"] = (long long)it->second.width;
        track["height"] = (long long)it->second.height;
        track["fpks"] = it->second.fpks;
      }
    }
    if ((hasAAC || hasH264) && myMeta.tracks.size() > 1){
      if (!hasAAC){issues << "HLS no audio!";}
      if (!hasH264){issues << "HLS no video!";}
    }
    if (issues.str().size()){details["issues"] = issues.str();}
    //return is by reference
  }

  /*LTS-START*/
  static bool liveBW(const char * param, const void * bwPtr){
    if (!param || !bwPtr){return false;}
    INFO_MSG("Comparing %s to %lu", param, *((uint32_t*)bwPtr));
    return JSON::Value(param).asInt() <= *((uint32_t*)bwPtr);
  }
  /*LTS-END*/

  /// \triggers
  /// The `"STREAM_BUFFER"` trigger is stream-specific, and is ran whenever the buffer changes state between playable (FULL) or not (EMPTY). It cannot be cancelled. It is possible to receive multiple EMPTY calls without FULL calls in between, as EMPTY is always generated when a stream is unloaded from memory, even if this stream never reached playable state in the first place (e.g. a broadcast was cancelled before filling enough buffer to be playable). Its payload is:
  /// ~~~~~~~~~~~~~~~
  /// streamname
  /// FULL, EMPTY, DRY or RECOVER (depending on current state)
  /// Detected issues in string format, or empty string if no issues
  /// ~~~~~~~~~~~~~~~
  void inputBuffer::updateMeta() {
    static bool wentDry = false;
    static long long unsigned int lastFragCount = 0xFFFFull;
    static uint32_t lastBPS = 0;/*LTS*/
    uint32_t currBPS = 0;
    long long unsigned int firstms = 0xFFFFFFFFFFFFFFFFull;
    long long unsigned int lastms = 0;
    long long unsigned int fragCount = 0xFFFFull;
    for (std::map<unsigned int, DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++) {
      currBPS += it->second.bps; /*LTS*/
      if (it->second.type == "meta" || !it->second.type.size()) {
        continue;
      }
      if (it->second.init.size()) {
        if (!initData.count(it->first) || initData[it->first] != it->second.init) {
          initData[it->first] = it->second.init;
        }
      } else {
        if (initData.count(it->first)) {
          it->second.init = initData[it->first];
        }
      }
      if (it->second.fragments.size() < fragCount) {
        fragCount = it->second.fragments.size();
      }
      if (it->second.firstms < firstms) {
        firstms = it->second.firstms;
      }
      if (it->second.lastms > lastms) {
        lastms = it->second.lastms;
      }
    }
    /*LTS-START*/
    if (currBPS != lastBPS){
      lastBPS = currBPS;
      if (Triggers::shouldTrigger("LIVE_BANDWIDTH", streamName, liveBW, &lastBPS)){
        std::string payload = streamName + "\n" + JSON::Value((long long)lastBPS).asStringRef();
        if (!Triggers::doTrigger("LIVE_BANDWIDTH", payload, streamName)){
          WARN_MSG("Shutting down buffer because bandwidth limit reached!");
          config->is_active = false;
          userPage.finishEach();
        }
      }
    }
    if (fragCount >= FRAG_BOOT && fragCount != 0xFFFFull && Triggers::shouldTrigger("STREAM_BUFFER", streamName)){
      JSON::Value stream_details;
      fillBufferDetails(stream_details);
      if (lastFragCount == 0xFFFFull) {
        std::string payload = streamName + "\nFULL\n" + stream_details.toString();
        Triggers::doTrigger("STREAM_BUFFER", payload, streamName);
      }else{
        if (stream_details.isMember("issues") != wentDry){
          if (stream_details.isMember("issues")){
            std::string payload = streamName + "\nDRY\n" + stream_details.toString();
            Triggers::doTrigger("STREAM_BUFFER", payload, streamName);
          }else{
            std::string payload = streamName + "\nRECOVER\n" + stream_details.toString();
            Triggers::doTrigger("STREAM_BUFFER", payload, streamName);
          }
        }
      }
      wentDry = stream_details.isMember("issues");
      lastFragCount = fragCount;
    }
    /*LTS-END*/
    myMeta.bufferWindow = lastms - firstms;
    myMeta.vod = false;
    myMeta.live = true;
    if (!liveMeta){
      static char liveSemName[NAME_BUFFER_SIZE];
      snprintf(liveSemName, NAME_BUFFER_SIZE, SEM_LIVE, streamName.c_str());
      liveMeta = new IPC::semaphore(liveSemName, O_CREAT | O_RDWR, ACCESSPERMS, 1);
    }
    liveMeta->wait();

    if (!nProxy.metaPages.count(0) || !nProxy.metaPages[0].mapped) {
      char pageName[NAME_BUFFER_SIZE];
      snprintf(pageName, NAME_BUFFER_SIZE, SHM_STREAM_INDEX, streamName.c_str());
      nProxy.metaPages[0].init(pageName, DEFAULT_STRM_PAGE_SIZE,  true);
      nProxy.metaPages[0].master = false;
    }
    myMeta.writeTo(nProxy.metaPages[0].mapped);
    memset(nProxy.metaPages[0].mapped + myMeta.getSendLen(), 0, (nProxy.metaPages[0].len > myMeta.getSendLen() ? std::min(nProxy.metaPages[0].len - myMeta.getSendLen(), 4ll) : 0));
    liveMeta->post();
  }

  ///Checks if removing a key from this track is allowed/safe, and if so, removes it.
  ///Returns true if a key was actually removed, false otherwise
  ///Aborts if any of the following conditions are true (while active):
  /// * no keys present
  /// * not at least 4 whole fragments present
  /// * first fragment hasn't been at least lastms-firstms ms in buffer
  /// * less than 8 times the biggest fragment duration is buffered
  /// If a key was deleted and the first buffered data page is no longer used, it is deleted also.
  bool inputBuffer::removeKey(unsigned int tid) {
    DTSC::Track & Trk = myMeta.tracks[tid];
    //If this track is empty, abort
    if (!Trk.keys.size()) {
      return false;
    }
    //the following checks only run if we're not shutting down
    if (config->is_active){
      //Make sure we have at least 4 whole fragments at all times,
      if (Trk.fragments.size() < 5) {
        return false;
      }
      //ensure we have each fragment buffered for at least the whole bufferTime
      if (!Trk.secsSinceFirstFragmentInsert() || (Trk.lastms - Trk.firstms) < bufferTime){
        return false;
      }
      if (Trk.fragments.size() > 2){
        ///Make sure we have at least 8X the target duration.
        //The target duration is the biggest fragment, rounded up to whole seconds.
        uint32_t targetDuration = (Trk.biggestFragment() / 1000 + 1) * 1000;
        //The start is the third fragment's begin
        uint32_t fragStart = Trk.getKey((++(++Trk.fragments.begin()))->getNumber()).getTime();
        //The end is the last fragment's begin
        uint32_t fragEnd = Trk.getKey(Trk.fragments.rbegin()->getNumber()).getTime();
        if ((fragEnd - fragStart) < targetDuration * 8){
          return false;
        }
      }
    }
    //Alright, everything looks good, let's delete the key and possibly also fragment
    Trk.removeFirstKey();
    //if there is more than one page buffered for this track...
    if (bufferLocations[tid].size() > 1) {
      //Check if the first key starts on the second page or higher
      if (Trk.keys[0].getNumber() >= (++(bufferLocations[tid].begin()))->first || !config->is_active){
        //If so, we can delete the first page entirely
        HIGH_MSG("Erasing track %d, keys %lu-%lu from buffer", tid, bufferLocations[tid].begin()->first, bufferLocations[tid].begin()->first + bufferLocations[tid].begin()->second.keyNum - 1);
        bufferRemove(tid, bufferLocations[tid].begin()->first);

        nProxy.curPageNum.erase(tid);
        char thisPageName[NAME_BUFFER_SIZE];
        snprintf(thisPageName, NAME_BUFFER_SIZE, SHM_TRACK_DATA, config->getString("streamname").c_str(), (unsigned long)tid, bufferLocations[tid].begin()->first);
        nProxy.curPage[tid].init(thisPageName, 20971520);
        nProxy.curPage[tid].master = true;
        nProxy.curPage.erase(tid);

        bufferLocations[tid].erase(bufferLocations[tid].begin());
      } else {
        VERYHIGH_MSG("%lu still on first page (%lu - %lu)", myMeta.tracks[tid].keys[0].getNumber(), bufferLocations[tid].begin()->first, bufferLocations[tid].begin()->first + bufferLocations[tid].begin()->second.keyNum - 1);
      }
    }
    return true;
  }

  void inputBuffer::eraseTrackDataPages(unsigned long tid) {
    if (!bufferLocations.count(tid)) {
      return;
    }
    for (std::map<unsigned long, DTSCPageData>::iterator it = bufferLocations[tid].begin(); it != bufferLocations[tid].end(); it++) {
      char thisPageName[NAME_BUFFER_SIZE];
      snprintf(thisPageName, NAME_BUFFER_SIZE, SHM_TRACK_DATA, config->getString("streamname").c_str(), tid, it->first);
      nProxy.curPage[tid].init(thisPageName, 20971520, false, false);
      nProxy.curPage[tid].master = true;
      nProxy.curPage.erase(tid);
    }
    bufferLocations.erase(tid);
    nProxy.metaPages[tid].master = true;
    nProxy.metaPages.erase(tid);
  }

  void inputBuffer::finish() {
    Input::finish();
    updateMeta();
    if (bufferLocations.size()) {
      std::set<unsigned long> toErase;
      for (std::map<unsigned long, std::map<unsigned long, DTSCPageData> >::iterator it = bufferLocations.begin(); it != bufferLocations.end(); it++) {
        toErase.insert(it->first);
      }
      for (std::set<unsigned long>::iterator it = toErase.begin(); it != toErase.end(); ++it) {
        eraseTrackDataPages(*it);
      }
    }
  }

  /// \triggers
  /// The `"STREAM_TRACK_REMOVE"` trigger is stream-specific, and is ran whenever a track is fully removed from a live strean buffer. It cannot be cancelled. Its payload is:
  /// ~~~~~~~~~~~~~~~
  /// streamname
  /// trackID
  /// ~~~~~~~~~~~~~~~
  void inputBuffer::removeUnused() {
    //first remove all tracks that have not been updated for too long
    bool changed = true;
    while (changed) {
      changed = false;
      long long unsigned int time = Util::bootSecs();
      long long unsigned int compareFirst = 0xFFFFFFFFFFFFFFFFull;
      long long unsigned int compareLast = 0;
      std::set<std::string> activeTypes;
      //for tracks that were updated in the last 5 seconds, get the first and last ms edges.
      for (std::map<unsigned int, DTSC::Track>::iterator it2 = myMeta.tracks.begin(); it2 != myMeta.tracks.end(); it2++) {
        if ((time - lastUpdated[it2->first]) > 5) {
          continue;
        }
        activeTypes.insert(it2->second.type);
        if (it2->second.lastms > compareLast) {
          compareLast = it2->second.lastms;
        }
        if (it2->second.firstms < compareFirst) {
          compareFirst = it2->second.firstms;
        }
      }
      for (std::map<unsigned int, DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++) {
        //if not updated for an entire buffer duration, or last updated track and this track differ by an entire buffer duration, erase the track.
        if ((long long int)(time - lastUpdated[it->first]) > (long long int)(bufferTime / 1000) ||
            (compareLast && activeTypes.count(it->second.type) && (long long int)(time - lastUpdated[it->first]) > 5 && (
               (compareLast < it->second.firstms && (long long int)(it->second.firstms - compareLast) > bufferTime)
               ||
               (compareFirst > it->second.lastms && (long long int)(compareFirst - it->second.lastms) > bufferTime)
             ))
           ) {
          unsigned int tid = it->first;
          //erase this track
          if ((long long int)(time - lastUpdated[it->first]) > (long long int)(bufferTime / 1000)) {
            WARN_MSG("Erasing %s track %d (%s/%s) because not updated for %ds (> %ds)", streamName.c_str(), it->first, it->second.type.c_str(), it->second.codec.c_str(), (long long int)(time - lastUpdated[it->first]), (long long int)(bufferTime / 1000));
          } else {
            WARN_MSG("Erasing %s inactive track %u (%s/%s) because it was inactive for 5+ seconds and contains data (%us - %us), while active tracks are (%us - %us), which is more than %us seconds apart.", streamName.c_str(), it->first, it->second.type.c_str(), it->second.codec.c_str(), it->second.firstms / 1000, it->second.lastms / 1000, compareFirst / 1000, compareLast / 1000, bufferTime / 1000);
          }
          /*LTS-START*/
          if (Triggers::shouldTrigger("STREAM_TRACK_REMOVE")) {
            std::string payload = config->getString("streamname") + "\n" + JSON::Value((long long)it->first).asString() + "\n";
            Triggers::doTrigger("STREAM_TRACK_REMOVE", payload, config->getString("streamname"));
          }
          /*LTS-END*/
          lastUpdated.erase(tid);
          /// \todo Consider replacing with eraseTrackDataPages(it->first)?
          while (bufferLocations[tid].size()) {
            char thisPageName[NAME_BUFFER_SIZE];
            snprintf(thisPageName, NAME_BUFFER_SIZE, SHM_TRACK_DATA, config->getString("streamname").c_str(), (unsigned long)tid, bufferLocations[tid].begin()->first);
            nProxy.curPage[tid].init(thisPageName, 20971520);
            nProxy.curPage[tid].master = true;
            nProxy.curPage.erase(tid);
            bufferLocations[tid].erase(bufferLocations[tid].begin());
          }
          if (pushLocation.count(it->first)) {
            // \todo Debugger says this is null sometimes. It shouldn't be. Figure out why!
            // For now, this if will prevent crashes in these cases.
            if (pushLocation[it->first]){
              //Reset the userpage, to allow repushing from TS
              IPC::userConnection userConn(pushLocation[it->first]);
              for (int i = 0; i < SIMUL_TRACKS; i++) {
                if (userConn.getTrackId(i) == it->first) {
                  userConn.setTrackId(i, 0);
                  userConn.setKeynum(i, 0);
                  break;
                }
              }
            }
            pushLocation.erase(it->first);
          }
          nProxy.curPageNum.erase(it->first);
          nProxy.metaPages[it->first].master = true;
          nProxy.metaPages.erase(it->first);
          activeTracks.erase(it->first);
          myMeta.tracks.erase(it->first);
          /*LTS-START*/
          if (!myMeta.tracks.size()) {
            if (Triggers::shouldTrigger("STREAM_BUFFER")) {
              std::string payload = config->getString("streamname") + "\nEMPTY";
              Triggers::doTrigger("STREAM_BUFFER", payload, config->getString("streamname"));
            }
          }
          /*LTS-END*/
          changed = true;
          break;
        }
      }
    }
    //find the earliest video keyframe stored
    unsigned int firstVideo = 1;
    for (std::map<unsigned int, DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++) {
      if (it->second.type == "video") {
        if (it->second.firstms < firstVideo || firstVideo == 1) {
          firstVideo = it->second.firstms;
        }
      }
    }
    for (std::map<unsigned int, DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++) {
      //non-video tracks need to have a second keyframe that is <= firstVideo
      //firstVideo = 1 happens when there are no tracks, in which case we don't care any more
      if (it->second.type != "video"){
        if (it->second.keys.size() < 2 || (it->second.keys[1].getTime() > firstVideo && firstVideo != 1)){
          continue;
        }
      }
      //Buffer cutting
      while (it->second.keys.size() > 1 && it->second.keys[0].getTime() < cutTime) {
        if (!removeKey(it->first)) {
          break;
        }
      }
      //Buffer size management
      /// \TODO Make sure data has been in the buffer for at least bufferTime after it goes in
      while (it->second.keys.size() > 1 && (it->second.lastms - it->second.keys[1].getTime()) > bufferTime) {
        if (!removeKey(it->first)) {
          break;
        }
      }
    }
    updateMeta();
    if (config->is_active){
      if (streamStatus){streamStatus.mapped[0] = hasPush ? STRMSTAT_READY : STRMSTAT_WAIT;}
    }
    static bool everHadPush = false;
    if (hasPush) {
      hasPush = false;
      everHadPush = true;
    } else if (everHadPush && !resumeMode && config->is_active) {
      INFO_MSG("Shutting down buffer because resume mode is disabled and the source disconnected");
      if (streamStatus){streamStatus.mapped[0] = STRMSTAT_SHUTDOWN;}
      config->is_active = false;
      userPage.finishEach();
    }
  }

  /// \triggers
  /// The `"STREAM_TRACK_ADD"` trigger is stream-specific, and is ran whenever a new track is added to a live strean buffer. It cannot be cancelled. Its payload is:
  /// ~~~~~~~~~~~~~~~
  /// streamname
  /// trackID
  /// ~~~~~~~~~~~~~~~
  void inputBuffer::userCallback(char * data, size_t len, unsigned int id) {
    /*LTS-START*/
    //Reload the configuration to make sure we stay up to date with changes through the api
    if (Util::epoch() - lastReTime > 4) {
      setup();
    }
    /*LTS-END*/
    //Static variable keeping track of the next temporary mapping to use for a track.
    static int nextTempId = 1001;
    //Get the counter of this user
    char counter = (*(data - 1));
    //Each user can have at maximum SIMUL_TRACKS elements in their userpage.
    IPC::userConnection userConn(data);
    for (int index = 0; index < SIMUL_TRACKS; index++) {
      //Get the track id from the current element
      unsigned long value = userConn.getTrackId(index);
      //Skip value 0xFFFFFFFF as this indicates a previously declined track
      if (value == 0xFFFFFFFF) {
        continue;
      }
      //Skip value 0 as this indicates an empty track
      if (value == 0) {
        continue;
      }

      //If the current value indicates a valid trackid, and it is pushed from this user
      if (pushLocation[value] == data) {
        //Check for timeouts, and erase the track if necessary
        if (counter == 126 || counter == 127){
          pushLocation.erase(value);
          if (negotiatingTracks.count(value)) {
            negotiatingTracks.erase(value);
          }
          if (activeTracks.count(value)) {
            updateMeta();
            eraseTrackDataPages(value);
            activeTracks.erase(value);
            bufferLocations.erase(value);
          }
          nProxy.metaPages[value].master = true;
          nProxy.metaPages.erase(value);
          continue;
        }
      }
      //Track is set to "New track request", assign new track id and create shared memory page
      //This indicates that the 'current key' part of the element is set to contain the original track id from the pushing process
      if (value & 0x80000000) {
        if (value & 0x40000000) {
          unsigned long finalMap = value & ~0xC0000000;
          //Register the new track as an active track.
          activeTracks.insert(finalMap);
          //Register the time of registration as initial value for the lastUpdated field, plus an extra 5 seconds just to be sure.
          lastUpdated[finalMap] = Util::bootSecs() + 5;
          //Register the user thats is pushing this element
          pushLocation[finalMap] = data;
          //Initialize the metadata for this track
          if (!myMeta.tracks.count(finalMap)) {
            DEBUG_MSG(DLVL_MEDIUM, "Inserting metadata for track number %d", finalMap);
            
            IPC::sharedPage tMeta;

            char tempMetaName[NAME_BUFFER_SIZE];
            snprintf(tempMetaName, NAME_BUFFER_SIZE, SHM_TRACK_META, config->getString("streamname").c_str(), finalMap);
            tMeta.init(tempMetaName, 8388608, false);

            //The page exist, now we try to read in the metadata of the track

            //Store the size of the dtsc packet to read.
            unsigned int len = ntohl(((int *)tMeta.mapped)[1]);
            //Temporary variable, won't be used again
            unsigned int tempForReadingMeta = 0;
            //Read in the metadata through a temporary JSON object
            ///\todo Optimize this part. Find a way to not have to store the metadata in JSON first, but read it from the page immediately
            JSON::Value tempJSONForMeta;
            JSON::fromDTMI((const unsigned char *)tMeta.mapped + 8, len, tempForReadingMeta, tempJSONForMeta);
            
            tMeta.master = true;

            //Construct a metadata object for the current track
            DTSC::Meta trackMeta(tempJSONForMeta);

            myMeta.tracks[finalMap] = trackMeta.tracks.begin()->second;
            myMeta.tracks[finalMap].firstms = 0;
            myMeta.tracks[finalMap].lastms = 0;

            userConn.setTrackId(index, finalMap);
            userConn.setKeynum(index, 0x0000);


            char firstPage[NAME_BUFFER_SIZE];
            snprintf(firstPage, NAME_BUFFER_SIZE, SHM_TRACK_INDEX, config->getString("streamname").c_str(), finalMap);
            nProxy.metaPages[finalMap].init(firstPage, SHM_TRACK_INDEX_SIZE, false);

            //Update the metadata for this track
            updateTrackMeta(finalMap);
            hasPush = true;
          }
          //Write the final mapped track number and keyframe number to the user page element
          //This is used to resume pushing as well as pushing new tracks
          userConn.setTrackId(index, finalMap);
          userConn.setKeynum(index, myMeta.tracks[finalMap].keys.size());
          //Update the metadata to reflect all changes
          updateMeta();
          continue;
        }
        //Set the temporary track id for this item, and increase the temporary value for use with the next track
        unsigned long long tempMapping = nextTempId++;
        //Add the temporary track id to the list of tracks that are currently being negotiated
        negotiatingTracks.insert(tempMapping);
        //Write the temporary id to the userpage element
        userConn.setTrackId(index, tempMapping);
        //Obtain the original track number for the pushing process
        unsigned long originalTrack = userConn.getKeynum(index);
        //Overwrite it with 0xFFFF
        userConn.setKeynum(index, 0xFFFF);
        DEBUG_MSG(DLVL_HIGH, "Incoming track %lu from pushing process %d has now been assigned temporary id %llu", originalTrack, id, tempMapping);
      }

      //The track id is set to the value of a track that we are currently negotiating about
      if (negotiatingTracks.count(value)) {
        //If the metadata page for this track is not yet registered, initialize it
        if (!nProxy.metaPages.count(value) || !nProxy.metaPages[value].mapped) {
          char tempMetaName[NAME_BUFFER_SIZE];
          snprintf(tempMetaName, NAME_BUFFER_SIZE, SHM_TRACK_META, config->getString("streamname").c_str(), value);
          nProxy.metaPages[value].init(tempMetaName, 8388608, false, false);
        }
        //If this tracks metdata page is not initialize, skip the entire element for now. It will be instantiated later
        if (!nProxy.metaPages[value].mapped) {
          //remove the negotiation if it has timed out
          if (++negotiationTimeout[value] >= 1000) {
            negotiatingTracks.erase(value);
            negotiationTimeout.erase(value);
          }
          continue;
        }

        //The page exist, now we try to read in the metadata of the track

        //Store the size of the dtsc packet to read.
        unsigned int len = ntohl(((int *)nProxy.metaPages[value].mapped)[1]);
        //Temporary variable, won't be used again
        unsigned int tempForReadingMeta = 0;
        //Read in the metadata through a temporary JSON object
        ///\todo Optimize this part. Find a way to not have to store the metadata in JSON first, but read it from the page immediately
        JSON::Value tempJSONForMeta;
        JSON::fromDTMI((const unsigned char *)nProxy.metaPages[value].mapped + 8, len, tempForReadingMeta, tempJSONForMeta);
        //Construct a metadata object for the current track
        DTSC::Meta trackMeta(tempJSONForMeta);
        //If the track metadata does not contain the negotiated track, assume the metadata is currently being written, and skip the element for now. It will be instantiated in the next call.
        if (!trackMeta.tracks.count(value)) {
          //remove the negotiation if it has timed out
          if (++negotiationTimeout[value] >= 1000) {
            negotiatingTracks.erase(value);
            //Set master to true before erasing the page, because we are responsible for cleaning up unused pages
            nProxy.metaPages[value].master = true;
            nProxy.metaPages.erase(value);
            negotiationTimeout.erase(value);
          }
          continue;
        }

        std::string trackIdentifier = trackMeta.tracks.find(value)->second.getIdentifier();
        DEBUG_MSG(DLVL_HIGH, "Attempting colision detection for track %s", trackIdentifier.c_str());
        /*LTS-START*/
        //Get the identifier for the track, and attempt colission detection.
        int collidesWith = -1;
        for (std::map<unsigned int, DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++) {
          //If the identifier of an existing track and the current track match, assume the are the same track and reject the negotiated one.
          ///\todo Maybe switch to a new form of detecting collisions, especially with regards to multiple audio languages and camera angles.
          if (it->second.getIdentifier() == trackIdentifier) {
            collidesWith = it->first;
            break;
          }
        }
        /*LTS-END*/
        //Remove the "negotiate" status in either case
        negotiatingTracks.erase(value);
        //Set master to true before erasing the page, because we are responsible for cleaning up unused pages
        nProxy.metaPages[value].master = true;
        nProxy.metaPages.erase(value);

        //Check if the track collides, and whether the track it collides with is active.
        if (collidesWith != -1 && activeTracks.count(collidesWith)) { /*LTS*/
          //Print a warning message and set the state of the track to rejected.
          WARN_MSG("Collision of temporary track %lu with existing track %d detected. Handling as a new valid track.", value, collidesWith);
          collidesWith = -1;
        }
        /*LTS-START*/
        unsigned long finalMap = collidesWith;
        if (finalMap == -1) {
          //No collision has been detected, assign a new final number
          finalMap = (myMeta.tracks.size() ? myMeta.tracks.rbegin()->first : 0) + 1;
          DEBUG_MSG(DLVL_DEVEL, "No colision detected for temporary track %lu from user %u, assigning final track number %lu", value, id, finalMap);
          if (Triggers::shouldTrigger("STREAM_TRACK_ADD")) {
            std::string payload = config->getString("streamname") + "\n" + JSON::Value((long long)finalMap).asString() + "\n";
            Triggers::doTrigger("STREAM_TRACK_ADD", payload, config->getString("streamname"));
          }
        }
        /*LTS-END*/
        //Resume either if we have more than 1 keyframe on the replacement track (assume it was already pushing before the track "dissapeared")
        //or if the firstms of the replacement track is later than the lastms on the existing track
        if (!myMeta.tracks.count(finalMap) || trackMeta.tracks.find(value)->second.keys.size() > 1 || trackMeta.tracks.find(value)->second.firstms >= myMeta.tracks[finalMap].lastms) {
          if (myMeta.tracks.count(finalMap) && myMeta.tracks[finalMap].lastms > 0) {
            INFO_MSG("Resume of track %lu detected, coming from temporary track %lu of user %u", finalMap, value, id);
          } else {
            INFO_MSG("New track detected, assigned track id %lu, coming from temporary track %lu of user %u", finalMap, value, id);
          }
        } else {
          //Otherwise replace existing track
          INFO_MSG("Replacement of track %lu detected, coming from temporary track %lu of user %u", finalMap, value, id);
          myMeta.tracks.erase(finalMap);
          //Set master to true before erasing the page, because we are responsible for cleaning up unused pages
          updateMeta();
          eraseTrackDataPages(value);
          nProxy.metaPages[finalMap].master = true;
          nProxy.metaPages.erase(finalMap);
          bufferLocations.erase(finalMap);
        }

        //Register the new track as an active track.
        activeTracks.insert(finalMap);
        //Register the time of registration as initial value for the lastUpdated field, plus an extra 5 seconds just to be sure.
        lastUpdated[finalMap] = Util::bootSecs() + 5;
        //Register the user thats is pushing this element
        pushLocation[finalMap] = data;
        //Initialize the metadata for this track if it was not in place yet.
        if (!myMeta.tracks.count(finalMap)) {
          DEBUG_MSG(DLVL_MEDIUM, "Inserting metadata for track number %d", finalMap);
          myMeta.tracks[finalMap] = trackMeta.tracks.begin()->second;
          myMeta.tracks[finalMap].firstms = 0;
          myMeta.tracks[finalMap].lastms = 0;
          myMeta.tracks[finalMap].trackID = finalMap;
        }
        //Write the final mapped track number and keyframe number to the user page element
        //This is used to resume pushing as well as pushing new tracks
        userConn.setTrackId(index, finalMap);
        userConn.setKeynum(index, myMeta.tracks[finalMap].keys.size());
        //Update the metadata to reflect all changes
        updateMeta();
      }
      //If the track is active, and this is the element responsible for pushing it
      if (activeTracks.count(value) && pushLocation[value] == data) {
        //Open the track index page if we dont have it open yet
        if (!nProxy.metaPages.count(value) || !nProxy.metaPages[value].mapped) {
          char firstPage[NAME_BUFFER_SIZE];
          snprintf(firstPage, NAME_BUFFER_SIZE, SHM_TRACK_INDEX, config->getString("streamname").c_str(), value);
          nProxy.metaPages[value].init(firstPage, SHM_TRACK_INDEX_SIZE, false, false);
        }
        if (nProxy.metaPages[value].mapped) {
          //Update the metadata for this track
          updateTrackMeta(value);
          hasPush = true;
        }
      }
    }
  }

  void inputBuffer::updateTrackMeta(unsigned long tNum) {
    //Store a reference for easier access
    std::map<unsigned long, DTSCPageData> & locations = bufferLocations[tNum];
    char * mappedPointer = nProxy.metaPages[tNum].mapped;
    if (!mappedPointer){return;}
    VERYHIGH_MSG("Updating meta for track %lu, %lu pages", tNum, locations.size());

    //First detect all entries on metaPage
    for (int i = 0; i < 8192; i += 8) {
      int * tmpOffset = (int *)(mappedPointer + i);
      if (tmpOffset[0] == 0 && tmpOffset[1] == 0) {
        continue;
      }
      unsigned long keyNum = ntohl(tmpOffset[0]);

      //Add an entry into bufferLocations[tNum] for the pages we haven't handled yet.
      if (!locations.count(keyNum)) {
        locations[keyNum].curOffset = 0;
        VERYHIGH_MSG("Page %d detected, with %d keys", keyNum, ntohl(tmpOffset[1]));
      }
      locations[keyNum].pageNum = keyNum;
      locations[keyNum].keyNum = ntohl(tmpOffset[1]);
    }
    //Since the map is ordered by keynumber, this loop updates the metadata for each page from oldest to newest
    for (std::map<unsigned long, DTSCPageData>::iterator pageIt = locations.begin(); pageIt != locations.end(); pageIt++) {
      updateMetaFromPage(tNum, pageIt->first);
    }
    updateMeta();
  }

  void inputBuffer::updateMetaFromPage(unsigned long tNum, unsigned long pageNum) {
    VERYHIGH_MSG("Updating meta for track %d page %d", tNum, pageNum);
    DTSCPageData & pageData = bufferLocations[tNum][pageNum];

    //If the current page is over its 8mb "splitting" boundary
    if (pageData.curOffset > (8 * 1024 * 1024)) {
      //And the last keyframe in the parsed metadata is further in the stream than this page
      if (pageData.pageNum + pageData.keyNum < myMeta.tracks[tNum].keys.rbegin()->getNumber()) {
        //Assume the entire page is already parsed
        return;
      }
    }

    //Otherwise open and parse the page

    //Open the page if it is not yet open
    if (!nProxy.curPageNum.count(tNum) || nProxy.curPageNum[tNum] != pageNum || !nProxy.curPage[tNum].mapped) {
      //DO NOT ERASE THE PAGE HERE, master is not set to true
      nProxy.curPageNum.erase(tNum);
      char nextPageName[NAME_BUFFER_SIZE];
      snprintf(nextPageName, NAME_BUFFER_SIZE, SHM_TRACK_DATA, config->getString("streamname").c_str(), tNum, pageNum);
      nProxy.curPage[tNum].init(nextPageName, 20971520);
      //If the page can not be opened, stop here
      if (!nProxy.curPage[tNum].mapped) {
        WARN_MSG("Could not open page: %s", nextPageName);
        return;
      }
      nProxy.curPageNum[tNum] = pageNum;
    }


    DTSC::Packet tmpPack;
    if (!nProxy.curPage[tNum].mapped[pageData.curOffset]) {
      VERYHIGH_MSG("No packet on page %lu for track %lu, waiting...", pageNum, tNum);
      return;
    }
    tmpPack.reInit(nProxy.curPage[tNum].mapped + pageData.curOffset, 0);
    //No new data has been written on the page since last update
    if (!tmpPack) {
      return;
    }
    lastUpdated[tNum] = Util::bootSecs();
    while (tmpPack) {
      //Update the metadata with this packet
      myMeta.update(tmpPack, segmentSize);/*LTS*/
      //Set the first time when appropriate
      if (pageData.firstTime == 0) {
        pageData.firstTime = tmpPack.getTime();
      }
      //Update the offset on the page with the size of the current packet
      pageData.curOffset += tmpPack.getDataLen();
      //Attempt to read in the next packet
      tmpPack.reInit(nProxy.curPage[tNum].mapped + pageData.curOffset, 0);
    }
  }

  bool inputBuffer::setup() {
    lastReTime = Util::epoch(); /*LTS*/
    std::string strName = config->getString("streamname");
    Util::sanitizeName(strName);
    strName = strName.substr(0, (strName.find_first_of("+ ")));
    IPC::sharedPage serverCfg(SHM_CONF, DEFAULT_CONF_PAGE_SIZE, false, false); ///< Contains server configuration and capabilities
    IPC::semaphore configLock(SEM_CONF, O_CREAT | O_RDWR, ACCESSPERMS, 1);
    configLock.wait();
    DTSC::Scan streamCfg = DTSC::Scan(serverCfg.mapped, serverCfg.len).getMember("streams").getMember(strName);
    long long tmpNum;

    //if stream is configured and setting is present, use it, always
    if (streamCfg && streamCfg.getMember("DVR")) {
      tmpNum = streamCfg.getMember("DVR").asInt();
    } else {
      if (streamCfg) {
        //otherwise, if stream is configured use the default
        tmpNum = config->getOption("bufferTime", true)[0u].asInt();
      } else {
        //if not, use the commandline argument
        tmpNum = config->getOption("bufferTime").asInt();
      }
    }
    if (tmpNum < 1000) {
      tmpNum = 1000;
    }
    //if the new value is different, print a message and apply it
    if (bufferTime != tmpNum) {
      DEBUG_MSG(DLVL_DEVEL, "Setting bufferTime from %u to new value of %lli", bufferTime, tmpNum);
      bufferTime = tmpNum;
    }

    /*LTS-START*/
    //if stream is configured and setting is present, use it, always
    if (streamCfg && streamCfg.getMember("cut")) {
      tmpNum = streamCfg.getMember("cut").asInt();
    } else {
      if (streamCfg) {
        //otherwise, if stream is configured use the default
        tmpNum = config->getOption("cut", true)[0u].asInt();
      } else {
        //if not, use the commandline argument
        tmpNum = config->getOption("cut").asInt();
      }
    }
    //if the new value is different, print a message and apply it
    if (cutTime != tmpNum) {
      INFO_MSG("Setting cutTime from %u to new value of %lli", cutTime, tmpNum);
      cutTime = tmpNum;
    }

    //if stream is configured and setting is present, use it, always
    if (streamCfg && streamCfg.getMember("resume")) {
      tmpNum = streamCfg.getMember("resume").asInt();
    } else {
      if (streamCfg) {
        //otherwise, if stream is configured use the default
        tmpNum = config->getOption("resume", true)[0u].asInt();
      } else {
        //if not, use the commandline argument
        tmpNum = config->getOption("resume").asInt();
      }
    }
    //if the new value is different, print a message and apply it
    if (resumeMode != (bool)tmpNum) {
      INFO_MSG("Setting resume mode from %s to new value of %s", resumeMode ? "enabled" : "disabled", tmpNum ? "enabled" : "disabled");
      resumeMode = tmpNum;
    }

    //if stream is configured and setting is present, use it, always
    if (streamCfg && streamCfg.getMember("segmentsize")) {
      tmpNum = streamCfg.getMember("segmentsize").asInt();
    } else {
      if (streamCfg) {
        //otherwise, if stream is configured use the default
        tmpNum = config->getOption("segmentsize", true)[0u].asInt();
      } else {
        //if not, use the commandline argument
        tmpNum = config->getOption("segmentsize").asInt();
      }
    }
    if (tmpNum < myMeta.biggestFragment()/2){
      tmpNum = myMeta.biggestFragment()/2;
    }
    //if the new value is different, print a message and apply it
    if (segmentSize != tmpNum) {
      INFO_MSG("Setting segmentSize from %u to new value of %lli", segmentSize, tmpNum);
      segmentSize = tmpNum;
    }
    /*LTS-END*/
    configLock.post();
    configLock.close();
    return true;
  }

  bool inputBuffer::readHeader() {
    return true;
  }

  void inputBuffer::getNext(bool smart) {}

  void inputBuffer::seek(int seekTime) {}

  void inputBuffer::trackSelect(std::string trackSpec) {}
}



