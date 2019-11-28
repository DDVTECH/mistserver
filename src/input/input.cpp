#include <fcntl.h>
#include <semaphore.h>
#include <sys/stat.h>

#include "input.h"
#include <fstream>
#include <iomanip>
#include <iterator>
#include <mist/auth.h>
#include <mist/defines.h>
#include <mist/downloader.h>
#include <mist/encode.h>
#include <mist/procs.h>
#include <mist/stream.h>
#include <mist/triggers.h>
#include <sstream>
#include <sys/wait.h>

namespace Mist{
  Util::Config *Input::config = NULL;

  void Input::userLeadIn(){connectedUsers = 0;}
  void Input::userOnActive(size_t id){
    ++connectedUsers;
    size_t track = users.getTrack(id);
    size_t key = users.getKeyNum(id);
    uint64_t time = M.getTimeForKeyIndex(track, key);
    size_t endKey = M.getKeyIndexForTime(track, time + 20000);
    for (size_t i = key; i <= endKey; i++){bufferFrame(track, i);}
  }
  void Input::userOnDisconnect(size_t id){}
  void Input::userLeadOut(){}

  void Input::reloadClientMeta(){
    if (M.getStreamName() != "" && M.getMaster()){return;}
    if (M.getStreamName() != streamName){
      meta.reInit(streamName, false);
    }else{
      meta.refresh();
    }
  }

  bool Input::hasMeta() const{return M && M.getStreamName() != "" && M.getValidTracks().size();}

  Input::Input(Util::Config *cfg) : InOutBase(){
    config = cfg;
    standAlone = true;

    JSON::Value option;
    option["long"] = "json";
    option["short"] = "j";
    option["help"] = "Output MistIn info in JSON format, then exit";
    option["value"].append(0);
    config->addOption("json", option);
    option.null();
    option["arg_num"] = 1;
    option["arg"] = "string";
    option["help"] = "Name of the input file or - for stdin";
    option["value"].append("-");
    config->addOption("input", option);
    option.null();
    option["arg_num"] = 2;
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
    option.null();
    option["short"] = "H";
    option["long"] = "headerOnly";
    option["value"].append(0u);
    option["help"] = "Generate .dtsh, then exit";
    config->addOption("headeronly", option);

    /*LTS-START*/
    /*
    //Encryption
    option.null();
    option["arg"] = "string";
    option["short"] = "e";
    option["long"] = "encryption";
    option["help"] = "a KID:KEY combo for auto-encrypting tracks";
    config->addOption("encryption", option);

    option.null();
    option["arg"] = "string";
    option["short"] = "B";
    option["long"] = "buydrm";
    option["help"] = "Your BuyDRM user string";
    config->addOption("buydrm", option);

    option.null();
    option["arg"] = "string";
    option["short"] = "C";
    option["long"] = "contentid";
    option["help"] = "The content ID for this stream, defaults to an md5 hash of the stream name";
    config->addOption("contentid", option);

    option.null();
    option["arg"] = "string";
    option["short"] = "K";
    option["long"] = "keyid";
    option["help"] = "The Key ID for this stream, defaults to an md5 hash of the content key";
    config->addOption("keyid", option);

    option.null();
    option["arg"] = "string";
    option["short"] = "W";
    option["long"] = "widevine";
    option["help"] = "The header to use for Widevine encryption.";
    config->addOption("widevine", option);

    option.null();
    option["arg"] = "string";
    option["short"] = "P";
    option["long"] = "playready";
    option["help"] = "The header to use for PlayReady encryption";
    config->addOption("playready", option);

    capa["optional"]["encryption"]["name"] = "encryption";
    capa["optional"]["encryption"]["help"] = "Encryption key.";
    capa["optional"]["encryption"]["option"] = "--encryption";
    capa["optional"]["encryption"]["type"] = "string";

    capa["optional"]["buydrm"]["name"] = "buydrm";
    capa["optional"]["buydrm"]["help"] = "BuyDRM User Key.";
    capa["optional"]["buydrm"]["option"] = "--buydrm";
    capa["optional"]["buydrm"]["type"] = "string";

    capa["optional"]["contentid"]["name"] = "contentid";
    capa["optional"]["contentid"]["help"] = "The content ID to use for this stream's encryption.
    Defaults to an md5 hash of the stream name."; capa["optional"]["contentid"]["option"] =
    "--contentid"; capa["optional"]["contentid"]["type"] = "string";

    capa["optional"]["keyid"]["name"] = "keyid";
    capa["optional"]["keyid"]["help"] = "The Key ID to use for this stream's encryption. Defaults to
    an md5 hash of the content key."; capa["optional"]["keyid"]["option"] = "--keyid";
    capa["optional"]["keyid"]["type"] = "string";

    capa["optional"]["widevine"]["name"] = "widevine";
    capa["optional"]["widevine"]["help"] = "The header to use for Widevine encryption.";
    capa["optional"]["widevine"]["option"] = "--widevine";
    capa["optional"]["widevine"]["type"] = "string";

    capa["optional"]["playready"]["name"] = "playready";
    capa["optional"]["playready"]["help"] = "The header to use for PlayReady encryption.";
    capa["optional"]["playready"]["option"] = "--playready";
    capa["optional"]["playready"]["type"] = "string";
    */

    option.null();
    option["long"] = "realtime";
    option["short"] = "r";
    option["help"] = "Feed the results of this input in realtime to the buffer";
    config->addOption("realtime", option);
    capa["optional"]["realtime"]["name"] = "Simulated Live";
    capa["optional"]["realtime"]["help"] = "Make this input run as a simulated live stream";
    capa["optional"]["realtime"]["option"] = "--realtime";

    option.null();
    option["long"] = "simulated-starttime";
    option["arg"] = "integer";
    option["short"] = "S";
    option["help"] = "Unix timestamp on which the simulated start of the stream is based.";
    option["value"].append(0);
    config->addOption("simulated-starttime", option);
    capa["optional"]["simulated-starttime"]["name"] = "Simulated start time";
    capa["optional"]["simulated-starttime"]["help"] =
        "The unix timestamp on which this stream is assumed to have started playback, or 0 for "
        "automatic";
    capa["optional"]["simulated-starttime"]["option"] = "--simulated-starttime";
    capa["optional"]["simulated-starttime"]["type"] = "uint";
    capa["optional"]["simulated-starttime"]["default"] = 0;

    /*LTS-END*/
    capa["optional"]["debug"]["name"] = "debug";
    capa["optional"]["debug"]["help"] = "The debug level at which messages need to be printed.";
    capa["optional"]["debug"]["option"] = "--debug";
    capa["optional"]["debug"]["type"] = "debug";

    hasSrt = false;
    srtTrack = 0;
  }

