#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <semaphore.h>
#include <iterator> //std::distance

#include <mist/stream.h>
#include <mist/defines.h>
#include <mist/http_parser.h>
#include <mist/timing.h>
#include "output.h"

namespace Mist {
  Util::Config * Output::config = NULL;
  JSON::Value Output::capa = JSON::Value();

  int getDTSCLen(char * mapped, long long int offset){
    return ntohl(((int*)(mapped + offset))[1]);
  }

  unsigned long long getDTSCTime(char * mapped, long long int offset){
    char * timePoint = mapped + offset + 12;
    return ((long long int)timePoint[0] << 56) | ((long long int)timePoint[1] << 48) | ((long long int)timePoint[2] << 40) | ((long long int)timePoint[3] << 32) | ((long long int)timePoint[4] << 24) | ((long long int)timePoint[5] << 16) | ((long long int)timePoint[6] << 8) | timePoint[7];
  }

  void Output::init(Util::Config * cfg){
    capa["optional"]["debug"]["name"] = "debug";
    capa["optional"]["debug"]["help"] = "The debug level at which messages need to be printed.";
    capa["optional"]["debug"]["option"] = "--debug";
    capa["optional"]["debug"]["type"] = "debug";
  }
  
  Output::Output(Socket::Connection & conn) : myConn(conn) {
    firstTime = 0;
    crc = getpid();
    parseData = false;
    wantRequest = true;
    sought = false;
    isInitialized = false;
    isBlocking = false;
    lastStats = 0;
    maxSkipAhead = 7500;
    minSkipAhead = 5000;
    realTime = 1000;
    if (myConn){
      setBlocking(true);
    }else{
      DEBUG_MSG(DLVL_WARN, "Warning: MistOut created with closed socket!");
    }
    sentHeader = false;
  }
  
  void Output::setBlocking(bool blocking){
    isBlocking = blocking;
    myConn.setBlocking(isBlocking);
  }
  
  Output::~Output(){}

  void Output::updateMeta(){
    //read metadata from page to myMeta variable
    IPC::semaphore liveMeta(std::string("liveMeta@" + streamName).c_str(), O_CREAT | O_RDWR, ACCESSPERMS, 1);
    bool lock = myMeta.live;
    if (lock){
      liveMeta.wait();
    }
    if (streamIndex.mapped){
      DTSC::Packet tmpMeta(streamIndex.mapped, streamIndex.len, true);
      if (tmpMeta.getVersion()){
        myMeta.reinit(tmpMeta);
      }
    }
    if (lock){
      liveMeta.post();
    }
  }
  
  /// Called when stream initialization has failed.
  /// The standard implementation will set isInitialized to false and close the client connection,
  /// thus causing the process to exit cleanly.
  void Output::onFail(){
    isInitialized = false;
    myConn.close();
  }

