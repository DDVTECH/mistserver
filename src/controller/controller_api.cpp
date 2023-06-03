#include "controller_api.h"
#include "controller_capabilities.h"
#include "controller_connectors.h"
#include "controller_statistics.h"
#include "controller_storage.h"
#include "controller_streams.h"
#include "controller_external_writers.h"
#include <dirent.h> //for browse API call
#include <fstream>
#include <mist/auth.h>
#include <mist/bitfields.h>
#include <mist/config.h>
#include <mist/defines.h>
#include <mist/http_parser.h>
#include <mist/procs.h>
#include <mist/stream.h>
#include <mist/timing.h>
#include <mist/url.h>
#include <sys/stat.h> //for browse API call
/*LTS-START*/
#include "controller_limits.h"
#include "controller_push.h"
#include "controller_variables.h"
#include "controller_updater.h"
/*LTS-END*/

/// Returns the challenge string for authentication, given the socket connection.
std::string getChallenge(Socket::Connection &conn){
  time_t Time = time(0);
  tm tmptime;
  tm *TimeInfo = localtime_r(&Time, &tmptime);
  std::stringstream Date;
  Date << TimeInfo->tm_mday << "-" << TimeInfo->tm_mon << "-" << TimeInfo->tm_year + 1900;
  return Secure::md5(Date.str().c_str() + conn.getHost());
}

/// Executes a single Playlist-based API command. Recurses if necessary.
static void executePlsCommand(JSON::Value &cmd, std::deque<std::string> &lines){
  if (!cmd.isArray() || !cmd.size()){
    FAIL_MSG("Not a valid playlist API command: %s", cmd.toString().c_str());
    return;
  }
  if (cmd[0u].isArray()){
    jsonForEach(cmd, it){executePlsCommand(*it, lines);}
    return;
  }
  if (!cmd[0u].isString()){
    FAIL_MSG("Not a valid playlist API command: %s", cmd.toString().c_str());
    return;
  }
  if (cmd[0u].asStringRef() == "append" && cmd.size() == 2 && cmd[1u].isString()){
    lines.push_back(cmd[1u].asStringRef());
    return;
  }
  if (cmd[0u].asStringRef() == "clear" && cmd.size() == 1){
    lines.clear();
    return;
  }
  if (cmd[0u].asStringRef() == "remove" && cmd.size() == 2 && cmd[1u].isString()){
    const std::string &toRemove = cmd[1u].asStringRef();
    for (std::deque<std::string>::iterator it = lines.begin(); it != lines.end(); ++it){
      if ((*it) == toRemove){(*it) = "";}
    }
    return;
  }
  if (cmd[0u].asStringRef() == "line" && cmd.size() == 3 && cmd[1u].isInt() && cmd[2u].isString()){
    if (cmd[1u].asInt() >= lines.size()){
      FAIL_MSG("Line number %d does not exist in playlist - cannot modify line", (int)cmd[1u].asInt());
      return;
    }
    lines[cmd[1u].asInt()] = cmd[2u].asStringRef();
    return;
  }
  if (cmd[0u].asStringRef() == "replace" && cmd.size() == 3 && cmd[1u].isString() && cmd[2u].isString()){
    const std::string &toReplace = cmd[1u].asStringRef();
    for (std::deque<std::string>::iterator it = lines.begin(); it != lines.end(); ++it){
      if ((*it) == toReplace){(*it) = cmd[2u].asStringRef();}
    }
    return;
  }
  FAIL_MSG("Not a valid playlist API command: %s", cmd.toString().c_str());
}

///\brief Checks an authorization request for a given user.
///\param Request The request to be parsed.
///\param Response The location to store the generated response.
///\param conn The user to be checked for authorization.
///\return True on successfull authorization, false otherwise.
///
/// \api
/// To login, an `"authorize"` request must be sent. Since HTTP does not use persistent connections,
/// you are required to re-sent authentication with every API request made. To prevent plaintext sending
/// of the password, a random challenge string is sent first, and then the password is hashed together with this challenge string to create a one-time-use string to login with.
/// If the user is not authorized, this request is the only request the server will respond to until properly authorized.
/// `"authorize"` requests take the form of:
/// ~~~~~~~~~~~~~~~{.js}
///{
///   //username to login as
///   "username": "test",
///   //hash of password to login with. Send empty value when no challenge for the hash is known yet.
///   //When the challenge is known, the value to be used here can be calculated as follows:
///   //   MD5( MD5("secret") + challenge)
///   //Where "secret" is the plaintext password.
///   "password": ""
///}
/// ~~~~~~~~~~~~~~~
/// and are responded to as:
/// ~~~~~~~~~~~~~~~{.js}
///{
///   //current login status. Either "OK", "CHALL", "NOACC" or "ACC_MADE".
///   "status": "CHALL",
///   //Random value to be used in hashing the password.
///   "challenge": "abcdef1234567890"
///}
/// ~~~~~~~~~~~~~~~
/// The challenge string is sent for all statuses, except `"NOACC"`, where it is left out.
/// A status of `"OK"` means you are currently logged in and have access to all other API requests.
/// A status of `"CHALL"` means you are not logged in, and a challenge has been provided to login with.
/// A status of `"NOACC"` means there are no valid accounts to login with. In this case - and ONLY
/// in this case - it is possible to create a initial login through the API itself. To do so, send a request as follows:
/// ~~~~~~~~~~~~~~~{.js}
///{
///   //username to create, as plain text
///   "new_username": "test",
///   //password to set, as plain text
///   "new_password": "secret"
///}
/// ~~~~~~~~~~~~~~~
/// Please note that this is NOT secure. At all. Never use this mechanism over a public network!
/// A status of `"ACC_MADE"` indicates the account was created successfully and can now be used to login as normal.
bool Controller::authorize(JSON::Value &Request, JSON::Value &Response, Socket::Connection &conn){
  std::string Challenge = getChallenge(conn);
  std::string retval;
  if (Request.isMember("authorize") && Request["authorize"]["username"].asString() != ""){
    std::string UserID = Request["authorize"]["username"];
    if (Storage["account"].isMember(UserID)){
      if (Secure::md5(Storage["account"][UserID]["password"].asString() + Challenge) ==
          Request["authorize"]["password"].asString()){
        Response["authorize"]["status"] = "OK";
        return true;
      }
    }
    if (Request["authorize"]["password"].asString() != ""){
      Log("AUTH", "Failed login attempt " + UserID + " from " + conn.getHost());
    }
  }
  Response["authorize"]["status"] = "CHALL";
  Response["authorize"]["challenge"] = Challenge;
  // the following is used to add the first account through the LSP
  if (!Storage["account"]){
    Response["authorize"]["status"] = "NOACC";
    if (Request["authorize"]["new_username"] && Request["authorize"]["new_password"]){
      // create account
      Controller::Log("CONF", "Created account " + Request["authorize"]["new_username"].asString() + " through API");
      Controller::Storage["account"][Request["authorize"]["new_username"].asString()]["password"] =
          Secure::md5(Request["authorize"]["new_password"].asString());
      Response["authorize"]["status"] = "ACC_MADE";
    }else{
      Response["authorize"].removeMember("challenge");
    }
  }
  return false;
}// Authorize

