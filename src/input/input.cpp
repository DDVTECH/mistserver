#include <semaphore.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <mist/defines.h>
#include "input.h"
#include <sstream>
#include <fstream>
#include <iterator>

namespace Mist {
  Input * Input::singleton = NULL;
  
  void Input::userCallback(char * data, size_t len, unsigned int id){
    for (int i = 0; i < 5; i++){
      unsigned long tid = ((unsigned long)(data[i*6]) << 24) | ((unsigned long)(data[i*6+1]) << 16) | ((unsigned long)(data[i*6+2]) << 8) | ((unsigned long)(data[i*6+3]));
      if (tid){
        unsigned long keyNum = ((unsigned long)(data[i*6+4]) << 8) | ((unsigned long)(data[i*6+5]));
        bufferFrame(tid, keyNum + 1);//Try buffer next frame
      }
    }
  }
  
  void Input::doNothing(char * data, size_t len, unsigned int id){
    DEBUG_MSG(DLVL_DONTEVEN, "Doing 'nothing'");
    singleton->userCallback(data, 30, id);//call the userCallback for this input
  }
  
  Input::Input(Util::Config * cfg){
    config = cfg;
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

  void Input::checkHeaderTimes(std::string streamFile){
    if ( streamFile == "-" ){
      return;
    }
    std::string headerFile = streamFile + ".dtsh";
    FILE * tmp = fopen(headerFile.c_str(),"r");
    if (tmp == NULL){
      DEBUG_MSG(DLVL_HIGH, "Can't open header: %s. Assuming all is fine.", headerFile.c_str() );  
      return;
    } 
    struct stat bufStream;
    struct stat bufHeader;
    //fstat(fileno(streamFile), &bufStream);
    //fstat(fileno(tmp), &bufHeader);
    if (stat(streamFile.c_str(), &bufStream) !=0 || stat(headerFile.c_str(), &bufHeader) !=0){
      DEBUG_MSG(DLVL_HIGH, "Could not compare stream and header timestamps - assuming all is fine.");
      fclose(tmp);
      return;
    }

    int timeStream = bufStream.st_mtime;
    int timeHeader = bufHeader.st_mtime;
    fclose(tmp);    
    if (timeHeader < timeStream){
      //delete filename
      INFO_MSG("Overwriting outdated DTSH header file: %s ",headerFile.c_str());
      remove(headerFile.c_str());
    }
  }

  int Input::run(){
    if (config->getBool("json")){
      std::cout << capa.toString() << std::endl;
      return 0;
    }
    if (!setup()){
      std::cerr << config->getString("cmd") << " setup failed." << std::endl;
      return 0;
    }
    checkHeaderTimes(config->getString("input"));
    if (!readHeader()){
      std::cerr << "Reading header for " << config->getString("input") << " failed." << std::endl;
      return 0;
    }
    parseHeader();
    
    if (!config->getString("streamname").size()){
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
        while (lastPack){
          newMeta.updatePosOverride(lastPack, bpos);
          file.write(lastPack.getData(), lastPack.getDataLen());
          bpos += lastPack.getDataLen();
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
    }else{
      //after this player functionality
      metaPage.init(config->getString("streamname"), (isBuffer ? DEFAULT_META_PAGE_SIZE : myMeta.getSendLen()), true);
      myMeta.writeTo(metaPage.mapped);
      userPage.init(config->getString("streamname") + "_users", 30, true);
      
      
      if (!isBuffer){
        for (std::map<unsigned int,DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++){
          bufferFrame(it->first, 1);
        }
      }
      
      DEBUG_MSG(DLVL_DONTEVEN,"Pre-While");
      
      long long int activityCounter = Util::bootSecs();
      while ((Util::bootSecs() - activityCounter) < 10){//10 second timeout
        Util::wait(1000);
        removeUnused();
        userPage.parseEach(doNothing);
        if (userPage.amount){
          activityCounter = Util::bootSecs();
          DEBUG_MSG(DLVL_INSANE, "Connected users: %d", userPage.amount);
        }else{
          DEBUG_MSG(DLVL_INSANE, "Timer running");
        }
      }
      DEBUG_MSG(DLVL_DEVEL,"Closing clean");
      //end player functionality
    }
    return 0;
  }

  void Input::removeUnused(){
    for (std::map<unsigned int, std::map<unsigned int, unsigned int> >::iterator it = pageCounter.begin(); it != pageCounter.end(); it++){
      for (std::map<unsigned int, unsigned int>::iterator it2 = it->second.begin(); it2 != it->second.end(); it2++){
        it2->second--;
      }
      bool change = true;
      while (change){
        change = false;
        for (std::map<unsigned int, unsigned int>::iterator it2 = it->second.begin(); it2 != it->second.end(); it2++){
          if (!it2->second){
            dataPages[it->first].erase(it2->first);
            pageCounter[it->first].erase(it2->first);
            for (int i = 0; i < 8192; i += 8){
              unsigned int thisKeyNum = ntohl(((((long long int *)(indexPages[it->first].mapped + i))[0]) >> 32) & 0xFFFFFFFF);
              if (thisKeyNum == it2->first){
                (((long long int *)(indexPages[it->first].mapped + i))[0]) = 0;
              }
            }
            change = true;
            break;
          }
        }
      }
    }
  }
  
  void Input::parseHeader(){
    DEBUG_MSG(DLVL_DONTEVEN,"Parsing the header");
    selectedTracks.clear();
    std::stringstream trackSpec;
    for (std::map<unsigned int, DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++) {
      DEBUG_MSG(DLVL_VERYHIGH, "Track %u encountered", it->first);
      if (trackSpec.str() != ""){
        trackSpec << " ";
      }
      trackSpec << it->first;
      DEBUG_MSG(DLVL_VERYHIGH, "Trackspec now %s", trackSpec.str().c_str());
      for (std::deque<DTSC::Key>::iterator it2 = it->second.keys.begin(); it2 != it->second.keys.end(); it2++){
        keyTimes[it->first].insert(it2->getTime());
      }
    }
    trackSelect(trackSpec.str());
    
    bool hasKeySizes = true;
    for (std::map<unsigned int, DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++){
      if (!it->second.keySizes.size()){
        hasKeySizes = false;
        break;
      }
    }
    if (hasKeySizes){
      for (std::map<unsigned int, DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++){
        char tmpId[20];
        sprintf(tmpId, "%u", it->first);
        DEBUG_MSG(DLVL_HIGH, "Making page %s", std::string(config->getString("streamname") + tmpId).c_str());
        indexPages[it->first].init(config->getString("streamname") + tmpId, 8 * 1024, true);//Pages of 8kb in size, room for 512 parts.
        bool newData = true;
        for (int i = 0; i < it->second.keys.size(); i++){
          if (newData){
            //i+1 because keys are 1-indexed
            pagesByTrack[it->first][i+1].firstTime = it->second.keys[i].getTime();
            newData = false;
          }
          pagesByTrack[it->first].rbegin()->second.keyNum++;
          pagesByTrack[it->first].rbegin()->second.partNum += it->second.keys[i].getParts();
          pagesByTrack[it->first].rbegin()->second.dataSize += it->second.keySizes[i];
          if (pagesByTrack[it->first].rbegin()->second.dataSize > FLIP_DATA_PAGE_SIZE){
            newData = true;
          }
        }
      }
    }else{
    std::map<int, DTSCPageData> curData;
    std::map<int, booking> bookKeeping;
    
    seek(0);
    getNext();

    while(lastPack){//loop through all
      unsigned int tid = lastPack.getTrackId();
      if (!tid){
        getNext(false);
        continue;
      }
      if (!bookKeeping.count(tid)){
        bookKeeping[tid].first = 1;
        bookKeeping[tid].curPart = 0;
        bookKeeping[tid].curKey = 0;
        
        curData[tid].lastKeyTime = 0xFFFFFFFF;
        curData[tid].keyNum = 1;
        curData[tid].partNum = 0;
        curData[tid].dataSize = 0;
        curData[tid].curOffset = 0;
        curData[tid].firstTime = myMeta.tracks[tid].keys[0].getTime();

        char tmpId[80];
        snprintf(tmpId, 80, "%s%u", config->getString("streamname").c_str(), tid);
        indexPages[tid].init(tmpId, 8 * 1024, true);//Pages of 8kb in size, room for 512 parts.
      }
      if (myMeta.tracks[tid].keys[bookKeeping[tid].curKey].getParts() + 1 == curData[tid].partNum){
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
      curData[tid].dataSize += lastPack.getDataLen();
      curData[tid].partNum ++;
      bookKeeping[tid].curPart ++;      
      DEBUG_MSG(DLVL_DONTEVEN, "Track %ld:%llu on page %d@%llu (len:%d), being part %d of key %d", lastPack.getTrackId(), lastPack.getTime(), bookKeeping[tid].first, curData[tid].dataSize, lastPack.getDataLen(), curData[tid].partNum, bookKeeping[tid].first+curData[tid].keyNum);
      getNext(false);
    }
    for (std::map<unsigned int, DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++) {
      if (curData.count(it->first) && !pagesByTrack[it->first].count(bookKeeping[it->first].first)){
        pagesByTrack[it->first][bookKeeping[it->first].first] = curData[it->first];
      }
    }
    }
    for (std::map<unsigned int, DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++){
      if (!pagesByTrack.count(it->first)){
	DEBUG_MSG(DLVL_WARN, "No pages for track %d found", it->first);
      }else{
	DEBUG_MSG(DLVL_MEDIUM, "Track %d (%s) split into %lu pages", it->first, myMeta.tracks[it->first].codec.c_str(), pagesByTrack[it->first].size());
	for (std::map<int, DTSCPageData>::iterator it2 = pagesByTrack[it->first].begin(); it2 != pagesByTrack[it->first].end(); it2++){
	  DEBUG_MSG(DLVL_VERYHIGH, "Page %u-%u, (%llu bytes)", it2->first, it2->first + it2->second.keyNum - 1, it2->second.dataSize);
	}
      }
    }
  }
  
  
  bool Input::bufferFrame(unsigned int track, unsigned int keyNum){
    if (keyNum < 1){keyNum = 1;}
    if (!pagesByTrack.count(track)){
      return false;
    }
    std::map<int, DTSCPageData>::iterator it = pagesByTrack[track].upper_bound(keyNum);
    if (it != pagesByTrack[track].begin()){
      it--;
    }
    unsigned int pageNum = it->first;
    pageCounter[track][pageNum] = 15;///Keep page 15seconds in memory after last use
    
    DEBUG_MSG(DLVL_DONTEVEN, "Attempting to buffer page %u key %d->%d", track, keyNum, pageNum);
    if (dataPages[track].count(pageNum)){
      return true;
    }
    char pageId[100];
    int pageIdLen = snprintf(pageId, 100, "%s%u_%u", config->getString("streamname").c_str(), track, pageNum);
    std::string tmpString(pageId, pageIdLen);
    dataPages[track][pageNum].init(tmpString, it->second.dataSize, true);
    DEBUG_MSG(DLVL_HIGH, "Buffering track %u page %u through %u datasize: %llu", track, pageNum, pageNum-1 + it->second.keyNum, it->second.dataSize);

    std::stringstream trackSpec;
    trackSpec << track;
    trackSelect(trackSpec.str());
    unsigned int keyIndex = pageNum-1;
    //if (keyIndex > 0){++keyIndex;}
    long long unsigned int startTime = myMeta.tracks[track].keys[keyIndex].getTime();
    long long unsigned int stopTime = myMeta.tracks[track].lastms + 1;
    if ((int)myMeta.tracks[track].keys.size() > keyIndex + it->second.keyNum){
      stopTime = myMeta.tracks[track].keys[keyIndex + it->second.keyNum].getTime();
    }
    DEBUG_MSG(DLVL_HIGH, "Buffering track %d from %d (%llus) to %d (%llus)", track, pageNum, startTime, pageNum-1 + it->second.keyNum, stopTime);
    seek(startTime);
    it->second.curOffset = 0;
    getNext();
    //in case earlier seeking was inprecise, seek to the exact point
    while (lastPack && lastPack.getTime() < startTime){
      getNext();
    }
    while (lastPack && lastPack.getTime() < stopTime){
      if (it->second.curOffset + lastPack.getDataLen() > pagesByTrack[track][pageNum].dataSize){
        DEBUG_MSG(DLVL_WARN, "Trying to write %u bytes on pos %llu where size is %llu (time: %llu / %llu, track %u page %u)", lastPack.getDataLen(), it->second.curOffset, pagesByTrack[track][pageNum].dataSize, lastPack.getTime(), stopTime, track, pageNum);
        break;
      }else{
//        DEBUG_MSG(DLVL_WARN, "Writing %u bytes on pos %llu where size is %llu (time: %llu / %llu, track %u page %u)", lastPack.getDataLen(), it->second.curOffset, pagesByTrack[track][pageNum].dataSize, lastPack.getTime(), stopTime, track, pageNum);
        memcpy(dataPages[track][pageNum].mapped + it->second.curOffset, lastPack.getData(), lastPack.getDataLen());
        it->second.curOffset += lastPack.getDataLen();
      }
      getNext();
    }
    for (int i = 0; i < indexPages[track].len / 8; i++){
      if (((long long int*)indexPages[track].mapped)[i] == 0){
        ((long long int*)indexPages[track].mapped)[i] = (((long long int)htonl(pageNum)) << 32) | htonl(it->second.keyNum);
        break;
      }
    }
    DEBUG_MSG(DLVL_HIGH, "Done buffering page %u for track %u", pageNum, track);
    return true;
  }
  
  bool Input::atKeyFrame(){
    static std::map<int, unsigned long long> lastSeen;
    //not in keyTimes? We're not at a keyframe.
    unsigned int c = keyTimes[lastPack.getTrackId()].count(lastPack.getTime());
    if (!c){
      return false;
    }
    //skip double times
    if (lastSeen.count(lastPack.getTrackId()) && lastSeen[lastPack.getTrackId()] == lastPack.getTime()){
      return false;
    }
    //set last seen, and return true
    lastSeen[lastPack.getTrackId()] = lastPack.getTime();
    return true;
  }
  
  void Input::play(int until){
    playing = -1;
    playUntil = until;
    initialTime = 0;
    benchMark = Util::getMS();
  }

  void Input::playOnce(){
    if (playing <= 0){
      playing = 1;
    }
    ++playing;
    benchMark = Util::getMS();
  }

  void Input::quitPlay(){
    playing = 0;
  }
}

