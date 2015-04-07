#include <semaphore.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <mist/stream.h>
#include <mist/defines.h>
#include "input.h"
#include <sstream>
#include <fstream>
#include <iterator>

namespace Mist {
  Input * Input::singleton = NULL;

  void Input::userCallback(char * data, size_t len, unsigned int id) {
    for (int i = 0; i < 5; i++) {
      unsigned long tid = ((unsigned long)(data[i * 6]) << 24) | ((unsigned long)(data[i * 6 + 1]) << 16) | ((unsigned long)(data[i * 6 + 2]) << 8) | ((unsigned long)(data[i * 6 + 3]));
      if (tid) {
        unsigned long keyNum = ((unsigned long)(data[i * 6 + 4]) << 8) | ((unsigned long)(data[i * 6 + 5]));
        bufferFrame(tid, keyNum + 1);//Try buffer next frame
      }
    }
  }

  void Input::callbackWrapper(char * data, size_t len, unsigned int id) {
    singleton->userCallback(data, 30, id);//call the userCallback for this input
  }

  Input::Input(Util::Config * cfg) : InOutBase() {
    config = cfg;
#ifdef INPUT_LIVE
    standAlone = false;
#else
    standAlone = true;
#endif

    JSON::Value option;
    option["long"] = "json";
    option["short"] = "j";
    option["help"] = "Output MistIn info in JSON format, then exit";
    option["value"].append(0ll);
    config->addOption("json", option);
    option.null();
    option["arg_num"] = 1ll;
    option["arg"] = "string";
    option["help"] = "Name of the input file or - for stdin";
    option["value"].append("-");
    config->addOption("input", option);
    option.null();
    option["arg_num"] = 2ll;
    option["arg"] = "string";
    option["help"] = "Name of the output file or - for stdout";
    option["value"].append("-");
    config->addOption("output", option);
    option.null();
    option["arg"] = "string";
    option["short"] = "s";
    option["long"] = "stream";
    option["help"] = "The name of the stream that this connector will provide in player mode";
    config->addOption("streamname", option);

    /*LTS-START*/
    //Encryption
    option["arg"] = "string";
    option["long"] = "verimatrix-playready";
    option["short"] = "P";
    option["help"] = "URL of the Verimatrix PlayReady keyserver";
    config->addOption("verimatrix-playready", option);
    capa["optional"]["verimatrix-playready"]["name"] = "Verimatrix PlayReady Server";
    capa["optional"]["verimatrix-playready"]["help"] = "URL of the Verimatrix PlayReady keyserver";
    capa["optional"]["verimatrix-playready"]["option"] = "--verimatrix-playready";
    capa["optional"]["verimatrix-playready"]["type"] = "str";
    capa["optional"]["verimatrix-playready"]["default"] = "";
    option.null();

    /*LTS-END*/
    capa["optional"]["debug"]["name"] = "debug";
    capa["optional"]["debug"]["help"] = "The debug level at which messages need to be printed.";
    capa["optional"]["debug"]["option"] = "--debug";
    capa["optional"]["debug"]["type"] = "debug";

    packTime = 0;
    lastActive = Util::epoch();
    playing = 0;
    playUntil = 0;

    singleton = this;
    isBuffer = false;
  }

  void Input::checkHeaderTimes(std::string streamFile) {
    if (streamFile == "-") {
      return;
    }
    std::string headerFile = streamFile + ".dtsh";
    FILE * tmp = fopen(headerFile.c_str(), "r");
    if (tmp == NULL) {
      DEBUG_MSG(DLVL_HIGH, "Can't open header: %s. Assuming all is fine.", headerFile.c_str());
      return;
    }
    struct stat bufStream;
    struct stat bufHeader;
    //fstat(fileno(streamFile), &bufStream);
    //fstat(fileno(tmp), &bufHeader);
    if (stat(streamFile.c_str(), &bufStream) != 0 || stat(headerFile.c_str(), &bufHeader) != 0) {
      DEBUG_MSG(DLVL_HIGH, "Could not compare stream and header timestamps - assuming all is fine.");
      fclose(tmp);
      return;
    }

    int timeStream = bufStream.st_mtime;
    int timeHeader = bufHeader.st_mtime;
    fclose(tmp);
    if (timeHeader < timeStream) {
      //delete filename
      INFO_MSG("Overwriting outdated DTSH header file: %s ", headerFile.c_str());
      remove(headerFile.c_str());
    }
  }