  void Output::negotiateWithBuffer(int tid){
    //Check whether the track exists
    if (!meta_out.tracks.count(tid)) {
      return;
    }
    //Do not re-negotiate already confirmed tracks
    if (trackMap.count(tid)){
      return;
    }
    //Do not re-negotiate if already at maximum for push tracks
    if (trackMap.size() >= 5){
      DEBUG_MSG(DLVL_FAIL, "Failed to negotiate for incoming track %d, already at maximum number of tracks", tid);
      return;
    }

    char * tmp = playerConn.getData();
    if (!tmp){
      DEBUG_MSG(DLVL_FAIL, "Failed to negotiate for incoming track %d, there does not seem to be a connection with the buffer", tid);
      return;
    }
    int bufConnOffset = trackMap.size();
    DEBUG_MSG(DLVL_DEVEL, "Starting negotiation for incoming track %d, at offset %d", tid, bufConnOffset);
    memset(tmp + 6 * bufConnOffset, 0, 4);
    tmp[6 * bufConnOffset] = 0x80;
    tmp[6 * bufConnOffset + 4] = 0xFF;
    tmp[6 * bufConnOffset + 5] = 0xFF;
    playerConn.keepAlive();
    unsigned int newTid = 0x80000000u;
    while (newTid == 0x80000000u){
      Util::sleep(100);
      newTid = ((long)(tmp[6 * bufConnOffset]) << 24) |  ((long)(tmp[6 * bufConnOffset + 1]) << 16) | ((long)(tmp[6 * bufConnOffset + 2]) << 8) | tmp[6 * bufConnOffset + 3];
    }
    DEBUG_MSG(DLVL_VERYHIGH, "Track %d temporarily mapped to %d", tid, newTid);

    char pageName[100];
    sprintf(pageName, "liveStream_%s%d", streamName.c_str(), newTid); 
    IPC::sharedPage metaPage(pageName, 8 * 1024 * 1024);
    DTSC::Meta tmpMeta = meta_out;
    tmpMeta.tracks.clear();
    tmpMeta.tracks[newTid] = meta_out.tracks[tid];
    tmpMeta.tracks[newTid].trackID = newTid;
    JSON::Value tmpVal = tmpMeta.toJSON();
    std::string tmpStr = tmpVal.toNetPacked();
    memcpy(metaPage.mapped, tmpStr.data(), tmpStr.size());
    DEBUG_MSG(DLVL_VERYHIGH, "Temporary metadata written for incoming track %d, handling as track %d", tid, newTid);

    unsigned short firstPage = 0xFFFF;
    unsigned int finalTid = newTid;
    while (firstPage == 0xFFFF){
      DEBUG_MSG(DLVL_VERYHIGH, "Re-checking at offset %d",  bufConnOffset);
      Util::sleep(100);
      finalTid = ((long)(tmp[6 * bufConnOffset]) << 24) |  ((long)(tmp[6 * bufConnOffset + 1]) << 16) | ((long)(tmp[6 * bufConnOffset + 2]) << 8) | tmp[6 * bufConnOffset + 3];
      firstPage = ((long)(tmp[6 * bufConnOffset + 4]) << 8) | tmp[6 * bufConnOffset + 5];
      if (finalTid == 0xFFFFFFFF){
        WARN_MSG("Buffer has declined incoming track %d", tid);
        return;
      }
    }
    //Reinitialize so we make sure we got the right values here
    finalTid = ((long)(tmp[6 * bufConnOffset]) << 24) |  ((long)(tmp[6 * bufConnOffset + 1]) << 16) | ((long)(tmp[6 * bufConnOffset + 2]) << 8) | tmp[6 * bufConnOffset + 3];
    firstPage = ((long)(tmp[6 * bufConnOffset + 4]) << 8) | tmp[6 * bufConnOffset + 5];
    if (finalTid == 0xFFFFFFFF){
      WARN_MSG("Buffer has declined incoming track %d", tid);
      memset(tmp + 6 * bufConnOffset, 0, 6);
      return;
    }

    INFO_MSG("Buffer has indicated that incoming track %d should start writing on track %d, page %d", tid, finalTid, firstPage);
    memset(pageName, 0, 100);
    sprintf(pageName, "%s%d_%d", streamName.c_str(), finalTid, firstPage);
    curPages[finalTid].init(pageName, DEFAULT_DATA_PAGE_SIZE);
    trackMap[tid] = finalTid;
    bookKeeping[finalTid] = DTSCPageData();
  }
  

  void Output::negotiatePushTracks() {
    int i = 0;
    for (std::map<unsigned int, DTSC::Track>::iterator it = meta_out.tracks.begin(); it != meta_out.tracks.end() && i < 5; it++){
      negotiateWithBuffer(it->first);
      i++;
    }
  }

