#include <fcntl.h>
#include <semaphore.h>
#include <sys/stat.h>

#include "input.h"
#include <fstream>
#include <iomanip>
#include <iterator>
#include <mist/auth.h>
#include <mist/defines.h>
#include <mist/encode.h>
#include <mist/procs.h>
#include <mist/stream.h>
#include <mist/triggers.h>
#include <mist/urireader.h>
#include <sstream>
#include <sys/wait.h>

namespace Mist{
  Util::Config *Input::config = NULL;
  JSON::Value Input::capa = JSON::Value();

  void Input::userLeadIn(){
    connectedUsers = 0;
    keyLoadPriority.clear();
  }

  void Input::userOnActive(size_t id){
    ++connectedUsers;
    size_t track = users.getTrack(id);
    if (!M.trackValid(track)){
      WARN_MSG("Player is inside invalid track: %zu", track);
      return;
    }
    //This is a bit tricky:
    //We want to make sure the next page is buffered before we need it, during playback.
    //To do so, we grab the current key's time and make sure current key and everything 120 seconds after it, is loaded.
    size_t key = users.getKeyNum(id);
    uint64_t time = M.getTimeForKeyIndex(track, key);
    size_t endKey = M.getKeyIndexForTime(track, time + 120000);
    //But! What if our current key is 20+ seconds long? HAVE YOU THOUGHT OF THAT?!
    //Exactly! I thought not! So, if the end key number == the first, we increase by one.
    if (endKey == key){++endKey;}
    if (endKey > key + 1000){endKey = key + 1000;}
    DONTEVEN_MSG("User with ID:%zu is on %zu:%zu -> %zu (timestamp %" PRIu64 ")", id, track, key, endKey, time);
    for (size_t i = key; i <= endKey; ){
      const Util::RelAccX &tPages = M.pages(track);
      Util::RelAccXFieldData firstkeyEntry = tPages.getFieldData("firstkey");
      Util::RelAccXFieldData keyCountEntry = tPages.getFieldData("keycount");
      if (!tPages.getEndPos()){return;}
      DTSC::Keys keys(M.keys(track));
      if (i > keys.getEndValid()){return;}
      bool found = false;
      uint64_t cnt = 1, pageNumber = 0;
      for (uint64_t j = tPages.getDeleted(); j < tPages.getEndPos(); j++){
        pageNumber = tPages.getInt(firstkeyEntry, j);
        cnt = tPages.getInt(keyCountEntry, j);
        if (pageNumber <= i && pageNumber + cnt > i){
          found = true;
          break;
        }
      }
      // Could not find key? Then we're done here.
      if (!found){return;}
      uint64_t pageTime = M.getTimeForKeyIndex(track, pageNumber);
      if (pageTime <= time){
        keyLoadPriority[trackKey(track, pageNumber)] += 10000;
      }else{
        keyLoadPriority[trackKey(track, pageNumber)] += 600 - (pageTime - time) / 1000;
      }
      // Make sure we always progress, even in edge cases where there is no full key buffered yet
      if (!cnt){cnt = 1;}
      i = pageNumber + cnt;
    }
    //Now, we can rest assured that the next ~120 seconds or so is pre-buffered in RAM.
  }
  void Input::userOnDisconnect(size_t id){}
  void Input::userLeadOut(){
    if (!keyLoadPriority.size()){return;}
    //Make reverse mapping
    std::multimap<uint64_t, trackKey> reverse;
    for (std::map<trackKey, uint64_t>::iterator i = keyLoadPriority.begin(); i != keyLoadPriority.end(); ++i){
      reverse.insert(std::pair<uint64_t, trackKey>(i->second, i->first));
      VERYHIGH_MSG("Key priority for %zu:%zu = %" PRIu64, i->first.track, i->first.key, i->second);
    }
    uint64_t timer = Util::bootMS();
    for (std::multimap<uint64_t, trackKey>::reverse_iterator i = reverse.rbegin(); i != reverse.rend() && Util::bootMS() < timer + INPUT_USER_INTERVAL; ++i){
      bufferFrame(i->second.track, i->second.key);
    }
  }

  void Input::reloadClientMeta(){
    if (M.getStreamName() != "" && M.getMaster()){return;}
    meta.reInit(streamName, false);
  }

  bool Input::hasMeta() const{return M && M.getStreamName() != "" && M.getValidTracks().size();}
  bool Input::trackLoaded(size_t idx) const{return (M && M.trackLoaded(idx));}

  Input::Input(Util::Config *cfg) : InOutBase(){
    config = cfg;
    standAlone = true;
    Util::Config::binaryType = Util::INPUT;
    inputTimeout = INPUT_TIMEOUT;

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
    option.null();
    option["short"] = "i";
    option["arg"] = "integer";
    option["long"] = "inputtimeout";
    option["value"].append(inputTimeout);
    option["help"] = "Time in seconds to keep the input process loaded without activity";
    config->addOption("inputtimeout", option);
    capa["optional"]["inputtimeout"]["name"] = "Input inactivity timeout";
    capa["optional"]["inputtimeout"]["help"] = "How long the input should remain loaded without activity";
    capa["optional"]["inputtimeout"]["default"] = inputTimeout;
    capa["optional"]["inputtimeout"]["unit"] = "s";
    capa["optional"]["inputtimeout"]["type"] = "uint";
    capa["optional"]["inputtimeout"]["option"] = "--inputtimeout";

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
    option["short"] = "P";
    option["long"] = "pagetimeout";
    option["arg"] = "integer";
    option["value"].append(DEFAULT_PAGE_TIMEOUT);
    option["help"] = "For bufferless or live inputs like HLS, set the timeout in seconds for old, inactive pages to be deleted. A longer value results in more memory usage, but ensures that recently buffered data stays in memory for longer";
    config->addOption("pagetimeout", option);
    capa["optional"]["pagetimeout"]["name"] = "Memory page timeout";
    capa["optional"]["pagetimeout"]["help"] = "For bufferless or live inputs like HLS, set the timeout in seconds for old, inactive pages to be deleted. A longer value results in more memory usage, but ensures that recently buffered data stays in memory for longer";
    capa["optional"]["pagetimeout"]["option"] = "--pagetimeout";
    capa["optional"]["pagetimeout"]["type"] = "uint";
    capa["optional"]["pagetimeout"]["default"] = DEFAULT_PAGE_TIMEOUT;

    /*LTS-END*/
    capa["optional"]["debug"]["name"] = "debug";
    capa["optional"]["debug"]["help"] = "The debug level at which messages need to be printed.";
    capa["optional"]["debug"]["option"] = "--debug";
    capa["optional"]["debug"]["type"] = "debug";

    hasSrt = false;
    srtTrack = 0;
    lastBufferCheck = 0;
    bufferPid = 0;
    internalOnly = false;
    isBuffer = false;
    startTime = Util::bootSecs();
    lastStats = 0;
  }