  void Input::checkHeaderTimes(std::string streamFile){
    struct stat bufStream;
    struct stat bufHeader;
    struct stat srtStream;

    std::string srtFile = streamFile + ".srt";
    if (stat(srtFile.c_str(), &srtStream) == 0){
      hasSrt = true;
      srtSource.open(srtFile.c_str());
      INFO_MSG("File %s opened as srt source", srtFile.c_str());
    }

    if (stat(streamFile.c_str(), &bufStream) != 0){
      INSANE_MSG("Source is not a file - ignoring header check");
      return;
    }
    std::string headerFile = streamFile + ".dtsh";
    if (stat(headerFile.c_str(), &bufHeader) != 0){
      INSANE_MSG("No header exists to compare - ignoring header check");
      return;
    }
    // the same second is not enough - add a 15 second window where we consider it too old
    if (bufHeader.st_mtime < bufStream.st_mtime + 15){
      INFO_MSG("Overwriting outdated DTSH header file: %s ", headerFile.c_str());
      remove(headerFile.c_str());
    }

    // the same second is not enough - add a 15 second window where we consider it too old
    if (hasSrt && bufHeader.st_mtime < srtStream.st_mtime + 15){
      INFO_MSG("Overwriting outdated DTSH header file: %s ", headerFile.c_str());
      remove(headerFile.c_str());
    }
  }

  void Input::readSrtHeader(){
    if (!hasSrt){return;}
    if (!srtSource.good()){return;}

    srtTrack = meta.addTrack();
    meta.setID(srtTrack, srtTrack);
    meta.setType(srtTrack, "meta");
    meta.setCodec(srtTrack, "subtitle");

    getNextSrt();
    while (srtPack){
      meta.update(srtPack);
      getNextSrt();
    }
    srtSource.clear();
    srtSource.seekg(0, srtSource.beg);
  }

  void Input::getNextSrt(bool smart){
    srtPack.null();
    std::string line;

    uint32_t index = 0;
    uint64_t timestamp = 0;
    uint32_t duration = 0;
    int lineNr = 0;
    std::string data;

    while (std::getline(srtSource, line)){// && !line.empty()){
      lineNr++;

      if (line.empty() || (line.size() == 1 && line.at(0) == '\r')){
        lineNr = 0;
        if (duration == 0){continue;}
        static JSON::Value thisPack;
        thisPack.null();
        thisPack["trackid"] = srtTrack;
        thisPack["bpos"] = srtSource.tellg();
        thisPack["data"] = data;
        thisPack["index"] = index;
        thisPack["time"] = timestamp;
        thisPack["duration"] = duration;

        std::string tmpStr = thisPack.toNetPacked();
        srtPack.reInit(tmpStr.data(), tmpStr.size());
        return;
      }
      // INFO_MSG("printline size: %d, string: %s", line.size(), line.c_str());
      if (lineNr == 1){
        index = atoi(line.c_str());
      }else if (lineNr == 2){
        // timestamp
        int from_hour = 0;
        int from_min = 0;
        int from_sec = 0;
        int from_ms = 0;

        int to_hour = 0;
        int to_min = 0;
        int to_sec = 0;
        int to_ms = 0;
        sscanf(line.c_str(), "%d:%d:%d,%d --> %d:%d:%d,%d", &from_hour, &from_min, &from_sec,
               &from_ms, &to_hour, &to_min, &to_sec, &to_ms);

        timestamp = (from_hour * 60 * 60 * 1000) + (from_min * 60 * 1000) + (from_sec * 1000) + from_ms;
        duration = ((to_hour * 60 * 60 * 1000) + (to_min * 60 * 1000) + (to_sec * 1000) + to_ms) - timestamp;
      }else{
        // subtitle
        if (data.size() > 1){data.append("\n");}
        data.append(line);
      }
    }

    srtPack.null();
    FAIL_MSG("Could not get next srt packet!");
  }