class streamStat{
public:
  streamStat(){
    status = 0;
    viewers = 0;
    inputs = 0;
    outputs = 0;
  }
  streamStat(const Util::RelAccX &rlx, uint64_t entry){
    status = rlx.getInt("status", entry);
    viewers = rlx.getInt("viewers", entry);
    inputs = rlx.getInt("inputs", entry);
    outputs = rlx.getInt("outputs", entry);
  }
  bool operator==(const streamStat &b) const{
    return (status == b.status && viewers == b.viewers && inputs == b.inputs && outputs == b.outputs);
  }
  bool operator!=(const streamStat &b) const{return !(*this == b);}
  uint8_t status;
  uint64_t viewers;
  uint64_t inputs;
  uint64_t outputs;
};

void Controller::handleWebSocket(HTTP::Parser &H, Socket::Connection &C){
  std::string logs = H.GetVar("logs");
  std::string accs = H.GetVar("accs");
  bool doStreams = H.GetVar("streams").size();
  HTTP::Parser req = H;
  H.Clean();
  HTTP::Websocket W(C, req, H);
  if (!W){return;}

  IPC::sharedPage shmLogs(SHM_STATE_LOGS, 1024 * 1024);
  IPC::sharedPage shmAccs(SHM_STATE_ACCS, 1024 * 1024);
  IPC::sharedPage shmStreams(SHM_STATE_STREAMS, 1024 * 1024);
  Util::RelAccX rlxStreams(shmStreams.mapped);
  Util::RelAccX rlxLog(shmLogs.mapped);
  Util::RelAccX rlxAccs(shmAccs.mapped);
  if (!rlxStreams.isReady()){doStreams = false;}
  uint64_t logPos = 0;
  bool doLog = false;
  uint64_t accsPos = 0;
  bool doAccs = false;
  if (logs.size() && rlxLog.isReady()){
    doLog = true;
    logPos = rlxLog.getEndPos();
    if (logs.substr(0, 6) == "since:"){
      uint64_t startLogs = JSON::Value(logs.substr(6)).asInt();
      logPos = rlxLog.getDeleted();
      while (logPos < rlxLog.getEndPos() && rlxLog.getInt("time", logPos) < startLogs){++logPos;}
    }else{
      uint64_t numLogs = JSON::Value(logs).asInt();
      if (logPos <= numLogs){
        logPos = rlxLog.getDeleted();
      }else{
        logPos -= numLogs;
      }
    }
  }
  if (accs.size() && rlxAccs.isReady()){
    doAccs = true;
    accsPos = rlxAccs.getEndPos();
    if (accs.substr(0, 6) == "since:"){
      uint64_t startAccs = JSON::Value(accs.substr(6)).asInt();
      accsPos = rlxAccs.getDeleted();
      while (accsPos < rlxAccs.getEndPos() && rlxAccs.getInt("time", accsPos) < startAccs){
        ++accsPos;
      }
    }else{
      uint64_t numAccs = JSON::Value(accs).asInt();
      if (accsPos <= numAccs){
        accsPos = rlxAccs.getDeleted();
      }else{
        accsPos -= numAccs;
      }
    }
  }
  std::map<std::string, streamStat> lastStrmStat;
  std::set<std::string> strmRemove;
  while (W){
    bool sent = false;
    while (doLog && rlxLog.getEndPos() > logPos){
      sent = true;
      JSON::Value tmp;
      tmp[0u] = "log";
      tmp[1u].append(rlxLog.getInt("time", logPos));
      tmp[1u].append(rlxLog.getPointer("kind", logPos));
      tmp[1u].append(rlxLog.getPointer("msg", logPos));
      tmp[1u].append(rlxLog.getPointer("strm", logPos));
      W.sendFrame(tmp.toString());
      logPos++;
    }
    while (doAccs && rlxAccs.getEndPos() > accsPos){
      sent = true;
      JSON::Value tmp;
      tmp[0u] = "access";
      tmp[1u].append(rlxAccs.getInt("time", accsPos));
      tmp[1u].append(rlxAccs.getPointer("session", accsPos));
      tmp[1u].append(rlxAccs.getPointer("stream", accsPos));
      tmp[1u].append(rlxAccs.getPointer("connector", accsPos));
      tmp[1u].append(rlxAccs.getPointer("host", accsPos));
      tmp[1u].append(rlxAccs.getInt("duration", accsPos));
      tmp[1u].append(rlxAccs.getInt("up", accsPos));
      tmp[1u].append(rlxAccs.getInt("down", accsPos));
      tmp[1u].append(rlxAccs.getPointer("tags", accsPos));
      W.sendFrame(tmp.toString());
      accsPos++;
    }
    if (doStreams){
      for (std::map<std::string, streamStat>::iterator it = lastStrmStat.begin();
           it != lastStrmStat.end(); ++it){
        strmRemove.insert(it->first);
      }
      uint64_t startPos = rlxStreams.getDeleted();
      uint64_t endPos = rlxStreams.getEndPos();
      for (uint64_t cPos = startPos; cPos < endPos; ++cPos){
        std::string strm = rlxStreams.getPointer("stream", cPos);
        strmRemove.erase(strm);
        streamStat tmpStat(rlxStreams, cPos);
        if (lastStrmStat[strm] != tmpStat){
          lastStrmStat[strm] = tmpStat;
          sent = true;
          JSON::Value tmp;
          tmp[0u] = "stream";
          tmp[1u].append(strm);
          tmp[1u].append(tmpStat.status);
          tmp[1u].append(tmpStat.viewers);
          tmp[1u].append(tmpStat.inputs);
          tmp[1u].append(tmpStat.outputs);
          W.sendFrame(tmp.toString());
        }
      }
      while (strmRemove.size()){
        std::string strm = *strmRemove.begin();
        sent = true;
        JSON::Value tmp;
        tmp[0u] = "stream";
        tmp[1u].append(strm);
        tmp[1u].append(0u);
        tmp[1u].append(0u);
        tmp[1u].append(0u);
        tmp[1u].append(0u);
        W.sendFrame(tmp.toString());
        strmRemove.erase(strm);
        lastStrmStat.erase(strm);
      }
    }
    if (!sent){Util::sleep(500);}
  }
}

