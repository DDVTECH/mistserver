#include "controller_storage.h"

#include "controller_api.h"
#include "controller_capabilities.h"
#include "controller_push.h"
#include "controller_streams.h"

#include <mist/auth.h>
#include <mist/defines.h>
#include <mist/downloader.h>
#include <mist/encode.h>
#include <mist/jwt.h>
#include <mist/shared_memory.h>
#include <mist/timing.h>
#include <mist/triggers.h>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <set>
#include <signal.h>
#include <sys/stat.h>

///\brief Holds everything unique to the controller.
namespace Controller{
  Event::Loop E;
  std::string instanceId; /// instanceId (previously uniqId) is set in controller.cpp
  std::string prometheus;
  std::string accesslog;
  std::string udpApiBindAddr;
  Util::Config conf;
  JSON::Value Storage; ///< Global storage of data.
  std::mutex configMutex;
  std::mutex logMutex;
  std::set<std::string> shmList;
  std::mutex shmListMutex;
  uint64_t logCounter = 0;
  uint64_t logsParsed = 0;
  uint64_t lastConfigChange = 0;
  uint64_t lastConfigWrite = 0;
  bool isTerminal = false;
  bool isColorized = false;
  uint32_t maxLogsRecs = 0;
  uint32_t maxAccsRecs = 0;
  uint64_t firstLog = 0;

  HTTP::Downloader jwkDL;
  std::map<std::string, std::set<JWT::Key>> jwkResolved;
  std::map<std::string, uint64_t> uriExpiresAt;
  std::map<std::string, std::string> uriEndpoint;
  std::map<std::string, JSON::Value> uriPerms;

  void *logMemory = 0;
  IPC::sharedPage *shmLogs = 0;
  Util::RelAccX *rlxLogs = 0;
  Util::RelAccXFieldData logFTime;
  Util::RelAccXFieldData logFKind;
  Util::RelAccXFieldData logFMsg;
  Util::RelAccXFieldData logFStrm;
  Util::RelAccXFieldData logFPID;
  Util::RelAccXFieldData logFExe;
  Util::RelAccXFieldData logFLine;

  IPC::sharedPage *shmAccs = 0;
  Util::RelAccX *rlxAccs = 0;
  IPC::sharedPage *shmStrm = 0;
  Util::RelAccX *rlxStrm = 0;
  uint64_t systemBoot = Util::unixMS() - Util::bootMS();

  JSON::Value lastConfigWriteAttempt;
  JSON::Value lastConfigSeen;
  std::map<std::string, JSON::Value> lastConfigWritten;

  Util::RelAccX *logAccessor(){return rlxLogs;}

  Util::RelAccX *accesslogAccessor(){return rlxAccs;}

  Util::RelAccX *streamsAccessor(){return rlxStrm;}

  /// Parses the logs written to shared memory by the logging library
  /// This function is called from the main thread
  void logParser() {
    std::lock_guard<std::mutex> guard(logMutex);
    static JSON::Value m;
    while (rlxLogs && logsParsed < logCounter) {
      if (logsParsed < rlxLogs->getDeleted()) {
        size_t skipped = rlxLogs->getDeleted() - logsParsed;
        logsParsed += skipped;
        WARN_MSG("Too many logs! Skipping %zu log lines to keep up", skipped);
      }
      const uint64_t time = rlxLogs->getInt(logFTime, logsParsed);
      const char *kind = rlxLogs->getPointer(logFKind, logsParsed);
      const char *msg = rlxLogs->getPointer(logFMsg, logsParsed);
      const char *strm = rlxLogs->getPointer(logFStrm, logsParsed);
      const uint64_t pid = rlxLogs->getInt(logFPID, logsParsed);
      const char *exe = rlxLogs->getPointer(logFExe, logsParsed);
      const char *line = rlxLogs->getPointer(logFLine, logsParsed);
      ++logsParsed;
      if (!time || !msg[0]) { continue; }

      m[0u] = time;
      m[1u] = kind;
      m[2u] = msg;
      m[3u] = strm;
      m[4u] = pid;
      m[5u] = exe;
      m[6u] = line;
      Storage["log"].append(m);
      Storage["log"].shrink(100); // limit to 100 log messages
      if (pid) {
        if (isPushActive(pid)) { pushLogMessage(pid, m); }
        if (isProcActive(pid)) { procLogMessage(pid, m); }
      }
      Controller::callLogger(time, kind, msg, strm, pid, exe, line);
    }
  }

  void logAccess(const std::string &sessId, const std::string &strm, const std::string &conn,
                 const std::string &host, uint64_t duration, uint64_t up, uint64_t down,
                 const std::string &tags){
    uint64_t t = Util::epoch();
    Controller::callAccess(t, sessId, strm, conn, host, duration, up, down, tags);
    if (rlxAccs && rlxAccs->isReady()){
      uint64_t newEndPos = rlxAccs->getEndPos();
      rlxAccs->setRCount(newEndPos + 1 > maxLogsRecs ? maxAccsRecs : newEndPos + 1);
      rlxAccs->setDeleted(newEndPos + 1 > maxAccsRecs ? newEndPos + 1 - maxAccsRecs : 0);
      rlxAccs->setInt("time", t, newEndPos);
      rlxAccs->setString("session", sessId, newEndPos);
      rlxAccs->setString("stream", strm, newEndPos);
      rlxAccs->setString("connector", conn, newEndPos);
      rlxAccs->setString("host", host, newEndPos);
      rlxAccs->setInt("duration", duration, newEndPos);
      rlxAccs->setInt("up", up, newEndPos);
      rlxAccs->setInt("down", down, newEndPos);
      rlxAccs->setString("tags", tags, newEndPos);
      rlxAccs->setEndPos(newEndPos + 1);
    }
  }