  void Output::bufferPacket(JSON::Value & pack){
    if (!pack["trackid"].asInt()){return;}
    if (myMeta.tracks[pack["trackid"].asInt()].type != "video"){
      if ((pack["time"].asInt() - bookKeeping[trackMap[pack["trackid"].asInt()]].lastKeyTime) >= 5000){
        pack["keyframe"] = 1LL;
        bookKeeping[trackMap[pack["trackid"].asInt()]].lastKeyTime = pack["time"].asInt();
      }
    }
    if (pack["trackid"].asInt() == 0){
      return;
    }
    //Re-negotiate declined tracks on each keyframe, to compensate for "broken" tracks
    if (!trackMap.count(pack["trackid"].asInt()) || !trackMap[pack["trackid"].asInt()]){
      if (pack.isMember("keyframe") && pack["keyframe"]){
        negotiateWithBuffer(pack["trackid"].asInt());
      }
    }
    if (!trackMap.count(pack["trackid"].asInt()) || !trackMap[pack["trackid"].asInt()]){
      //declined track;
      return;
    }
    pack["trackid"] = trackMap[pack["trackid"].asInt()];
    long long unsigned int tNum = pack["trackid"].asInt();
    if (!bookKeeping.count(tNum)){
      return;
    }
    int pageNum = bookKeeping[tNum].pageNum;
    std::string tmp = pack.toNetPacked();
    if (bookKeeping[tNum].curOffset > FLIP_DATA_PAGE_SIZE && pack.isMember("keyframe") && pack["keyframe"]){
      //open new page
      char nextPage[100];
      sprintf(nextPage, "%s%llu_%d", streamName.c_str(), tNum, bookKeeping[tNum].pageNum + bookKeeping[tNum].keyNum);
      INFO_MSG("Continuing track %llu on page %d, from pos %llu", tNum, bookKeeping[tNum].pageNum + bookKeeping[tNum].keyNum, bookKeeping[tNum].curOffset);
      curPages[tNum].init(nextPage, DEFAULT_DATA_PAGE_SIZE);
      bookKeeping[tNum].pageNum += bookKeeping[tNum].keyNum;
      bookKeeping[tNum].keyNum = 0;
      bookKeeping[tNum].curOffset = 0;
    }
    if (!curPages[tNum].mapped){
      //prevent page init failures from crashing everything.
      myConn.close();//closes the connection to trigger a clean shutdown
      return;
    }
    if (bookKeeping[tNum].curOffset + tmp.size() < (unsigned long long)curPages[tNum].len){
      bookKeeping[tNum].keyNum += (pack.isMember("keyframe") && pack["keyframe"]);
      memcpy(curPages[tNum].mapped + bookKeeping[tNum].curOffset, tmp.data(), tmp.size());
      bookKeeping[tNum].curOffset += tmp.size();
    }else{
      bookKeeping[tNum].curOffset += tmp.size();
      DEBUG_MSG(DLVL_WARN, "Can't buffer frame on page %d, track %llu, time %lld, keyNum %d, offset %llu", pageNum, tNum, pack["time"].asInt(), bookKeeping[tNum].pageNum + bookKeeping[tNum].keyNum, bookKeeping[tNum].curOffset);
    }
  }


  
  void Output::initialize(){
    if (isInitialized){
      return;
    }
    if (streamIndex.mapped){
      return;
    }
    if (streamName.size() < 1){
      return; //abort - no stream to initialize...
    }
    if (!Util::startInput(streamName)){
      DEBUG_MSG(DLVL_FAIL, "Opening stream disallowed - aborting initalization");
      onFail();
      return;
    }
    isInitialized = true;
    streamIndex.init(streamName, DEFAULT_META_PAGE_SIZE);
    if (!streamIndex.mapped){
      DEBUG_MSG(DLVL_FAIL, "Could not connect to server for %s\n", streamName.c_str());
      onFail();
      return;
    }
    statsPage = IPC::sharedClient("statistics", STAT_EX_SIZE, true);
    playerConn = IPC::sharedClient(streamName + "_users", 30, true);
    updateMeta();
    selectDefaultTracks();
    sought = false;
  }
  