/// Handles a single incoming API connection.
/// Assumes the connection is unauthorized and will allow for 4 requests without authorization before disconnecting.
int Controller::handleAPIConnection(Socket::Connection &conn){
  // set up defaults
  unsigned int logins = 0;
  bool authorized = false;
  bool isLocal = false;
  HTTP::Parser H;
  // while connected and not past login attempt limit
  while (conn && logins < 4){
    if ((conn.spool() || conn.Received().size()) && H.Read(conn)){
      // Are we local and not forwarded? Instant-authorized.
      if (!authorized && !H.hasHeader("X-Real-IP") && conn.isLocal()){
        MEDIUM_MSG("Local API access automatically authorized");
        isLocal = true;
        authorized = true;
      }
#ifdef NOAUTH
      // If auth is disabled, always allow access.
      authorized = true;
#endif
      if (!authorized && H.hasHeader("Authorization")){
        std::string auth = H.GetHeader("Authorization");
        if (auth.substr(0, 5) == "json "){
          INFO_MSG("Checking auth header");
          JSON::Value req;
          req["authorize"] = JSON::fromString(auth.substr(5));
          if (Storage["account"]){
            tthread::lock_guard<tthread::mutex> guard(configMutex);
            authorized = authorize(req, req, conn);
            if (!authorized){
              H.Clean();
              H.body = "Please login first or provide a valid token authentication.";
              H.SetHeader("Server", APPIDENT);
              H.SetHeader("WWW-Authenticate", "json " + req["authorize"].toString());
              H.SendResponse("403", "Not authorized", conn);
              H.Clean();
              continue;
            }
          }
        }
      }
      // Catch websocket requests
      if (H.url == "/ws"){
        if (!authorized){
          H.Clean();
          H.body = "Please login first or provide a valid token authentication.";
          H.SetHeader("Server", APPIDENT);
          H.SendResponse("403", "Not authorized", conn);
          H.Clean();
          continue;
        }
        handleWebSocket(H, conn);
        H.Clean();
        continue;
      }
      // Catch prometheus requests
      if (Controller::prometheus.size()){
        if (H.url == "/" + Controller::prometheus){
          handlePrometheus(H, conn, PROMETHEUS_TEXT);
          H.Clean();
          continue;
        }
        if (H.url.substr(0, Controller::prometheus.size() + 6) == "/" + Controller::prometheus + ".json"){
          handlePrometheus(H, conn, PROMETHEUS_JSON);
          H.Clean();
          continue;
        }
      }
      JSON::Value Response;
      JSON::Value Request = JSON::fromString(H.GetVar("command"));
      // invalid request? send the web interface, unless requested as "/api"
      if (!Request.isObject() && H.url != "/api" && H.url != "/api2"){
#include "server.html.h"
        H.Clean();
        H.SetHeader("Content-Type", "text/html");
        H.SetHeader("X-Info", "To force an API response, request the file /api");
        H.SetHeader("Server", APPIDENT);
        H.SetHeader("Content-Length", server_html_len);
        H.SetHeader("X-UA-Compatible", "IE=edge;chrome=1");
        H.SendResponse("200", "OK", conn);
        conn.SendNow(server_html, server_html_len);
        H.Clean();
        break;
      }
      if (H.url == "/api2"){Request["minimal"] = true;}
      {// lock the config mutex here - do not unlock until done processing
        tthread::lock_guard<tthread::mutex> guard(configMutex);
        // if already authorized, do not re-check for authorization
        if (authorized && Storage["account"]){
          Response["authorize"]["status"] = "OK";
          if (isLocal){Response["authorize"]["local"] = true;}
        }else{
          authorized |= authorize(Request, Response, conn);
        }
        if (authorized){
          handleAPICommands(Request, Response);
          Controller::checkServerLimits(); /*LTS*/
        }
      }// config mutex lock
      if (!authorized){
        // sleep a second to prevent bruteforcing.
        // We need to make sure this happens _after_ unlocking the mutex!
        Util::sleep(1000);
        logins++;
      }
      // send the response, either normally or through JSONP callback.
      std::string jsonp = "";
      if (H.GetVar("callback") != ""){jsonp = H.GetVar("callback");}
      if (H.GetVar("jsonp") != ""){jsonp = H.GetVar("jsonp");}
      H.Clean();
      H.SetHeader("Content-Type", "text/javascript");
      H.setCORSHeaders();
      if (jsonp == ""){
        H.SetBody(Response.toString() + "\n\n");
      }else{
        H.SetBody(jsonp + "(" + Response.toString() + ");\n\n");
      }
      H.SendResponse("200", "OK", conn);
      H.Clean();
    }// if HTTP request received
  }// while connected
  return 0;
}

void Controller::handleUDPAPI(void *np){
  Socket::UDPConnection uSock(true);
  uint16_t boundPort = uSock.bind(UDP_API_PORT, UDP_API_HOST);
  if (!boundPort){
    FAIL_MSG("Could not open local API UDP socket - not all functionality will be available");
    return;
  }
  HTTP::URL boundAddr;
  boundAddr.protocol = "udp";
  boundAddr.setPort(boundPort);
  boundAddr.host = uSock.getBoundAddress();
  {
    tthread::lock_guard<tthread::mutex> guard(configMutex);
    udpApiBindAddr = boundAddr.getUrl();
    Controller::writeConfig();
  }
  Util::Procs::socketList.insert(uSock.getSock());
  uSock.allocateDestination();
  while (Controller::conf.is_active){
    if (uSock.Receive()){
      MEDIUM_MSG("UDP API: %s", (const char*)uSock.data);
      JSON::Value Request = JSON::fromString(uSock.data, uSock.data.size());
      Request["minimal"] = true;
      JSON::Value Response;
      if (Request.isObject()){
        tthread::lock_guard<tthread::mutex> guard(configMutex);
        Response["authorize"]["local"] = true;
        handleAPICommands(Request, Response);
        Response.removeMember("authorize");
        uSock.SendNow(Response.toString());
      }else{
        WARN_MSG("Invalid API command received over UDP: %s", (const char*)uSock.data);
      }
    }else{
      Util::sleep(500);
    }
  }
}