  size_t jwkUriCheck() {
    if (jwkDL.isEventLooping()) return 5000;
    size_t nowTime = Util::bootMS();
    size_t nextCallTime = nowTime + 60000;

    // Find all URIs in the storage and add them to the map if they are new
    jsonForEachConst (Storage["jwks"], it) {
      const std::string uri = (it->isArray() && it->size()) ? (*it)[0u].asStringRef() : it->asStringRef();
      if (uri.find("://") == std::string::npos || uriExpiresAt.count(uri)) continue;
      JSON::Value perms = (it->isArray() && it->size() > 1) ? (*it)[1u] : JSON::EMPTY;
      uriExpiresAt[uri] = nowTime;
      uriPerms[uri] = perms;
    }

    // When there is a URI expiring within 60s shorten the next call time
    // Do not check explicitly for URI, the count() call is probably faster than find() on a JWK
    std::string mostExpiredUri;
    JSON::Value mostExpiredPerms;
    for (auto jwk : uriExpiresAt) {
      if (jwk.second >= nextCallTime) continue;
      nextCallTime = jwk.second;
      mostExpiredUri = jwk.first;
      mostExpiredPerms = uriPerms[jwk.first];
    }

    // No (expired) URI means we do not have to download anything right now
    nextCallTime = std::max(nextCallTime, nowTime);
    if (mostExpiredUri.empty() || nextCallTime > nowTime) return nextCallTime - nowTime;

    // Scooby Doo says 'ruh-roh! this uri is expired!'
    jwkDL.getEventLooped(Controller::E, mostExpiredUri, 5, [mostExpiredPerms, nowTime, mostExpiredUri]() {
      // Success callback function
      uriExpiresAt[mostExpiredUri] = nowTime + 60000;
      if (!jwkDL.isOk()) return;

      // Set the new expiry time depending on the cache control header
      const std::string & cc = jwkDL.getHeader("Cache-Control");
      size_t pos = cc.find("max-age=");
      if (pos != std::string::npos) { uriExpiresAt[mostExpiredUri] = nowTime + 1000 * std::atoi(cc.data() + pos + 8); }

      // Get the response, should be either a 'well-known' response or an array of keys
      const JSON::Value & response = JSON::fromString(jwkDL.data());

      // In the event that the endpoint changed remove all old keys associated with it
      std::string jwksUri = response["jwks_uri"].asString();
      if (uriEndpoint.count(mostExpiredUri) && jwksUri != uriEndpoint[mostExpiredUri]) {
        // Schedule JWK retrieval it within a second from now and erase from all structures
        jwkResolved.erase(uriEndpoint[mostExpiredUri]);
        uriExpiresAt.erase(uriEndpoint[mostExpiredUri]);
      }

      // Parse the JWKs endpoint from the generic well-known endpoint
      if (jwksUri.size()) {
        uriExpiresAt[jwksUri] = nowTime;
        uriEndpoint[mostExpiredUri] = jwksUri;
        uriPerms[jwksUri] = mostExpiredPerms;
      } else {
        uriExpiresAt.erase(jwksUri);
        uriEndpoint.erase(mostExpiredUri);
        uriPerms.erase(jwksUri);
      }

      // Parse any keys that are present in the 'keys' member
      if (response["keys"].isArray()) {
        jsonForEachConst (response["keys"], key) {
          const std::string & kty = (*key)["kty"].asStringRef();
          if (kty != "oct" && kty != "RSA" && kty != "EC") continue;
          JWT::Key jwk = JWT::Key(*key, mostExpiredPerms);
          jwkResolved[mostExpiredUri].insert(jwk);
        }
      } else {
        jwkResolved.erase(mostExpiredUri);
      }
      writeConfig();
    }, [nowTime, mostExpiredUri]() {
      // Failure callback function
      uriExpiresAt[mostExpiredUri] = nowTime + 60000;
    });

    return nextCallTime - nowTime;
  }

  void normalizeTrustedProxies(JSON::Value &tp){
    // First normalize to arrays
    if (!tp.isArray()){tp.append(tp.asString());}
    // Now, wipe any empty entries, and convert spaces to array entries
    std::set<std::string> n;
    jsonForEach(tp, jit){
      if (!jit->isString()){*jit = jit->asString();}
      if (jit->asStringRef().find(' ') == std::string::npos){
        n.insert(jit->asStringRef());
        continue;
      }
      std::string tmp = jit->asStringRef();
      while (tmp.find(' ') != std::string::npos){
        size_t p = tmp.find(' ');
        n.insert(tmp.substr(0, p));
        tmp.erase(0, p + 1);
      }
      if (tmp.size()){n.insert(tmp);}
    }
    n.erase("");
    // Re-write the entire array, which is now normalized
    tp.shrink(0);
    for (std::set<std::string>::iterator it = n.begin(); it != n.end(); ++it){tp.append(*it);}
  }

  ///\brief Write contents to Filename
  ///\param Filename The full path of the file to write to.
  ///\param contents The data to be written to the file.
  bool WriteFile(std::string Filename, std::string contents){
    if (!Util::createPathFor(Filename)){
      ERROR_MSG("Could not create parent folder for file %s!", Filename.c_str());
      return false;
    }
    std::ofstream File;
    File.open(Filename.c_str());
    File << contents << std::endl;
    File.close();
    return File.good();
  }

  void initStorage() {
    shmAccs = new IPC::sharedPage(SHM_STATE_ACCS, 1024 * 1024, false, false); // max 1M of accesslogs cached
    if (!shmAccs || !shmAccs->mapped){
      if (shmAccs){delete shmAccs;}
      shmAccs = new IPC::sharedPage(SHM_STATE_ACCS, 1024 * 1024, true); // max 1M of accesslogs cached
    }
    if (!shmAccs->mapped){
      FAIL_MSG("Could not open memory page for access logs buffer");
      return;
    }
    rlxAccs = new Util::RelAccX(shmAccs->mapped, false);
    if (!rlxAccs->isReady()){
      rlxAccs->addField("time", RAX_64UINT);
      rlxAccs->addField("session", RAX_32STRING);
      rlxAccs->addField("stream", RAX_128STRING);
      rlxAccs->addField("connector", RAX_32STRING);
      rlxAccs->addField("host", RAX_64STRING);
      rlxAccs->addField("duration", RAX_32UINT);
      rlxAccs->addField("up", RAX_64UINT);
      rlxAccs->addField("down", RAX_64UINT);
      rlxAccs->addField("tags", RAX_256STRING);
      rlxAccs->setReady();
    }
    maxAccsRecs = (1024 * 1024 - rlxAccs->getOffset()) / rlxAccs->getRSize();

    shmStrm = new IPC::sharedPage(SHM_STATE_STREAMS, 5*1024 * 1024, false, false); // max 5M of stream data
    if (!shmStrm || !shmStrm->mapped){
      if (shmStrm){delete shmStrm;}
      shmStrm = new IPC::sharedPage(SHM_STATE_STREAMS, 5*1024 * 1024, true); // max 5M of stream data
    }
    if (!shmStrm->mapped){
      FAIL_MSG("Could not open memory page for stream data");
      return;
    }
    rlxStrm = new Util::RelAccX(shmStrm->mapped, false);
    if (!rlxStrm->isReady()){
      rlxStrm->addField("stream", RAX_128STRING);
      rlxStrm->addField("status", RAX_UINT, 1);
      rlxStrm->addField("viewers", RAX_64UINT);
      rlxStrm->addField("inputs", RAX_64UINT);
      rlxStrm->addField("outputs", RAX_64UINT);
      rlxStrm->addField("unspecified", RAX_64UINT);
      rlxStrm->addField("tags", RAX_512STRING);
      rlxStrm->setReady();
    }
    rlxStrm->setRCount((1024 * 1024 - rlxStrm->getOffset()) / rlxStrm->getRSize());
  }