  int Input::run() {
    if (streamName != "") {
      config->getOption("streamname") = streamName;
    }
    streamName = config->getString("streamname");
    if (config->getBool("json")) {
      std::cout << capa.toString() << std::endl;
      return 0;
    }
    if (!setup()) {
      std::cerr << config->getString("cmd") << " setup failed." << std::endl;
      return 0;
    }
//Do not read the header if this is a live stream
#ifndef INPUT_LIVE
    checkHeaderTimes(config->getString("input"));
    if (!readHeader()) {
      std::cerr << "Reading header for " << config->getString("input") << " failed." << std::endl;
      return 0;
    }
    parseHeader();
#endif

//Live inputs only have a serve() mode
#ifndef INPUT_LIVE
    if (!config->getString("streamname").size()){
      convert();
    }else{
#endif
      serve();
#ifndef INPUT_LIVE
    }
#endif
    return 0;
  }

  void Input::convert(){
    //check filename for no -
    if (config->getString("output") != "-"){
      std::string filename = config->getString("output");
      if (filename.size() < 5 || filename.substr(filename.size() - 5) != ".dtsc"){
        filename += ".dtsc";
      }
      //output to dtsc
      DTSC::Meta newMeta = myMeta;
      newMeta.reset();
      std::ofstream file(filename.c_str());
      long long int bpos = 0;
      seek(0);
      getNext();
      while (thisPacket){
        newMeta.updatePosOverride(thisPacket, bpos);
        file.write(thisPacket.getData(), thisPacket.getDataLen());
        bpos += thisPacket.getDataLen();
        getNext();
      }
      //close file
      file.close();
      //create header
      file.open((filename+".dtsh").c_str());
      file << newMeta.toJSON().toNetPacked();
      file.close();
    }else{
      DEBUG_MSG(DLVL_FAIL,"No filename specified, exiting");
    }
  }
  
  void Input::serve(){
    char userPageName[NAME_BUFFER_SIZE];
    snprintf(userPageName, NAME_BUFFER_SIZE, SHM_USERS, streamName.c_str());
#ifdef INPUT_LIVE
    Util::startInput(streamName);
    userClient = IPC::sharedClient(userPageName, 30, true);
    getNext();
    while (thisPacket || config->is_active){
      unsigned long tid = thisPacket.getTrackId();
      //Check for eligibility of track
      IPC::userConnection userConn(userClient.getData());
      if (trackOffset.count(tid) && !userConn.getTrackId(trackOffset[tid])){
        trackOffset.erase(tid);
        trackState.erase(tid);
        trackMap.erase(tid);
        trackBuffer.erase(tid);
        pagesByTrack.erase(tid);
        metaPages.erase(tid);
        curPageNum.erase(tid);
        curPage.erase(tid);
        INFO_MSG("Erasing track %d", tid);
        continue;
      }
      if (thisPacket){
        continueNegotiate(thisPacket.getTrackId());
        bufferLivePacket(thisPacket);
      }
      getNext();
      userClient.keepAlive();
    }
    userClient.finish();
#else
    userPage.init(userPageName, PLAY_EX_SIZE, true);
    if (!isBuffer){
      for (std::map<unsigned int,DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++){
        bufferFrame(it->first, 1);
      }
    }
    
    DEBUG_MSG(DLVL_DEVEL,"Input for stream %s started", streamName.c_str());
    
    long long int activityCounter = Util::bootSecs();
    while ((Util::bootSecs() - activityCounter) < 10 && config->is_active){//10 second timeout
      Util::wait(1000);
      userPage.parseEach(callbackWrapper);
      removeUnused();
      if (userPage.amount){
        activityCounter = Util::bootSecs();
        DEBUG_MSG(DLVL_INSANE, "Connected users: %d", userPage.amount);
      }else{
        DEBUG_MSG(DLVL_INSANE, "Timer running");
      }
    }
#endif
    finish();
    DEBUG_MSG(DLVL_DEVEL,"Input for stream %s closing clean", streamName.c_str());
    //end player functionality
  }

  void Input::finish() {
    for (std::map<unsigned int, std::map<unsigned int, unsigned int> >::iterator it = pageCounter.begin(); it != pageCounter.end(); it++) {
      for (std::map<unsigned int, unsigned int>::iterator it2 = it->second.begin(); it2 != it->second.end(); it2++) {
        it2->second = 1;
      }
    }
    removeUnused();
    if (standAlone) {
      for (std::map<unsigned long, IPC::sharedPage>::iterator it = metaPages.begin(); it != metaPages.end(); it++) {
        it->second.master = true;
      }
    }
  }

