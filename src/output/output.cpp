/// Recording to file
/// 
/// Currently MistServer has basic support for recording for which the
/// functionality is spread over a couple of files. The general flow in
/// mist (this is my understanding and I'm a newb to MistServer, roxlu),
/// is like this:
/// 
/// The controller creates a couple of protocol handlers, e.g. for
/// RTMP. When a new live connection is made, an output is created through
/// this protocol handler.  In the case of a live source, all received
/// data is passed into a inputBuffer object (see input_buffer.cpp).
/// 
/// So, when the inputBuffer is created, the `setup()` function is
/// called. In this function the `config` object is available that holds
/// the configuration values for the specific stream. This is also where a
/// recording gets initialized.
/// 
/// An recording is initialized by starting another output with a call to
/// `startRecording()`.  `startRecording()` forks the current process and
/// then calls `execvp()` to take over the child process with
/// e.g. `MistOutFLV()`.  When `execvp()` starts the other process (that
/// records the data), it passes the `--outputFilename` command line
/// argument.
/// 
/// Each output checks if it's started with the `--outputFilename` flag;
/// this is done in the constructor of `Output`. In Output, it opens the
/// given filename and uses `dup2()` which makes sure that all `stdout`
/// data is written into the recording file.
/// 
/// Though, because some or probably most outputs also write HTTP to
/// stdout, I created the function `HTTPOutput::sendResponse()` which
/// checks if the current output is creating a recording. When creating a
/// recording it simply skips the HTTP output.
/// 
///      +-------------------------+
///      |  inputBuffer::setup()   |
///      +-------+-----------------+  
///              |
///              o---- calls Util::startRecording() (stream.cpp)
///              |
///              v 
///      +------------------------+
///      | stream::startRecording | -> Kicks off output app with --outputFilename
///      +-------+----------------+                
///              |
///              v
///      +----------------+
///      | MistOut[XXX]   | -> Checks if started with --outputFilename, 
///      +----------------+    in Output::Output() and starts recording. 
/// 
///  The following files contain updates that were made for the recording:
/// 
///  - stream.cpp         - startRecording()
///  - output.cpp         - Output(),                         - added --outputFilename option
///                         ~Output(),                        - closes the filedescriptor if opened.
///                         openOutputFileForRecording()      - opens the output file descriptor, uses dup2().
///                         closeOutputFileForRecording()     - closes the output file descriptor.
///  - input_buffer.cpp   - setup()                           - executes an MistOut[XXX] app.

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <semaphore.h>
#include <iterator> //std::distance

#include <mist/bitfields.h>
#include <mist/stream.h>
#include <mist/defines.h>
#include <mist/http_parser.h>
#include <mist/timing.h>
#include "output.h"

/*LTS-START*/
#include <mist/triggers.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>
/*LTS-END*/

namespace Mist {
  JSON::Value Output::capa = JSON::Value();

  int getDTSCLen(char * mapped, long long int offset){
    return Bit::btohl(mapped + offset + 4);
  }

  unsigned long long getDTSCTime(char * mapped, long long int offset){
    return Bit::btohll(mapped + offset + 12);
  }

  void Output::init(Util::Config * cfg){
    capa["optional"]["debug"]["name"] = "debug";
    capa["optional"]["debug"]["help"] = "The debug level at which messages need to be printed.";
    capa["optional"]["debug"]["option"] = "--debug";
    capa["optional"]["debug"]["type"] = "debug";
    capa["optional"]["startpos"]["name"] = "Starting position in live buffer";
    capa["optional"]["startpos"]["help"] = "For live, where in the buffer the stream starts playback by default. 0 = beginning, 1000 = end";
    capa["optional"]["startpos"]["option"] = "--startPos";
    capa["optional"]["startpos"]["short"] = "P";
    capa["optional"]["startpos"]["default"] = (long long)500;
    capa["optional"]["startpos"]["type"] = "uint";
    capa["optional"]["outputfilename"]["type"] = "str";
    capa["optional"]["outputfilename"]["name"] = "outputfilename";
    capa["optional"]["outputfilename"]["help"] = "Name of the file into which we write the recording.";
    capa["optional"]["outputfilename"]["option"] = "--outputFilename";
    capa["optional"]["outputfilename"]["short"] = "O";
    capa["optional"]["outputfilename"]["default"] = "";
  }
  
  Output::Output(Socket::Connection & conn) : myConn(conn) {
    firstTime = 0;
    crc = getpid();
    parseData = false;
    wantRequest = true;
    sought = false;
    isInitialized = false;
    isBlocking = false;
    completeKeysOnly = false;
    lastStats = 0;
    maxSkipAhead = 7500;
    minSkipAhead = 5000;
    realTime = 1000;
    completeKeyReadyTimeOut = false;
    if (myConn){
      setBlocking(true);
    }else{
      DEBUG_MSG(DLVL_WARN, "Warning: MistOut created with closed socket!");
    }
    sentHeader = false;
    /* begin-roxlu */
        outputFileDescriptor = -1;

    // When the stream has a output filename defined we open it so we can start recording.
    if (config != NULL
        && config->getString("outputfilename").size() != 0)
      {
        openOutputFileForRecording();
      }
    /* end-roxlu */
  }
  
  void Output::setBlocking(bool blocking){
    isBlocking = blocking;
    myConn.setBlocking(isBlocking);
  }
  
  /*begin-roxlu*/
  Output::~Output(){
    if (config != NULL && config->getString("outputfilename").size() != 0){
      closeOutputFileForRecording();
    }
  }
  /*end-roxlu*/
  