  void deinitStorage(bool leaveBehind) {
    std::lock_guard<std::mutex> guard(logMutex);
    if (!leaveBehind){
      if (rlxLogs){rlxLogs->setExit();}
      if (shmLogs){shmLogs->master = true;}
      if (rlxAccs){rlxAccs->setExit();}
      if (shmAccs){shmAccs->master = true;}
      if (rlxStrm){rlxStrm->setExit();}
      if (shmStrm){shmStrm->master = true;}
      wipeShmPages();
    }else{
      if (shmLogs){shmLogs->master = false;}
      if (shmAccs){shmAccs->master = false;}
      if (shmStrm){shmStrm->master = false;}
    }
    Util::RelAccX *tmp = rlxLogs;
    rlxLogs = 0;
    if (tmp){delete tmp;}
    if (shmLogs){delete shmLogs;}
    shmLogs = 0;
    tmp = rlxAccs;
    rlxAccs = 0;
    if (tmp){delete tmp;}
    if (shmAccs){delete shmAccs;}
    shmAccs = 0;
    tmp = rlxStrm;
    rlxStrm = 0;
    if (tmp){delete tmp;}
    if (shmStrm){delete shmStrm;}
    shmStrm = 0;
  }

  void handleMsg(int logFd) {
    Util::nameThread("logHandler");

    // Block most signals, so we don't catch them in this thread
    sigset_t x;
    sigemptyset(&x);
    sigaddset(&x, SIGUSR1);
    sigaddset(&x, SIGUSR2);
    sigaddset(&x, SIGHUP);
    sigaddset(&x, SIGINT);
    sigaddset(&x, SIGTERM);
    sigaddset(&x, SIGCONT);
    sigaddset(&x, SIGPIPE);
    pthread_sigmask(SIG_SETMASK, &x, 0);

    {
      const size_t logSize = 1024 * 1024;
      std::lock_guard<std::mutex> guard(logMutex);
      shmLogs = new IPC::sharedPage(SHM_STATE_LOGS, logSize, false, false);
      if (!shmLogs || !shmLogs->mapped) {
        if (shmLogs) { delete shmLogs; }
        shmLogs = new IPC::sharedPage(SHM_STATE_LOGS, logSize, true);
      }
      if (shmLogs->mapped) {
        rlxLogs = new Util::RelAccX(shmLogs->mapped, false);
      } else {
        FAIL_MSG("Could not open memory page for logs buffer; falling back to local memory");
        logMemory = malloc(logSize);
        if (!logMemory) {
          FAIL_MSG("Failed to allocate local log memory as well; aborting logging!");
          return;
        }
        rlxLogs = new Util::RelAccX((char *)logMemory, false);
      }
      if (rlxLogs->isReady()) {
        logsParsed = logCounter = rlxLogs->getEndPos();
      } else {
        rlxLogs->addField("time", RAX_64UINT);
        rlxLogs->addField("kind", RAX_32STRING);
        rlxLogs->addField("msg", RAX_512STRING);
        rlxLogs->addField("strm", RAX_128STRING);
        rlxLogs->addField("pid", RAX_64UINT);
        rlxLogs->addField("exe", RAX_32STRING);
        rlxLogs->addField("line", RAX_64STRING);
        rlxLogs->setReady();
      }
      logFTime = rlxLogs->getFieldData("time");
      logFKind = rlxLogs->getFieldData("kind");
      logFMsg = rlxLogs->getFieldData("msg");
      logFStrm = rlxLogs->getFieldData("strm");
      logFPID = rlxLogs->getFieldData("pid");
      logFExe = rlxLogs->getFieldData("exe");
      logFLine = rlxLogs->getFieldData("line");
      maxLogsRecs = (logSize - rlxLogs->getOffset()) / rlxLogs->getRSize();
      rlxLogs->setRCount(maxLogsRecs);
    }

    // Start parsing logs
    Util::logParser(logFd, 1, Controller::isColorized,
                    [](const std::string & kind, const std::string & message, const std::string & stream,
                       uint64_t progPid, const std::string & exe, const std::string & line) {
      uint64_t logTime = Util::epoch();
      if (rlxLogs && rlxLogs->isReady()) {
        if (rlxLogs->getPresent() >= maxLogsRecs) { rlxLogs->deleteRecords(1); }
        rlxLogs->setInt(logFTime, logTime, logCounter);
        rlxLogs->setString(logFKind, kind, logCounter);
        rlxLogs->setString(logFMsg, message, logCounter);
        rlxLogs->setString(logFStrm, stream, logCounter);
        rlxLogs->setInt(logFPID, progPid, logCounter);
        rlxLogs->setString(logFExe, exe, logCounter);
        rlxLogs->setString(logFLine, line, logCounter);
        rlxLogs->addRecords(1);
      }
      logCounter++;
    });
  }

  void getConfigAsWritten(JSON::Value & conf){
    std::set<std::string> skip;
    skip.insert("log");
    skip.insert("online");
    skip.insert("error");
    conf.assignFrom(Controller::Storage, skip);
  }