  void Output::selectDefaultTracks(){
    if (!isInitialized){
      initialize();
      return;
    }
    //check which tracks don't actually exist
    std::set<long unsigned int> toRemove;
    for (std::set<long unsigned int>::iterator it = selectedTracks.begin(); it != selectedTracks.end(); it++){
      if (!myMeta.tracks.count(*it)){
        toRemove.insert(*it);
      }
    }
    //remove those from selectedtracks
    for (std::set<long unsigned int>::iterator it = toRemove.begin(); it != toRemove.end(); it++){
      selectedTracks.erase(*it);
    }
    
    //loop through all codec combinations, count max simultaneous active
    unsigned int bestSoFar = 0;
    unsigned int bestSoFarCount = 0;
    unsigned int index = 0;
    for (JSON::ArrIter it = capa["codecs"].ArrBegin(); it != capa["codecs"].ArrEnd(); it++){
      unsigned int genCounter = 0;
      unsigned int selCounter = 0;
      if ((*it).size() > 0){
        for (JSON::ArrIter itb = (*it).ArrBegin(); itb != (*it).ArrEnd(); itb++){
          if ((*itb).size() > 0){
            bool found = false;
            for (JSON::ArrIter itc = (*itb).ArrBegin(); itc != (*itb).ArrEnd() && !found; itc++){
              for (std::set<long unsigned int>::iterator itd = selectedTracks.begin(); itd != selectedTracks.end(); itd++){
                if (myMeta.tracks[*itd].codec == (*itc).asStringRef()){
                  selCounter++;
                  found = true;
                  break;
                }
              }
              if (!found){
                for (std::map<unsigned int,DTSC::Track>::iterator trit = myMeta.tracks.begin(); trit != myMeta.tracks.end(); trit++){
                  if (trit->second.codec == (*itc).asStringRef()){
                    genCounter++;
                    found = true;
                    break;
                  }
                }
              }
            }
          }
        }
        if (selCounter == selectedTracks.size()){
          if (selCounter + genCounter > bestSoFarCount){
            bestSoFarCount = selCounter + genCounter;
            bestSoFar = index;
            DEBUG_MSG(DLVL_HIGH, "Match (%u/%u): %s", selCounter, selCounter+genCounter, (*it).toString().c_str());
          }
        }else{
          DEBUG_MSG(DLVL_VERYHIGH, "Not a match for currently selected tracks: %s", (*it).toString().c_str());
        }
      }
      index++;
    }
    
    DEBUG_MSG(DLVL_MEDIUM, "Trying to fill: %s", capa["codecs"][bestSoFar].toString().c_str());
    //try to fill as many codecs simultaneously as possible
    if (capa["codecs"][bestSoFar].size() > 0){
      for (JSON::ArrIter itb = capa["codecs"][bestSoFar].ArrBegin(); itb != capa["codecs"][bestSoFar].ArrEnd(); itb++){
        if ((*itb).size() && myMeta.tracks.size()){
          bool found = false;
          for (JSON::ArrIter itc = (*itb).ArrBegin(); itc != (*itb).ArrEnd() && !found; itc++){
            if (selectedTracks.size()){
              for (std::set<long unsigned int>::iterator itd = selectedTracks.begin(); itd != selectedTracks.end(); itd++){
                if (myMeta.tracks[*itd].codec == (*itc).asStringRef()){
                  found = true;
                  break;
                }
              }
            }
            if (!found){
              for (std::map<unsigned int,DTSC::Track>::iterator trit = myMeta.tracks.begin(); trit != myMeta.tracks.end(); trit++){
                if (trit->second.codec == (*itc).asStringRef()){
                  selectedTracks.insert(trit->first);
                  found = true;
                  break;
                }
              }
            }
          }
        }
      }
    }
    
    if (Util::Config::printDebugLevel >= DLVL_MEDIUM){
      //print the selected tracks
      std::stringstream selected;
      if (selectedTracks.size()){
        for (std::set<long unsigned int>::iterator it = selectedTracks.begin(); it != selectedTracks.end(); it++){
          if (it != selectedTracks.begin()){
            selected << ", ";
          }
          selected << (*it);
        }
      }
      DEBUG_MSG(DLVL_MEDIUM, "Selected tracks: %s (%lu)", selected.str().c_str(), selectedTracks.size());    
    }
    
  }
  
  /// Clears the buffer, sets parseData to false, and generally makes not very much happen at all.
  void Output::stop(){
    buffer.clear();
    parseData = false;
  }
  