  void Input::removeUnused() {
    for (std::map<unsigned int, std::map<unsigned int, unsigned int> >::iterator it = pageCounter.begin(); it != pageCounter.end(); it++) {
      for (std::map<unsigned int, unsigned int>::iterator it2 = it->second.begin(); it2 != it->second.end(); it2++) {
        it2->second--;
      }
      bool change = true;
      while (change) {
        change = false;
        for (std::map<unsigned int, unsigned int>::iterator it2 = it->second.begin(); it2 != it->second.end(); it2++) {
          if (!it2->second) {
            bufferRemove(it->first, it2->first);
            pageCounter[it->first].erase(it2->first);
            for (int i = 0; i < 8192; i += 8) {
              unsigned int thisKeyNum = ntohl(((((long long int *)(metaPages[it->first].mapped + i))[0]) >> 32) & 0xFFFFFFFF);
              if (thisKeyNum == it2->first) {
                (((long long int *)(metaPages[it->first].mapped + i))[0]) = 0;
              }
            }
            change = true;
            break;
          }
        }
      }
    }
  }

  void Input::parseHeader() {
    DEBUG_MSG(DLVL_DONTEVEN, "Parsing the header");
    selectedTracks.clear();
    std::stringstream trackSpec;
    for (std::map<unsigned int, DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++) {
      DEBUG_MSG(DLVL_VERYHIGH, "Track %u encountered", it->first);
      if (trackSpec.str() != "") {
        trackSpec << " ";
      }
      trackSpec << it->first;
      DEBUG_MSG(DLVL_VERYHIGH, "Trackspec now %s", trackSpec.str().c_str());
      for (std::deque<DTSC::Key>::iterator it2 = it->second.keys.begin(); it2 != it->second.keys.end(); it2++) {
        keyTimes[it->first].insert(it2->getTime());
      }
    }
    trackSelect(trackSpec.str());

    bool hasKeySizes = true;
    for (std::map<unsigned int, DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++) {
      if (!it->second.keySizes.size()) {
        hasKeySizes = false;
        break;
      }
    }
    if (hasKeySizes) {
      for (std::map<unsigned int, DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++) {
        bool newData = true;
        for (int i = 0; i < it->second.keys.size(); i++) {
          if (newData) {
            //i+1 because keys are 1-indexed
            pagesByTrack[it->first][i + 1].firstTime = it->second.keys[i].getTime();
            newData = false;
          }
          pagesByTrack[it->first].rbegin()->second.keyNum++;
          pagesByTrack[it->first].rbegin()->second.partNum += it->second.keys[i].getParts();
          pagesByTrack[it->first].rbegin()->second.dataSize += it->second.keySizes[i];
          if (pagesByTrack[it->first].rbegin()->second.dataSize > FLIP_DATA_PAGE_SIZE) {
            newData = true;
          }
        }
      }
    } else {
      std::map<int, DTSCPageData> curData;
      std::map<int, booking> bookKeeping;

      seek(0);
      getNext();

      while (thisPacket) { //loop through all
        unsigned int tid = thisPacket.getTrackId();
        if (!tid) {
          getNext(false);
          continue;
        }
        if (!bookKeeping.count(tid)) {
          bookKeeping[tid].first = 1;
          bookKeeping[tid].curPart = 0;
          bookKeeping[tid].curKey = 0;

          curData[tid].lastKeyTime = 0xFFFFFFFF;
          curData[tid].keyNum = 1;
          curData[tid].partNum = 0;
          curData[tid].dataSize = 0;
          curData[tid].curOffset = 0;
          curData[tid].firstTime = myMeta.tracks[tid].keys[0].getTime();

        }
        if (myMeta.tracks[tid].keys[bookKeeping[tid].curKey].getParts() + 1 == curData[tid].partNum) {
          if (curData[tid].dataSize > FLIP_DATA_PAGE_SIZE) {
            pagesByTrack[tid][bookKeeping[tid].first] = curData[tid];
            bookKeeping[tid].first += curData[tid].keyNum;
            curData[tid].keyNum = 0;
            curData[tid].dataSize = 0;
            curData[tid].firstTime = myMeta.tracks[tid].keys[bookKeeping[tid].curKey].getTime();
          }
          bookKeeping[tid].curKey++;
          curData[tid].keyNum++;
          curData[tid].partNum = 0;
        }
        curData[tid].dataSize += thisPacket.getDataLen();
        curData[tid].partNum ++;
        bookKeeping[tid].curPart ++;
        DEBUG_MSG(DLVL_DONTEVEN, "Track %ld:%llu on page %d@%llu (len:%d), being part %lu of key %lu", thisPacket.getTrackId(), thisPacket.getTime(), bookKeeping[tid].first, curData[tid].dataSize, thisPacket.getDataLen(), curData[tid].partNum, bookKeeping[tid].first + curData[tid].keyNum);
        getNext(false);
      }
      for (std::map<unsigned int, DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++) {
        if (curData.count(it->first) && !pagesByTrack[it->first].count(bookKeeping[it->first].first)) {
          pagesByTrack[it->first][bookKeeping[it->first].first] = curData[it->first];
        }
      }
    }
    for (std::map<unsigned int, DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++) {
      if (!pagesByTrack.count(it->first)) {
        DEBUG_MSG(DLVL_WARN, "No pages for track %d found", it->first);
      } else {
        DEBUG_MSG(DLVL_MEDIUM, "Track %d (%s) split into %lu pages", it->first, myMeta.tracks[it->first].codec.c_str(), pagesByTrack[it->first].size());
        for (std::map<unsigned long, DTSCPageData>::iterator it2 = pagesByTrack[it->first].begin(); it2 != pagesByTrack[it->first].end(); it2++) {
          DEBUG_MSG(DLVL_VERYHIGH, "Page %lu-%lu, (%llu bytes)", it2->first, it2->first + it2->second.keyNum - 1, it2->second.dataSize);
        }
      }
    }
  }
  
  
  bool Input::bufferFrame(unsigned int track, unsigned int keyNum){
    VERYHIGH_MSG("bufferFrame for stream %s, track %u, key %u", streamName.c_str(), track, keyNum);
    if (keyNum >= myMeta.tracks[track].keys.size()){
      //End of movie here, returning true to avoid various error messages
      VERYHIGH_MSG("Key number is higher than total key count. Cancelling bufferFrame"); 
      return true;
    }
    if (keyNum < 1) {
      keyNum = 1;
    }
    if (isBuffered(track, keyNum)) {
      //get corresponding page number
      int pageNumber = 0;
      for (std::map<unsigned long, DTSCPageData>::iterator it = pagesByTrack[track].begin(); it != pagesByTrack[track].end(); it++) {
        if (it->first <= keyNum) {
          pageNumber = it->first;
        } else {
          break;
        }
      }
      pageCounter[track][pageNumber] = 15;
      VERYHIGH_MSG("Track %u, key %u is already buffered in page %d. Cancelling bufferFrame", track, keyNum, pageNumber); 
      return true;
    }
    if (!pagesByTrack.count(track)){
      WARN_MSG("No pages for track %u found! Cancelling bufferFrame", track); 
      return false;
    }
    //Update keynum to point to the corresponding page
    INFO_MSG("Loading key %u from page %lu", keyNum, (--(pagesByTrack[track].upper_bound(keyNum)))->first);
    keyNum = (--(pagesByTrack[track].upper_bound(keyNum)))->first;
    if (!bufferStart(track, keyNum)){
      WARN_MSG("bufferStart failed! Cancelling bufferFrame"); 
      return false;
    }

    std::stringstream trackSpec;
    trackSpec << track;
    trackSelect(trackSpec.str());
    seek(myMeta.tracks[track].keys[keyNum - 1].getTime());
    long long unsigned int stopTime = myMeta.tracks[track].lastms + 1;
    if ((int)myMeta.tracks[track].keys.size() > keyNum - 1 + pagesByTrack[track][keyNum].keyNum) {
      stopTime = myMeta.tracks[track].keys[keyNum - 1 + pagesByTrack[track][keyNum].keyNum].getTime();
    }
    DEBUG_MSG(DLVL_HIGH, "Playing from %llu to %llu", myMeta.tracks[track].keys[keyNum - 1].getTime(), stopTime);
    getNext();
    //in case earlier seeking was inprecise, seek to the exact point
    while (thisPacket && thisPacket.getTime() < (unsigned long long)myMeta.tracks[track].keys[keyNum - 1].getTime()) {
      getNext();
    }
    while (thisPacket && thisPacket.getTime() < stopTime) {
      bufferNext(thisPacket);
      getNext();
    }
    bufferFinalize(track);
    DEBUG_MSG(DLVL_DEVEL, "Done buffering page %d for track %d", keyNum, track);
    pageCounter[track][keyNum] = 15;
    return true;
  }

  bool Input::atKeyFrame() {
    static std::map<int, unsigned long long> lastSeen;
    //not in keyTimes? We're not at a keyframe.
    unsigned int c = keyTimes[thisPacket.getTrackId()].count(thisPacket.getTime());
    if (!c) {
      return false;
    }
    //skip double times
    if (lastSeen.count(thisPacket.getTrackId()) && lastSeen[thisPacket.getTrackId()] == thisPacket.getTime()) {
      return false;
    }
    //set last seen, and return true
    lastSeen[thisPacket.getTrackId()] = thisPacket.getTime();
    return true;
  }

  void Input::play(int until) {
    playing = -1;
    playUntil = until;
    initialTime = 0;
    benchMark = Util::getMS();
  }

  void Input::playOnce() {
    if (playing <= 0) {
      playing = 1;
    }
    ++playing;
    benchMark = Util::getMS();
  }

  void Input::quitPlay() {
    playing = 0;
  }
}