  void Input::checkHeaderTimes(const HTTP::URL & streamFile){

    /// \TODO Implement remote URLs?

    if (streamFile.isLocalPath()){
      const std::string & f = streamFile.getFilePath();

      struct stat bufStream;
      struct stat bufHeader;
      struct stat srtStream;
      std::string srtFile = f + ".srt";
      if (stat(srtFile.c_str(), &srtStream) == 0){
        hasSrt = true;
        srtSource.open(srtFile.c_str());
        INFO_MSG("File %s opened as srt source", srtFile.c_str());
      }

      if (stat(f.c_str(), &bufStream) != 0){
        INSANE_MSG("Source is not a file - ignoring header check");
        return;
      }
      std::string headerFile = f + ".dtsh";
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
    inputTimeout = config->getInteger("inputtimeout");
    Util::setStreamName(streamName);

    if (config->getBool("json")){
      capa["version"] = PACKAGE_VERSION;
      std::cout << capa.toString() << std::endl;
      return 0;
    }

    if (config->hasOption("enumerate") && config->getString("enumerate").size()){
      std::cout << enumerateSources(config->getString("enumerate")).toString() << std::endl;
      return 0;
    }

    if (config->hasOption("getcapa") && config->getString("getcapa").size()){
      std::cout << getSourceCapa(config->getString("getcapa")).toString() << std::endl;
      return 0;
    }

    INFO_MSG("Input booting");

    //Check if the input uses the name-based-override, and strip it
    {
      std::string input = config->getString("input");
      std::string prefix = capa["name"].asStringRef() + ":";
      Util::stringToLower(prefix);
      if (input.size() > prefix.size()){
        std::string match = input.substr(0, prefix.size());
        Util::stringToLower(match);
        if (prefix == match){
          //We have a prefix match - make sure we don't _also_ have a proper source_match
          bool source_match = false;
          if (capa["source_match"].size()){
            jsonForEach(capa["source_match"], it){
              const std::string & source = it->asStringRef();
              std::string front = source.substr(0, source.find('*'));
              std::string back = source.substr(source.find('*') + 1);

              if (input.size() > front.size()+back.size() && input.substr(0, front.size()) == front && input.substr(input.size() - back.size()) == back){
                source_match = true;
                break;
              }
            }
          }else{
            const std::string & source = capa["source_match"].asStringRef();
            std::string front = source.substr(0, source.find('*'));
            std::string back = source.substr(source.find('*') + 1);

            if (input.size() > front.size()+back.size() && input.substr(0, front.size()) == front && input.substr(input.size() - back.size()) == back){
              source_match = true;
            }
          }
          //Only if no source_match, strip the prefix from the input string
          if (!source_match){
            config->getOption("input", true).append(input.substr(prefix.size()));
          }
        }
      }
    }

    if (!checkArguments()){
      Util::logExitReason(ER_UNKNOWN, "Setup failed - exiting");
      return exitAndLogReason();
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
        //Set stream status to STRMSTAT_INIT, then close the page in non-master mode to keep it around
        char pageName[NAME_BUFFER_SIZE];
        snprintf(pageName, NAME_BUFFER_SIZE, SHM_STREAM_STATE, streamName.c_str());
        streamStatus.init(pageName, 2, false, false);
        if (!streamStatus){streamStatus.init(pageName, 2, true, false);}
        if (streamStatus){streamStatus.mapped[0] = STRMSTAT_INIT;}
        streamStatus.master = false;
        streamStatus.close();
        //Set stream input PID to current PID
        snprintf(pageName, NAME_BUFFER_SIZE, SHM_STREAM_IPID, streamName.c_str());
        pidPage.init(pageName, 8, true, false);
        if (pidPage){(*(uint64_t*)(pidPage.mapped)) = getpid();}
        pidPage.master = false;
        pidPage.close();
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
          //Set stream pull PID to current PID
          char pageName[NAME_BUFFER_SIZE];
          snprintf(pageName, NAME_BUFFER_SIZE, SHM_STREAM_PPID, streamName.c_str());
          pidPage.init(pageName, 8, true, false);
          if (pidPage){(*(uint64_t*)(pidPage.mapped)) = getpid();}
          pidPage.master = false;
          pidPage.close();
        }
      }
    }

    config->activate();