  unsigned int Output::getKeyForTime(long unsigned int trackId, long long timeStamp){
    DTSC::Track & trk = myMeta.tracks[trackId];
    if (!trk.keys.size()){
      return 0;
    }
    unsigned int keyNo = trk.keys.begin()->getNumber();
    unsigned int partCount = 0;
    std::deque<DTSC::Key>::iterator it;
    for (it = trk.keys.begin(); it != trk.keys.end() && it->getTime() <= timeStamp; it++){
      keyNo = it->getNumber();
      partCount += it->getParts();
    }
    //if the time is before the next keyframe but after the last part, correctly seek to next keyframe
    if (partCount && it != trk.keys.end() && timeStamp > it->getTime() - trk.parts[partCount-1].getDuration()){
      ++keyNo;
    }
    return keyNo;
  }
  
  int Output::pageNumForKey(long unsigned int trackId, long long int keyNum){
    if (!indexPages.count(trackId)){
      char id[100];
      sprintf(id, "%s%lu", streamName.c_str(), trackId);
      indexPages[trackId].init(id, 8 * 1024);
    }
    for (int i = 0; i < indexPages[trackId].len / 8; i++){
      long amountKey = ntohl((((long long int*)indexPages[trackId].mapped)[i]) & 0xFFFFFFFF);
      if (amountKey == 0){continue;}
      long tmpKey = ntohl(((((long long int*)indexPages[trackId].mapped)[i]) >> 32) & 0xFFFFFFFF);
      if (tmpKey <= keyNum && ((tmpKey?tmpKey:1) + amountKey) > keyNum){
        return tmpKey;
      }
    }
    return -1;
  }
  
  void Output::loadPageForKey(long unsigned int trackId, long long int keyNum){
    if (myMeta.vod && keyNum > myMeta.tracks[trackId].keys.rbegin()->getNumber()){
      curPages.erase(trackId);
      currKeyOpen.erase(trackId);
      return;
    }
    DEBUG_MSG(DLVL_HIGH, "Loading track %lu, containing key %lld", trackId, keyNum);
    unsigned int timeout = 0;
    int pageNum = pageNumForKey(trackId, keyNum);
    while (pageNum == -1){
      if (!timeout){
        DEBUG_MSG(DLVL_DEVEL, "Requesting/waiting for page that has key %lu:%lld...", trackId, keyNum);
      }
      if (timeout++ > 100){
        DEBUG_MSG(DLVL_FAIL, "Timeout while waiting for requested page. Aborting.");
        curPages.erase(trackId);
        currKeyOpen.erase(trackId);
        return;
      }
      if (keyNum){
        nxtKeyNum[trackId] = keyNum-1;
      }else{
        nxtKeyNum[trackId] = 0;
      }
      stats();
      Util::sleep(100);
      pageNum = pageNumForKey(trackId, keyNum);
    }
    
    if (keyNum){
      nxtKeyNum[trackId] = keyNum-1;
    }else{
      nxtKeyNum[trackId] = 0;
    }
    stats();
    nxtKeyNum[trackId] = pageNum;
    
    if (currKeyOpen.count(trackId) && currKeyOpen[trackId] == (unsigned int)pageNum){
      return;
    }
    char id[100];
    snprintf(id, 100, "%s%lu_%d", streamName.c_str(), trackId, pageNum);
    curPages[trackId].init(id, DEFAULT_DATA_PAGE_SIZE);
    if (!(curPages[trackId].mapped)){
      DEBUG_MSG(DLVL_FAIL, "Initializing page %s failed", curPages[trackId].name.c_str());
      return;
    }
    currKeyOpen[trackId] = pageNum;
  }
  
  /// Prepares all tracks from selectedTracks for seeking to the specified ms position.
  void Output::seek(unsigned long long pos){
    sought = true;
    firstTime = Util::getMS() - pos;
    if (!isInitialized){
      initialize();
    }
    buffer.clear();
    currentPacket.null();
    updateMeta();
    DEBUG_MSG(DLVL_MEDIUM, "Seeking to %llums", pos);
    for (std::set<long unsigned int>::iterator it = selectedTracks.begin(); it != selectedTracks.end(); it++){
      seek(*it, pos);
    }
  }