  /// Starts checks the SEM_INPUT lock, starts an angel process and then
  int Input::boot(int argc, char *argv[]){
    if (!(config->parseArgs(argc, argv))){return 1;}
    streamName = config->getString("streamname");
    Util::Config::streamName = streamName;

    if (config->getBool("json")){
      capa["version"] = PACKAGE_VERSION;
      std::cout << capa.toString() << std::endl;
      return 0;
    }

    INFO_MSG("Input booting");

    if (!checkArguments()){
      FAIL_MSG("Setup failed - exiting");
      return 0;
    }

    IPC::semaphore playerLock;
    IPC::semaphore pullLock;

    // If we're not converting, we might need a lock.
    if (streamName.size()){
      if (needsLock()){
        // needsLock() == true means this input is the sole responsible input for a stream
        // That means it's MistInBuffer for live, or the actual input binary for VoD
        // For these cases, we lock the SEM_INPUT semaphore.
        char semName[NAME_BUFFER_SIZE];
        snprintf(semName, NAME_BUFFER_SIZE, SEM_INPUT, streamName.c_str());
        playerLock.open(semName, O_CREAT | O_RDWR, ACCESSPERMS, 1);
        if (!playerLock.tryWait()){
          INFO_MSG("A player for this stream is already running");
          playerLock.close();
          return 1;
        }
        char pageName[NAME_BUFFER_SIZE];
        snprintf(pageName, NAME_BUFFER_SIZE, SHM_STREAM_STATE, streamName.c_str());
        streamStatus.init(pageName, 1, true, false);
        if (streamStatus){streamStatus.mapped[0] = STRMSTAT_INIT;}
        streamStatus.master = false;
        streamStatus.close();
      }else{
        // needsLock() == false means this binary will itself start the sole responsible input
        // So, we definitely do NOT lock SEM_INPUT, since the child process will do that later.
        // However, most of these processes are singular, meaning they expect to be the only source
        // of data. To prevent multiple singular processes starting, we use the MstPull semaphore if
        // this input is indeed a singular input type.
        if (isSingular()){
          pullLock.open(std::string("/MstPull_" + streamName).c_str(), O_CREAT | O_RDWR, ACCESSPERMS, 1);
          if (!pullLock){
            FAIL_MSG("Could not open pull lock - aborting!");
            return 1;
          }
          // We wait at most 5 seconds for a lock
          if (!pullLock.tryWait(5000)){
            WARN_MSG("A pull process for this stream is already running");
            pullLock.close();
            return 1;
          }
        }
      }
    }

    config->activate();

    if (getenv("NOFORK")){
      INFO_MSG("Not using angel process due to NOFORK environment variable");
      if (playerLock){
        // Re-init streamStatus, previously closed
        char pageName[NAME_BUFFER_SIZE];
        snprintf(pageName, NAME_BUFFER_SIZE, SHM_STREAM_STATE, streamName.c_str());
        streamStatus.init(pageName, 1, true, false);
        streamStatus.master = false;
        if (streamStatus){streamStatus.mapped[0] = STRMSTAT_INIT;}
      }
      int ret = 0;
      if (preRun()){ret = run();}
      if (playerLock){
        playerLock.unlink();
        char pageName[NAME_BUFFER_SIZE];
        snprintf(pageName, NAME_BUFFER_SIZE, SHM_STREAM_STATE, streamName.c_str());
        streamStatus.init(pageName, 1, true, false);
        streamStatus.close();
      }
      playerLock.unlink();
      pullLock.unlink();
      return ret;
    }

#if DEBUG < DLVL_DEVEL
    uint64_t reTimer = 0;
#endif

    while (config->is_active){
      Util::Procs::fork_prepare();
      pid_t pid = fork();
      if (pid == 0){
        Util::Procs::fork_complete();
        if (playerLock){
          // Re-init streamStatus, previously closed
          char pageName[NAME_BUFFER_SIZE];
          snprintf(pageName, NAME_BUFFER_SIZE, SHM_STREAM_STATE, streamName.c_str());
          streamStatus.init(pageName, 1, true, false);
          streamStatus.master = false;
          if (streamStatus){streamStatus.mapped[0] = STRMSTAT_INIT;}
        }
        // Abandon all semaphores, ye who enter here.
        playerLock.abandon();
        pullLock.abandon();
        if (!preRun()){return 0;}
        return run();
      }
      Util::Procs::fork_complete();
      if (pid == -1){
        FAIL_MSG("Unable to spawn input process");
        // We failed. Release the kra... semaphores!
        // post() contains an is-open check already, no need to double-check.
        playerLock.unlink();
        pullLock.unlink();
        return 2;
      }
      HIGH_MSG("Waiting for child for stream %s", streamName.c_str());
      // wait for the process to exit
      int status;
      while (waitpid(pid, &status, 0) != pid && errno == EINTR){
        if (!config->is_active){
          INFO_MSG("Shutting down input for stream %s because of signal interrupt...", streamName.c_str());
          Util::Procs::Stop(pid);
        }
        continue;
      }
      HIGH_MSG("Done waiting for child for stream %s", streamName.c_str());
      // if the exit was clean, don't restart it
      if (WIFEXITED(status) && (WEXITSTATUS(status) == 0)){
        INFO_MSG("Input for stream %s shut down cleanly", streamName.c_str());
        break;
      }
      if (playerLock){
        char pageName[NAME_BUFFER_SIZE];
        snprintf(pageName, NAME_BUFFER_SIZE, SHM_STREAM_STATE, streamName.c_str());
        streamStatus.init(pageName, 1, true, false);
        if (streamStatus){streamStatus.mapped[0] = STRMSTAT_INVALID;}
      }
#if DEBUG >= DLVL_DEVEL
      WARN_MSG(
          "Input for stream %s uncleanly shut down! Aborting restart; this is a development build.",
          streamName.c_str());
      break;
#else
      WARN_MSG("Input for stream %s uncleanly shut down! Restarting...", streamName.c_str());
      onCrash();
      Util::wait(reTimer);
      reTimer += 1000;
#endif
    }

    if (playerLock){
      playerLock.unlink();
      char pageName[NAME_BUFFER_SIZE];
      snprintf(pageName, NAME_BUFFER_SIZE, SHM_STREAM_STATE, streamName.c_str());
      streamStatus.init(pageName, 1, true, false);
      streamStatus.close();
    }
    pullLock.unlink();

    HIGH_MSG("Angel process for %s exiting", streamName.c_str());
    return 0;
  }

  int Input::run(){
    if (streamStatus){streamStatus.mapped[0] = STRMSTAT_BOOT;}
    checkHeaderTimes(config->getString("input"));
    if (needHeader()){
      uint64_t timer = Util::bootMS();
      bool headerSuccess = readHeader();
      if (!headerSuccess || (!M && needsLock())){
        FAIL_MSG("Reading header for '%s' failed.", config->getString("input").c_str());
        return 0;
      }
      timer = Util::bootMS() - timer;
      INFO_MSG("Read header in %" PRIu64 "ms (%zu tracks)", timer, M.trackCount());
    }
    if (config->getBool("headeronly")){return 0;}
    if (M && M.getVod()){
      meta.removeEmptyTracks();
      parseHeader();
      INFO_MSG("Header parsed, %lu tracks", M.getValidTracks().size());
    }

    if (!streamName.size()){
      // If we don't have a stream name, that means we're in stand-alone conversion mode.
      MEDIUM_MSG("Starting convert");
      convert();
    }else if (!needsLock()){
      // We have a name and aren't the sole process. That means we're streaming live data to a
      // buffer.
      INFO_MSG("Starting stream");
      stream();
    }else{
      // We are the sole process and have a name. That means this is a Buffer or VoD input.
      INFO_MSG("Starting serve");
      serve();
    }
    return 0;
  }

  void Input::convert(){
    INFO_MSG("Starting conversion");
    std::string fileName = config->getString("output");
    if (fileName == "-"){
      FAIL_MSG("No filename specified, exiting");
      return;
    }
    if (fileName.size() < 5 || fileName.substr(fileName.size() - 5) != ".dtsc"){
      fileName += ".dtsc";
    }
    std::ofstream file(fileName.c_str());

    DTSC::Meta outMeta;

    std::set<size_t> validTracks = M.getValidTracks();
    for (std::set<size_t>::iterator it = validTracks.begin(); it != validTracks.end(); it++){
      std::string type = M.getType(*it);
      size_t idx = outMeta.addTrack(M.fragments(*it).getPresent(), M.keys(*it).getPresent(),
                                    M.parts(*it).getPresent());
      outMeta.setID(idx, M.getID(*it));
      outMeta.setType(idx, type);
      outMeta.setCodec(idx, M.getCodec(*it));
      outMeta.setInit(idx, M.getInit(*it));
      outMeta.setLang(idx, M.getLang(*it));
      if (type == "video"){
        outMeta.setHeight(idx, M.getHeight(*it));
        outMeta.setWidth(idx, M.getWidth(*it));
        outMeta.setFpks(idx, M.getFpks(*it));
      }
      if (type == "audio"){
        outMeta.setRate(idx, M.getRate(*it));
        outMeta.setSize(idx, M.getSize(*it));
        outMeta.setChannels(idx, M.getChannels(*it));
      }
    }
    // output to dtsc
    uint64_t bpos = 0;

    seek(0);
    getNext();
    while (thisPacket){
      outMeta.updatePosOverride(thisPacket, bpos);
      file.write(thisPacket.getData(), thisPacket.getDataLen());
      bpos += thisPacket.getDataLen();
      getNext();
    }
    // close file
    file.close();
    outMeta.toFile(fileName + ".dtsh");
  }