  /// Writes the current config to the location set in the configFile setting.
  /// On error, prints an error-level message and the config to stdout.
  void writeConfigToDisk(bool forceWrite){
    bool success = true;
    JSON::Value tmp;
    getConfigAsWritten(tmp);

    // We keep an extra copy temporarily, since we want to keep the "full" config around for comparisons
    JSON::Value mainConfig = tmp;

    if (Controller::Storage.isMember("config_split")){
      jsonForEach(Controller::Storage["config_split"], cs){
        if (cs->isString() && tmp.isMember(cs.key())){
          // Only (attempt to) write if there was a change since last write success
          if (!forceWrite && lastConfigWritten[cs.key()] == tmp[cs.key()]){
            if (cs.key() != "config_split"){mainConfig.removeMember(cs.key());}
            continue;
          }
          JSON::Value tmpConf = JSON::fromFile(cs->asStringRef());
          tmpConf[cs.key()] = tmp[cs.key()];
          // Attempt to write this section to the given file
          if (!Controller::WriteFile(cs->asStringRef(), tmpConf.toString())){
            success = false;
            // Only print the error + config data if this is new data since the last write attempt
            if (tmp[cs.key()] != lastConfigWriteAttempt[cs.key()]){
              ERROR_MSG("Error writing config.%s to %s", cs.key().c_str(), cs->asStringRef().c_str());
              std::cout << "**config." << cs.key() <<"**" << std::endl;
              std::cout << tmp[cs.key()].toString() << std::endl;
              std::cout << "**End config." << cs.key() << "**" << std::endl;
            }
          }else{
            // Log the successfully written data
            lastConfigWritten[cs.key()] = tmp[cs.key()];
          }
          // Remove this section from the to-be-written main config
          if (cs.key() != "config_split"){mainConfig.removeMember(cs.key());}
        }
      }
    }

    // Only (attempt to) write if there was a change since last write success
    if (forceWrite || lastConfigWritten[""] != mainConfig){
      // Attempt to write this section to the given file
      if (!Controller::WriteFile(Controller::conf.getString("configFile"), tmp.toString())){
        success = false;
        // Only print the error + config data if this is new data since the last write attempt
        if (tmp != lastConfigWriteAttempt){
          ERROR_MSG("Error writing config to %s", Controller::conf.getString("configFile").c_str());
          std::cout << "**Config**" << std::endl;
          std::cout << mainConfig.toString() << std::endl;
          std::cout << "**End config**" << std::endl;
        }
      }else{
        lastConfigWritten[""] = mainConfig;
      }
    }

    if (success){
      INFO_MSG("Wrote updated configuration to disk");
      lastConfigWrite = Util::epoch();
    }
    lastConfigWriteAttempt = tmp;
  }