    if (getenv("NOFORK") || getenv("ATHEIST")){
      INFO_MSG("Not using angel process due to ATHEIST environment variable");
      if (playerLock){
        // Re-init streamStatus, previously closed
        char pageName[NAME_BUFFER_SIZE];
        snprintf(pageName, NAME_BUFFER_SIZE, SHM_STREAM_STATE, streamName.c_str());
        streamStatus.init(pageName, 2, false, false);
        if (!streamStatus){streamStatus.init(pageName, 2, true, false);}
        streamStatus.master = false;
        if (streamStatus){streamStatus.mapped[0] = STRMSTAT_INIT;}
      }
      int ret = 1;
      if (preRun()){
        ret = run();
      }else{
        return exitAndLogReason();
      }
      if (playerLock){
        playerLock.unlink();
        char pageName[NAME_BUFFER_SIZE];
        snprintf(pageName, NAME_BUFFER_SIZE, SHM_STREAM_STATE, streamName.c_str());
        streamStatus.init(pageName, 2, false, false);
        if (streamStatus){
          streamStatus.master = true;
          streamStatus.close();
        }
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
          streamStatus.init(pageName, 2, false, false);
          if (!streamStatus){streamStatus.init(pageName, 2, true, false);}
          streamStatus.master = false;
          if (streamStatus){streamStatus.mapped[0] = STRMSTAT_INIT;}
        }
        // Abandon all semaphores, ye who enter here.
        playerLock.abandon();
        pullLock.abandon();
        if (!preRun()){
          return exitAndLogReason();
        }
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
      int exitCode = WEXITSTATUS(status);
      if (WIFEXITED(status) && (exitCode == 0 || exitCode == 1)){
        HIGH_MSG("Input for stream %s shut down cleanly", streamName.c_str());
        break;
      }
      if (playerLock){
        char pageName[NAME_BUFFER_SIZE];
        snprintf(pageName, NAME_BUFFER_SIZE, SHM_STREAM_STATE, streamName.c_str());
        streamStatus.init(pageName, 2, false, false);
        if (streamStatus){streamStatus.mapped[0] = STRMSTAT_INVALID;}
      }
      // Fire the INPUT_ABORT trigger if the child process ends with an abnormal exit code
      // Prevents automatic restarts of the input for unrecoverable errors
      if (WIFEXITED(status)){
        char exitReason[256];
        memcpy(exitReason, Util::exitReason, 256);
        if (exitCode == 2){
          WARN_MSG("Child process %u exited with exit code %i (major error occurred), cleaning up...", pid, exitCode);
          Util::logExitReason(ER_UNKNOWN, "Child process %u exited with exit code %i (major error occurred)", pid, exitCode);
        }else if (exitCode == 132){
          WARN_MSG("Child process %u exited with exit code %i (SIGILL), cleaning up...", pid, exitCode);
          Util::logExitReason(ER_SIGILL, "Child process %u exited with exit code %i (SIGILL)", pid, exitCode);
        }else if (exitCode == 133){
          WARN_MSG("Child process %u exited with exit code %i (SIGTRAP)", pid, exitCode);
          Util::logExitReason(ER_SIGTRAP, "Child process %u exited with exit code %i (SIGTRAP)", pid, exitCode);
        }else if (exitCode == 134){
          WARN_MSG("Child process %u exited with exit code %i (SIGABRT)", pid, exitCode);
          Util::logExitReason(ER_SIGABRT, "Child process %u exited with exit code %i (SIGABRT)", pid, exitCode);
        }else if (exitCode == 136){
          WARN_MSG("Child process %u exited with exit code %i (SIGFPE)", pid, exitCode);
          Util::logExitReason(ER_SIGFPE, "Child process %u exited with exit code %i (SIGFPE)", pid, exitCode);
        }else if (exitCode == 137){
          WARN_MSG("Child process %u exited with exit code %i (used too much memory), cleaning up...", pid, exitCode);
          Util::logExitReason(ER_MEMORY, "Child process %u exited with exit code %i (used too much memory)", pid, exitCode);
        }else if (exitCode == 138){
          WARN_MSG("Child process %u exited with exit code %i (SIGBUS)", pid, exitCode);
          Util::logExitReason(ER_SIGBUS, "Child process %u exited with exit code %i (SIGBUS)", pid, exitCode);
        }else if (exitCode == 139){
          WARN_MSG("Child process %u exited with exit code %i (SEGFAULT)", pid, exitCode);
          Util::logExitReason(ER_SEGFAULT, "Child process %u exited with exit code %i (SEGFAULT)", pid, exitCode);
        }else{
          WARN_MSG("Child process %u exited with an unhandled exit code %i, cleaning up...", pid, exitCode);
          Util::logExitReason(ER_UNKNOWN, "Child process %u exited with an unhandled exit code %i", pid, exitCode);
        }
        doInputAbortTrigger(pid, Util::mRExitReason, Util::exitReason);
        memcpy(Util::exitReason, exitReason, 256);
      }
      
      #if DEBUG >= DLVL_DEVEL
      WARN_MSG(
          "Input for stream %s uncleanly shut down! Aborting restart; this is a development build.",
          streamName.c_str());
      break;
      #else
      if (config->is_active){
        WARN_MSG("Input for stream %s uncleanly shut down! Cleaning and restarting...", streamName.c_str());
      }else{
        WARN_MSG("Input for stream %s uncleanly killed, cleaning up...", streamName.c_str());
      }
      onCrash();
      if (config->is_active){
        Util::wait(reTimer);
        reTimer += 1000;
      }
#endif
    }

    if (playerLock){
      //Clear stream input PID
      char pageName[NAME_BUFFER_SIZE];
      snprintf(pageName, NAME_BUFFER_SIZE, SHM_STREAM_IPID, streamName.c_str());
      pidPage.init(pageName, 8, false, false);
      if (pidPage){
        pidPage.master = true;
        pidPage.close();
      }
      //Clear stream state
      snprintf(pageName, NAME_BUFFER_SIZE, SHM_STREAM_STATE, streamName.c_str());
      streamStatus.init(pageName, 2, false, false);
      if (streamStatus){
        streamStatus.master = true;
        streamStatus.close();
      }
      //Delete lock
      playerLock.unlink();
    }
    if (pullLock){
      //Clear stream pull PID
      char pageName[NAME_BUFFER_SIZE];
      snprintf(pageName, NAME_BUFFER_SIZE, SHM_STREAM_PPID, streamName.c_str());
      pidPage.init(pageName, 8, false, false);
      pidPage.master = true;
      pidPage.close();
      //Delete lock
      pullLock.unlink();
    }