  /// Checks in the server configuration if this stream is set to always on or not.
  /// Returns true if it is, or if the stream could not be found in the configuration.
  /// If the compiled default debug level is < INFO, instead returns false if the stream is not found.
  bool Input::isAlwaysOn(){
    bool ret = true;
    std::string strName = streamName.substr(0, (streamName.find_first_of("+ ")));

    char tmpBuf[NAME_BUFFER_SIZE];
    snprintf(tmpBuf, NAME_BUFFER_SIZE, SHM_STREAM_CONF, strName.c_str());
    Util::DTSCShmReader rStrmConf(tmpBuf);
    DTSC::Scan streamCfg = rStrmConf.getScan();
    if (streamCfg){
      if (!streamCfg.getMember("always_on") || !streamCfg.getMember("always_on").asBool()){
        ret = false;
      }
    }else{
#if DEBUG < DLVL_DEVEL
      ret = false;
#endif
    }
    return ret;
  }

  /// The main loop for inputs in stream serving mode.
  ///
  /// \triggers
  /// The `"STREAM_READY"` trigger is stream-specific, and is ran whenever an input finished loading
  /// and started serving a stream. If cancelled, the input is immediately shut down again. Its payload is:
  /// ~~~~~~~~~~~~~~~
  /// streamname
  /// input name
  /// ~~~~~~~~~~~~~~~
  void Input::serve(){
    users.reload(streamName, true);

    if (!M){
      // Initialize meta page
      meta.reInit(streamName, true);
    }else{
      std::set<size_t> validTracks = M.getValidTracks(true);
      for (std::set<size_t>::iterator it = validTracks.begin(); it != validTracks.end(); it++){
        bufferFrame(*it, 0);
      }
    }
    meta.setSource(config->getString("input"));

    bool internalOnly = (config->getString("input").find("INTERNAL_ONLY") != std::string::npos);

    /*LTS-START*/
    if (Triggers::shouldTrigger("STREAM_READY", config->getString("streamname"))){
      std::string payload = config->getString("streamname") + "\n" + capa["name"].asStringRef();
      if (!Triggers::doTrigger("STREAM_READY", payload, config->getString("streamname"))){
        config->is_active = false;
      }
    }
    /*LTS-END*/
    if (streamStatus){streamStatus.mapped[0] = STRMSTAT_READY;}

    INFO_MSG("Input started");
    activityCounter = Util::bootSecs();
    // main serve loop
    while (keepRunning()){
      // load pages for connected clients on request
      userLeadIn();
      COMM_LOOP(users, userOnActive(id), userOnDisconnect(id))
      userLeadOut();

      // unload pages that haven't been used for a while
      removeUnused();

      if (M.getLive() && !internalOnly){
        uint64_t currLastUpdate = M.getLastUpdated();
        if (currLastUpdate > activityCounter){activityCounter = currLastUpdate;}
      }else{
        if (connectedUsers && M.getValidTracks().size()){activityCounter = Util::bootSecs();}
      }
      // if not shutting down, wait 1 second before looping
      if (config->is_active){Util::wait(INPUT_USER_INTERVAL);}
    }
    if (streamStatus){streamStatus.mapped[0] = STRMSTAT_SHUTDOWN;}
    config->is_active = false;
    finish();
    INFO_MSG("Input closing clean, reason: %s", Util::exitReason);
    userSelect.clear();
    if (streamStatus){streamStatus.mapped[0] = STRMSTAT_OFF;}
  }

  /// This function checks if an input in serve mode should keep running or not.
  /// The default implementation checks for interruption by signals and otherwise waits until a
  /// save amount of time has passed before shutting down.
  /// For live streams, this is twice the biggest fragment duration.
  /// For non-live streams this is INPUT_TIMEOUT seconds.
  /// The default Pro implementation also allows cancelling the shutdown through the STREAM_UNLOAD trigger.
  bool Input::keepRunning(){
    // We keep running in serve mode if the config is still active AND either
    // - INPUT_TIMEOUT seconds haven't passed yet,
    // - this is a live stream and at least two of the biggest fragment haven't passed yet,
    bool ret = config->is_active && ((Util::bootSecs() - activityCounter) < INPUT_TIMEOUT);
    if (!ret && config->is_active && isAlwaysOn()){
      ret = true;
      activityCounter = Util::bootSecs();
    }
    /*LTS-START*/
    if (!ret){
      if (Triggers::shouldTrigger("STREAM_UNLOAD", config->getString("streamname"))){
        std::string payload = config->getString("streamname") + "\n" + capa["name"].asStringRef() + "\n";
        if (!Triggers::doTrigger("STREAM_UNLOAD", payload, config->getString("streamname"))){
          activityCounter = Util::bootSecs();
          config->is_active = true;
          ret = true;
        }
      }
    }
    /*LTS-END*/
    if (!ret && ((Util::bootSecs() - activityCounter) >= INPUT_TIMEOUT)){
      Util::logExitReason("no activity for %u seconds", Util::bootSecs() - activityCounter);
    }
    return ret;
  }