  void readConfigFromDisk(){
    // reload config from config file
    Controller::Storage = JSON::fromFile(Controller::conf.getString("configFile"));

    if (Controller::Storage.isMember("config_split")){
      jsonForEach(Controller::Storage["config_split"], cs){
        if (cs->isString()){
          JSON::Value tmpConf = JSON::fromFile(cs->asStringRef());
          if (tmpConf.isMember(cs.key())){
            INFO_MSG("Loading '%s' section of config from file %s", cs.key().c_str(), cs->asStringRef().c_str());
            Controller::Storage[cs.key()] = tmpConf[cs.key()];
          }else{
            WARN_MSG("There is no '%s' section in file %s; skipping load", cs.key().c_str(), cs->asStringRef().c_str());
          }
        }
      }
    }
    // Set default delay before retry
    if (!Controller::Storage.isMember("push_settings")){
      Controller::Storage["push_settings"]["wait"] = 3;
      Controller::Storage["push_settings"]["maxspeed"] = 0;
    }
    if (Controller::conf.getOption("debug", true).size() > 1){
      Controller::Storage["config"]["debug"] = Controller::conf.getInteger("debug");
    }
    if (Controller::Storage.isMember("config") && Controller::Storage["config"].isMember("debug") &&
        Controller::Storage["config"]["debug"].isInt()){
      Util::printDebugLevel = Controller::Storage["config"]["debug"].asInt();
    }
    // check for port, interface and username in arguments
    // if they are not there, take them from config file, if there
    if (Controller::Storage["config"]["controller"]["port"]){
      Controller::conf.getOption("port", true)[0u] =
          Controller::Storage["config"]["controller"]["port"];
    }
    if (Controller::Storage["config"]["controller"]["interface"]){
      Controller::conf.getOption("interface", true)[0u] = Controller::Storage["config"]["controller"]["interface"];
    }
    if (Controller::Storage["config"]["controller"]["username"]){
      Controller::conf.getOption("username", true)[0u] = Controller::Storage["config"]["controller"]["username"];
    }
    if (Controller::Storage["config"]["controller"].isMember("prometheus")){
      if (Controller::Storage["config"]["controller"]["prometheus"]){
        Controller::Storage["config"]["prometheus"] =
            Controller::Storage["config"]["controller"]["prometheus"];
      }
      Controller::Storage["config"]["controller"].removeMember("prometheus");
    }
    if (Controller::Storage["config"]["prometheus"]){
      Controller::conf.getOption("prometheus", true)[0u] =
          Controller::Storage["config"]["prometheus"];
    }
    if (Controller::Storage["config"].isMember("accesslog")){
      Controller::conf.getOption("accesslog", true)[0u] = Controller::Storage["config"]["accesslog"];
    }
    Controller::Storage["config"]["prometheus"] = Controller::conf.getString("prometheus");
    Controller::Storage["config"]["accesslog"] = Controller::conf.getString("accesslog");
    Controller::normalizeTrustedProxies(Controller::Storage["config"]["trustedproxy"]);
    if (!Controller::Storage["config"]["sessionViewerMode"]){
      Controller::Storage["config"]["sessionViewerMode"] = SESS_BUNDLE_DEFAULT_VIEWER;
    }
    if (!Controller::Storage["config"]["sessionInputMode"]){
      Controller::Storage["config"]["sessionInputMode"] = SESS_BUNDLE_DEFAULT_OTHER;
    }
    if (!Controller::Storage["config"]["sessionOutputMode"]){
      Controller::Storage["config"]["sessionOutputMode"] = SESS_BUNDLE_DEFAULT_OTHER;
    }
    if (!Controller::Storage["config"]["sessionUnspecifiedMode"]){
      Controller::Storage["config"]["sessionUnspecifiedMode"] = 0;
    }
    if (!Controller::Storage["config"]["sessionStreamInfoMode"]){
      Controller::Storage["config"]["sessionStreamInfoMode"] = SESS_DEFAULT_STREAM_INFO_MODE;
    }
    if (!Controller::Storage["config"].isMember("tknMode")){
      Controller::Storage["config"]["tknMode"] = SESS_TKN_DEFAULT_MODE;
    }
    if (!Controller::Storage.isMember("bandwidth") || !Controller::Storage["bandwidth"].isMember("exceptions") || !Controller::Storage["bandwidth"]["exceptions"].size()){
      LOG_MSG("CONF", "Adding default bandwidth exception ranges (local networks) because nothing is configured");
      Controller::Storage["bandwidth"]["exceptions"].append("::1");
      Controller::Storage["bandwidth"]["exceptions"].append("127.0.0.0/8");
      Controller::Storage["bandwidth"]["exceptions"].append("10.0.0.0/8");
      Controller::Storage["bandwidth"]["exceptions"].append("192.168.0.0/16");
      Controller::Storage["bandwidth"]["exceptions"].append("172.16.0.0/12");
    }
    Controller::prometheus = Controller::Storage["config"]["prometheus"].asStringRef();
    Controller::accesslog = Controller::Storage["config"]["accesslog"].asStringRef();

    // Upgrade old configurations
    {
      bool foundCMAF = false;
      bool edit = false;
      JSON::Value newVal;
      jsonForEach(Controller::Storage["config"]["protocols"], it){
        if ((*it)["connector"].asStringRef() == "HSS"){
          edit = true;
          continue;
        }
        if ((*it)["connector"].asStringRef() == "DASH"){
          edit = true;
          continue;
        }

        if ((*it)["connector"].asStringRef() == "SRT"){
          edit = true;
          JSON::Value newSubRip = *it;
          newSubRip["connector"] = "SubRip";
          newVal.append(newSubRip);
          continue;
        }

        if ((*it)["connector"].asStringRef() == "CMAF"){foundCMAF = true;}
        newVal.append(*it);
      }
      if (edit && !foundCMAF){newVal.append(JSON::fromString("{\"connector\":\"CMAF\"}"));}
      if (edit){
        Controller::Storage["config"]["protocols"] = newVal;
        LOG_MSG("CONF", "Translated protocols to new versions");
      }
    }
    // Update old variabels format to new variables format
    if (Controller::Storage.isMember("variables") && Controller::Storage["variables"].isArray()) {
      JSON::Value old = Controller::Storage["variables"];
      Controller::Storage["variables"].null();
      jsonForEach (old, it) {
        JSON::Value & i = *it;
        if (i.size() != 6) { continue; }
        std::string name = i[0u];
        JSON::Value & V = Controller::Storage["variables"][name];
        if (i[1u]) { V["target"] = i[1u].asString(); }
        if (i[2u]) { V["interval"] = i[2u].asDouble(); }
        if (i[3u]) { V["lastunix"] = i[3u].asInt(); }
        if (i[4u]) { V["value"] = i[4u].asString(); }
        if (i[5u]) { V["waitTime"] = i[5u].asDouble(); }
      }
      LOG_MSG("CONF", "Translated custom variables to object-based format");
    }
    // Convert autopushes array into auto_push object
    // Only if auto_push does not exist yet and autopushes is non-empty
    if (!Controller::Storage.isMember("auto_push") && Controller::Storage.isMember("autopushes") && Controller::Storage["autopushes"].isArray() && Controller::Storage["autopushes"].size()){
      JSON::Value & aPush = Controller::Storage["auto_push"];
      jsonForEachConst(Controller::Storage["autopushes"], it){
        if (it->size() < 2){continue;} // Invalid entries
        JSON::Value tmp = Controller::makePushObject(*it);
        if (tmp.isNull()){continue;} // Invalid entries
        aPush[Secure::md5(tmp.toString())] = tmp;
      }
      LOG_MSG("CONF", "Converted old-style autopushes to new-style auto_push");
    }

    Controller::lastConfigChange = Controller::lastConfigWrite = Util::epoch();
    Controller::lastConfigWriteAttempt.null();
    getConfigAsWritten(Controller::lastConfigWriteAttempt);
    lastConfigSeen = lastConfigWriteAttempt;
  }

  void writeCapabilities(){
    std::string temp = capabilities.toPacked();
    static IPC::sharedPage mistCapaOut(SHM_CAPA, temp.size() + 100, false, false);
    if (mistCapaOut){
      Util::RelAccX tmpA(mistCapaOut.mapped, false);
      if (tmpA.isReady()){tmpA.setReload();}
      mistCapaOut.master = true;
      mistCapaOut.close();
    }
    mistCapaOut.init(SHM_CAPA, temp.size() + 100, true, false);
    addShmPage(SHM_CAPA);
    if (!mistCapaOut.mapped){
      FAIL_MSG("Could not open capabilities config for writing! Is shared memory enabled on your "
               "system?");
      return;
    }
    Util::RelAccX A(mistCapaOut.mapped, false);
    A.addField("dtsc_data", RAX_DTSC, temp.size());
    // write config
    memcpy(A.getPointer("dtsc_data"), temp.data(), temp.size());
    A.setRCount(1);
    A.setEndPos(1);
    A.setReady();
    mistCapaOut.master = false;
  }