  void Output::updateMeta(){
    //read metadata from page to myMeta variable
    if (nProxy.metaPages[0].mapped){
      IPC::semaphore * liveSem = 0;
      if (myMeta.live){
        static char liveSemName[NAME_BUFFER_SIZE];
        snprintf(liveSemName, NAME_BUFFER_SIZE, SEM_LIVE, streamName.c_str());
        liveSem = new IPC::semaphore(liveSemName, O_CREAT | O_RDWR, ACCESSPERMS, 1);
        liveSem->wait();
      }
      DTSC::Packet tmpMeta(nProxy.metaPages[0].mapped, nProxy.metaPages[0].len, true);
      if (tmpMeta.getVersion()){
        myMeta.reinit(tmpMeta);
      }
      if (liveSem){
        liveSem->post();
        delete liveSem;
        liveSem = 0;
      }
    }
  }
  
  /// Called when stream initialization has failed.
  /// The standard implementation will set isInitialized to false and close the client connection,
  /// thus causing the process to exit cleanly.
  void Output::onFail(){
    isInitialized = false;
    wantRequest = true;
    parseData= false;
    streamName.clear();
    myConn.close();
  }

  /// \triggers 
  /// The `"CONN_PLAY"` trigger is stream-specific, and is ran when an active connection first opens a stream. Its payload is:
  /// ~~~~~~~~~~~~~~~
  /// streamname
  /// connected client host
  /// output handler name
  /// request URL (if any)
  /// ~~~~~~~~~~~~~~~
  /// The `"USER_NEW"` trigger is stream-specific, and is ran when a new user first opens a stream. Segmented protcols are unduplicated over the duration of the statistics log (~10 minutes), true streaming protocols (RTMP, RTSP) are not deduplicated as no duplication ever takes place. Its payload is:
  /// ~~~~~~~~~~~~~~~
  /// streamname
  /// connected client host
  /// User agent checksum (CRC32 of User-agent string)
  /// output handler name
  /// request URL (if any)
  /// ~~~~~~~~~~~~~~~
  void Output::initialize(){
    if (isInitialized){
      return;
    }
    if (nProxy.metaPages[0].mapped){
      return;
    }
    if (streamName.size() < 1){
      return; //abort - no stream to initialize...
    }
    isInitialized = true;
    reconnect();
    //if the connection failed, fail
    if (streamName.size() < 1){
      onFail();
      return;
    }
    selectDefaultTracks();
    sought = false;
    /*LTS-START*/
    if(Triggers::shouldTrigger("CONN_PLAY", streamName)){
      std::string payload = streamName+"\n" + getConnectedHost() +"\n"+capa["name"].asStringRef()+"\n"+reqUrl;
      if (!Triggers::doTrigger("CONN_PLAY", payload, streamName)){
        myConn.close();
      }
    }
    if(Triggers::shouldTrigger("USER_NEW", streamName)){
      //sync byte 0 = no sync yet, wait for sync from controller...
      IPC::statExchange tmpEx(statsPage.getData());
      unsigned int i = 0;
      while (!tmpEx.getSync() && i < 30){
        Util::sleep(100);
        stats();
      }
      HIGH_MSG("USER_NEW sync achieved: %u", (unsigned int)tmpEx.getSync());
      //1 = check requested (connection is new)
      if (tmpEx.getSync() == 1){
        std::string payload = streamName+"\n" + myConn.getHost() +"\n" + JSON::Value((long long)crc).asString() + "\n"+capa["name"].asStringRef()+"\n"+reqUrl;
        if (!Triggers::doTrigger("USER_NEW", payload, streamName)){
          MEDIUM_MSG("Closing connection because denied by USER_NEW trigger");
          myConn.close();
          tmpEx.setSync(100);//100 = denied
        }else{
          tmpEx.setSync(10);//10 = accepted
        }
      }
      //100 = denied
      if (tmpEx.getSync() == 100){
        myConn.close();
        MEDIUM_MSG("Closing connection because denied by USER_NEW sync byte");
      }
      //anything else = accepted
    }
    /*LTS-END*/
  }

  std::string Output::getConnectedHost(){
    return myConn.getHost();
  }

  std::string Output::getConnectedBinHost(){
    return myConn.getBinHost();
  }
 
  /// Connects or reconnects to the stream.
  /// Assumes streamName class member has been set already.
  /// Will start input if not currently active, calls onFail() if this does not succeed.
  /// After assuring stream is online, clears nProxy.metaPages, then sets nProxy.metaPages[0], statsPage and nProxy.userClient to (hopefully) valid handles.
  /// Finally, calls updateMeta()
  void Output::reconnect(){
    if (!Util::startInput(streamName)){
      DEBUG_MSG(DLVL_FAIL, "Opening stream failed - aborting initalization");
      onFail();
      return;
    }
    char pageId[NAME_BUFFER_SIZE];
    snprintf(pageId, NAME_BUFFER_SIZE, SHM_STREAM_INDEX, streamName.c_str());
    nProxy.metaPages.clear();
    nProxy.metaPages[0].init(pageId, DEFAULT_META_PAGE_SIZE);
    if (!nProxy.metaPages[0].mapped){
      FAIL_MSG("Could not connect to server for %s", streamName.c_str());
      onFail();
      return;
    }
    if (statsPage.getData()){
      statsPage.finish();
    }
    statsPage = IPC::sharedClient(SHM_STATISTICS, STAT_EX_SIZE, true);
    if (nProxy.userClient.getData()){
      nProxy.userClient.finish();
    }
    char userPageName[NAME_BUFFER_SIZE];
    snprintf(userPageName, NAME_BUFFER_SIZE, SHM_USERS, streamName.c_str());
    nProxy.userClient = IPC::sharedClient(userPageName, PLAY_EX_SIZE, true);
    updateMeta();
  }