  /// Main loop for stream-style inputs.
  /// This loop will do the following, in order:
  /// - exit if another stream() input is already open for this streamname
  /// - start a buffer in push mode
  /// - connect to it
  /// - run parseStreamHeader
  /// - if there are tracks, register as a non-viewer on the user page of the buffer
  /// - call getNext() in a loop, buffering packets
  void Input::stream(){

    std::map<std::string, std::string> overrides;
    overrides["throughboot"] = "";
    if (config->getBool("realtime") ||
        (capa.isMember("hardcoded") && capa["hardcoded"].isMember("resume") && capa["hardcoded"]["resume"])){
      overrides["resume"] = "1";
    }
    if (isSingular()){
      if (!config->getBool("realtime") && Util::streamAlive(streamName)){
        WARN_MSG("Stream already online, cancelling");
        return;
      }
      overrides["singular"] = "";
      if (!Util::startInput(streamName, "push://INTERNAL_ONLY:" + config->getString("input"), true,
                            true, overrides)){// manually override stream url to start the buffer
        WARN_MSG("Could not start buffer, cancelling");
        return;
      }
    }else{
      if (!Util::startInput(streamName, "push://INTERNAL_PUSH:" + capa["name"].asStringRef(), true,
                            true, overrides)){// manually override stream url to start the buffer
        WARN_MSG("Could not start buffer, cancelling");
        return;
      }
    }

    INFO_MSG("Input started");
    meta.reInit(streamName, false);

    if (!openStreamSource()){
      FAIL_MSG("Unable to connect to source");
      return;
    }
    parseStreamHeader();

    std::set<size_t> validTracks;

    if (publishesTracks()){
      validTracks = M.getMySourceTracks(getpid());
      if (!validTracks.size()){
        userSelect.clear();
        finish();
        INFO_MSG("No tracks found, cancelling");
        return;
      }
    }

    timeOffset = 0;
    uint64_t minFirstMs = 0;

    // If resume mode is on, find matching tracks and set timeOffset values to make sure we append to the tracks.
    if (publishesTracks() && config->getBool("realtime")){
      seek(0);

      minFirstMs = 0xFFFFFFFFFFFFFFFFull;
      uint64_t maxFirstMs = 0;
      uint64_t minLastMs = 0xFFFFFFFFFFFFFFFFull;
      uint64_t maxLastMs = 0;

      // track lowest firstms value
      for (std::set<size_t>::iterator it = validTracks.begin(); it != validTracks.end(); ++it){
        if (meta.getFirstms(*it) < minFirstMs){minFirstMs = meta.getFirstms(*it);}
        if (meta.getFirstms(*it) > maxFirstMs){maxFirstMs = meta.getFirstms(*it);}
        if (meta.getLastms(*it) < minLastMs){minLastMs = meta.getLastms(*it);}
        if (meta.getLastms(*it) > maxLastMs){maxLastMs = meta.getLastms(*it);}
      }
      if (maxFirstMs - minFirstMs > 500){
        WARN_MSG("Begin timings of tracks for this file are %" PRIu64
                 " ms apart. This may mess up playback to some degree. (Range: %" PRIu64
                 "ms - %" PRIu64 "ms)",
                 maxFirstMs - minFirstMs, minFirstMs, maxFirstMs);
      }
      if (maxLastMs - minLastMs > 500){
        WARN_MSG("Stop timings of tracks for this file are %" PRIu64
                 " ms apart. This may mess up playback to some degree. (Range: %" PRIu64
                 "ms - %" PRIu64 "ms)",
                 maxLastMs - minLastMs, minLastMs, maxLastMs);
      }
      // find highest current time
      for (std::set<size_t>::iterator it = validTracks.begin(); it != validTracks.end(); ++it){
        timeOffset = std::max(timeOffset, (int64_t)meta.getLastms(*it));
      }

      if (timeOffset){
        if (minFirstMs == 0xFFFFFFFFFFFFFFFFull){minFirstMs = 0;}
        MEDIUM_MSG("Offset is %" PRId64
                   "ms, adding 40ms and subtracting the start time of %" PRIu64,
                   timeOffset, minFirstMs);
        timeOffset += 40; // Add an artificial frame at 25 FPS to make sure we append, not overwrite
        timeOffset -= minFirstMs; // we don't need to add the lowest firstms value to the offset, as it's already there
      }
    }
    if (publishesTracks()){
      for (std::set<size_t>::iterator it = validTracks.begin(); it != validTracks.end(); ++it){
        meta.setFirstms(*it, meta.getFirstms(*it)+timeOffset);
        meta.setLastms(*it, 0);
      }
    }

    simStartTime = config->getInteger("simulated-starttime");
    if (!simStartTime){simStartTime = Util::bootMS();}

    std::string reason;
    if (config->getBool("realtime")){
      realtimeMainLoop();
    }else{
      streamMainLoop();
    }

    closeStreamSource();

    userSelect.clear();

    finish();
    INFO_MSG("Input closing clean; reason: %s", Util::exitReason);
    return;
  }

  void Input::streamMainLoop(){
    uint64_t statTimer = 0;
    uint64_t startTime = Util::bootSecs();
    size_t tid;
    size_t idx;
    Comms::Statistics statComm;
    getNext();
    tid = thisPacket.getTrackId();
    idx = M.trackIDToIndex(tid, getpid());
    if (thisPacket && !userSelect.count(idx)){
      userSelect[idx].reload(streamName, idx, COMM_STATUS_SOURCE | COMM_STATUS_DONOTTRACK);
    }
    while (thisPacket && config->is_active && userSelect[idx].isAlive()){
      if (userSelect[idx].getStatus() == COMM_STATUS_REQDISCONNECT){
        Util::logExitReason("buffer requested shutdown");
        break;
      }
      bufferLivePacket(thisPacket);
      userSelect[idx].keepAlive();
      getNext();
      if (!thisPacket){
        Util::logExitReason("invalid packet from getNext");
        break;
      }
      tid = thisPacket.getTrackId();
      idx = M.trackIDToIndex(tid, getpid());
      if (thisPacket && !userSelect.count(idx)){
        userSelect[idx].reload(streamName, idx, COMM_STATUS_SOURCE | COMM_STATUS_DONOTTRACK);
      }

      if (Util::bootSecs() - statTimer > 1){
        // Connect to stats for INPUT detection
        if (!statComm){statComm.reload();}
        if (statComm){
          if (!statComm.isAlive()){
            config->is_active = false;
            Util::logExitReason("received shutdown request from controller");
            return;
          }
          uint64_t now = Util::bootSecs();
          statComm.setNow(now);
          statComm.setCRC(getpid());
          statComm.setStream(streamName);
          statComm.setConnector("INPUT:" + capa["name"].asStringRef());
          statComm.setUp(0);
          statComm.setDown(streamByteCount());
          statComm.setTime(now - startTime);
          statComm.setLastSecond(0);
          statComm.setHost(getConnectedBinHost());
          statComm.keepAlive();
        }

        statTimer = Util::bootSecs();
      }
    }
  }