    HIGH_MSG("Angel process for %s exiting", streamName.c_str());
    return 0;
  }

  int Input::run(){
    Comms::sessionConfigCache();
    if (streamStatus){streamStatus.mapped[0] = STRMSTAT_BOOT;}
    checkHeaderTimes(HTTP::localURIResolver().link(config->getString("input")));
    //needHeader internally calls readExistingHeader which in turn attempts to read header cache
    if (needHeader()){
      uint64_t timer = Util::getMicros();
      if (!readHeader() || (!M && needsLock())){
        Util::logExitReason(ER_READ_START_FAILURE, "Reading header for '%s' failed", config->getString("input").c_str());
        return exitAndLogReason();
      }
      timer = Util::getMicros(timer);
      INFO_MSG("Created header in %.3f ms (%zu tracks)", (double)timer/1000.0, M?M.trackCount():(size_t)0);
      //Write header to file for caching purposes
      M.toFile(config->getString("input") + ".dtsh");
    }
    postHeader();
    if (config->getBool("headeronly")){return 0;}
    if (M && M.getVod()){
      meta.removeEmptyTracks();
      parseHeader();
      INFO_MSG("Header parsed, %zu tracks", M.getValidTracks().size());
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
    return exitAndLogReason();
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
    // Export DTSH to a file or remote target
    outMeta.toFile(fileName + ".dtsh");
  }

  // Fires the `INPUT_ABORT` trigger. Allows overrides to allow parent processes
  // to fire the trigger for child processes which have already exited (EG segfaults)
  void Input::doInputAbortTrigger(pid_t pid, char *mRExitReason, char *exitReason){
    // Calculate variables required for the trigger
    if (Util::Config::binaryType == Util::INPUT && Triggers::shouldTrigger("INPUT_ABORT", streamName)){
      std::ostringstream pidString;
      pidString << pid;
      std::string payload = streamName + "\n" + config->getString("input") + "\n" \
        + "MistIn" + capa["name"].asString() + "\n" + pidString.str() + "\n" \
        + mRExitReason + "\n" + exitReason;
      Triggers::doTrigger("INPUT_ABORT", payload, streamName);
    }
  }

  // Logs the current exit reason and returns a 0 or 1 depending on whether
  // this was a clean exit or not
  bool Input::exitAndLogReason(){
    int returnCode = 1;
    // If no reason is set at all, return the default status
    if (!Util::exitReason[0]){
      INFO_MSG("Input closing without a set exit reason");
    }else if(strncmp(Util::mRExitReason, "CLEAN", 5) == 0){
      INFO_MSG("Input closing cleanly with reason: %s", Util::exitReason);
      returnCode = 0; 
    }else{
      WARN_MSG("Input closing unclean, reason: %s", Util::exitReason);
    }
    // If this is an unclean exit, fire the INPUT_ABORT trigger
    if (returnCode){
      doInputAbortTrigger(getpid(), Util::mRExitReason, Util::exitReason);
    }
    return returnCode;
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
    startTime = Util::bootSecs();

    if (!M){
      // Initialize meta page
      meta.reInit(streamName, true);
    }else{
      //std::set<size_t> validTracks = M.getValidTracks(true);
      //for (std::set<size_t>::iterator it = validTracks.begin(); it != validTracks.end(); it++){
      //  bufferFrame(*it, 0);
      //}
    }
    meta.setSource(config->getString("input"));

    internalOnly = (config->getString("input").find("INTERNAL_ONLY") != std::string::npos);
    isBuffer = (capa["name"].asStringRef() == "Buffer");

    /*LTS-START*/
    if (Triggers::shouldTrigger("STREAM_READY", config->getString("streamname"))){
      std::string payload = config->getString("streamname") + "\n" + capa["name"].asStringRef();
      if (!Triggers::doTrigger("STREAM_READY", payload, config->getString("streamname"))){
        Util::logExitReason(ER_TRIGGER, "STREAM_READY trigger returned false");
        config->is_active = false;
      }
    }
    /*LTS-END*/
    if (streamStatus){streamStatus.mapped[0] = STRMSTAT_READY;}

    INFO_MSG("Input started");
    activityCounter = Util::bootSecs();
    // main serve loop
    while (keepRunning()){
      uint64_t preMs = Util::bootMS();
      // load pages for connected clients on request
      userLeadIn();
      COMM_LOOP(users, userOnActive(id), userOnDisconnect(id))
      userLeadOut();

      // unload pages that haven't been used for a while
      removeUnused();
      if (!M){
        Util::logExitReason(ER_SHM_LOST, "Lost connection to metadata");
        break;
      }

      if (M.getLive() && !internalOnly){
        uint64_t currLastUpdate = M.getLastUpdated();
        if (currLastUpdate > activityCounter){activityCounter = currLastUpdate;}
      }else{
        if ((connectedUsers || isAlwaysOn()) && M.getValidTracks().size()){activityCounter = Util::bootSecs();}
      }

      inputServeStats();
      // if not shutting down, wait 1 second before looping
      preMs = Util::bootMS() - preMs;
      uint64_t waitMs = INPUT_USER_INTERVAL;
      if (preMs >= waitMs){waitMs = 0;}else{waitMs -= preMs;}
      if (config->is_active && waitMs){
        Util::wait(waitMs);
      }
    }
    if (!isThread()){
      if (streamStatus){streamStatus.mapped[0] = STRMSTAT_SHUTDOWN;}
      config->is_active = false;
    }
    finish();
    userSelect.clear();
    if (!isThread()){
      if (streamStatus){streamStatus.mapped[0] = STRMSTAT_OFF;}
    }
  }

  void Input::inputServeStats(){
    uint64_t now = Util::bootSecs();
    if (now != lastStats){
      if (!internalOnly && !isBuffer){
        if (!statComm){statComm.reload(streamName, getConnectedBinHost(), JSON::Value(getpid()).asString(), "INPUT:" + capa["name"].asStringRef(), "");}
        if (statComm){
          statComm.setNow(now);
          statComm.setStream(streamName);
          statComm.setTime(now - startTime);
          statComm.setLastSecond(0);
          connStats(statComm);
        }
      }
      lastStats = now;
    }
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
    bool ret = config->is_active && ((Util::bootSecs() - activityCounter) < inputTimeout);
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
    if (!ret && ((Util::bootSecs() - activityCounter) >= inputTimeout)){
      Util::logExitReason(ER_CLEAN_INACTIVE, "no activity for %us (> %" PRIu64 "s)", Util::bootSecs() - activityCounter, inputTimeout);
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
                            true, overrides, &bufferPid)){// manually override stream url to start the buffer
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

    //Simulated real time inputs bypass most normal logic
    if (config->getBool("realtime")){
      realtimeMainLoop();
      finish();
      return;
    }

    meta.reInit(streamName, false);

    if (!openStreamSource()){
      Util::logExitReason(ER_READ_START_FAILURE, "Unable to connect to source");
      return;
    }
    parseStreamHeader();

    if (publishesTracks()){
      std::set<size_t> validTracks = M.getMySourceTracks(getpid());
      if (!validTracks.size()){
        Util::logExitReason(ER_CLEAN_EOF, "No tracks found, cancelling pull");
        userSelect.clear();
        finish();
        return;
      }
    }

    streamMainLoop();
    closeStreamSource();
    userSelect.clear();
    finish();
    return;
  }

  bool Input::bufferActive(){
    if (bufferPid && Util::bootSecs() > lastBufferCheck){
      if (!Util::Procs::isRunning(bufferPid)){bufferPid = 0;}
    }
    return bufferPid;
  }

  void Input::streamMainLoop(){
    uint64_t statTimer = 0;
    uint64_t startTime = Util::bootSecs();
    Comms::Connections statComm;
    getNext();
    if (thisPacket && !userSelect.count(thisIdx)){
      userSelect[thisIdx].reload(streamName, thisIdx, COMM_STATUS_ACTIVE | COMM_STATUS_SOURCE | COMM_STATUS_DONOTTRACK);
    }
    while (thisPacket && config->is_active && userSelect[thisIdx]){
      if (userSelect[thisIdx].getStatus() & COMM_STATUS_REQDISCONNECT){
        Util::logExitReason(ER_CLEAN_LIVE_BUFFER_REQ, "buffer requested shutdown");
        break;
      }
      if (isSingular() && !bufferActive()){
        Util::logExitReason(ER_SHM_LOST, "Buffer shut down");
        return;
      }
      bufferLivePacket(thisPacket);
      getNext();
      if (!thisPacket){
        Util::logExitReason(ER_CLEAN_EOF, "no more data");
        break;
      }
      if (thisPacket && !userSelect.count(thisIdx)){
        userSelect[thisIdx].reload(streamName, thisIdx, COMM_STATUS_ACTIVE | COMM_STATUS_SOURCE | COMM_STATUS_DONOTTRACK);
      }

      if (Util::bootSecs() - statTimer > 1){
        // Connect to stats for INPUT detection
        if (!statComm){statComm.reload(streamName, getConnectedBinHost(), JSON::Value(getpid()).asString(), "INPUT:" + capa["name"].asStringRef(), "");}
        if (statComm){
          if (!statComm){
            config->is_active = false;
            Util::logExitReason(ER_CLEAN_CONTROLLER_REQ, "received shutdown request from controller");
            return;
          }
          uint64_t now = Util::bootSecs();
          statComm.setNow(now);
          statComm.setStream(streamName);
          statComm.setTime(now - startTime);
          statComm.setLastSecond(0);
          connStats(statComm);
        }

        statTimer = Util::bootSecs();
      }
    }
  }
  
  void Input::connStats(Comms::Connections &statComm){
    statComm.setUp(0);
    statComm.setDown(streamByteCount());
  }

  void Input::realtimeMainLoop(){
    MEDIUM_MSG("Starting real-time main loop!");
    uint64_t statTimer = 0;
    uint64_t startTime = Util::bootSecs();
    size_t idx;
    Comms::Connections statComm;


    DTSC::Meta liveMeta(config->getString("streamname"), false);
    DTSC::veryUglyJitterOverride = SIMULATED_LIVE_BUFFER;


    uint64_t minFirstMs = 0xFFFFFFFFFFFFFFFFull;
    uint64_t maxFirstMs = 0;
    uint64_t minLastMs = 0xFFFFFFFFFFFFFFFFull;
    uint64_t maxLastMs = 0;

    // track lowest firstms value
    std::set<size_t> validTracks = M.getValidTracks();
    INFO_MSG("VoD metadata has %zu valid tracks", validTracks.size());
    if (!validTracks.size()){
      FAIL_MSG("No valid tracks! Aborting!");
      return;
    }
    for (std::set<size_t>::iterator it = validTracks.begin(); it != validTracks.end(); ++it){
      if (M.getFirstms(*it) < minFirstMs){minFirstMs = M.getFirstms(*it);}
      if (M.getFirstms(*it) > maxFirstMs){maxFirstMs = M.getFirstms(*it);}
      if (M.getLastms(*it) < minLastMs){minLastMs = M.getLastms(*it);}
      if (M.getLastms(*it) > maxLastMs){maxLastMs = M.getLastms(*it);}
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
    if (minFirstMs == 0xFFFFFFFFFFFFFFFFull){minFirstMs = 0;}

    // find highest current time
    int64_t timeOffset = 0;
    validTracks = liveMeta.getValidTracks();
    for (std::set<size_t>::iterator it = validTracks.begin(); it != validTracks.end(); ++it){
      timeOffset = std::max(timeOffset, (int64_t)liveMeta.getLastms(*it));
    }
    INFO_MSG("Live metadata has %zu valid tracks, last timestamp %" PRIu64, validTracks.size(), timeOffset);
    if (timeOffset){
      MEDIUM_MSG("Offset is %" PRId64
                 "ms, adding 40ms and subtracting the start time of %" PRIu64,
                 timeOffset, minFirstMs);
      timeOffset += 40; // Add an artificial frame at 25 FPS to make sure we append, not overwrite
    }
    timeOffset -= minFirstMs; // we don't need to add the lowest firstms value to the offset, as it's already there

    /// This maps local track offsets to stream track offsets
    std::map<uint64_t, uint64_t> realTimeTrackMap;

    //No time offset and/or no currently valid tracks?
    //That means this must be the first entry in this realtime stream. Create the tracks!
    if (!timeOffset || !validTracks.size()){
      liveMeta.setBootMsOffset(Util::bootMS());
      validTracks = M.getValidTracks();
      size_t newID = 0;
      for (std::set<size_t>::iterator it = validTracks.begin(); it != validTracks.end(); ++it){
        size_t newIdx = liveMeta.addTrack();
        realTimeTrackMap[*it] = newIdx;
        MEDIUM_MSG("Gonna write track %zu to %zu", *it, newIdx);
        liveMeta.setID(newIdx, newID++);
        liveMeta.setType(newIdx, M.getType(*it));
        liveMeta.setCodec(newIdx, M.getCodec(*it));
        liveMeta.setFpks(newIdx, M.getFpks(*it));
        liveMeta.setInit(newIdx, M.getInit(*it));
        liveMeta.setLang(newIdx, M.getLang(*it));
        liveMeta.setRate(newIdx, M.getRate(*it));
        liveMeta.setSize(newIdx, M.getSize(*it));
        liveMeta.setWidth(newIdx, M.getWidth(*it));
        liveMeta.setHeight(newIdx, M.getHeight(*it));
      }
    }else{
      validTracks = M.getValidTracks();
      std::set<size_t> validLive = liveMeta.getValidTracks();
      for (std::set<size_t>::iterator it = validTracks.begin(); it != validTracks.end(); ++it){
        for (std::set<size_t>::iterator lit = validLive.begin(); lit != validLive.end(); ++lit){
          if (liveMeta.isClaimed(*lit)){continue;}
          if (liveMeta.getType(*lit) != M.getType(*it)){continue;}
          if (liveMeta.getCodec(*lit) != M.getCodec(*it)){continue;}
          if (liveMeta.getInit(*lit) != M.getInit(*it)){continue;}
          //Matching type/codec/init! Use it!
          realTimeTrackMap[*it] = *lit;
          liveMeta.claimTrack(*lit);
          MEDIUM_MSG("Gonna write track %zu to existing track %zu", *it, *lit);
          break;
        }
      }
    }
    int64_t bootMsOffset = liveMeta.getBootMsOffset();
    validTracks.clear();

    seek(0);/// \TODO Is this actually needed?
    while (config->is_active){
      getNext();
      if (!thisPacket){
        Util::logExitReason(ER_CLEAN_EOF, "no more data");
        break;
      }
      idx = realTimeTrackMap.count(thisIdx) ? realTimeTrackMap[thisIdx] : INVALID_TRACK_ID;
      if (thisPacket && !userSelect.count(idx)){
        userSelect[idx].reload(streamName, idx, COMM_STATUS_ACTIVE | COMM_STATUS_SOURCE | COMM_STATUS_DONOTTRACK);
      }
      if (userSelect[idx].getStatus() & COMM_STATUS_REQDISCONNECT){
        Util::logExitReason(ER_CLEAN_LIVE_BUFFER_REQ, "buffer requested shutdown");
        break;
      }
      while (config->is_active && userSelect[idx] &&
             Util::bootMS() + SIMULATED_LIVE_BUFFER < (thisTime + timeOffset) + bootMsOffset){
        Util::sleep(std::min(((thisTime + timeOffset) + bootMsOffset) - (Util::getMS() + SIMULATED_LIVE_BUFFER),
                             (uint64_t)1000));
      }

      //Buffer the packet
      if (idx == INVALID_TRACK_ID){
        INFO_MSG("Packet for track %zu has no valid index!", thisIdx);
      }else{
        char *data;
        size_t dataLen;
        thisPacket.getString("data", data, dataLen);
        bufferLivePacket(thisTime+timeOffset, thisPacket.getInt("offset"), idx, data, dataLen, 0, thisPacket.getFlag("keyframe"), liveMeta);
      }


      if (Util::bootSecs() - statTimer > 1){
        // Connect to stats for INPUT detection
        if (!statComm){statComm.reload(streamName, getConnectedBinHost(), JSON::Value(getpid()).asString(), "INPUT:" + capa["name"].asStringRef(), "");}
        if (statComm){
          if (statComm.getStatus() & COMM_STATUS_REQDISCONNECT){
            config->is_active = false;
            Util::logExitReason(ER_CLEAN_CONTROLLER_REQ, "received shutdown request from controller");
            return;
          }
          uint64_t now = Util::bootSecs();
          statComm.setNow(now);
          statComm.setStream(streamName);
          statComm.setConnector("INPUT:" + capa["name"].asStringRef());
          statComm.setTime(now - startTime);
          statComm.setLastSecond(0);
          connStats(statComm);
        }

        statTimer = Util::bootSecs();
      }
    }
    for (std::map<uint64_t, uint64_t>::iterator it = realTimeTrackMap.begin(); it != realTimeTrackMap.end(); ++it){
      liveMeta.abandonTrack(it->second);
    }
  }

  void Input::finish(){
    if (!standAlone || config->getBool("realtime")){return;}
    for (std::map<size_t, std::map<uint32_t, uint64_t> >::iterator it = pageCounter.begin();
         it != pageCounter.end(); it++){
      for (std::map<uint32_t, uint64_t>::iterator it2 = it->second.begin(); it2 != it->second.end(); it2++){
        it2->second = 1;
      }
    }
    removeUnused();
  }

  void Input::removeUnused(){
    uint64_t timeout = config->getInteger("pagetimeout");
    uint64_t bufferTime = timeout * 1000;
    if (config->hasOption("bufferTime")){
      bufferTime = config->getInteger("bufferTime");
    }
    uint64_t cTime = Util::bootSecs();
    for (std::map<size_t, std::map<uint32_t, uint64_t> >::iterator it = pageCounter.begin();
         it != pageCounter.end(); it++){
      std::set<uint32_t> deletedEntries;
      for (std::map<uint32_t, uint64_t>::iterator it2 = it->second.begin(); it2 != it->second.end(); it2++){
        if (isRecentLivePage(it->first, it2->first, bufferTime)){continue;}
        if (cTime > it2->second + timeout){
          deletedEntries.insert(it2->first);
          bufferRemove(it->first, it2->first);
          HIGH_MSG("Unloading page %u track %lu", it2->first, it->first);
        }
      }
      while (deletedEntries.size()){
        it->second.erase(*(deletedEntries.begin()));
        deletedEntries.erase(deletedEntries.begin());
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

  /// Attempts to create a header.
  /// Returns true on success.
  /// Default implementation fails and prints a warning.
  bool Input::readHeader(){
    WARN_MSG("Default readHeader implementation called - this is not expected to happen");
    return false;
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
            if ((tPages.getEndPos() - tPages.getDeleted()) >= tPages.getRCount()){
              meta.resizeTrack(*it, M.fragments(*it).getRCount(), M.keys(*it).getRCount(), M.parts(*it).getRCount(), tPages.getRCount() * 2, "not enough pages");
            }
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
      MEDIUM_MSG("Track %zu (%s) split into %" PRIu64 " pages", *it, M.getCodec(*it).c_str(), tPages.getEndPos());
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
    if (!M.getVod()){return true;}
    DONTEVEN_MSG("Buffering track %zu, key %" PRIu32, idx, keyNum);
    bool isVideo = M.getType(idx) == "video";
    size_t sourceIdx = M.getSourceTrack(idx);
    if (sourceIdx == INVALID_TRACK_ID){sourceIdx = idx;}

    const Util::RelAccX &tPages = M.pages(idx);
    DTSC::Keys keys(M.keys(idx));
    uint64_t firstKey = keys.getFirstValid();
    if (keyNum < firstKey){
      HIGH_MSG("Key %" PRIu32 " on track %zu no longer seekable (earliest requestable key is %" PRIu64
                "). Cancelling buffering.",
                keyNum, idx, firstKey);
      return true;
    }
    uint64_t lastKey = keys.getEndValid();
    if (keyNum > lastKey){
      // End of movie here, returning true to avoid various error messages
      if (keyNum > lastKey + 1){
        WARN_MSG("Key %" PRIu32 " on track %zu is higher than total (latest key is %" PRIu64
                 "). Cancelling buffering.",
                 keyNum, idx, lastKey);
      }
      return true;
    }
    if (!tPages.getEndPos()){
      WARN_MSG("No pages for track %zu found! Cancelling bufferFrame", idx);
      return false;
    }
    uint64_t pageIdx = 0;
    for (uint64_t i = tPages.getDeleted(); i < tPages.getEndPos(); i++){
      if (tPages.getInt("firstkey", i) > keyNum) break;
      pageIdx = i;
    }
    uint32_t pageNumber = tPages.getInt("firstkey", pageIdx);
    pageCounter[idx][pageNumber] = Util::bootSecs();
    if (isBuffered(idx, pageNumber, meta)){
      // Mark the page as still actively requested
      DONTEVEN_MSG("Track %zu, key %" PRIu32 " is already buffered in page %" PRIu32
                   ". Cancelling bufferFrame",
                   idx, keyNum, pageNumber);
      return true;
    }
    // Update keynum to point to the corresponding page
    uint64_t bufferTimer = Util::bootMS();
    keyNum = pageNumber;
    IPC::sharedPage page;
    if (!bufferStart(idx, pageNumber, page, meta)){
      WARN_MSG("bufferStart failed! Cancelling bufferFrame");
      return false;
    }

    uint64_t keyTime = keys.getTime(keyNum);

    inputServeStats();

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
      // in case earlier seeking was imprecise, seek to the exact point
      while (srtPack && srtPack.getTime() < keyTime){getNextSrt();}
    }else{
      getNext(sourceIdx);
      // in case earlier seeking was imprecise, seek to the exact point
      while (thisPacket && thisTime < keyTime){getNext(sourceIdx);}
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
                     srtPack.getFlag("keyframe"), page);
          ++packCounter;
          byteCounter += srtPack.getDataLen();
          lastBuffered = srtPack.getTime();
        }
        getNextSrt();
      }
    }else{
      size_t prevPos = 0;
      size_t partNo = keys.getFirstPart(keyNum);
      DTSC::Parts parts(M.parts(idx));
      while (thisPacket && thisTime < stopTime){
        if (connectedUsers || isAlwaysOn()){activityCounter = Util::bootSecs();}
        if (thisTime >= lastBuffered){
          if (sourceIdx != idx){
#ifdef SSL
            if (encryption.find(":") != std::string::npos || M.getEncryption(idx).find(":") != std::string::npos){
              if (encryption == ""){
                encryption = M.getEncryption(idx);
                std::string encryptionKey =
                    Encodings::Hex::decode(encryption.substr(encryption.find(":") + 1));
                aesCipher.setEncryptKey(encryptionKey.c_str());
              }
              if (encryption.substr(0, encryption.find('/')) == "CTR128"){
                DTSC::Packet encPacket = aesCipher.encryptPacketCTR(
                    M, thisPacket, M.getIvec(idx) + M.getPartIndex(thisTime, idx), idx);
                thisPacket = encPacket;
              }else if (encryption.substr(0, encryption.find('/')) == "CBC128"){
                char ivec[] ={0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
                Bit::htobll(ivec + 8, M.getIvec(idx) + M.getPartIndex(thisTime, idx));
                DTSC::Packet encPacket = aesCipher.encryptPacketCBC(M, thisPacket, ivec, idx);
                thisPacket = encPacket;
              }
            }else{
#endif
              thisPacket = DTSC::Packet(thisPacket, idx);
#ifdef SSL
            }
#endif
          }
          //Sanity check: are we matching the key's data size?
          if (thisPacket.getFlag("keyframe")){
            size_t currPos = tPages.getInt("avail", pageIdx);
            if (currPos){
              size_t keySize = keys.getSize(keyNum);
              if (currPos-prevPos == keySize){
                VERYHIGH_MSG("Key %" PRIu32 " was %zu bytes", keyNum, currPos-prevPos);
              }else if (currPos-prevPos > keySize){
                FAIL_MSG("Key %" PRIu32 " was %zu bytes but should've been %zu bytes! (differs %d)", keyNum, currPos-prevPos, keySize, (int)(currPos-prevPos-keySize));
              }else{
                MEDIUM_MSG("Key %" PRIu32 " was %zu bytes but should've been %zu bytes! (differs %d)", keyNum, currPos-prevPos, keySize, (int)(currPos-prevPos-keySize));
              }
              ++keyNum;
              prevPos = currPos;
            }
          }
          char *data;
          size_t dataLen;
          thisPacket.getString("data", data, dataLen);
          if (dataLen != parts.getSize(partNo)){
            if (partNo && dataLen == parts.getSize(partNo-1)){
              FAIL_MSG("Part size for part %zu (%" PRIu64 "ms) matches part before it, instead. Aborting page load!", partNo, thisTime);
              break;
            }else if (dataLen == parts.getSize(partNo+1)){
              FAIL_MSG("Part size for part %zu (%" PRIu64 "ms) matches part after it, instead. Aborting page load!", partNo, thisTime);
              break;
            }else{
              INFO_MSG("Part %zu (@%" PRIu64 ") size mismatch: %zu (actual) != %zu (expected)", partNo, thisTime, dataLen, parts.getSize(partNo));
            }
          }
          ++partNo;
          HIGH_MSG("Buffering VoD packet (%zuB) @%" PRIu64 " ms on track %zu with offset %" PRIu64, dataLen, thisTime, idx, thisPacket.getInt("offset"));
          bufferNext(thisTime, thisPacket.getInt("offset"), idx, data, dataLen,
                     thisPacket.getInt("bpos"), thisPacket.getFlag("keyframe"), page);
          ++packCounter;
          byteCounter += thisPacket.getDataLen();
          lastBuffered = thisTime;
        }
        inputServeStats();
        getNext(sourceIdx);
      }
      //Sanity check: are we matching the key's data size?
      if (isVideo){
        size_t currPos = tPages.getInt("avail", pageIdx);
        if (currPos){
          size_t keySize = keys.getSize(keyNum);
          if (currPos-prevPos == keySize){
            VERYHIGH_MSG("Key %" PRIu32 " was %zu bytes", keyNum, currPos-prevPos);
          }else if (currPos-prevPos > keySize){
            FAIL_MSG("Key %" PRIu32 " was %zu bytes but should've been %zu bytes! (differs %d)", keyNum, currPos-prevPos, keySize, (int)(currPos-prevPos-keySize));
          }else{
            MEDIUM_MSG("Key %" PRIu32 " was %zu bytes but should've been %zu bytes! (differs %d)", keyNum, currPos-prevPos, keySize, (int)(currPos-prevPos-keySize));
          }
          ++keyNum;
          prevPos = currPos;
        }
      }
    }
    page.close();
    bufferTimer = Util::bootMS() - bufferTimer;
    if (packCounter < tPages.getInt("parts", pageIdx)){
      FAIL_MSG("Track %zu, page %" PRIu32 " (" PRETTY_PRINT_MSTIME " - " PRETTY_PRINT_MSTIME ") NOT FULLY buffered in %" PRIu64 "ms - erasing for later retry",
               idx, pageNumber, PRETTY_ARG_MSTIME(tPages.getInt("firsttime", pageIdx)), PRETTY_ARG_MSTIME(thisTime), bufferTimer);
      INFO_MSG("  (%" PRIu32 "/%" PRIu64 " parts, %" PRIu64 " bytes)", packCounter,
               tPages.getInt("parts", pageIdx), byteCounter);
      pageCounter[idx].erase(pageNumber);
      bufferRemove(idx, pageNumber, pageIdx);
      return false;
    }else{
      INFO_MSG("Track %zu, page %" PRIu32 " (" PRETTY_PRINT_MSTIME " - " PRETTY_PRINT_MSTIME ") buffered in %" PRIu64 "ms",
               idx, pageNumber, PRETTY_ARG_MSTIME(tPages.getInt("firsttime", pageIdx)), PRETTY_ARG_MSTIME(thisTime), bufferTimer);
      INFO_MSG("  (%" PRIu32 "/%" PRIu64 " parts, %" PRIu64 " bytes)", packCounter,
               tPages.getInt("parts", pageIdx), byteCounter);
      pageCounter[idx][pageNumber] = Util::bootSecs();
      return true;
    }
  }

  bool Input::atKeyFrame(){
    static std::map<size_t, uint64_t> lastSeen;
    // not in keyTimes? We're not at a keyframe.
    if (!keyTimes[thisIdx].count(thisTime)){return false;}
    // skip double times
    if (lastSeen.count(thisIdx) && lastSeen[thisIdx] == thisTime){return false;}
    // set last seen, and return true
    lastSeen[thisIdx] = thisTime;
    return true;
  }

  bool Input::readExistingHeader(){
    if (!config->getBool("realtime")){
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
    }
    // Try to read any existing DTSH file
    std::string fileName = config->getString("input") + ".dtsh";
    HIGH_MSG("Loading metadata for stream '%s' from file '%s'", streamName.c_str(), fileName.c_str());
    char *scanBuf;
    size_t fileSize;
    HTTP::URIReader inFile(fileName);
    if (!inFile){return false;}
    inFile.readAll(scanBuf, fileSize);
    inFile.close();
    if (!fileSize){return false;}
    DTSC::Packet pkt(scanBuf, fileSize, true);
    HIGH_MSG("Retrieved header of %zu bytes", fileSize);
    meta.reInit(config->getBool("realtime") ? "" : streamName, pkt.getScan());

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
      if (it->second){isAlive = true;}
    }
    return isAlive && config->is_active;
  }
}// namespace Mist