  void writeProtocols(){
    static std::string proxy_written;
    std::string tmpProxy;
    if (Storage["config"]["trustedproxy"].isArray()){
      jsonForEachConst(Storage["config"]["trustedproxy"], jit){
        if (tmpProxy.size()){
          tmpProxy += " " + jit->asString();
        }else{
          tmpProxy = jit->asString();
        }
      }
    }else{
      tmpProxy = Storage["config"]["trustedproxy"].asString();
    }
    if (proxy_written != tmpProxy){
      proxy_written = tmpProxy;
      static IPC::sharedPage mistProxOut(SHM_PROXY, proxy_written.size() + 100, true, false);
      addShmPage(SHM_PROXY);
      mistProxOut.close();
      mistProxOut.init(SHM_PROXY, proxy_written.size() + 100, false, false);
      if (mistProxOut){
        Util::RelAccX tmpA(mistProxOut.mapped, false);
        if (tmpA.isReady()){tmpA.setReload();}
        mistProxOut.master = true;
        mistProxOut.close();
      }
      mistProxOut.init(SHM_PROXY, proxy_written.size() + 100, true, false);
      if (!mistProxOut.mapped){
        FAIL_MSG("Could not open trusted proxy config for writing! Is shared memory enabled on "
                 "your system?");
        return;
      }else{
        Util::RelAccX A(mistProxOut.mapped, false);
        A.addField("proxy_data", RAX_STRING, proxy_written.size());
        // write config
        memcpy(A.getPointer("proxy_data"), proxy_written.data(), proxy_written.size());
        A.setRCount(1);
        A.setEndPos(1);
        A.setReady();
      }
    }
    static JSON::Value proto_written;
    std::set<std::string> skip;
    skip.insert("online");
    skip.insert("error");
    if (Storage["config"]["protocols"].compareExcept(proto_written, skip)){return;}
    proto_written.assignFrom(Storage["config"]["protocols"], skip);
    std::string temp = proto_written.toPacked();
    static IPC::sharedPage mistProtoOut(SHM_PROTO, temp.size() + 100, false, false);
    if (mistProtoOut){
      Util::RelAccX tmpA(mistProtoOut.mapped, false);
      if (tmpA.isReady()){tmpA.setReload();}
      mistProtoOut.master = true;
      mistProtoOut.close();
    }
    mistProtoOut.init(SHM_PROTO, temp.size() + 100, true, false);
    addShmPage(SHM_PROTO);
    if (!mistProtoOut.mapped){
      FAIL_MSG(
          "Could not open protocol config for writing! Is shared memory enabled on your system?");
      return;
    }
    // write config
    {
      Util::RelAccX A(mistProtoOut.mapped, false);
      A.addField("dtsc_data", RAX_DTSC, temp.size());
      // write config
      memcpy(A.getPointer("dtsc_data"), temp.data(), temp.size());
      A.setRCount(1);
      A.setEndPos(1);
      A.setReady();
    }
    mistProtoOut.master = false;
  }

  void writeStream(const std::string &sName, const JSON::Value &sConf){
    static std::map<std::string, JSON::Value> writtenStrms;
    static std::set<std::string> skip;
    if (!skip.size()){
      skip.insert("online");
      skip.insert("error");
      skip.insert("x-LSP-name");
    }
    if (sConf.isNull()){
      writtenStrms.erase(sName);
      IPC::sharedPage P;
      char tmpBuf[NAME_BUFFER_SIZE];
      snprintf(tmpBuf, NAME_BUFFER_SIZE, SHM_STREAM_CONF, sName.c_str());
      P.init(tmpBuf, 0, false, false);
      if (P) { P.master = true; }
      return;
    }
    if (!writtenStrms.count(sName) || !writtenStrms[sName].compareExcept(sConf, skip)){
      writtenStrms[sName].assignFrom(sConf, skip);
      std::string temp = writtenStrms[sName].toPacked();
      char tmpBuf[NAME_BUFFER_SIZE];
      snprintf(tmpBuf, NAME_BUFFER_SIZE, SHM_STREAM_CONF, sName.c_str());
      IPC::sharedPage P(tmpBuf, temp.size() + 100, false, false);
      if (P){
        Util::RelAccX tmpA(P.mapped, false);
        if (tmpA.isReady()){tmpA.setReload();}
        P.master = true;
        P.close();
      }
      P.init(tmpBuf, temp.size() + 100, true, false);
      if (!P){
        writtenStrms.erase(sName);
        return;
      }
      addShmPage(tmpBuf);
      P.master = false;
      Util::RelAccX A(P.mapped, false);
      A.addField("dtsc_data", RAX_DTSC, temp.size());
      // write config
      memcpy(A.getPointer("dtsc_data"), temp.data(), temp.size());
      A.setRCount(1);
      A.setEndPos(1);
      A.setReady();
    }
  }