  void Input::realtimeMainLoop(){
    uint64_t statTimer = 0;
    uint64_t startTime = Util::bootSecs();
    Comms::Statistics statComm;
    getNext();
    if (thisPacket && !userSelect.count(thisPacket.getTrackId())){
      size_t tid = thisPacket.getTrackId();
      userSelect[tid].reload(streamName, tid, COMM_STATUS_SOURCE | COMM_STATUS_DONOTTRACK);
    }
    while (thisPacket && config->is_active && userSelect[thisPacket.getTrackId()].isAlive()){
      thisPacket.nullMember("bpos");
      while (config->is_active && userSelect[thisPacket.getTrackId()].isAlive() &&
             Util::bootMS() + SIMULATED_LIVE_BUFFER < (thisPacket.getTime() + timeOffset) + simStartTime){
        Util::sleep(std::min(((thisPacket.getTime() + timeOffset) + simStartTime) - (Util::getMS() + SIMULATED_LIVE_BUFFER),
                             (uint64_t)1000));
        userSelect[thisPacket.getTrackId()].keepAlive();
      }
      uint64_t originalTime = thisPacket.getTime();
      thisPacket.setTime(originalTime + timeOffset);
      bufferLivePacket(thisPacket);
      thisPacket.setTime(originalTime);

      userSelect[thisPacket.getTrackId()].keepAlive();
      getNext();
      if (thisPacket && !userSelect.count(thisPacket.getTrackId())){
        size_t tid = thisPacket.getTrackId();
        userSelect[tid].reload(streamName, tid, COMM_STATUS_SOURCE | COMM_STATUS_DONOTTRACK);
      }

      if (Util::bootSecs() - statTimer > 1){
        // Connect to stats for INPUT detection
        if (!statComm){statComm.reload();}
        if (statComm){
          if (!statComm.isAlive()){
            config->is_active = false;
            Util::logExitReason("received shutdown request from controller");
            return;
          }
          uint64_t now = Util::bootSecs();
          statComm.setNow(now);
          statComm.setCRC(getpid());
          statComm.setStream(streamName);
          statComm.setConnector("INPUT:" + capa["name"].asStringRef());
          statComm.setUp(0);
          statComm.setDown(streamByteCount());
          statComm.setTime(now - startTime);
          statComm.setLastSecond(0);
          statComm.setHost(getConnectedBinHost());
          statComm.keepAlive();
        }

        statTimer = Util::bootSecs();
      }
    }
    if (!thisPacket){
      Util::logExitReason("invalid packet from getNext");
    }
    if (thisPacket && !userSelect[thisPacket.getTrackId()].isAlive()){
      Util::logExitReason("buffer shutdown");
    }
  }

  void Input::finish(){
    if (!standAlone || config->getBool("realtime")){return;}
    for (std::map<size_t, std::map<uint32_t, size_t> >::iterator it = pageCounter.begin();
         it != pageCounter.end(); it++){
      for (std::map<uint32_t, size_t>::iterator it2 = it->second.begin(); it2 != it->second.end(); it2++){
        it2->second = 1;
      }
    }
    removeUnused();
  }

  void Input::removeUnused(){
    std::set<size_t> validTracks = M.getValidTracks();
    for (std::set<size_t>::iterator it = validTracks.begin(); it != validTracks.end(); ++it){
      Util::RelAccX &tPages = meta.pages(*it);
      for (size_t i = tPages.getDeleted(); i < tPages.getEndPos(); i++){
        uint64_t pageNum = tPages.getInt("firstkey", i);
        if (pageCounter[*it].count(pageNum)){
          --pageCounter[*it][pageNum];
          if (!pageCounter[*it][pageNum]){
            pageCounter[*it].erase(pageNum);
            bufferRemove(*it, pageNum);
          }
        }
      }
    }
  }

  std::string formatGUID(const std::string &val){
    std::stringstream r;
    r << std::hex << std::setw(2) << std::setfill('0');
    r << (int)val[0] << (int)val[1] << (int)val[2] << (int)val[3];
    r << "-";
    r << (int)val[4] << (int)val[5];
    r << "-";
    r << (int)val[6] << (int)val[7];
    r << "-";
    r << (int)val[8] << (int)val[9];
    r << "-";
    r << (int)val[10] << (int)val[11] << (int)val[12] << (int)val[13] << (int)val[14] << (int)val[15];
    return r.str();
  }

  bool Input::readHeader(){
    INFO_MSG("Empty header created by default readHeader handler");
    meta.reInit(streamName);
    return true;
  }

  void Input::parseHeader(){
    if (hasSrt){readSrtHeader();}
    DONTEVEN_MSG("Parsing the header");
    bool hasKeySizes = true;

    std::set<size_t> validTracks = M.getValidTracks(true);

    for (std::set<size_t>::iterator it = validTracks.begin(); it != validTracks.end(); ++it){
      DTSC::Keys keys(M.keys(*it));
      size_t endKey = keys.getEndValid();
      INFO_MSG("Track %zu has %zu keys", *it, endKey);
      std::set<uint64_t> &kTimes = keyTimes[*it];
      for (size_t j = 0; j < endKey; j++){
        kTimes.insert(keys.getTime(j));
        if (hasKeySizes && keys.getSize(j) == 0){hasKeySizes = false;}
      }
    }

    if (!hasKeySizes){
      for (std::set<size_t>::iterator it = validTracks.begin(); it != validTracks.end(); ++it){
        DTSC::Keys keys(M.keys(*it));
        DTSC::Parts parts(M.parts(*it));
        size_t partIndex = 0;
        size_t keyCount = keys.getEndValid();
        for (size_t i = 0; i < keyCount; ++i){
          size_t keySize = 0;
          size_t partCount = keys.getParts(i);
          for (size_t j = 0; j < partCount; ++j){keySize += parts.getSize(partIndex + j);}
          keys.setSize(i, keySize);
          partIndex += partCount;
        }
      }
    }
    for (std::set<size_t>::iterator it = validTracks.begin(); it != validTracks.end(); ++it){
      bool newData = true;

      DTSC::Keys keys(M.keys(*it));
      uint32_t endKey = keys.getEndValid();

      Util::RelAccX &tPages = meta.pages(*it);
      // Generate page data only if not set yet (might be crash-recovering here)
      if (!tPages.getEndPos()){
        int32_t pageNum = -1;
        for (uint32_t j = 0; j < endKey; j++){
          uint64_t keyTime = keys.getTime(j);
          if (newData){
            tPages.addRecords(1);
            ++pageNum;
            tPages.setInt("firsttime", keyTime, pageNum);
            tPages.setInt("firstkey", j, pageNum);

            newData = false;
          }
          tPages.setInt("keycount", tPages.getInt("keycount", pageNum) + 1, pageNum);
          tPages.setInt("parts", tPages.getInt("parts", pageNum) + keys.getParts(j), pageNum);
          tPages.setInt("size", tPages.getInt("size", pageNum) + keys.getSize(j), pageNum);
          tPages.setInt("lastkeytime", keyTime, pageNum);
          if ((tPages.getInt("size", pageNum) > FLIP_DATA_PAGE_SIZE ||
               keyTime - tPages.getInt("firsttime", pageNum) > FLIP_TARGET_DURATION) &&
              keyTime - tPages.getInt("firsttime", pageNum) > FLIP_MIN_DURATION){
            newData = true;
          }
        }
      }
      /*LTS-START*/
      /*
      if (config->getString("encryption") == "" && config->getString("buydrm") != ""){
        handleBuyDRM();
      }
      if (config->getString("encryption").find(":") != std::string::npos){
        size_t tNumber = meta.addCopy(*it);
        meta.setID(tNumber, tNumber);
        meta.setType(tNumber, M.getType(*it));
        meta.setCodec(tNumber, M.getCodec(*it));
        meta.setEncryption(tNumber, "CTR128/" + config->getString("encryption"));
        meta.setIvec(tNumber, 0x0CD00C657BA88D47);//Poke_compat
//        meta.setIvec(tNumber, 0x5DC800E53A65018A);//Dash_compat
        meta.setWidevine(tNumber, config->getString("widevine"));
        meta.setPlayReady(tNumber, config->getString("playready"));

        tNumber = meta.addCopy(*it);
        meta.setID(tNumber, tNumber);
        meta.setType(tNumber, M.getType(*it));
        meta.setCodec(tNumber, M.getCodec(*it));
        meta.setEncryption(tNumber, "CBC128/" + config->getString("encryption"));
        meta.setIvec(tNumber, 0x0CD00C657BA88D47);//Poke_compat
//        meta.setIvec(tNumber, 0x5DC800E53A65018A);//Dash_compat
        meta.setWidevine(tNumber, config->getString("widevine"));
        meta.setPlayReady(tNumber, config->getString("playready"));
      }
      */
      /*LTS-END*/
    }

    for (std::set<size_t>::iterator it = validTracks.begin(); it != validTracks.end(); ++it){
      const Util::RelAccX &tPages = meta.pages(*it);
      if (!tPages.getEndPos()){
        WARN_MSG("No pages for track %zu found", *it);
        continue;
      }
      MEDIUM_MSG("Track %zu (%s) split into %zu pages", *it, M.getCodec(*it).c_str(), tPages.getEndPos());
      for (size_t j = tPages.getDeleted(); j < tPages.getEndPos(); j++){
        size_t pageNumber = tPages.getInt("firstkey", j);
        size_t pageKeys = tPages.getInt("keycount", j);
        size_t pageSize = tPages.getInt("size", j);

        HIGH_MSG("  Page %zu-%zu, (%zu bytes)", pageNumber, pageNumber + pageKeys - 1, pageSize);
      }
    }
  }