  bool Output::seek(unsigned int tid, unsigned long long pos, bool getNextKey){
    loadPageForKey(tid, getKeyForTime(tid, pos) + (getNextKey?1:0));
    if (!curPages.count(tid) || !curPages[tid].mapped){
      DEBUG_MSG(DLVL_DEVEL, "Aborting seek to %llums in track %u, not available.", pos, tid);
      return false;
    }
    sortedPageInfo tmp;
    tmp.tid = tid;
    tmp.offset = 0;
    DTSC::Packet tmpPack;
    tmpPack.reInit(curPages[tid].mapped + tmp.offset, 0, true);
    tmp.time = tmpPack.getTime();
    while ((long long)tmp.time < pos && tmpPack){
      tmp.offset += tmpPack.getDataLen();
      tmpPack.reInit(curPages[tid].mapped + tmp.offset, 0, true);
      tmp.time = tmpPack.getTime();
    }
    if (tmpPack){
      buffer.insert(tmp);
      return true;
    }else{
      //don't print anything for empty packets - not sign of corruption, just unfinished stream.
      if (curPages[tid].mapped[tmp.offset] != 0){
        DEBUG_MSG(DLVL_FAIL, "Noes! Couldn't find packet on track %d because of some kind of corruption error or somesuch.", tid);
      }else{
        DEBUG_MSG(DLVL_FAIL, "Track %d no data (key %u @ %u) - waiting...", tid, getKeyForTime(tid, pos) + (getNextKey?1:0), tmp.offset);
        unsigned int i = 0;
        while (curPages[tid].mapped[tmp.offset] == 0 && ++i < 42){
          Util::wait(100);
        }
        if (curPages[tid].mapped[tmp.offset] == 0){
          DEBUG_MSG(DLVL_FAIL, "Track %d no data (key %u) - timeout", tid, getKeyForTime(tid, pos) + (getNextKey?1:0));
        }else{
          return seek(tid, pos, getNextKey);
        }
      }
      return false;
    }
  }
  
  void Output::requestHandler(){
    static bool firstData = true;//only the first time, we call onRequest if there's data buffered already.
    if ((firstData && myConn.Received().size()) || myConn.spool()){
      firstData = false;
      DEBUG_MSG(DLVL_DONTEVEN, "onRequest");
      onRequest();
    }else{
      if (!isBlocking && !parseData){
        Util::sleep(500);
      }
    }
  }
 
  int Output::run() {
    DEBUG_MSG(DLVL_MEDIUM, "MistOut client handler started");
    while (myConn.connected() && (wantRequest || parseData)){
      stats();
      if (wantRequest){
        requestHandler();
      }
      if (parseData){
        if (!isInitialized){
          initialize();
        }
        if ( !sentHeader){
          DEBUG_MSG(DLVL_DONTEVEN, "sendHeader");
          sendHeader();
        }
        prepareNext();
        if (currentPacket){
          sendNext();
        }else{
          if (!onFinish()){
            break;
          }
        }
      }
    }
    DEBUG_MSG(DLVL_MEDIUM, "MistOut client handler shutting down: %s, %s, %s", myConn.connected() ? "conn_active" : "conn_closed", wantRequest ? "want_request" : "no_want_request", parseData ? "parsing_data" : "not_parsing_data");
    stats();
    playerConn.finish();
    statsPage.finish();
    myConn.close();
    return 0;
  }
  
  /// Returns the ID of the main selected track, or 0 if no tracks are selected.
  /// The main track is the first video track, if any, and otherwise the first other track.
  long unsigned int Output::getMainSelectedTrack(){
    if (!selectedTracks.size()){
      return 0;
    }
    for (std::set<long unsigned int>::iterator it = selectedTracks.begin(); it != selectedTracks.end(); it++){
      if (myMeta.tracks[*it].type == "video"){
        return *it;
      }
    }
    return *(selectedTracks.begin());
  }
  