  /// Writes the current config to shared memory to be used in other processes
  /// \triggers
  /// The `"SYSTEM_START"` trigger is global, and is ran as soon as the server configuration is first stable. It has no payload. If cancelled,
  /// the system immediately shuts down again.
  /// \n
  /// The `"SYSTEM_CONFIG"` trigger is global, and is ran every time the server configuration is updated. Its payload is the new configuration in
  /// JSON format. This trigger cannot be cancelled.
  void writeConfig(){
    writeProtocols();
    jsonForEach(Storage["streams"], it){
      it->removeNullMembers();
      writeStream(it.key(), *it);
    }

    {
      // Global configuration options, if any
      IPC::sharedPage globCfg;
      globCfg.init(SHM_GLOBAL_CONF, 4096, false, false);
      if (!globCfg.mapped){globCfg.init(SHM_GLOBAL_CONF, 4096, true, false);}
      if (globCfg.mapped){
        Util::RelAccX globAccX(globCfg.mapped, false);

        // if fields missing, recreate the page
        if (globAccX.isReady()){
          if(globAccX.getFieldAccX("systemBoot") && globAccX.getInt("systemBoot")){
            systemBoot = globAccX.getInt("systemBoot");
          }
          if(!globAccX.getFieldAccX("defaultStream")
             || !globAccX.getFieldAccX("systemBoot")
             || !globAccX.getFieldAccX("sessionViewerMode")
             || !globAccX.getFieldAccX("sessionInputMode")
             || !globAccX.getFieldAccX("sessionOutputMode")
             || !globAccX.getFieldAccX("sessionUnspecifiedMode")
             || !globAccX.getFieldAccX("sessionStreamInfoMode")
             || !globAccX.getFieldAccX("tknMode")
             || !globAccX.getFieldAccX("udpApi")
             || !globAccX.getFieldAccX("iid")
             || !globAccX.getFieldAccX("hrn")
             ){
            globAccX.setReload();
            globCfg.master = true;
            globCfg.close();
            globCfg.init(SHM_GLOBAL_CONF, 4096, true, false);
            globAccX = Util::RelAccX(globCfg.mapped, false);
          }
        }
        if (!globAccX.isReady()){
          globAccX.addField("defaultStream", RAX_128STRING);
          globAccX.addField("systemBoot", RAX_64UINT);
          globAccX.addField("sessionViewerMode", RAX_64UINT);
          globAccX.addField("sessionInputMode", RAX_64UINT);
          globAccX.addField("sessionOutputMode", RAX_64UINT);
          globAccX.addField("sessionUnspecifiedMode", RAX_64UINT);
          globAccX.addField("sessionStreamInfoMode", RAX_64UINT);
          globAccX.addField("tknMode", RAX_64UINT);
          globAccX.addField("udpApi", RAX_128STRING);
          globAccX.addField("iid", RAX_64STRING);
          globAccX.addField("hrn", RAX_128STRING);
          globAccX.setRCount(1);
          globAccX.setEndPos(1);
          globAccX.setReady();
        }
        globAccX.setString("defaultStream", Storage["config"]["defaultStream"].asStringRef());
        globAccX.setInt("sessionViewerMode", Storage["config"]["sessionViewerMode"].asInt());
        globAccX.setInt("sessionInputMode", Storage["config"]["sessionInputMode"].asInt());
        globAccX.setInt("sessionOutputMode", Storage["config"]["sessionOutputMode"].asInt());
        globAccX.setInt("sessionUnspecifiedMode", Storage["config"]["sessionUnspecifiedMode"].asInt());
        globAccX.setInt("sessionStreamInfoMode", Storage["config"]["sessionStreamInfoMode"].asInt());
        globAccX.setInt("tknMode", Storage["config"]["tknMode"].asInt());
        globAccX.setString("udpApi", udpApiBindAddr);
        globAccX.setInt("systemBoot", systemBoot);
        globAccX.setString("iid", instanceId);
        globAccX.setString("hrn", Storage["config"]["serverid"].asString());
        globCfg.master = false; // leave the page after closing
        addShmPage(SHM_GLOBAL_CONF);
      }
    }

    // Write streamkeys to shared memory
    {
      static IPC::sharedPage streamkeyPage(SHM_STREAMKEYS, 1 * 1024 * 1024, false, false);
      if (streamkeyPage) {
        Util::RelAccX tmpA(streamkeyPage.mapped, false);
        if (tmpA.isReady()) { tmpA.setReload(); }
        streamkeyPage.master = true;
        streamkeyPage.close();
      }
      streamkeyPage.init(SHM_STREAMKEYS, 1 * 1024 * 1024, true, false);
      Util::RelAccX tPage(streamkeyPage.mapped, false);
      tPage.addField("key", RAX_256STRING);
      tPage.addField("stream", RAX_128STRING);
      Util::RelAccXFieldData keyField = tPage.getFieldData("key");
      Util::RelAccXFieldData streamField = tPage.getFieldData("stream");
      uint32_t i = 0;
      uint32_t max = (streamkeyPage.len - tPage.getOffset()) / tPage.getRSize();
      jsonForEach (Controller::Storage["streamkeys"], keyIt) {
        if (i >= max) {
          ERROR_MSG("Not all streamkeys fit on the memory page!");
          break;
        }
        tPage.setString(keyField, keyIt.key(), i);
        tPage.setString(streamField, keyIt->asStringRef(), i);
        ++i;
      }
      tPage.setRCount(std::min(i, max));
      tPage.setEndPos(std::min(i, max));
      tPage.setReady();
    }

    static std::map<std::string, IPC::sharedPage> pageForType; // should contain one page for every trigger type
    static JSON::Value writtenTrigs;
    char tmpBuf[NAME_BUFFER_SIZE];

    if (writtenTrigs != Storage["config"]["triggers"]){
      writtenTrigs = Storage["config"]["triggers"];
      // for all shm pages that hold triggers
      pageForType.clear();

      if (Storage["config"]["triggers"].size()){
        jsonForEach(Storage["config"]["triggers"], it){
          snprintf(tmpBuf, NAME_BUFFER_SIZE, SHM_TRIGGER, (it.key()).c_str());
          pageForType[it.key()].init(tmpBuf, 32 * 1024, false, false);
          if (pageForType[it.key()]){
            Util::RelAccX tmpA(pageForType[it.key()].mapped, false);
            if (tmpA.isReady()){tmpA.setReload();}
            pageForType[it.key()].master = true;
            pageForType[it.key()].close();
          }
          pageForType[it.key()].init(tmpBuf, 32 * 1024, true, false);
          Util::RelAccX tPage(pageForType[it.key()].mapped, false);
          tPage.addField("url", RAX_128STRING);
          tPage.addField("sync", RAX_UINT);
          tPage.addField("streams", RAX_256RAW);
          tPage.addField("params", RAX_128STRING);
          tPage.addField("default", RAX_128STRING);
          tPage.setReady();
          uint32_t i = 0;
          uint32_t max = (32 * 1024 - tPage.getOffset()) / tPage.getRSize();

          // write data to page
          jsonForEach(*it, triggIt){
            if (i >= max){
              ERROR_MSG("Not all %s triggers fit on the memory page!", (it.key()).c_str());
              break;
            }

            if (triggIt->isArray()){
              tPage.setString("url", (*triggIt)[0u].asStringRef(), i);
              tPage.setInt("sync", ((*triggIt)[1u].asBool() ? 1 : 0), i);
              char *strmP = tPage.getPointer("streams", i);
              if (strmP){
                ((unsigned int *)strmP)[0] = 0; // reset first 4 bytes of stream list pointer
                if ((triggIt->size() >= 3) && (*triggIt)[2u].size()){
                  std::string namesArray;
                  jsonForEach((*triggIt)[2u], shIt){
                    ((unsigned int *)tmpBuf)[0] = shIt->asString().size();
                    namesArray.append(tmpBuf, 4);
                    namesArray.append(shIt->asString());
                  }
                  if (namesArray.size()){
                    memcpy(strmP, namesArray.data(), std::min(namesArray.size(), (size_t)256));
                  }
                }
              }
            }

            if (triggIt->isObject()) {
              tPage.setString("url", (*triggIt)["handler"].asStringRef(), i);
              tPage.setInt("sync", ((*triggIt)["sync"].asBool() ? 1 : 0), i);
              char *strmP = tPage.getPointer("streams", i);
              if (strmP){
                ((unsigned int *)strmP)[0] = 0; // reset first 4 bytes of stream list pointer
                if ((triggIt->isMember("streams")) && (*triggIt)["streams"].size()){
                  std::string namesArray;
                  jsonForEach((*triggIt)["streams"], shIt){
                    ((unsigned int *)tmpBuf)[0] = shIt->asString().size();
                    namesArray.append(tmpBuf, 4);
                    namesArray.append(shIt->asString());
                  }
                  if (namesArray.size()){
                    namesArray.append("\000\000\000\000", 4);
                    memcpy(strmP, namesArray.data(), std::min(namesArray.size(), (size_t)256));
                  }
                }
              }
              if (triggIt->isMember("params") && !(*triggIt)["params"].isNull()){
                tPage.setString("params", (*triggIt)["params"].asStringRef(), i);
              }else{
                tPage.setString("params", "", i);
              }
              if (triggIt->isMember("default") && !(*triggIt)["default"].isNull()){
                tPage.setString("default", (*triggIt)["default"].asStringRef(), i);
              }else{
                tPage.setString("default", "", i);
              }
            }

            ++i;
          }
          tPage.setRCount(std::min(i, max));
          tPage.setEndPos(std::min(i, max));
        }
      }
    }

    static bool serverStartTriggered;
    if (!serverStartTriggered){
      if (Triggers::shouldTrigger("SYSTEM_START")){
        if (!Triggers::doTrigger("SYSTEM_START", PACKAGE_VERSION)){
          INFO_MSG("Shutting down because of SYSTEM_START trigger response");
          conf.is_active = false;
        }
      }
      serverStartTriggered = true;
    }
    /*LTS-END*/

    { // Scope for writing JWKs to their own shared memory page
      jwkUriCheck();

      // First retrieve all storage entries that are JWKs, for URIs check if they are still valid
      std::set<std::string> uris;
      jsonForEachConst (Storage["jwks"], it) {
        const std::string uri = (it->isArray() && it->size()) ? (*it)[0u].asStringRef() : it->asStringRef();
        // There is some URI in JWK resolved that is maybe not in storage anymore, store in set to later ignore
        if (uri.find("://") != std::string::npos) {
          uris.insert(uri);
          continue;
        }
        JWT::Key jwk = JWT::Key((it->isArray()) ? (*it)[0u] : *it, (it->isArray()) ? (*it)[1u] : JSON::EMPTY);
        jwkResolved["default"].insert(jwk);
      }

      // Remove any nulled elements from the keystore and start setting up the shared page
      IPC::sharedPage keyCfg;
      keyCfg.init(SHM_JWK, 8 * 1024 * 1024, false, false);

      if (keyCfg) {
        Util::RelAccX tmpAccX(keyCfg.mapped, false);
        if (tmpAccX.isReady()) { tmpAccX.setReload(); }
        keyCfg.master = true;
        keyCfg.close();
      }

      keyCfg.init(SHM_JWK, 8 * 1024 * 1024, true);
      Util::RelAccX keyAccX(keyCfg.mapped, false);
      keyAccX.addField("kid", RAX_64STRING);
      keyAccX.addField("kty", RAX_32STRING);
      keyAccX.addField("key", RAX_STRING, 8192);
      keyAccX.addField("perms", RAX_UINT, 1);
      keyAccX.addField("stream", RAX_128STRING);

      // For faster writing to shared memory
      Util::RelAccXFieldData kidFd = keyAccX.getFieldData("kid");
      Util::RelAccXFieldData ktyFd = keyAccX.getFieldData("kty");
      Util::RelAccXFieldData keyFd = keyAccX.getFieldData("key");
      Util::RelAccXFieldData prmFd = keyAccX.getFieldData("perms");
      Util::RelAccXFieldData strFd = keyAccX.getFieldData("stream");

      uint32_t i = 0;
      uint32_t max = (8 * 1024 * 1024 - keyAccX.getOffset()) / keyAccX.getRSize();

      std::map<std::string, uint32_t> uniques;

      // For each key in the storage write it to shared memory if it was present in the config
      for (const auto & jwkUriPair : jwkResolved) {
        // Erase the keys if the config does not have the URI anymore
        if (!uris.count(jwkUriPair.first) && jwkUriPair.first != "default") {
          uriExpiresAt.erase(jwkUriPair.first);
          uriPerms.erase(jwkUriPair.first);
          continue;
        }
        for (const auto & jwk : jwkUriPair.second) {
          std::string raw = jwk.toString(false);
          // Duplicate prevention based on full key matching
          uint32_t j = i;
          if (uniques.count(raw))
            j = uniques[raw];
          else
            uniques.insert({raw, i++}); // inc. after insert

          // Actually set the data in the memory page
          keyAccX.setString(ktyFd, jwk["kty"], j);
          keyAccX.setString(keyFd, raw, j);
          keyAccX.setString(strFd, jwk.getStream(), j);
          keyAccX.setString(kidFd, jwk["kid"], j);
          keyAccX.setInt(prmFd, jwk.getPerms(), j);
        }
      }

      if (i >= max) ERROR_MSG("Not all JWKs fit on the memory page!");
      keyAccX.setRCount(std::min(i, max));
      keyAccX.setEndPos(std::min(i, max));
      keyAccX.setReady();
      keyCfg.master = false;
    }
  }

  void addShmPage(const std::string & page){
    std::lock_guard<std::mutex> guard(shmListMutex);
    shmList.insert(page);
  }

  void wipeShmPages(){
    std::lock_guard<std::mutex> guard(shmListMutex);
    if (!shmList.size()){return;}
    std::set<std::string>::iterator it;
    for (it = shmList.begin(); it != shmList.end(); ++it){
      IPC::sharedPage page(*it, 0, false, false);
      if (page){page.master = true;}
    }
    shmList.clear();
  }

}// namespace Controller