  void getCData(std::string &val){
    if (val.find("<![CDATA[") == 0){
      val.erase(0, 9);
      val.erase(val.size() - 3);
    }
  }

  std::string getXMLValue(const std::string &xml, const std::string &field){
    size_t offset = xml.find("<" + field + ">") + field.size() + 2;
    std::string res = xml.substr(offset, xml.find("</" + field + ">") - offset);
    getCData(res);
    return res;
  }

  void Input::handleBuyDRM(){
    std::string contentID = config->getString("contentid");
    if (contentID == ""){contentID = Secure::md5(config->getString("streamname"));}
    std::string keyID = config->getString("keyid");
    if (keyID == ""){keyID = Secure::md5(contentID);}

    std::stringstream soap;
    soap << "<s:Envelope "
            "xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\"><s:Body><RequestEncryptionInfo "
            "xmlns=\"http://tempuri.org/\"><ServerKey>"
            "52B11D80-21E2-4281-BB83-1B4191002534"
            "</ServerKey><RequestXml>"
            "<![CDATA["
            "<KeyOSEncryptionInfoRequest><APIVersion>5.0.0.2</APIVersion><DRMType>smooth</"
            "DRMType><EncoderVersion>" APPIDENT "</EncoderVersion><UserKey>";
    soap << config->getString("buydrm");
    soap << "</UserKey><KeyID>";
    soap << formatGUID(contentID);
    soap << "</KeyID><ContentID>";
    soap << formatGUID(keyID);
    soap << "</ContentID><fl_GeneratePRHeader>true</fl_GeneratePRHeader><fl_GenerateWVHeader>true</"
            "fl_GenerateWVHeader><MediaID>";
    soap << config->getString("streamname");
    soap << "</MediaID></KeyOSEncryptionInfoRequest>"
            "]]>"
            "</RequestXml></RequestEncryptionInfo></s:Body></s:Envelope>";
    INFO_MSG("Sending soap request %s", soap.str().c_str());

    HTTP::Downloader dld;
    dld.setHeader("Content-Type", "text/xml; charset=utf-8");
    dld.setHeader("SOAPAction", "\"http://tempuri.org/ISmoothPackager/RequestEncryptionInfo\"");
    dld.post(HTTP::URL("https://packager.licensekeyserver.com/pck/"), soap.str());
    const std::string &result = dld.getHTTP().body;
    std::string replaced;
    replaced.reserve(result.size());
    size_t i = 0;
    while (i < result.size()){
      if (result.at(i) == '&'){
        if (result.at(i + 1) == 'l' && result.at(i + 2) == 't'){
          replaced += '<';
        }else if (result.at(i + 1) == 'g' && result.at(i + 2) == 't'){
          replaced += '>';
        }else if (result.at(i + 1) == '#' && result.at(i + 2) == 'x' && result.at(i + 3) == 'D'){
          replaced += '\r';
        }else{
          replaced.append(result.data() + i, result.find(';', i) - i);
        }
        i = result.find(';', i) + 1;
        continue;
      }
      replaced += result.at(i);
      i++;
    }
    while (replaced.find('\r') != std::string::npos){replaced.erase(replaced.find('\r'), 1);}
    while (replaced.find('\n') != std::string::npos){replaced.erase(replaced.find('\n'), 1);}
    INFO_MSG("ContentKey: %s", getXMLValue(replaced, "ContentKey").c_str());
    INFO_MSG("WVHeader:   %s", getXMLValue(replaced, "WVHeader").c_str());
    INFO_MSG("PRHeader:   %s", getXMLValue(replaced, "PRHeader").c_str());
    std::string cKey = Encodings::Base64::decode(getXMLValue(replaced, "ContentKey"));
    config->getOption("encryption", true).append(keyID + ":" + Encodings::Hex::encode(cKey));
    config->getOption("widevine", true).append(getXMLValue(replaced, "WVHeader"));
    config->getOption("playready", true).append(getXMLValue(replaced, "PRHeader"));
    INFO_MSG("Set encryption to %s", config->getString("encryption").c_str());
  }