  void Output::prepareNext(){
    static int nonVideoCount = 0;
    if (!sought){
      if (myMeta.live){
        long unsigned int mainTrack = getMainSelectedTrack();
        unsigned long long seekPos = myMeta.tracks[mainTrack].keys.begin()->getTime();
        if (seekPos < 5000){
          seekPos = 0;
        }
        seek(seekPos);
      }else{
        seek(0);
      }
    }
    static unsigned int emptyCount = 0;
    if (!buffer.size()){
      currentPacket.null();
      DEBUG_MSG(DLVL_DEVEL, "Buffer completely played out");
      onFinish();
      return;
    }
    sortedPageInfo nxt = *(buffer.begin());
    buffer.erase(buffer.begin());

    DEBUG_MSG(DLVL_DONTEVEN, "Loading track %u (next=%lu), %llu ms", nxt.tid, nxtKeyNum[nxt.tid], nxt.time);
    
    if (nxt.offset >= curPages[nxt.tid].len){
      loadPageForKey(nxt.tid, ++nxtKeyNum[nxt.tid]);
      nxt.offset = 0;
      if (curPages.count(nxt.tid) && curPages[nxt.tid].mapped){
        if (getDTSCTime(curPages[nxt.tid].mapped, nxt.offset) < nxt.time){
          DEBUG_MSG(DLVL_DEVEL, "Time going backwards in track %u - dropping track.", nxt.tid);
        }else{
          nxt.time = getDTSCTime(curPages[nxt.tid].mapped, nxt.offset);
          buffer.insert(nxt);
        }
        prepareNext();
        return;
      }
    }
    
    if (!curPages.count(nxt.tid) || !curPages[nxt.tid].mapped){
      //mapping failure? Drop this track and go to next.
      //not an error - usually means end of stream.
      DEBUG_MSG(DLVL_DEVEL, "Track %u no page - dropping track.", nxt.tid);
      prepareNext();
      return;
    }
    
    //have we arrived at the end of the memory page? (4 zeroes mark the end)
    if (!memcmp(curPages[nxt.tid].mapped + nxt.offset, "\000\000\000\000", 4)){
      //if we don't currently know where we are, we're lost. We should drop the track.
      if (!nxt.time){
        DEBUG_MSG(DLVL_DEVEL, "Timeless empty packet on track %u - dropping track.", nxt.tid);
        prepareNext();
        return;
      }
      //if this is a live stream, we might have just reached the live point.
      //check where the next key is
      int nextPage = pageNumForKey(nxt.tid, nxtKeyNum[nxt.tid]+1);
      //are we live, and the next key hasn't shown up on another page? then we're waiting.
      if (myMeta.live && currKeyOpen.count(nxt.tid) && (currKeyOpen[nxt.tid] == (unsigned int)nextPage || nextPage == -1)){
        if (myMeta && ++emptyCount < 42){
          //we're waiting for new data. Simply retry.
          buffer.insert(nxt);
        }else{
          //after ~10 seconds, give up and drop the track.
          DEBUG_MSG(DLVL_DEVEL, "Empty packet on track %u @ key %lu (next=%d) - could not reload, dropping track.", nxt.tid, nxtKeyNum[nxt.tid]+1, nextPage);
        }
        //keep updating the metadata at 250ms intervals while waiting for more data
        Util::sleep(250);
        updateMeta();
      }else{
        //if we're not live, we've simply reached the end of the page. Load the next key.
        loadPageForKey(nxt.tid, ++nxtKeyNum[nxt.tid]);
        nxt.offset = 0;
        if (curPages.count(nxt.tid) && curPages[nxt.tid].mapped){
          unsigned long long nextTime = getDTSCTime(curPages[nxt.tid].mapped, nxt.offset);
          if (nextTime && nextTime < nxt.time){
            DEBUG_MSG(DLVL_DEVEL, "Time going backwards in track %u - dropping track.", nxt.tid);
          }else{
            if (nextTime){
              nxt.time = nextTime;
            }
            buffer.insert(nxt);
            DEBUG_MSG(DLVL_MEDIUM, "Next page for track %u starts at %llu.", nxt.tid, nxt.time);
          }
        }else{
          DEBUG_MSG(DLVL_DEVEL, "Could not load next memory page for track %u - dropping track.", nxt.tid);
        }
      }
      prepareNext();
      return;
    }
    currentPacket.reInit(curPages[nxt.tid].mapped + nxt.offset, 0, true);
    if (currentPacket){
      if (currentPacket.getTime() != nxt.time && nxt.time){
        DEBUG_MSG(DLVL_MEDIUM, "ACTUALLY Loaded track %ld (next=%lu), %llu ms", currentPacket.getTrackId(), nxtKeyNum[nxt.tid], currentPacket.getTime());
      }
      if ((myMeta.tracks[nxt.tid].type == "video" && currentPacket.getFlag("keyframe")) || (++nonVideoCount % 30 == 0)){
        if (myMeta.live){
          updateMeta();
        }
        nxtKeyNum[nxt.tid] = getKeyForTime(nxt.tid, currentPacket.getTime());
        DEBUG_MSG(DLVL_VERYHIGH, "Track %u @ %llums = key %lu", nxt.tid, currentPacket.getTime(), nxtKeyNum[nxt.tid]);
      }
      emptyCount = 0;
    }
    nxt.offset += currentPacket.getDataLen();
    if (realTime){
      while (nxt.time > (Util::getMS() - firstTime + maxSkipAhead)*1000/realTime) {
        Util::sleep(nxt.time - (Util::getMS() - firstTime + minSkipAhead)*1000/realTime);
      }
    }
    if (curPages[nxt.tid]){
      if (nxt.offset < curPages[nxt.tid].len){
        unsigned long long nextTime = getDTSCTime(curPages[nxt.tid].mapped, nxt.offset);
        if (nextTime){
          nxt.time = nextTime;
        }else{
          ++nxt.time;
        }
      }
      buffer.insert(nxt);
    }
    stats();
  }