/// Local-only helper function that checks for duplicate protocols and removes them
static void removeDuplicateProtocols(){
  JSON::Value &P = Controller::Storage["config"]["protocols"];
  jsonForEach(P, it){it->removeNullMembers();}
  std::set<std::string> ignores;
  ignores.insert("online");
  bool reloop = true;
  while (reloop){
    reloop = false;
    jsonForEach(P, it){
      jsonForEach(P, jt){
        if (it.num() == jt.num()){continue;}
        if ((*it).compareExcept(*jt, ignores)){
          jt.remove();
          reloop = true;
          break;
        }
      }
      if (reloop){break;}
    }
  }
}

/// Helper function for nuke_stream and related calls
static void nukeStream(const std::string & strm){
  std::deque<std::string> command;
  command.push_back(Util::getMyPath() + "MistUtilNuke");
  command.push_back(strm);
  int stdIn = 0;
  int stdOut = 1;
  int stdErr = 2;
  Util::Procs::StartPiped(command, &stdIn, &stdOut, &stdErr);
}


void Controller::handleAPICommands(JSON::Value &Request, JSON::Value &Response){
  /*LTS-START*/
  // These are only used internally. We abort further processing if encountered.
  if (Request.isMember("trigger_stat")){
    JSON::Value &tStat = Request["trigger_stat"];
    if (tStat.isMember("name") && tStat.isMember("ms")){
      Controller::triggerLog &tLog = Controller::triggerStats[tStat["name"].asStringRef()];
      tLog.totalCount++;
      tLog.ms += tStat["ms"].asInt();
      if (!tStat.isMember("ok") || !tStat["ok"].asBool()){tLog.failCount++;}
    }
    return;
  }
  if (Request.isMember("trigger_fail")){
    Controller::triggerStats[Request["trigger_fail"].asStringRef()].failCount++;
    return;
  }
  if (Request.isMember("push_status_update")){
    JSON::Value &statUp = Request["push_status_update"];
    if (statUp.isMember("id") && statUp.isMember("status")){
      setPushStatus(statUp["id"].asInt(), statUp["status"]);
    }
  }
  if (Request.isMember("proc_status_update")){
    JSON::Value &statUp = Request["proc_status_update"];
    if (statUp.isMember("id") && statUp.isMember("status") && statUp.isMember("source") && statUp.isMember("proc") && statUp.isMember("sink")){
      setProcStatus(statUp["id"].asInt(), statUp["proc"].asStringRef(), statUp["source"].asStringRef(), statUp["sink"].asStringRef(), statUp["status"]);
    }
  }
  /*LTS-END*/

  if (Request.isMember("config_backup")){
    std::set<std::string> skip;
    skip.insert("log");
    skip.insert("online");
    skip.insert("error");
    Response["config_backup"].assignFrom(Controller::Storage, skip);
  }

  if (Request.isMember("config_reload")){
    INFO_MSG("Reloading configuration from disk on request");
    Controller::readConfigFromDisk();
  }

  if (Request.isMember("config_restore")){
    std::set<std::string> skip;
    skip.insert("log");
    skip.insert("online");
    skip.insert("error");
    Controller::CheckStreams(Request["config_restore"]["streams"], Controller::Storage["streams"]);
    Request["config_restore"]["streams"] = Controller::Storage["streams"];
    Controller::Storage.assignFrom(Request["config_restore"], skip);
    removeDuplicateProtocols();
    Controller::accesslog = Controller::Storage["config"]["accesslog"].asStringRef();
    Controller::prometheus = Controller::Storage["config"]["prometheus"].asStringRef();
    if (Util::printDebugLevel != (Controller::Storage["config"]["debug"].isInt() ? Controller::Storage["config"]["debug"].asInt() : DEBUG)){
      Util::printDebugLevel = (Controller::Storage["config"]["debug"].isInt() ? Controller::Storage["config"]["debug"].asInt() : DEBUG);
      INFO_MSG("Debug level set to %u", Util::printDebugLevel);
    }
    WARN_MSG("Restored configuration over API, replacing previous configuration entirely");
  }

  // Parse config and streams from the request.
  if (Request.isMember("config") && Request["config"].isObject()){
    const JSON::Value &in = Request["config"];
    JSON::Value &out = Controller::Storage["config"];
    if (in.isMember("debug")){
      out["debug"] = in["debug"];
      if (Util::printDebugLevel != (out["debug"].isInt() ? out["debug"].asInt() : DEBUG)){
        Util::printDebugLevel = (out["debug"].isInt() ? out["debug"].asInt() : DEBUG);
        INFO_MSG("Debug level set to %u", Util::printDebugLevel);
      }
    }
    if (in.isMember("protocols")){
      out["protocols"] = in["protocols"];
      removeDuplicateProtocols();
    }
    if (in.isMember("trustedproxy")){
      out["trustedproxy"] = in["trustedproxy"];
      Controller::normalizeTrustedProxies(out["trustedproxy"]);
    }
    if (in.isMember("controller")){out["controller"] = in["controller"];}
    if (in.isMember("serverid")){out["serverid"] = in["serverid"];}
    if (in.isMember("triggers")){
      out["triggers"] = in["triggers"];
      if (!out["triggers"].isObject()){
        out.removeMember("triggers");
      }else{
        jsonForEach(out["triggers"], it){
          if (it->isArray()){
            jsonForEach((*it), jt){jt->removeNullMembers();}
          }
        }
      }
    }
    if (in.isMember("accesslog")){
      out["accesslog"] = in["accesslog"];
      Controller::accesslog = out["accesslog"].asStringRef();
    }
    if (in.isMember("prometheus")){
      out["prometheus"] = in["prometheus"];
      Controller::prometheus = out["prometheus"].asStringRef();
    }
    if (in.isMember("sessionViewerMode")){out["sessionViewerMode"] = in["sessionViewerMode"];}
    if (in.isMember("sessionInputMode")){out["sessionInputMode"] = in["sessionInputMode"];}
    if (in.isMember("sessionOutputMode")){out["sessionOutputMode"] = in["sessionOutputMode"];}
    if (in.isMember("sessionUnspecifiedMode")){out["sessionUnspecifiedMode"] = in["sessionUnspecifiedMode"];}
    if (in.isMember("sessionStreamInfoMode")){out["sessionStreamInfoMode"] = in["sessionStreamInfoMode"];}
    if (in.isMember("tknMode")){out["tknMode"] = in["tknMode"];}
    if (in.isMember("defaultStream")){out["defaultStream"] = in["defaultStream"];}
    if (in.isMember("location") && in["location"].isObject()){
      out["location"]["lat"] = in["location"]["lat"].asDouble();
      out["location"]["lon"] = in["location"]["lon"].asDouble();
      out["location"]["name"] = in["location"]["name"].asStringRef();
    }
  }
  if (Request.isMember("bandwidth")){
    if (Request["bandwidth"].isObject()){
      if (Request["bandwidth"].isMember("limit") && Request["bandwidth"]["limit"].isInt()){
        Controller::Storage["bandwidth"]["limit"] = Request["bandwidth"]["limit"];
      }
      if (Request["bandwidth"].isMember("exceptions") && Request["bandwidth"]["exceptions"].isArray()){
        Controller::Storage["bandwidth"]["exceptions"] = Request["bandwidth"]["exceptions"];
      }
      Controller::updateBandwidthConfig();
    }
    Response["bandwidth"] = Controller::Storage["bandwidth"];
  }
  if (Request.isMember("streams")){
    Controller::CheckStreams(Request["streams"], Controller::Storage["streams"]);
  }
  if (Request.isMember("addstream")){
    Controller::AddStreams(Request["addstream"], Controller::Storage["streams"]);
  }
  if (Request.isMember("deletestream")){
    // if array, delete all elements
    // if object, delete all entries
    // if string, delete just the one
    if (Request["deletestream"].isString()){
      Controller::deleteStream(Request["deletestream"].asStringRef(),
                               Controller::Storage["streams"]);
    }
    if (Request["deletestream"].isArray()){
      jsonForEach(Request["deletestream"], it){
        Controller::deleteStream(it->asStringRef(), Controller::Storage["streams"]);
      }
    }
    if (Request["deletestream"].isObject()){
      jsonForEach(Request["deletestream"], it){
        Controller::deleteStream(it.key(), Controller::Storage["streams"]);
      }
    }
  }
  if (Request.isMember("deletestreamsource")){
    // if array, delete all elements
    // if object, delete all entries
    // if string, delete just the one
    if (Request["deletestreamsource"].isString()){
      switch (Controller::deleteStream(Request["deletestreamsource"].asStringRef(),
                                       Controller::Storage["streams"], true)){
      case 0: Response["deletestreamsource"] = "0: No action taken"; break;
      case 1: Response["deletestreamsource"] = "1: Source file deleted"; break;
      case 2: Response["deletestreamsource"] = "2: Source file and dtsh deleted"; break;
      case -1: Response["deletestreamsource"] = "-1: Stream deleted, source remains"; break;
      case -2: Response["deletestreamsource"] = "-2: Stream and source file deleted"; break;
      case -3: Response["deletestreamsource"] = "-3: Stream, source file and dtsh deleted"; break;
      }
    }
    if (Request["deletestreamsource"].isArray()){
      jsonForEach(Request["deletestreamsource"], it){
        switch (Controller::deleteStream(it->asStringRef(), Controller::Storage["streams"], true)){
        case 0: Response["deletestreamsource"][it.num()] = "0: No action taken"; break;
        case 1: Response["deletestreamsource"][it.num()] = "1: Source file deleted"; break;
        case 2: Response["deletestreamsource"][it.num()] = "2: Source file and dtsh deleted"; break;
        case -1:
          Response["deletestreamsource"][it.num()] = "-1: Stream deleted, source remains";
          break;
        case -2:
          Response["deletestreamsource"][it.num()] = "-2: Stream and source file deleted";
          break;
        case -3:
          Response["deletestreamsource"][it.num()] = "-3: Stream, source file and dtsh deleted";
          break;
        }
      }
    }
    if (Request["deletestreamsource"].isObject()){
      jsonForEach(Request["deletestreamsource"], it){
        switch (Controller::deleteStream(it.key(), Controller::Storage["streams"], true)){
        case 0: Response["deletestreamsource"][it.key()] = "0: No action taken"; break;
        case 1: Response["deletestreamsource"][it.key()] = "1: Source file deleted"; break;
        case 2: Response["deletestreamsource"][it.key()] = "2: Source file and dtsh deleted"; break;
        case -1:
          Response["deletestreamsource"][it.key()] = "-1: Stream deleted, source remains";
          break;
        case -2:
          Response["deletestreamsource"][it.key()] = "-2: Stream and source file deleted";
          break;
        case -3:
          Response["deletestreamsource"][it.key()] = "-3: Stream, source file and dtsh deleted";
          break;
        }
      }
    }
  }
  if (Request.isMember("addprotocol")){
    if (Request["addprotocol"].isArray()){
      jsonForEach(Request["addprotocol"], it){
        Controller::Storage["config"]["protocols"].append(*it);
      }
    }
    if (Request["addprotocol"].isObject()){
      Controller::Storage["config"]["protocols"].append(Request["addprotocol"]);
    }
    removeDuplicateProtocols();
  }
  if (Request.isMember("deleteprotocol")){
    std::set<std::string> ignores;
    ignores.insert("online");
    if (Request["deleteprotocol"].isArray() && Request["deleteprotocol"].size()){
      JSON::Value newProtocols;
      jsonForEach(Controller::Storage["config"]["protocols"], it){
        bool add = true;
        jsonForEach(Request["deleteprotocol"], pit){
          if ((*it).compareExcept(*pit, ignores)){
            add = false;
            break;
          }
        }
        if (add){newProtocols.append(*it);}
      }
      Controller::Storage["config"]["protocols"] = newProtocols;
    }
    if (Request["deleteprotocol"].isObject()){
      JSON::Value newProtocols;
      jsonForEach(Controller::Storage["config"]["protocols"], it){
        if (!(*it).compareExcept(Request["deleteprotocol"], ignores)){newProtocols.append(*it);}
      }
      Controller::Storage["config"]["protocols"] = newProtocols;
    }
  }
  if (Request.isMember("updateprotocol")){
    std::set<std::string> ignores;
    ignores.insert("online");
    if (Request["updateprotocol"].isArray() && Request["updateprotocol"].size() == 2){
      jsonForEach(Controller::Storage["config"]["protocols"], it){
        if ((*it).compareExcept(Request["updateprotocol"][0u], ignores)){
          // If the connector type didn't change, mark it as needing a reload
          if ((*it)["connector"] == Request["updateprotocol"][1u]["connector"]){
            reloadProtocol(it.num());
          }
          (*it) = Request["updateprotocol"][1u];
        }
      }
      removeDuplicateProtocols();
    }else{
      FAIL_MSG("Cannot parse updateprotocol call: needs to be in the form [A, B]");
    }
  }

  // Tag handling
  if (Request.isMember("tags") && Request["tags"].isArray()){
    //Set entire tag list to new given list, sorted and de-duplicated
    Controller::Storage["tags"].shrink(0);
    std::set<std::string> newTags;
    jsonForEach(Request["tags"], tag){
      std::string val = tag->asString();
      if (val.size()){newTags.insert(val);}
    }
    //Set new tags
    for (std::set<std::string>::iterator it = newTags.begin(); it != newTags.end(); ++it){
      Controller::Storage["tags"].append(*it);
    }
  }
  if (Request.isMember("add_tag")){
    //Add given tag(s) to existing list
    std::set<std::string> newTags;
    //Get current tags
    if (Controller::Storage.isMember("tags") && Controller::Storage["tags"].isArray() && Controller::Storage["tags"].size()){
      jsonForEach(Controller::Storage["tags"], tag){
        std::string val = tag->asString();
        if (val.size()){newTags.insert(val);}
      }
    }
    Controller::Storage["tags"].shrink(0);
    if (Request["add_tag"].isString()){
      newTags.insert(Request["add_tag"].asStringRef());
    }else if (Request["add_tag"].isArray()){
      jsonForEach(Request["add_tag"], tag){
        std::string val = tag->asString();
        if (val.size()){newTags.insert(val);}
      }
    }else if (Request["add_tag"].isObject()){
      jsonForEach(Request["add_tag"], tag){
        std::string val = tag.key();
        if (val.size()){newTags.insert(val);}
      }
    }
    //Set new tags
    for (std::set<std::string>::iterator it = newTags.begin(); it != newTags.end(); ++it){
      Controller::Storage["tags"].append(*it);
    }
  }
  if (Request.isMember("remove_tag")){
    //Remove given tag(s) from existing list
    std::set<std::string> newTags;
    //Get current tags
    if (Controller::Storage.isMember("tags") && Controller::Storage["tags"].isArray() && Controller::Storage["tags"].size()){
      jsonForEach(Controller::Storage["tags"], tag){
        std::string val = tag->asString();
        if (val.size()){newTags.insert(val);}
      }
    }
    Controller::Storage["tags"].shrink(0);
    if (Request["remove_tag"].isString()){
      newTags.erase(Request["remove_tag"].asStringRef());
    }else if (Request["remove_tag"].isArray()){
      jsonForEach(Request["remove_tag"], tag){
        newTags.erase(tag->asString());
      }
    }else if (Request["remove_tag"].isObject()){
      jsonForEach(Request["remove_tag"], tag){
        newTags.erase(tag.key());
      }
    }
    //Set new tags
    for (std::set<std::string>::iterator it = newTags.begin(); it != newTags.end(); ++it){
      Controller::Storage["tags"].append(*it);
    }
  }
  if (Request.isMember("tags") || Request.isMember("add_tag") || Request.isMember("remove_tag")){
    if (Controller::Storage.isMember("tags") && Controller::Storage["tags"].isArray()){
      Response["tags"] = Controller::Storage["tags"];
    }else{
      Response["tags"].append("");
      Response["tags"].shrink(0);
    }
  }

  if (Request.isMember("capabilities")){
    Controller::checkCapable(capabilities);
    Response["capabilities"] = capabilities;
  }

  if (Request.isMember("browse")){
    if (Request["browse"] == ""){Request["browse"] = ".";}
    DIR *dir;
    struct dirent *ent;
    struct stat filestat;
    char *rpath = realpath(Request["browse"].asString().c_str(), 0);
    if (rpath == NULL){
      Response["browse"]["path"].append(Request["browse"].asString());
    }else{
      Response["browse"]["path"].append(rpath); // Request["browse"].asString());
      if ((dir = opendir(Request["browse"].asString().c_str())) != NULL){
        while ((ent = readdir(dir)) != NULL){
          if (strcmp(ent->d_name, ".") != 0 && strcmp(ent->d_name, "..") != 0){
            std::string filepath = Request["browse"].asString() + "/" + std::string(ent->d_name);
            if (stat(filepath.c_str(), &filestat)) continue;
            if (S_ISDIR(filestat.st_mode)){
              Response["browse"]["subdirectories"].append(ent->d_name);
            }else{
              Response["browse"]["files"].append(ent->d_name);
            }
          }
        }
        closedir(dir);
      }
    }
    free(rpath);
  }

  // Examples of valid playlist requests:
  //"playlist":{"streamname": ["append", "path/to/file.ts"]}
  //"playlist":{"streamname": ["remove", "path/to/file.ts"]}
  //"playlist":{"streamname": ["line", 2, "path/to/file.ts"]}
  //"playlist":{"streamname": true}
  //"playlist":{"streamname": [["append", "path/to/file.ts"], ["remove", "path/to/file.ts"]]}
  if (Request.isMember("playlist")){
    if (!Request["playlist"].isObject()){
      ERROR_MSG("Playlist API call requires object payload, no object given");
    }else{
      jsonForEach(Request["playlist"], it){
        if (!Controller::Storage["streams"].isMember(it.key()) ||
            !Controller::Storage["streams"][it.key()].isMember("source")){
          FAIL_MSG("Playlist API call (partially) not executed: stream '%s' not configured",
                   it.key().c_str());
        }else{
          std::string src = Controller::Storage["streams"][it.key()]["source"].asString();
          if (src.substr(src.size() - 4) != ".pls"){
            FAIL_MSG(
                "Playlist API call (partially) not executed: stream '%s' is not playlist-based",
                it.key().c_str());
          }else{
            bool readFirst = true;
            struct stat fileinfo;
            if (stat(src.c_str(), &fileinfo) != 0){
              if (errno == EACCES){
                FAIL_MSG("Playlist API call (partially) not executed: stream '%s' playlist '%s' "
                         "cannot be accessed (no file permissions)",
                         it.key().c_str(), src.c_str());
                break;
              }
              if (errno == ENOENT){
                WARN_MSG("Creating playlist file: %s", src.c_str());
                readFirst = false;
              }
            }
            std::deque<std::string> lines;
            if (readFirst){
              std::ifstream plsRead(src.c_str());
              if (!plsRead.good()){
                FAIL_MSG("Playlist (%s) for stream '%s' could not be opened for reading; aborting "
                         "command(s)",
                         src.c_str(), it.key().c_str());
                break;
              }
              std::string line;
              do{
                std::getline(plsRead, line);
                if (line.size() || plsRead.good()){lines.push_back(line);}
              }while (plsRead.good());
            }
            unsigned int plsNo = 0;
            for (std::deque<std::string>::iterator plsIt = lines.begin(); plsIt != lines.end(); ++plsIt){
              MEDIUM_MSG("Before playlist command item %u: %s", plsNo, plsIt->c_str());
              ++plsNo;
            }
            if (!it->isBool()){executePlsCommand(*it, lines);}
            JSON::Value &outPls = Response["playlist"][it.key()];
            std::ofstream plsOutFile(src.c_str(), std::ios_base::trunc);
            if (!plsOutFile.good()){
              FAIL_MSG("Could not open playlist for writing: %s", src.c_str());
              break;
            }
            plsNo = 0;
            for (std::deque<std::string>::iterator plsIt = lines.begin(); plsIt != lines.end(); ++plsIt){
              MEDIUM_MSG("After playlist command item %u: %s", plsNo, plsIt->c_str());
              ++plsNo;
              outPls.append(*plsIt);
              if (plsNo < lines.size() || (*plsIt).size()){plsOutFile << (*plsIt) << "\n";}
            }
          }
        }
      }
    }
  }

  if (Request.isMember("ui_settings")){
    if (Request["ui_settings"].isObject()){Storage["ui_settings"] = Request["ui_settings"];}
    Response["ui_settings"] = Storage["ui_settings"];
  }
  /*LTS-START*/
  ///
  /// \api
  /// LTS builds will always include an `"LTS"` response, set to 1.
  ///
  Response["LTS"] = 1;
///
/// \api
/// `"autoupdate"` requests (LTS-only) will cause MistServer to apply a rolling update to itself, and are not responded to.
///
#ifdef UPDATER
  if (Request.isMember("autoupdate")){Controller::checkUpdates();}
  if (Request.isMember("update") || Request.isMember("checkupdate") || Request.isMember("autoupdate")){
    Controller::insertUpdateInfo(Response["update"]);
  }
#endif
  /*LTS-END*/
  if (!Request.isMember("minimal") || Request.isMember("streams") ||
      Request.isMember("addstream") || Request.isMember("deletestream")){
    if (!Request.isMember("streams") &&
        (Request.isMember("addstream") || Request.isMember("deletestream"))){
      Response["streams"]["incomplete list"] = 1u;
      if (Request.isMember("addstream")){
        jsonForEach(Request["addstream"], jit){
          if (Controller::Storage["streams"].isMember(jit.key())){
            Response["streams"][jit.key()] = Controller::Storage["streams"][jit.key()];
          }
        }
      }
    }else{
      Response["streams"] = Controller::Storage["streams"];
    }
  }
  // sent current configuration, if not minimal or was changed/requested
  if (!Request.isMember("minimal") || Request.isMember("config")){
    Response["config"] = Controller::Storage["config"];
    Response["config"]["iid"] = instanceId;
    Response["config"]["version"] = PACKAGE_VERSION " " RELEASE;
    // add required data to the current unix time to the config, for syncing reasons
    Response["config"]["time"] = Util::epoch();
    if (!Response["config"].isMember("serverid")){Response["config"]["serverid"] = "";}
  }
  // sent any available logs and statistics
  ///
  /// \api
  /// `"log"` responses are always sent, and cannot be requested:
  /// ~~~~~~~~~~~~~~~{.js}
  /// [
  ///   [
  ///     1398978357, //unix timestamp of this log message
  ///     "CONF", //shortcode indicating the type of log message
  ///     "Starting connector:{\"connector\":\"HTTP\"}" //string containing the log message itself
  ///   ],
  ///   //the above structure repeated for all logs
  /// ]
  /// ~~~~~~~~~~~~~~~
  /// It's possible to clear the stored logs by sending an empty `"clearstatlogs"` request.
  ///
  if (Request.isMember("clearstatlogs") || Request.isMember("log") || !Request.isMember("minimal")){
    tthread::lock_guard<tthread::mutex> guard(logMutex);
    if (!Request.isMember("minimal") || Request.isMember("log")){
      Response["log"] = Controller::Storage["log"];
    }
    // clear log if requested
    if (Request.isMember("clearstatlogs")){Controller::Storage["log"].null();}
  }
  if (Request.isMember("clients")){
    if (Request["clients"].isArray()){
      for (unsigned int i = 0; i < Request["clients"].size(); ++i){
        Controller::fillClients(Request["clients"][i], Response["clients"][i]);
      }
    }else{
      Controller::fillClients(Request["clients"], Response["clients"]);
    }
  }
  if (Request.isMember("totals")){
    if (Request["totals"].isArray()){
      for (unsigned int i = 0; i < Request["totals"].size(); ++i){
        Controller::fillTotals(Request["totals"][i], Response["totals"][i]);
      }
    }else{
      Controller::fillTotals(Request["totals"], Response["totals"]);
    }
  }
  if (Request.isMember("active_streams")){
    Controller::fillActive(Request["active_streams"], Response["active_streams"]);
  }
  if (Request.isMember("stats_streams")){
    Controller::fillHasStats(Request["stats_streams"], Response["stats_streams"]);
  }

  if (Request.isMember("api_endpoint")){
    HTTP::URL url("http://localhost:4242");
    url.host = Util::listenInterface;
    if (url.host == "::"){url.host = "::1";}
    if (url.host == "0.0.0.0"){url.host = "127.0.0.1";}
    url.port = JSON::Value(Util::listenPort).asString();
    Response["api_endpoint"] = url.getUrl();
  }

  if (Request.isMember("shutdown")){
    if (Response.isMember("authorize") && Response["authorize"].isMember("local")){
      std::string reason;
      if (Request["shutdown"].isObject() || Request["shutdown"].isArray()){
        reason = Request["shutdown"].toString();
      }else{
        reason = Request["shutdown"].asString();
      }
      WARN_MSG("Shutdown requested through local API: %s", reason.c_str());
      Controller::conf.is_active = false;
      kill(getpid(), SIGINT);
      Response["shutdown"] = "Shutting down";
    }else{
      Response["shutdown"] = "Ignored - only local users may request shutdown";
    }
  }

  if (Request.isMember("nuke_stream") && Request["nuke_stream"].isString() && Request["nuke_stream"].asStringRef().size()){
    nukeStream(Request["nuke_stream"].asStringRef());
  }

  if (Request.isMember("no_unconfigured_streams")){
    JSON::Value emptyRequest;
    JSON::Value currStreams;
    Controller::fillActive(emptyRequest, currStreams);
    jsonForEach(currStreams, strm){
      std::string S = strm->asStringRef();
      //Remove wildcard, if any
      if (S.find('+') != std::string::npos){S.erase(S.find('+'));}
      if (!Controller::Storage["streams"].isMember(S) || !Controller::Storage["streams"][S].isMember("source")){
        WARN_MSG("Shutting down unconfigured stream %s", strm->asStringRef().c_str());
        nukeStream(strm->asStringRef());
        continue;
      }
      char pageName[NAME_BUFFER_SIZE];
      snprintf(pageName, NAME_BUFFER_SIZE, SHM_STREAM_META, strm->asStringRef().c_str());
      IPC::sharedPage streamPage(pageName, 0, false, false);
      if (streamPage){
        Util::RelAccX rlxStrm(streamPage.mapped, false);
        if (rlxStrm.isReady()){
          std::string source = rlxStrm.getPointer("source");
          const std::string & oriSource = Controller::Storage["streams"][S]["source"].asStringRef();
          if (source != oriSource){
            WARN_MSG("Source for %s is %s instead of %s; shutting it down", strm->asStringRef().c_str(), source.c_str(), oriSource.c_str());
            nukeStream(strm->asStringRef());
          }
        }
      }
    }
  }

  if (Request.isMember("invalidate_sessions")){
    if (Request["invalidate_sessions"].isArray()){
      for (unsigned int i = 0; i < Request["invalidate_sessions"].size(); ++i){
        Controller::sessions_invalidate(Request["invalidate_sessions"][i].asStringRef());
      }
    }else{
      Controller::sessions_invalidate(Request["invalidate_sessions"].asStringRef());
    }
  }

  if (Request.isMember("stop_sessions")){
    if (Request["stop_sessions"].isArray() || Request["stop_sessions"].isObject()){
      jsonForEach(Request["stop_sessions"], it){Controller::sessions_shutdown(it);}
    }else{
      Controller::sessions_shutdown(Request["stop_sessions"].asStringRef());
    }
  }

  if (Request.isMember("stop_sessid")){
    if (Request["stop_sessid"].isArray() || Request["stop_sessid"].isObject()){
      jsonForEach(Request["stop_sessid"], it){Controller::sessId_shutdown(it->asStringRef());}
    }else{
      Controller::sessId_shutdown(Request["stop_sessid"].asStringRef());
    }
  }

  if (Request.isMember("stop_tag")){
    if (Request["stop_tag"].isArray() || Request["stop_tag"].isObject()){
      jsonForEach(Request["stop_tag"], it){Controller::tag_shutdown(it->asStringRef());}
    }else{
      Controller::tag_shutdown(Request["stop_tag"].asStringRef());
    }
  }

  if (Request.isMember("tag_sessid")){
    if (Request["tag_sessid"].isObject()){
      jsonForEach(Request["tag_sessid"], it){
        Controller::sessId_tag(it.key(), it->asStringRef());
      }
    }
  }

  if (Request.isMember("push_start")){
    std::string stream;
    std::string target;
    if (Request["push_start"].isArray()){
      stream = Request["push_start"][0u].asStringRef();
      target = Request["push_start"][1u].asStringRef();
    }else{
      stream = Request["push_start"]["stream"].asStringRef();
      target = Request["push_start"]["target"].asStringRef();
    }
    Util::sanitizeName(stream);
    if (*stream.rbegin() != '+'){
      startPush(stream, target);
    }else{
      std::set<std::string> activeStreams = Controller::getActiveStreams(stream);
      if (activeStreams.size()){
        for (std::set<std::string>::iterator jt = activeStreams.begin(); jt != activeStreams.end(); ++jt){
          std::string streamname = *jt;
          std::string target_tmp = target;
          startPush(streamname, target_tmp);
        }
      }
    }
  }

  if (Request.isMember("proc_list")){
    getProcsForStream(Request["proc_list"].asStringRef(), Response["proc_list"]);
  }

  if (Request.isMember("push_list")){Controller::listPush(Response["push_list"]);}

  if (Request.isMember("push_stop")){
    if (Request["push_stop"].isArray()){
      jsonForEach(Request["push_stop"], it){Controller::stopPush(it->asInt());}
    }else{
      Controller::stopPush(Request["push_stop"].asInt());
    }
  }

  if (Request.isMember("push_auto_add")){Controller::addPush(Request["push_auto_add"], Response["push_list"]);}

  if (Request.isMember("push_auto_remove")){
    if (Request["push_auto_remove"].isArray()){
      jsonForEach(Request["push_auto_remove"], it){Controller::removePush(*it, Response["push_list"]);}
    }else{
      Controller::removePush(Request["push_auto_remove"], Response["push_list"]);
    }
  }

  if (Request.isMember("push_auto_list")){
    Response["push_auto_list"] = Controller::Storage["autopushes"];
  }

  if (Request.isMember("push_settings")){
    Controller::pushSettings(Request["push_settings"], Response["push_settings"]);
  }

  if (Request.isMember("variable_list")){Controller::listCustomVariables(Response["variable_list"]);}
  if (Request.isMember("variable_add")){Controller::addVariable(Request["variable_add"], Response["variable_list"]);}
  if (Request.isMember("variable_remove")){Controller::removeVariable(Request["variable_remove"], Response["variable_list"]);}

  if (Request.isMember("external_writer_remove")){Controller::removeExternalWriter(Request["external_writer_remove"]);}
  if (Request.isMember("external_writer_add")){Controller::addExternalWriter(Request["external_writer_add"]);}
  if (Request.isMember("external_writer_remove") || Request.isMember("external_writer_add") || 
      Request.isMember("external_writer_list")){
    Controller::listExternalWriters(Response["external_writer_list"]);
  }

  Controller::writeConfig();

  if (Request.isMember("save")){
    Controller::Log("CONF", "Writing config to file on request through API");
    Controller::writeConfigToDisk(true);
  }

}