  bool Input::bufferFrame(size_t idx, uint32_t keyNum){
    if (M.getLive()){return true;}
    HIGH_MSG("Buffering track %zu, key %" PRIu32, idx, keyNum);
    size_t sourceIdx = M.getSourceTrack(idx);
    if (sourceIdx == INVALID_TRACK_ID){sourceIdx = idx;}

    const Util::RelAccX &tPages = M.pages(idx);
    DTSC::Keys keys(M.keys(idx));
    uint32_t keyCount = keys.getValidCount();
    if (!tPages.getEndPos()){
      WARN_MSG("No pages for track %zu found! Cancelling bufferFrame", idx);
      return false;
    }
    if (keyNum > keyCount){
      // End of movie here, returning true to avoid various error messages
      if (keyNum > keyCount + 1){
        WARN_MSG("Key %" PRIu32 " on track %zu is higher than total (%" PRIu32
                 "). Cancelling buffering.",
                 keyNum, idx, keyCount);
      }
      return true;
    }
    uint64_t pageIdx = 0;
    for (uint64_t i = tPages.getDeleted(); i < tPages.getEndPos(); i++){
      if (tPages.getInt("firstkey", i) > keyNum) break;
      pageIdx = i;
    }
    uint32_t pageNumber = tPages.getInt("firstkey", pageIdx);
    if (isBuffered(idx, pageNumber)){
      // get corresponding page number
      pageCounter[idx][pageNumber] = 15;
      VERYHIGH_MSG("Track %zu, key %" PRIu32 "is already buffered in page %" PRIu32
                   ". Cancelling bufferFrame",
                   idx, keyNum, pageNumber);
      return true;
    }
    // Update keynum to point to the corresponding page
    uint64_t bufferTimer = Util::bootMS();
    keyNum = pageNumber;
    if (!bufferStart(idx, keyNum)){
      WARN_MSG("bufferStart failed! Cancelling bufferFrame");
      return false;
    }

    uint64_t keyTime = keys.getTime(keyNum);
    userSelect[idx].reload(streamName, idx);

    bool isSrt = (hasSrt && idx == srtTrack);
    if (isSrt){
      srtSource.clear();
      srtSource.seekg(0, srtSource.beg);
      srtPack.null();
    }else{
      seek(keyTime, sourceIdx);
    }
    uint64_t stopTime = M.getLastms(idx) + 1;
    if (pageIdx != tPages.getEndPos() - 1){
      stopTime = keys.getTime(pageNumber + tPages.getInt("keycount", pageIdx));
    }
    HIGH_MSG("Playing from %" PRIu64 " to %" PRIu64, keyTime, stopTime);
    if (isSrt){
      getNextSrt();
      // in case earlier seeking was inprecise, seek to the exact point
      while (srtPack && srtPack.getTime() < keys.getTime(keyNum)){getNextSrt();}
    }else{
      getNext(sourceIdx);
      // in case earlier seeking was inprecise, seek to the exact point
      while (thisPacket && thisPacket.getTime() < keys.getTime(keyNum)){getNext(sourceIdx);}
    }
    uint64_t lastBuffered = 0;
    uint32_t packCounter = 0;
    uint64_t byteCounter = 0;
    std::string encryption;
    if (isSrt){
      while (srtPack && srtPack.getTime() < stopTime){
        if (srtPack.getTime() >= lastBuffered){
          char *data;
          size_t dataLen;
          srtPack.getString("data", data, dataLen);
          bufferNext(srtPack.getTime(), 0, idx, data, dataLen, srtPack.getInt("bpos"),
                     srtPack.getFlag("keyframe"));
          ++packCounter;
          byteCounter += srtPack.getDataLen();
          lastBuffered = srtPack.getTime();
        }
        getNextSrt();
      }
    }else{
      while (thisPacket && thisPacket.getTime() < stopTime){
        if (thisPacket.getTime() >= lastBuffered){
          if (sourceIdx != idx){
            if (encryption.find(":") != std::string::npos || M.getEncryption(idx).find(":") != std::string::npos){
              if (encryption == ""){
                encryption = M.getEncryption(idx);
                std::string encryptionKey =
                    Encodings::Hex::decode(encryption.substr(encryption.find(":") + 1));
                aesCipher.setEncryptKey(encryptionKey.c_str());
              }
              if (encryption.substr(0, encryption.find('/')) == "CTR128"){
                DTSC::Packet encPacket = aesCipher.encryptPacketCTR(
                    M, thisPacket, M.getIvec(idx) + M.getPartIndex(thisPacket.getTime(), idx), idx);
                thisPacket = encPacket;
              }else if (encryption.substr(0, encryption.find('/')) == "CBC128"){
                char ivec[] ={0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
                Bit::htobll(ivec + 8, M.getIvec(idx) + M.getPartIndex(thisPacket.getTime(), idx));
                DTSC::Packet encPacket = aesCipher.encryptPacketCBC(M, thisPacket, ivec, idx);
                thisPacket = encPacket;
              }
            }else{
              thisPacket = DTSC::Packet(thisPacket, idx);
            }
          }
          char *data;
          size_t dataLen;
          thisPacket.getString("data", data, dataLen);
          bufferNext(thisPacket.getTime(), thisPacket.getInt("offset"), idx, data, dataLen,
                     thisPacket.getInt("bpos"), thisPacket.getFlag("keyframe"));
          ++packCounter;
          byteCounter += thisPacket.getDataLen();
          lastBuffered = thisPacket.getTime();
        }
        getNext(sourceIdx);
      }
    }
    bufferFinalize(idx);
    bufferTimer = Util::bootMS() - bufferTimer;
    INFO_MSG("Track %zu, page %" PRIu32 " (%" PRIu64 " - %" PRIu64 " ms) buffered in %" PRIu64 "ms",
             idx, keyNum, tPages.getInt("firsttime", pageIdx), thisPacket.getTime(), bufferTimer);
    INFO_MSG("  (%" PRIu32 "/%" PRIu64 " parts, %" PRIu64 " bytes)", packCounter,
             tPages.getInt("parts", pageIdx), byteCounter);
    pageCounter[idx][keyNum] = 15;
    return true;
  }

  bool Input::atKeyFrame(){
    static std::map<size_t, uint64_t> lastSeen;
    size_t idx = thisPacket.getTrackId();
    // not in keyTimes? We're not at a keyframe.
    if (!keyTimes[idx].count(thisPacket.getTime())){return false;}
    // skip double times
    if (lastSeen.count(idx) && lastSeen[idx] == thisPacket.getTime()){return false;}
    // set last seen, and return true
    lastSeen[idx] = thisPacket.getTime();
    return true;
  }

  bool Input::readExistingHeader(){
    char pageName[NAME_BUFFER_SIZE];
    snprintf(pageName, NAME_BUFFER_SIZE, SHM_STREAM_META, config->getString("streamname").c_str());
    IPC::sharedPage sp(pageName, 0, false, false);
    if (sp){
      sp.close();
      meta.reInit(config->getString("streamname"), false);
      if (meta){
        meta.setMaster(true);
        INFO_MSG("Read existing header");
        return true;
      }
    }
    meta.reInit(config->getString("streamname"), config->getString("input") + ".dtsh");
    if (meta.version != DTSH_VERSION){
      INFO_MSG("Updating wrong version header file from version %u to %u", meta.version, DTSH_VERSION);
      return false;
    }
    return meta;
  }

  bool Input::keepAlive(){
    if (!userSelect.size()){return config->is_active;}

    bool isAlive = false;
    for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end(); it++){
      if (it->second.isAlive()){isAlive = true;}
      it->second.keepAlive();
    }
    return isAlive && config->is_active;
  }
}// namespace Mist