  void Output::stats(){
    static bool setHost = true;
    if (!isInitialized){
      return;
    }
    if (statsPage.getData()){
      unsigned long long int now = Util::epoch();
      if (now != lastStats){
        lastStats = now;
        IPC::statExchange tmpEx(statsPage.getData());
        tmpEx.now(now);
        if (setHost){
          tmpEx.host(myConn.getBinHost());
          setHost = false;
        }
        tmpEx.crc(crc);
        tmpEx.streamName(streamName);
        tmpEx.connector(capa["name"].asString());
        tmpEx.up(myConn.dataUp());
        tmpEx.down(myConn.dataDown());
        tmpEx.time(now - myConn.connTime());
        if (currentPacket){
          tmpEx.lastSecond(currentPacket.getTime());
        }else{
          tmpEx.lastSecond(0);
        }
        statsPage.keepAlive();
      }
    }
    int tNum = 0;
    if (!playerConn.getData()){
      playerConn = IPC::sharedClient(streamName + "_users", 30, true);
      if (!playerConn.getData()){
        DEBUG_MSG(DLVL_WARN, "Player connection failure - aborting output");
        myConn.close();
        return;
      }
    }
    if (trackMap.size()){
      for (std::map<int, int>::iterator it = trackMap.begin(); it != trackMap.end() && tNum < 5; it++){
        unsigned int tId = it->second;
        char * thisData = playerConn.getData() + (6 * tNum);
        thisData[0] = ((tId >> 24) & 0xFF);
        thisData[1] = ((tId >> 16) & 0xFF);
        thisData[2] = ((tId >> 8) & 0xFF);
        thisData[3] = ((tId) & 0xFF);
        thisData[4] = 0xFF;
        thisData[5] = 0xFF;
        tNum ++;
      }
    }else{
      for (std::set<unsigned long>::iterator it = selectedTracks.begin(); it != selectedTracks.end() && tNum < 5; it++){
        unsigned int tId = *it;
        char * thisData = playerConn.getData() + (6 * tNum);
        thisData[0] = ((tId >> 24) & 0xFF);
        thisData[1] = ((tId >> 16) & 0xFF);
        thisData[2] = ((tId >> 8) & 0xFF);
        thisData[3] = ((tId) & 0xFF);
        thisData[4] = ((nxtKeyNum[tId] >> 8) & 0xFF);
        thisData[5] = ((nxtKeyNum[tId]) & 0xFF);
        tNum ++;
      }
    }
    playerConn.keepAlive();
    if (tNum >= 5){
      DEBUG_MSG(DLVL_WARN, "Too many tracks selected, using only first 5");
    }
  }
  
  void Output::onRequest(){
    //simply clear the buffer, we don't support any kind of input by default
    myConn.Received().clear();
    wantRequest = false;
  }

  void Output::sendHeader(){
    //just set the sentHeader bool to true, by default
    sentHeader = true;
  }
  
}