  void Output::selectDefaultTracks(){
    if (!isInitialized){
      initialize();
      return;
    }
    //check which tracks don't actually exist
    std::set<unsigned long> toRemove;
    for (std::set<unsigned long>::iterator it = selectedTracks.begin(); it != selectedTracks.end(); it++){
      if (!myMeta.tracks.count(*it)){
        toRemove.insert(*it);
      }
    }
    //remove those from selectedtracks
    for (std::set<unsigned long>::iterator it = toRemove.begin(); it != toRemove.end(); it++){
      selectedTracks.erase(*it);
    }
    
    //loop through all codec combinations, count max simultaneous active
    unsigned int bestSoFar = 0;
    unsigned int bestSoFarCount = 0;
    unsigned int index = 0;
    jsonForEach(capa["codecs"], it) {
      unsigned int genCounter = 0;
      unsigned int selCounter = 0;
      if ((*it).size() > 0){
        jsonForEach((*it), itb) {
          if ((*itb).size() > 0){
            bool found = false;
            jsonForEach(*itb, itc) {
              for (std::set<unsigned long>::iterator itd = selectedTracks.begin(); itd != selectedTracks.end(); itd++){
                if (myMeta.tracks[*itd].codec == (*itc).asStringRef()){
                  selCounter++;
                  found = true;
                  break;
                }
              }
              if (!found){
                for (std::map<unsigned int,DTSC::Track>::iterator trit = myMeta.tracks.begin(); trit != myMeta.tracks.end(); trit++){
                  if (trit->second.codec == (*itc).asStringRef() || (*itc).asStringRef() == "*"){
                    genCounter++;
                    found = true;
                    if ((*itc).asStringRef() != "*"){
                      break;
                    }
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
      jsonForEach(capa["codecs"][bestSoFar], itb) {
        if ((*itb).size() && myMeta.tracks.size()){
          bool found = false;
          jsonForEach((*itb), itc) {
            INFO_MSG("Filling codec: '%s'", (*itc).asStringRef().c_str());
            if (found) {
              break;
            }
            for (std::set<unsigned long>::iterator itd = selectedTracks.begin(); itd != selectedTracks.end(); itd++){
              if (myMeta.tracks[*itd].codec == (*itc).asStringRef()){
                found = true;
                break;
              }
            }
            if (!found){
              for (std::map<unsigned int,DTSC::Track>::iterator trit = myMeta.tracks.begin(); trit != myMeta.tracks.end(); trit++){
                if (trit->second.codec == (*itc).asStringRef() || (*itc).asStringRef() == "*"){
                  selectedTracks.insert(trit->first);
                  found = true;
                  if ((*itc).asStringRef() != "*"){
                    break;
                  }
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
    
    /*begin-roxlu*/
    // Added this check while working on the recording, because when the output cant
    // select a track it means it won't be able to start the recording. Therefore
    // when we don't see this explicitly it makes debugging the recording feature
    // a bit painfull :) 
    if (selectedTracks.size() == 0) {
      INSANE_MSG("We didn't find any tracks which that we can use. selectedTrack.size() is 0.");
      for (std::map<unsigned int,DTSC::Track>::iterator trit = myMeta.tracks.begin(); trit != myMeta.tracks.end(); trit++){
        INSANE_MSG("Found track/codec: %s", trit->second.codec.c_str());
      }
      static std::string source;
      if (!source.size()){
        IPC::sharedPage serverCfg(SHM_CONF, DEFAULT_CONF_PAGE_SIZE, false, false); ///< Contains server configuration and capabilities
        IPC::semaphore configLock(SEM_CONF, O_CREAT | O_RDWR, ACCESSPERMS, 1);
        configLock.wait();
        std::string smp = streamName.substr(0, streamName.find_first_of("+ "));
        //check if smp (everything before + or space) exists
        DTSC::Scan streamCfg = DTSC::Scan(serverCfg.mapped, serverCfg.len).getMember("streams").getMember(smp);
        if (streamCfg){
          source = streamCfg.getMember("source").asString();
        }
        configLock.post();
        configLock.close();
      }
      if (!myMeta.tracks.size() && (source.find("dtsc://") == 0)){
        //Wait 5 seconds and try again. Keep a counter, try at most 3 times
        static int counter = 0;
        if (counter++ < 10){
          Util::wait(1000);
          nProxy.userClient.keepAlive();
          stats();
          updateMeta();
          selectDefaultTracks();
        }
      }
    }
    /*end-roxlu*/
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
    if (!nProxy.metaPages.count(trackId)){
      char id[NAME_BUFFER_SIZE];
      snprintf(id, NAME_BUFFER_SIZE, SHM_TRACK_INDEX, streamName.c_str(), trackId);
      nProxy.metaPages[trackId].init(id, 8 * 1024);
    }
    int len = nProxy.metaPages[trackId].len / 8;
    for (int i = 0; i < len; i++){
      int * tmpOffset = (int *)(nProxy.metaPages[trackId].mapped + (i * 8));
      long amountKey = ntohl(tmpOffset[1]);
      if (amountKey == 0){continue;}
      long tmpKey = ntohl(tmpOffset[0]);
      if (tmpKey <= keyNum && ((tmpKey?tmpKey:1) + amountKey) > keyNum){
        return tmpKey;
      }
    }
    return -1;
  }
  
  void Output::loadPageForKey(long unsigned int trackId, long long int keyNum){
    if (myMeta.vod && keyNum > myMeta.tracks[trackId].keys.rbegin()->getNumber()){
      nProxy.curPage.erase(trackId);
      currKeyOpen.erase(trackId);
      return;
    }
    DEBUG_MSG(DLVL_VERYHIGH, "Loading track %lu, containing key %lld", trackId, keyNum);
    unsigned int timeout = 0;
    unsigned long pageNum = pageNumForKey(trackId, keyNum);
    while (pageNum == -1){
      if (!timeout){
        DEBUG_MSG(DLVL_HIGH, "Requesting page with key %lu:%lld", trackId, keyNum);
      }
      ++timeout;
      //if we've been waiting for this page for 3 seconds, reconnect to the stream - something might be going wrong...
      if (timeout == 30){
        DEVEL_MSG("Loading is taking longer than usual, reconnecting to stream %s...", streamName.c_str());
        reconnect();
      }
      if (timeout > 100){
        DEBUG_MSG(DLVL_FAIL, "Timeout while waiting for requested page %lld for track %lu. Aborting.", keyNum, trackId);
        nProxy.curPage.erase(trackId);
        currKeyOpen.erase(trackId);
        return;
      }
      if (keyNum){
        nxtKeyNum[trackId] = keyNum-1;
      }else{
        nxtKeyNum[trackId] = 0;
      }
      stats();
      Util::wait(100);
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
    char id[NAME_BUFFER_SIZE];
    snprintf(id, NAME_BUFFER_SIZE, SHM_TRACK_DATA, streamName.c_str(), trackId, pageNum);
    nProxy.curPage[trackId].init(id, DEFAULT_DATA_PAGE_SIZE);
    if (!(nProxy.curPage[trackId].mapped)){
      DEBUG_MSG(DLVL_FAIL, "Initializing page %s failed", nProxy.curPage[trackId].name.c_str());
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
    thisPacket.null();
    if (myMeta.live){
      updateMeta();
    }
    DEBUG_MSG(DLVL_MEDIUM, "Seeking to %llums", pos);
    for (std::set<long unsigned int>::iterator it = selectedTracks.begin(); it != selectedTracks.end(); it++){
      seek(*it, pos);
    }
  }

  bool Output::seek(unsigned int tid, unsigned long long pos, bool getNextKey){
    loadPageForKey(tid, getKeyForTime(tid, pos) + (getNextKey?1:0));
    if (!nProxy.curPage.count(tid) || !nProxy.curPage[tid].mapped){
      INFO_MSG("Aborting seek to %llums in track %u, not available.", pos, tid);
      return false;
    }
    sortedPageInfo tmp;
    tmp.tid = tid;
    tmp.offset = 0;
    DTSC::Packet tmpPack;
    tmpPack.reInit(nProxy.curPage[tid].mapped + tmp.offset, 0, true);
    tmp.time = tmpPack.getTime();
    char * mpd = nProxy.curPage[tid].mapped;
    while ((long long)tmp.time < pos && tmpPack){
      tmp.offset += tmpPack.getDataLen();
      tmpPack.reInit(mpd + tmp.offset, 0, true);
      tmp.time = tmpPack.getTime();
    }
    if (tmpPack){
      buffer.insert(tmp);
      return true;
    }else{
      //don't print anything for empty packets - not sign of corruption, just unfinished stream.
      if (nProxy.curPage[tid].mapped[tmp.offset] != 0){
        DEBUG_MSG(DLVL_FAIL, "Noes! Couldn't find packet on track %d because of some kind of corruption error or somesuch.", tid);
      }else{
        VERYHIGH_MSG("Track %d no data (key %u @ %u) - waiting...", tid, getKeyForTime(tid, pos) + (getNextKey?1:0), tmp.offset);
        unsigned int i = 0;
        while (nProxy.curPage[tid].mapped[tmp.offset] == 0 && ++i < 42){
          Util::wait(100);
        }
        if (nProxy.curPage[tid].mapped[tmp.offset] == 0){
          FAIL_MSG("Track %d no data (key %u) - timeout", tid, getKeyForTime(tid, pos) + (getNextKey?1:0));
        }else{
          return seek(tid, pos, getNextKey);
        }
      }
      return false;
    }
  }

  /*LTS-START*/
  bool Output::onList(std::string ip, std::string list){
    if (list == ""){
      return false;
    }
    std::string entry;
    std::string lowerIpv6;//lower-case
    std::string upperIpv6;//full-caps
    do{
      entry = list.substr(0,list.find(" "));//make sure we have a single entry
      lowerIpv6 = "::ffff:" + entry;
      upperIpv6 = "::FFFF:" + entry;
      if (entry == ip || lowerIpv6 == ip || upperIpv6 == ip){
        return true;
      }
      long long unsigned int starPos = entry.find("*");
      if (starPos == std::string::npos){
        if (ip == entry){
          return true;
        }
      }else{
        if (starPos == 0){//beginning of the filter
          if (ip.substr(ip.length() - entry.size() - 1) == entry.substr(1)){
            return true;
          }
        }else{
          if (starPos == entry.size() - 1){//end of the filter
            if (ip.find(entry.substr(0, entry.size() - 1)) == 0 ){
              return true;
            }
            if (ip.find(entry.substr(0, lowerIpv6.size() - 1)) == 0 ){
              return true;
            }
            if (ip.find(entry.substr(0, upperIpv6.size() - 1)) == 0 ){
              return true;
            }
          }else{
            Log("CONF","Invalid list entry detected: " + entry);
          }
        }
      }
      list.erase(0, entry.size() + 1);
    }while (list != "");
    return false;
  }

  void Output::Log(std::string type, std::string message){
    /// \todo These logs need to show up in the controller.
    /// \todo Additionally, the triggering and untriggering of limits should be recorded in the controller as well.
    if (type == "HLIM"){
      DEBUG_MSG(DLVL_HIGH, "HardLimit Triggered: %s", message.c_str());
    }
    if (type == "SLIM"){
      DEBUG_MSG(DLVL_HIGH, "SoftLimit Triggered: %s", message.c_str());
    }
  }
  
  std::string Output::hostLookup(std::string ip){
    struct sockaddr_in6 sa;
    char hostName[1024];
    char service[20];
    if (inet_pton(AF_INET6, ip.c_str(), &(sa.sin6_addr)) != 1){
      return "\n";
    }
    sa.sin6_family = AF_INET6;
    sa.sin6_port = 0;
    sa.sin6_flowinfo = 0;
    sa.sin6_scope_id = 0;
    int tmpRet = getnameinfo((struct sockaddr*)&sa, sizeof sa, hostName, sizeof hostName, service, sizeof service, NI_NAMEREQD );
    if ( tmpRet == 0){
      return hostName;
    }
    return "";
  }
  
  bool Output::isBlacklisted(std::string host, std::string streamName, int timeConnected){
    return false;//blacklisting temporarily disabled for performance reasons
    JSON::Value Storage = JSON::fromFile(Util::getTmpFolder() + "streamlist");
    std::string myHostName = hostLookup(host);
    if (myHostName == "\n"){
      return false;
    }
    std::string myCountryName = getCountry(host);
    bool hasWhitelist = false;
    bool hostOnWhitelist = false;
    if (Storage["streams"].isMember(streamName)){
      if (Storage["streams"][streamName].isMember("limits") && Storage["streams"][streamName]["limits"].size()){
        jsonForEach(Storage["streams"][streamName]["limits"], limitIt){
          if ((*limitIt)["name"].asString() == "host"){
            if ((*limitIt)["value"].asString()[0] == '+'){
              if (!onList(host, (*limitIt)["value"].asString().substr(1))){
                if (myHostName == ""){
                  if (timeConnected > Storage["config"]["limit_timeout"].asInt()){
                    return true;
                  }
                }else{
                  if ( !onList(myHostName, (*limitIt)["value"].asStringRef().substr(1))){
                    if ((*limitIt)["type"].asStringRef() == "hard"){
                      Log("HLIM", "Host " + host + " not whitelisted for stream " + streamName);
                      return true;
                    }else{
                      Log("SLIM", "Host " + host + " not whitelisted for stream " + streamName);
                    }
                  }
                }
              }
            }else{
              if ((*limitIt)["value"].asStringRef().size() > 1 && (*limitIt)["value"].asStringRef()[0] == '-'){
                if (onList(host, (*limitIt)["value"].asStringRef().substr(1))){
                  if ((*limitIt)["type"].asStringRef() == "hard"){
                    Log("HLIM", "Host " + host + " blacklisted for stream " + streamName);
                    return true;
                  }else{
                    Log("SLIM", "Host " + host + " blacklisted for stream " + streamName);
                  }
                }
                if (myHostName != "" && onList(myHostName, (*limitIt)["value"].asString().substr(1))){
                  if ((*limitIt)["type"].asStringRef() == "hard"){
                    Log("HLIM", "Host " + myHostName + " blacklisted for stream " + streamName);
                    return true;
                  }else{
                    Log("SLIM", "Host " + myHostName + " blacklisted for stream " + streamName);
                  }
                }
              }
            }
          }
          if ((*limitIt)["name"].asString() == "geo"){
            if ((*limitIt)["value"].asString()[0] == '+'){
              if (myCountryName == ""){
                if ((*limitIt)["type"].asString() == "hard"){
                  Log("HLIM", "Host " + host + " with unknown location blacklisted for stream " + streamName);
                  return true;
                }else{
                  Log("SLIM", "Host " + host + " with unknown location blacklisted for stream " + streamName);
                }
              }
              if (!onList(myCountryName, (*limitIt)["value"].asString().substr(1))){
                if ((*limitIt)["type"].asString() == "hard"){
                  Log("HLIM", "Host " + host + " with location " + myCountryName + " not whitelisted for stream " + streamName);
                  return true;
                }else{
                  Log("SLIM", "Host " + host + " with location " + myCountryName + " not whitelisted for stream " + streamName);
                }
              }
            }else{
              if ((*limitIt)["value"].asString()[0] == '-'){
                if (onList(myCountryName, (*limitIt)["value"].asString().substr(1))){
                  if ((*limitIt)["type"].asString() == "hard"){
                    Log("HLIM", "Host " + host + " with location " + myCountryName + " blacklisted for stream " + streamName);
                    return true;
                  }else{
                    Log("SLIM", "Host " + host + " with location " + myCountryName + " blacklisted for stream " + streamName);
                  }
                }
              }
            }
          }
        }
      }
    }
    if (Storage["config"]["limits"].size()){
      jsonForEach(Storage["config"]["limits"], limitIt){
        if ((*limitIt)["name"].asString() == "host"){
          if ((*limitIt)["value"].asString()[0] == '+'){
            if (!onList(host, (*limitIt)["value"].asString().substr(1))){
              if (myHostName == ""){
                if (timeConnected > Storage["config"]["limit_timeout"].asInt()){
                  return true;
                }
              }else{
                if ( !onList(myHostName, (*limitIt)["value"].asString().substr(1))){
                  if ((*limitIt)["type"].asString() == "hard"){
                    Log("HLIM", "Host " + host + " not whitelisted for stream " + streamName);
                    return true;
                  }else{
                    Log("SLIM", "Host " + host + " not whitelisted for stream " + streamName);
                  }
                }
              }
            }
          }else{
            if ((*limitIt)["value"].asString()[0] == '-'){
              if (onList(host, (*limitIt)["value"].asString().substr(1))){
                if ((*limitIt)["type"].asString() == "hard"){
                  Log("HLIM", "Host " + host + " blacklisted for stream " + streamName);
                  return true;
                }else{
                  Log("SLIM", "Host " + host + " blacklisted for stream " + streamName);
                }
              }
              if (myHostName != "" && onList(myHostName, (*limitIt)["value"].asString().substr(1))){
                if ((*limitIt)["type"].asString() == "hard"){
                  Log("HLIM", "Host " + myHostName + " blacklisted for stream " + streamName);
                  return true;
                }else{
                  Log("SLIM", "Host " + myHostName + " blacklisted for stream " + streamName);
                }
              }
            }
          }
        }
        if ((*limitIt)["name"].asString() == "geo"){
          if ((*limitIt)["value"].asString()[0] == '+'){
            if (myCountryName == ""){
              if ((*limitIt)["type"].asString() == "hard"){
                Log("HLIM", "Host " + host + " with unknown location blacklisted for stream " + streamName);
                return true;
              }else{
                Log("SLIM", "Host " + host + " with unknown location blacklisted for stream " + streamName);
              }
            }
            if (!onList(myCountryName, (*limitIt)["value"].asString().substr(1))){
              if ((*limitIt)["type"].asString() == "hard"){
                Log("HLIM", "Host " + host + " with location " + myCountryName + " not whitelisted for stream " + streamName);
                return true;
              }else{
                Log("SLIM", "Host " + host + " with location " + myCountryName + " not whitelisted for stream " + streamName);
              }
            }
          }else{
            if ((*limitIt)["value"].asStringRef().size() > 1 && (*limitIt)["value"].asStringRef()[0] == '-'){
              if (onList(myCountryName, (*limitIt)["value"].asStringRef().substr(1))){
                if ((*limitIt)["type"].asString() == "hard"){
                  Log("HLIM", "Host " + host + " with location " + myCountryName + " blacklisted for stream " + streamName);
                  return true;
                }else{
                  Log("SLIM", "Host " + host + " with location " + myCountryName + " blacklisted for stream " + streamName);
                }
              }
            }
          }
        }
      }
    }
    if (hasWhitelist){
      if (hostOnWhitelist || myHostName == ""){
        return false;
      }else{
        return true;
      }
    }
    return false;
  }

  #ifdef GEOIP
  GeoIP * Output::geoIP4 = 0;
  GeoIP * Output::geoIP6 = 0;
  #endif
  std::string Output::getCountry(std::string ip){
    char * code = NULL;
    #ifdef GEOIP
    if (geoIP4){
      code = (char*)GeoIP_country_code_by_addr(geoIP4, ip.c_str());
    }
    if (!code && geoIP6){
      code = (char*)GeoIP_country_code_by_addr_v6(geoIP6, ip.c_str());
    }
    #endif
    if (!code){
      return "";
    }
    return code;
  }
  /*LTS-END*/
  
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
 
  /// \triggers 
  /// The `"CONN_OPEN"` trigger is stream-specific, and is ran when a connection is made or passed to a new handler. Its payload is:
  /// ~~~~~~~~~~~~~~~
  /// streamname
  /// connected client host
  /// output handler name
  /// request URL (if any)
  /// ~~~~~~~~~~~~~~~
  /// The `"CONN_CLOSE"` trigger is stream-specific, and is ran when a connection closes. Its payload is:
  /// ~~~~~~~~~~~~~~~
  /// streamname
  /// connected client host
  /// output handler name
  /// request URL (if any)
  /// ~~~~~~~~~~~~~~~
  int Output::run() {
    /*LTS-START*/
    if(Triggers::shouldTrigger("CONN_OPEN", streamName)){
      std::string payload = streamName+"\n" + getConnectedHost() +"\n"+capa["name"].asStringRef()+"\n"+reqUrl;
      if (!Triggers::doTrigger("CONN_OPEN", payload, streamName)){
        return 1;
      }
    }
    /*LTS-END*/
    DEBUG_MSG(DLVL_MEDIUM, "MistOut client handler started");
    while (config->is_active && myConn.connected() && (wantRequest || parseData)){
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
          bool waitLonger = false;
          if (!myMeta.tracks.size()){
            waitLonger = true;
          }else{
            for (std::map<unsigned int, DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++){
              if (!it->second.keys.size()){
                waitLonger = true;
                break;
              }
            }
          }
          if (waitLonger){
            updateMeta();
            Util::sleep(1000);
            static unsigned int metaTries = 0;
            if(++metaTries < 7){
              continue;
            }
          }
          sendHeader();
        }
        prepareNext();
        if (thisPacket){
          sendNext();
        }else{
          if (!onFinish()){
            break;
          }
        }
      }
    }
    DEBUG_MSG(DLVL_MEDIUM, "MistOut client handler shutting down: %s, %s, %s", myConn.connected() ? "conn_active" : "conn_closed", wantRequest ? "want_request" : "no_want_request", parseData ? "parsing_data" : "not_parsing_data");
    
    /*LTS-START*/
    if(Triggers::shouldTrigger("CONN_CLOSE", streamName)){
      std::string payload = streamName+"\n"+getConnectedHost()+"\n"+capa["name"].asStringRef()+"\n"+reqUrl; ///\todo generate payload
      Triggers::doTrigger("CONN_CLOSE", payload, streamName); //no stream specified    
    }
    /*LTS-END*/
  
    stats();
    nProxy.userClient.finish();
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
        if (myMeta.tracks[mainTrack].keys.size() < 2){
          if (!myMeta.tracks[mainTrack].keys.size()){
            myConn.close();
            return;
          }else{
            seek(myMeta.tracks[mainTrack].keys.begin()->getTime());
            prepareNext();
            return;
          }
        }
        unsigned long long seekPos = myMeta.tracks[mainTrack].keys.rbegin()->getTime();
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
      thisPacket.null();
      DEBUG_MSG(DLVL_DEVEL, "Buffer completely played out");
      onFinish();
      /*LTS-START*/      
      if(Triggers::shouldTrigger("CONN_STOP", streamName)){
        std::string payload = streamName+"\n" + getConnectedHost() +"\n"+capa["name"].asStringRef()+"\n";
        Triggers::doTrigger("CONN_STOP", payload, streamName);
      }
      /*LTS-END*/
      return;
    }
    sortedPageInfo nxt = *(buffer.begin());
    buffer.erase(buffer.begin());

    DEBUG_MSG(DLVL_DONTEVEN, "Loading track %u (next=%lu), %llu ms", nxt.tid, nxtKeyNum[nxt.tid], nxt.time);
    
    if (nxt.offset >= nProxy.curPage[nxt.tid].len){
      nxtKeyNum[nxt.tid] = getKeyForTime(nxt.tid, thisPacket.getTime());
      loadPageForKey(nxt.tid, ++nxtKeyNum[nxt.tid]);
      nxt.offset = 0;
      if (nProxy.curPage.count(nxt.tid) && nProxy.curPage[nxt.tid].mapped){
        if (getDTSCTime(nProxy.curPage[nxt.tid].mapped, nxt.offset) < nxt.time){
          ERROR_MSG("Time going backwards in track %u - dropping track.", nxt.tid);
        }else{
          nxt.time = getDTSCTime(nProxy.curPage[nxt.tid].mapped, nxt.offset);
          buffer.insert(nxt);
        }
        prepareNext();
        return;
      }
    }
    
    if (!nProxy.curPage.count(nxt.tid) || !nProxy.curPage[nxt.tid].mapped){
      //mapping failure? Drop this track and go to next.
      //not an error - usually means end of stream.
      DEBUG_MSG(DLVL_DEVEL, "Track %u no page - dropping track.", nxt.tid);
      prepareNext();
      return;
    }
    
    //have we arrived at the end of the memory page? (4 zeroes mark the end)
    if (!memcmp(nProxy.curPage[nxt.tid].mapped + nxt.offset, "\000\000\000\000", 4)){
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
          //roxlu edited this line:
          DEBUG_MSG(DLVL_DEVEL, "Empty packet on track %u (%s) @ key %lu (next=%d) - could not reload, dropping track.", nxt.tid, myMeta.tracks[nxt.tid].type.c_str(), nxtKeyNum[nxt.tid]+1, nextPage);
        }
        //keep updating the metadata at 250ms intervals while waiting for more data
        Util::sleep(250);
        updateMeta();
      }else{
        //if we're not live, we've simply reached the end of the page. Load the next key.
        nxtKeyNum[nxt.tid] = getKeyForTime(nxt.tid, thisPacket.getTime());
        loadPageForKey(nxt.tid, ++nxtKeyNum[nxt.tid]);
        nxt.offset = 0;
        if (nProxy.curPage.count(nxt.tid) && nProxy.curPage[nxt.tid].mapped){
          unsigned long long nextTime = getDTSCTime(nProxy.curPage[nxt.tid].mapped, nxt.offset);
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
    thisPacket.reInit(nProxy.curPage[nxt.tid].mapped + nxt.offset, 0, true);
    if (thisPacket){
      if (thisPacket.getTime() != nxt.time && nxt.time){
        WARN_MSG("Loaded track %ld@%llu instead of %ld@%llu", thisPacket.getTrackId(), thisPacket.getTime(), nxt.tid, nxt.time);
      }
      if ((myMeta.tracks[nxt.tid].type == "video" && thisPacket.getFlag("keyframe")) || (++nonVideoCount % 30 == 0)){
        if (myMeta.live){
          updateMeta();
        }
        nxtKeyNum[nxt.tid] = getKeyForTime(nxt.tid, thisPacket.getTime());
        DEBUG_MSG(DLVL_VERYHIGH, "Track %u @ %llums = key %lu", nxt.tid, thisPacket.getTime(), nxtKeyNum[nxt.tid]);
      }
      emptyCount = 0;
    }
    nxt.offset += thisPacket.getDataLen();
    if (realTime){
      while (nxt.time > (((Util::getMS() - firstTime)*1000)+maxSkipAhead)/realTime) {
        Util::sleep(nxt.time - (((Util::getMS() - firstTime)*1000)+minSkipAhead)/realTime);
      }
    }
    //delay the stream until its current keyframe is complete
    if (completeKeysOnly){
      bool completeKeyReady = false;
      int timeoutTries = 40;//attempts to updateMeta before timeOut and moving on; default is approximately 10 seconds
      for (std::map<unsigned int, DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++){
        if (it->second.keys.size() >1){
          int thisTimeoutTries = ((it->second.lastms - it->second.firstms) / (it->second.keys.size()-1)) / 125;
          if (thisTimeoutTries > timeoutTries) timeoutTries = thisTimeoutTries;
        }
      }
      while(!completeKeyReady && timeoutTries>0){
        completeKeyReady = true;
        for (std::set<unsigned long>::iterator it = selectedTracks.begin(); it != selectedTracks.end(); it++){
          if (!myMeta.tracks[*it].keys.size() || myMeta.tracks[*it].keys.rbegin()->getTime() + myMeta.tracks[*it].keys.rbegin()->getLength() <= nxt.time ){
            completeKeyReady = false;
            break;
          }
        }
        if (!completeKeyReady){
          if (completeKeyReadyTimeOut){
            INSANE_MSG("Complete Key not ready and time-out is being skipped");
            timeoutTries = 0;
          }else{
            INSANE_MSG("Timeout: %d",timeoutTries);
            timeoutTries--;//we count down
            stats();
            Util::wait(250);
            updateMeta();
          }
        }
      }
      if (timeoutTries<=0){
        if (!completeKeyReadyTimeOut){
          INFO_MSG("Wait for keyframe Timeout triggered! Ended to avoid endless loops");
        }
        completeKeyReadyTimeOut = true;
      }else{
        //untimeout handling
        completeKeyReadyTimeOut = false;
      }
    }
    if (nProxy.curPage[nxt.tid]){
      if (nxt.offset < nProxy.curPage[nxt.tid].len){
        unsigned long long nextTime = getDTSCTime(nProxy.curPage[nxt.tid].mapped, nxt.offset);
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
    if (!isInitialized){
      return;
    }
    if (statsPage.getData()){
      unsigned long long int now = Util::epoch();
      if (now != lastStats){
        /*LTS-START*/
        if (statsPage.getData()[-1] > 127){
          myConn.close();
          return;
        }
        /*LTS-END*/
        lastStats = now;
        IPC::statExchange tmpEx(statsPage.getData());
        tmpEx.now(now);
        if (tmpEx.host() == std::string("\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000", 16)){
          tmpEx.host(getConnectedBinHost());
        }
        tmpEx.crc(crc);
        tmpEx.streamName(streamName);
        tmpEx.connector(capa["name"].asString());
        tmpEx.up(myConn.dataUp());
        tmpEx.down(myConn.dataDown());
        tmpEx.time(now - myConn.connTime());
        if (thisPacket){
          tmpEx.lastSecond(thisPacket.getTime());
        }else{
          tmpEx.lastSecond(0);
        }
        statsPage.keepAlive();
      }
    }
    int tNum = 0;
    if (!nProxy.userClient.getData()){
      char userPageName[NAME_BUFFER_SIZE];
      snprintf(userPageName, NAME_BUFFER_SIZE, SHM_USERS, streamName.c_str());
      nProxy.userClient = IPC::sharedClient(userPageName, PLAY_EX_SIZE, true);
      if (!nProxy.userClient.getData()){
        DEBUG_MSG(DLVL_WARN, "Player connection failure - aborting output");
        myConn.close();
        return;
      }
    }
    if (nProxy.userClient.getData()[-1] > 127){
      myConn.close();
      return;
    }
    if (!nProxy.trackMap.size()){
      IPC::userConnection userConn(nProxy.userClient.getData());
      for (std::set<unsigned long>::iterator it = selectedTracks.begin(); it != selectedTracks.end() && tNum < SIMUL_TRACKS; it++){
        userConn.setTrackId(tNum, *it);
        userConn.setKeynum(tNum, nxtKeyNum[*it]);
        tNum ++;
      }
    }
    nProxy.userClient.keepAlive();
    if (tNum > SIMUL_TRACKS){
      DEBUG_MSG(DLVL_WARN, "Too many tracks selected, using only first %d", SIMUL_TRACKS);
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
  
  /*begin-roxlu*/
  bool Output::openOutputFileForRecording() {

    if (NULL == config) {
      FAIL_MSG("Cannot open the output file for recording because the config member is NULL and we can't check if we actually want a recording.");
      return false;
    }

    // We won't open the output file when the user didn't set the outputfile through the admin. 
    if (config->getString("outputfilename").size() == 0) {
      FAIL_MSG("Cannot open the output file for recording because the given name is empty.");
      return false;
    }
    
    if (outputFileDescriptor != -1) {
      FAIL_MSG("Cannot open the output file for recording because it seems that it's already open. Make sure it's closed correctly.");
      return false;
    }

    // The RECORDING_START trigger needs to be execute before we open the file because
    // the trigger may need to create some directories where we need to save the recording.
    if (Triggers::shouldTrigger("RECORDING_START")) {

      if (0 == config->getString("streamname").size()) {
        ERROR_MSG("Streamname is empty; the RECORDING_START trigger will not know what stream started it's recording. We do execute the trigger.");
      }

      std::string payload = config->getString("streamname");
      Triggers::doTrigger("RECORDING_START", payload, streamName.c_str());
    }

    // Open the output file.
    int flags = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
    int mode = O_RDWR | O_CREAT | O_TRUNC;
    
    outputFileDescriptor = open(config->getString("outputfilename").c_str(), mode, flags);
    if (outputFileDescriptor < 0) {
      ERROR_MSG("Failed to open the file that we want to use to store the recording, error: %s", strerror(errno));
      return false;
    }
    
    // Make a copy of the socket into outputFileDescriptor. Whenever we write to the socket we write to file.
    int r = dup2(outputFileDescriptor, myConn.getSocket());
    if (r == -1) {
      ERROR_MSG("Failed to create an alias for the socket using dup2: %s.", strerror(errno));
      return false;
    }
    
    //make this output ready for recording to file
    onRecord();
    
    INFO_MSG("Opened %s for recording.", config->getString("outputfilename").c_str());

    return true;
  }

  bool Output::closeOutputFileForRecording() {

    if (config == NULL) {
      ERROR_MSG("Config member is NULL, we cannot close the output file for the recording.");
      return false;
    }

    if (outputFileDescriptor == -1) {
      ERROR_MSG("Requested to close the output file for the recording, but we're not making a recording.");
      return false;
    }

    if  (config->getString("outputfilename").size() == 0) {
      ERROR_MSG("Requested to close the output file for the recording, but the output filename is empty; not supposed to happen. We're still going to close the file descriptor though.");
    }

    if (close(outputFileDescriptor) < 0) {
      FAIL_MSG("Error: failed to close the output file: %s. We're resetting the file descriptor anyway.", strerror(errno));
    }

    outputFileDescriptor = -1;

    INFO_MSG("Close the file for the recording: %s", config->getString("outputfilename").c_str());

    if (Triggers::shouldTrigger("RECORDING_STOP")) {
      
      if (0 == config->getString("streamname").size()) {
        ERROR_MSG("Streamname is empty; the RECORDING_STOP trigger will not know what stream stopped it's recording. We do execute the trigger.");
      }
      
      std::string payload;
      payload = config->getString("streamname") +"\n";
      payload += config->getString("outputfilename");
      
      Triggers::doTrigger("RECORDING_STOP", payload, streamName.c_str());
    }
    
    return true;
  }
  /*end-roxlu*/
  bool Output::recording(){
    if (config->getString("outputfilename").size() > 0) {
      DONTEVEN_MSG("We're not sending a HTTP response because we're currently creating a recording.");
      return true;
    }
    return false;
  }

}
