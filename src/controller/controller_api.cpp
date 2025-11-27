#include "controller_api.h"

#include "controller_capabilities.h"
#include "controller_connectors.h"
#include "controller_external_writers.h"
#include "controller_push.h"
#include "controller_statistics.h"
#include "controller_storage.h"
#include "controller_streams.h"
#include "controller_updater.h"
#include "controller_variables.h"

#include <mist/auth.h>
#include <mist/bitfields.h>
#include <mist/config.h>
#include <mist/defines.h>
#include <mist/http_parser.h>
#include <mist/jwt.h>
#include <mist/procs.h>
#include <mist/stream.h>
#include <mist/timing.h>
#include <mist/url.h>

#include <dirent.h>
#include <fstream>
#include <signal.h>
#include <sstream>
#include <sys/stat.h> //for browse API call

std::set<APIConn *> reggedLoggers;
std::set<APIConn *> reggedAccess;
std::set<APIConn *> reggedStreams;

void Controller::registerLogger(APIConn *aConn) {
  reggedLoggers.insert(aConn);
}

void Controller::registerAccess(APIConn *aConn) {
  reggedAccess.insert(aConn);
}

void Controller::registerStreams(APIConn *aConn) {
  reggedStreams.insert(aConn);
}

void Controller::deregister(APIConn *aConn) {
  reggedLoggers.erase(aConn);
  reggedAccess.erase(aConn);
  reggedStreams.erase(aConn);
}

void Controller::callLogger(uint64_t time, const std::string & kind, const std::string & message, const std::string & stream,
                            uint64_t progPid, const std::string & exe, const std::string & line) {
  std::set<APIConn *> toDel;
  for (auto A : reggedLoggers) {
    A->log(time, kind, message, stream, progPid, exe, line);
    if (!A->C) { toDel.insert(A); }
  }
  for (auto A : toDel) { delete A; }
}

void Controller::callAccess(uint64_t time, const std::string & session, const std::string & stream, const std::string & connector,
                            const std::string & host, uint64_t duration, uint64_t up, uint64_t down, const std::string & tags) {
  std::set<APIConn *> toDel;
  for (auto A : reggedAccess) {
    A->access(time, session, stream, connector, host, duration, up, down, tags);
    if (!A->C) { toDel.insert(A); }
  }
  for (auto A : toDel) { delete A; }
}

void Controller::callStreams(const std::string & stream, uint8_t status, uint64_t viewers, uint64_t inputs,
                             uint64_t outputs, const std::string & tags) {
  std::set<APIConn *> toDel;
  for (auto A : reggedStreams) {
    A->stream(stream, status, viewers, inputs, outputs, tags);
    if (!A->C) { toDel.insert(A); }
  }
  for (auto A : toDel) { delete A; }
}

APIConn::APIConn(Event::Loop & evLp, Socket::Server & srv) : E(evLp) {
  authorized = false;
  attempts = 0;
  isLocal = false;
  isWebSocket = false;
  W = 0;
  authTime = 0;
  C = srv.accept(true);

  sock = C.getSocket();
  if (sock >= 1024) {
    FAIL_MSG("New incoming API connection %d rejected: select() only supports file descriptors under 1024", sock);
    delete this;
    return;
  }
  Util::Procs::socketList.insert(sock);
  C.setBlocking(false);
  E.addSendSocket(sock, [](void *c) {
    APIConn *A = (APIConn *)c;
    A->C.send(0, 0);
    if (!A->C) { delete A; }
  }, [](void *c) {
    APIConn *A = (APIConn *)c;
    return A->C.sendingBlocked(1);
  }, this);
  C.onClose([this](int s) { E.remove(s); });
  E.addSocket(sock, [](void *c) {
    APIConn *A = (APIConn *)c;
    if (!Controller::handleAPIConnection(A)) { delete A; }
  }, this);
}

APIConn::~APIConn() {
  if (W) { delete W; }
  C.close();
  Util::Procs::socketList.erase(sock);
  Controller::deregister(this);
  E.remove(sock);
}

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
      LOG_MSG("AUTH", "Failed login attempt %s from %s", UserID.c_str(), conn.getHost().c_str());
    }
  }
  Response["authorize"]["status"] = "CHALL";
  Response["authorize"]["challenge"] = Challenge;
  // the following is used to add the first account through the LSP
  if (!Storage["account"]){
    Response["authorize"]["status"] = "NOACC";
    if (Request["authorize"]["new_username"] && Request["authorize"]["new_password"]){
      // create account
      LOG_MSG("CONF", "Created account %s through API", Request["authorize"]["new_username"].asString().c_str());
      Controller::Storage["account"][Request["authorize"]["new_username"].asString()]["password"] =
          Secure::md5(Request["authorize"]["new_password"].asString());
      Response["authorize"]["status"] = "ACC_MADE";
    }else{
      Response["authorize"].removeMember("challenge");
    }
  }
  return false;
}// Authorize

void APIConn::log(uint64_t time, const std::string & kind, const std::string & message, const std::string & stream,
                  uint64_t progPid, const std::string & exe, const std::string & line) {
  if (!isWebSocket || !W || !*W) { return; }

  // If we have more than ~10k pending bytes to send, stop sending logs
  if (C.sendingBlocked(10000)) { return; }

  JSON::Value tmp;
  tmp[0u] = "log";
  tmp[1u].append(time);
  tmp[1u].append(kind);
  tmp[1u].append(message);
  tmp[1u].append(stream);
  tmp[1u].append(progPid);
  tmp[1u].append(exe);
  tmp[1u].append(line);
  W->sendFrame(tmp.toString());
}

void APIConn::access(uint64_t time, const std::string & session, const std::string & stream, const std::string & connector,
                     const std::string & host, uint64_t duration, uint64_t up, uint64_t down, const std::string & tags) {
  if (!isWebSocket || !W || !*W) { return; }

  // If we have more than ~10k pending bytes to send, stop sending logs
  if (C.sendingBlocked(10000)) { return; }

  JSON::Value tmp;
  tmp[0u] = "access";
  tmp[1u].append(time);
  tmp[1u].append(session);
  tmp[1u].append(stream);
  tmp[1u].append(connector);
  tmp[1u].append(host);
  tmp[1u].append(duration);
  tmp[1u].append(up);
  tmp[1u].append(down);
  tmp[1u].append(tags);
  W->sendFrame(tmp.toString());
}

void APIConn::stream(const std::string & stream, uint8_t status, uint64_t viewers, uint64_t inputs, uint64_t outputs,
                     const std::string & tags) {
  if (!isWebSocket || !W || !*W) { return; }

  // If we have more than ~10k pending bytes to send, stop sending logs
  if (C.sendingBlocked(10000)) { return; }

  JSON::Value tmp;
  tmp[0u] = "stream";
  tmp[1u].append(stream);
  tmp[1u].append(status);
  tmp[1u].append(viewers);
  tmp[1u].append(inputs);
  tmp[1u].append(outputs);
  tmp[1u].append(tags);
  W->sendFrame(tmp.toString());
}

void Controller::handleWebSocket(APIConn *aConn) {
  // Not a websocket yet? Set it up!
  if (!aConn->isWebSocket) {
    aConn->logArg = aConn->H.GetVar("logs");
    aConn->accsArg = aConn->H.GetVar("accs");
    aConn->strmsArg = aConn->H.GetVar("streams");
    HTTP::Parser req = aConn->H;
    aConn->H.Clean();
    aConn->W = new HTTP::Websocket(aConn->C, req, aConn->H);
    if (!aConn->W) {
      FAIL_MSG("Could not allocate WS handler: out of memory");
      aConn->C.close();
      return;
    }
    if (!*(aConn->W)) {
      aConn->C.close();
      return;
    }
    aConn->isWebSocket = true;
    aConn->C.setBlocking(false);
    if (aConn->authorized) {
      aConn->W->sendFrame("[\"auth\", true]");
    } else {
      aConn->W->sendFrame("[\"auth\", false]");
    }
    aConn->authTime = Util::bootMS();
  }

  // No (valid) HTTP::Websocket? Abort and disconnect
  if (!aConn->W || !*(aConn->W)) {
    aConn->isWebSocket = false;
    aConn->C.close();
    return;
  }

  // If we're not authorized (yet), only accept auth commands and nothing else
  if (!aConn->authorized) {
    if (aConn->W->readFrame(true)) {

      // only handle text frames
      if (aConn->W->frameType != 1) { return; }

      // Parse JSON and check command type
      JSON::Value command = JSON::fromString(aConn->W->data, aConn->W->data.size());
      if (command.isArray() && command[0u].asString() == "auth") {
        std::lock_guard<std::mutex> guard(configMutex);
        JSON::Value req;
        req["authorize"] = command[1u];
        aConn->authorized = authorize(req, req, aConn->C);
        aConn->W->sendFrame("[\"auth\", " + req["authorize"].toString() + "]");
      }
    }
    if (Util::bootMS() > aConn->authTime + 10000) {
      aConn->W->sendFrame("[\"auth\",\"Too slow, sorry\"]");
      aConn->C.close();
      return;
    }
    if (!aConn->authorized) { return; }
  }

  // Parse logArg if not yet parsed (set to empty after parsing)
  if (aConn->logArg.size()) {
    // Subscribe to log events
    registerLogger(aConn);
    Util::RelAccX *haveLog = Controller::logAccessor();
    if (haveLog && haveLog->isReady()) {
      Util::RelAccX & rlxLog = *haveLog;
      Util::RelAccXFieldData fTime = rlxLog.getFieldData("time");
      Util::RelAccXFieldData fKind = rlxLog.getFieldData("kind");
      Util::RelAccXFieldData fMsg = rlxLog.getFieldData("msg");
      Util::RelAccXFieldData fStrm = rlxLog.getFieldData("strm");
      Util::RelAccXFieldData fPID = rlxLog.getFieldData("pid");
      Util::RelAccXFieldData fExe = rlxLog.getFieldData("exe");
      Util::RelAccXFieldData fLine = rlxLog.getFieldData("line");

      uint64_t logPos = rlxLog.getEndPos();
      if (aConn->logArg.substr(0, 6) == "since:") {
        uint64_t startLogs = JSON::Value(aConn->logArg.substr(6)).asInt();
        logPos = rlxLog.getDeleted();
        while (logPos < rlxLog.getEndPos() && rlxLog.getInt(fTime, logPos) < startLogs) { ++logPos; }
      } else {
        uint64_t numLogs = JSON::Value(aConn->logArg).asInt();
        if (logPos <= numLogs) {
          logPos = rlxLog.getDeleted();
        } else {
          logPos -= numLogs;
        }
      }

      // Send historical data
      while (rlxLog.getEndPos() > logPos) {
        aConn->log(rlxLog.getInt(fTime, logPos), rlxLog.getPointer(fKind, logPos), rlxLog.getPointer(fMsg, logPos),
                   rlxLog.getPointer(fStrm, logPos), rlxLog.getInt(fPID, logPos), rlxLog.getPointer(fExe, logPos),
                   rlxLog.getPointer(fLine, logPos));
        logPos++;
      }
    }
    aConn->logArg.clear();
  }

  // Parse accsArg if not yet parsed (set to empty after parsing)
  if (aConn->accsArg.size()) {
    // Subscribe to access log events
    registerAccess(aConn);
    Util::RelAccX *haveAccs = Controller::accesslogAccessor();
    if (haveAccs && haveAccs->isReady()) {
      Util::RelAccX & rlxAccs = *haveAccs;
      Util::RelAccXFieldData fTime = rlxAccs.getFieldData("time");
      Util::RelAccXFieldData fSess = rlxAccs.getFieldData("session");
      Util::RelAccXFieldData fStrm = rlxAccs.getFieldData("stream");
      Util::RelAccXFieldData fConn = rlxAccs.getFieldData("connector");
      Util::RelAccXFieldData fHost = rlxAccs.getFieldData("host");
      Util::RelAccXFieldData fDura = rlxAccs.getFieldData("duration");
      Util::RelAccXFieldData fUp = rlxAccs.getFieldData("up");
      Util::RelAccXFieldData fDown = rlxAccs.getFieldData("down");
      Util::RelAccXFieldData fTags = rlxAccs.getFieldData("tags");

      uint64_t accsPos = rlxAccs.getEndPos();
      if (aConn->accsArg.substr(0, 6) == "since:") {
        uint64_t startAccs = JSON::Value(aConn->accsArg.substr(6)).asInt();
        accsPos = rlxAccs.getDeleted();
        while (accsPos < rlxAccs.getEndPos() && rlxAccs.getInt(fTime, accsPos) < startAccs) { ++accsPos; }
      } else {
        uint64_t numAccs = JSON::Value(aConn->accsArg).asInt();
        if (accsPos <= numAccs) {
          accsPos = rlxAccs.getDeleted();
        } else {
          accsPos -= numAccs;
        }
      }

      // Send historical data
      while (rlxAccs.getEndPos() > accsPos) {
        aConn->access(rlxAccs.getInt(fTime, accsPos), rlxAccs.getPointer(fSess, accsPos), rlxAccs.getPointer(fStrm, accsPos),
                      rlxAccs.getPointer(fConn, accsPos), rlxAccs.getPointer(fHost, accsPos), rlxAccs.getInt(fDura, accsPos),
                      rlxAccs.getInt(fUp, accsPos), rlxAccs.getInt(fDown, accsPos), rlxAccs.getPointer(fTags, accsPos));
        accsPos++;
      }
    }
    aConn->accsArg.clear();
  }

  if (aConn->strmsArg.size()) {
    registerStreams(aConn);
    Util::RelAccX *haveStrms = Controller::streamsAccessor();
    if (haveStrms && haveStrms->isReady()) {
      Util::RelAccX & rlxStreams = *haveStrms;
      Util::RelAccXFieldData fStrm = rlxStreams.getFieldData("stream");
      Util::RelAccXFieldData fStat = rlxStreams.getFieldData("status");
      Util::RelAccXFieldData fView = rlxStreams.getFieldData("viewers");
      Util::RelAccXFieldData fInpt = rlxStreams.getFieldData("inputs");
      Util::RelAccXFieldData fOutp = rlxStreams.getFieldData("outputs");
      Util::RelAccXFieldData fTags = rlxStreams.getFieldData("tags");

      uint64_t startPos = rlxStreams.getDeleted();
      uint64_t endPos = rlxStreams.getEndPos();
      for (uint64_t cPos = startPos; cPos < endPos; ++cPos) {
        aConn->stream(rlxStreams.getPointer(fStrm, cPos), rlxStreams.getInt(fStat, cPos), rlxStreams.getInt(fView, cPos),
                      rlxStreams.getInt(fInpt, cPos), rlxStreams.getInt(fOutp, cPos), rlxStreams.getPointer(fTags, cPos));
      }
    }
    aConn->strmsArg.clear();
  }

  // Ignore any incoming frames
  while (aConn->W->readFrame(true)) {}
}

/// Handles a single incoming API connection.
/// Assumes the connection is unauthorized and will allow for 4 requests without authorization before disconnecting.
bool Controller::handleAPIConnection(APIConn *aConn) {
  if (aConn->isWebSocket) {
    handleWebSocket(aConn);
    if (aConn->isWebSocket) { return aConn->C; }
  }
  while (aConn->C.spool() && aConn->C.Received().size() && aConn->H.Read(aConn->C)) {
    // Are we local and not forwarded? Instant-authorized.
    if (!aConn->authorized && !aConn->H.hasHeader("X-Forwarded-For") && !aConn->H.hasHeader("X-Real-IP") && aConn->C.isLocal()) {
      MEDIUM_MSG("Local API access automatically authorized");
      aConn->isLocal = true;
      aConn->authorized = true;
    }
#ifdef NOAUTH
    // If auth is disabled, always allow access.
    aConn->authorized = true;
#endif
    if (!aConn->authorized && aConn->H.hasHeader("Authorization")) {
      std::string auth = aConn->H.GetHeader("Authorization");
      if (auth.substr(0, 5) == "json ") {
        INFO_MSG("Checking auth header");
        JSON::Value req;
        req["authorize"] = JSON::fromString(auth.substr(5));
        if (Storage["account"]) {
          std::lock_guard<std::mutex> guard(configMutex);
          if (!Controller::conf.is_active) { return 0; }
          aConn->authorized = authorize(req, req, aConn->C);
          if (!aConn->authorized) {
            aConn->H.Clean();
            aConn->H.body = "Please login first or provide a valid token authentication.";
            aConn->H.SetHeader("Server", APPIDENT);
            aConn->H.SetHeader("WWW-Authenticate", "json " + req["authorize"].toString());
            aConn->H.SendResponse("403", "Not authorized", aConn->C);
            aConn->H.Clean();
            continue;
          }
        }
      }
    }
    // Catch websocket requests
    if (aConn->H.url == "/ws") {
      handleWebSocket(aConn);
      return aConn->C;
    }
    // Catch prometheus requests
    if (Controller::prometheus.size()) {
      if (aConn->H.url == "/" + Controller::prometheus) {
        handlePrometheus(aConn->H, aConn->C, PROMETHEUS_TEXT);
        aConn->H.Clean();
        continue;
      }
      if (aConn->H.url.substr(0, Controller::prometheus.size() + 6) == "/" + Controller::prometheus + ".json") {
        handlePrometheus(aConn->H, aConn->C, PROMETHEUS_JSON);
        aConn->H.Clean();
        continue;
      }
    }
    JSON::Value Response;
    JSON::Value Request;
    std::string reqContType = aConn->H.GetHeader("Content-Type");
    if (reqContType == "application/json") {
      Request = JSON::fromString(aConn->H.body);
    } else {
      Request = JSON::fromString(aConn->H.GetVar("command"));
    }
    // invalid request? send the web interface, unless requested as "/api"
    if (!Request.isObject() && aConn->H.url != "/api" && aConn->H.url != "/api2") {
#include "server.html.h"
      aConn->H.Clean();
      aConn->H.SetHeader("Content-Type", "text/html");
      aConn->H.SetHeader("X-Info", "To force an API response, request the file /api");
      aConn->H.SetHeader("Server", APPIDENT);
      aConn->H.SetHeader("Content-Length", server_html_len);
      aConn->H.SetHeader("X-UA-Compatible", "IE=edge;chrome=1");
      aConn->H.SendResponse("200", "OK", aConn->C);
      aConn->C.SendNow(server_html, server_html_len);
      aConn->H.Clean();
      break;
    }
    if (aConn->H.url == "/api2") { Request["minimal"] = true; }
    { // lock the config mutex here - do not unlock until done processing
      std::lock_guard<std::mutex> guard(configMutex);
      if (!Controller::conf.is_active) { return 0; }
      // if already authorized, do not re-check for authorization
      if (aConn->authorized && Storage["account"]) {
        Response["authorize"]["status"] = "OK";
        if (aConn->isLocal) { Response["authorize"]["local"] = true; }
      } else {
        aConn->authorized |= authorize(Request, Response, aConn->C);
      }
      if (aConn->authorized) {
        handleAPICommands(Request, Response);
        if (Request.isMember("logout")) { aConn->authorized = false; }
      }
    } // config mutex lock
    if (!aConn->authorized) { aConn->attempts++; }
    // send the response, either normally or through JSONP callback.
    std::string jsonp = "";
    if (aConn->H.GetVar("callback") != "") { jsonp = aConn->H.GetVar("callback"); }
    if (aConn->H.GetVar("jsonp") != "") { jsonp = aConn->H.GetVar("jsonp"); }
    aConn->H.Clean();
    aConn->H.SetHeader("Content-Type", "text/javascript");
    aConn->H.setCORSHeaders();
    if (jsonp == "") {
      aConn->H.SetBody(Response.toString() + "\n\n");
    } else {
      aConn->H.SetBody(jsonp + "(" + Response.toString() + ");\n\n");
    }
    aConn->H.SendResponse("200", "OK", aConn->C);
    aConn->H.Clean();
  }
  return aConn->C;
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
    if (statUp.isMember("id") && statUp.isMember("status")) { setPushStatus(statUp); }
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

  /// Scope for replacing keys, adding keys and deleting keys, accepts single or arrays of keys
  if (Request.isMember("jwks") || Request.isMember("addjwks") || Request.isMember("deletejwks")) {
    JSON::Value in, &out = Controller::Storage["jwks"], *response = 0;

    /// Helper function for setting default permissions
    auto setDefaultPerms = [&](JSON::Value & perms) {
      if (!perms.isMember("input")) perms["input"] = JWK_DFLT_INPUT;
      if (!perms.isMember("output")) perms["output"] = JWK_DFLT_OUTPUT;
      if (!perms.isMember("admin")) perms["admin"] = JWK_DFLT_ADMIN;
      if (!perms.isMember("stream")) perms["stream"] = JWK_DFLT_STREAM;
    };

    /// Define a lambda for adding or deleting keys so we can deal with nested structures
    std::function<void(const JSON::Value &, JSON::Value &)> parseKeys;
    parseKeys = [&parseKeys, &response, &setDefaultPerms](const JSON::Value & in, JSON::Value & target) {
      // Lambda to check for endpoint as elements or object itself may be a URL to an endpoint
      auto addEndpoint = [&target, &response](const JSON::Value & jwk) {
        std::string url = (jwk.isArray() && jwk.size()) ? jwk[0u].asStringRef() : jwk.asStringRef();
        if (url.find("://") != std::string::npos) {
          JSON::Value writeOut;
          if (jwk.isArray() && jwk.size() > 1) {
            writeOut.append(url);
            writeOut.append(jwk[1u]);
          } else {
            writeOut = url;
          }

          // Append the result to both the target (will be written to storage) and API response
          bool dupe = false;
          jsonForEachConst (target, urlMaybePerms) {
            JSON::Value urlNoPerms = urlMaybePerms->isArray() ? (*urlMaybePerms)[0u] : *urlMaybePerms;
            if (url != urlNoPerms.asStringRef()) continue;
            dupe = true;
            break;
          }

          if (!dupe) {
            target.append(writeOut);
            response->append(writeOut);
          }
          return true;
        }
        return false;
      };

      // Check if the JSON value can be added to storage as string containing a URL
      if (in.isArray() && in.size() == 1 && addEndpoint(in)) return;

      // Wrap the object into an array so it is parsed correctly
      if (in.isObject() && !in.isMember("keys")) {
        JSON::Value wrapped;
        wrapped.append(in);
        parseKeys(wrapped, target);
        return;
      }

      jsonForEachConst (in, it) {
        // May be a URL to an endpoint, see if we can add it as URL
        if (addEndpoint(*it)) continue;

        // Check for nested structures of keys with or without permissions and call recursively if found
        if ((it->isArray() && (*it)[0u].isObject() && (*it)[0u].isMember("keys") && (*it)[0u]["keys"].isArray()) ||
            (it->isObject() && it->isMember("keys") && (*it)["keys"].isArray())) {
          const JSON::Value & keyStore = it->isArray() ? (*it)[0u]["keys"] : (*it)["keys"];
          const JSON::Value & perms = (it->isArray() && it->size() > 1 && (*it)[1u].isObject()) ? (*it)[1u] : JSON::Value();

          jsonForEachConst (keyStore, jwk) {
            if (!perms.isNull()) {
              JSON::Value jwkWithPerms;
              jwkWithPerms[0u] = *jwk;
              jwkWithPerms[1u] = perms;
              parseKeys(jwkWithPerms, target);
            } else {
              parseKeys(*jwk, target);
            }
          }
          continue;
        }

        // May be a key object or array with key object and permissions object
        if (!it->isMember("kty") && !(*it)[0u].isMember("kty")) continue;
        const JSON::Value jwkIn = (it->isArray() && it->size()) ? (*it)[0u] : *it;
        const JSON::Value prmIn = (it->isArray() && it->size()) ? (*it)[1u] : JSON::EMPTY;
        JSON::Value jwkOut, prmOut, writeOut;

        std::set<std::string> unique;
        const std::string & kty = jwkIn["kty"].asStringRef();
        switch (kty.at(0)) {
          case 'o':
            jwkOut.extend(jwkIn, emptyset, {"k"});
            unique = {"k"};
            break;
          case 'R':
            jwkOut.extend(jwkIn, emptyset, {"n", "e", "d", "p", "q", "dp", "dq", "qi"});
            unique = {"n"};
            break;
          case 'E':
            jwkOut.extend(jwkIn, emptyset, {"crv", "x", "y", "d"});
            unique = {"x", "y"};
            break;
          default: continue;
        }
        jwkOut.extend(jwkIn, emptyset, {"kty", "use", "key_ops", "alg", "kid", "x5u", "x5c", "x5t", "x5t#S256"});

        // Detect duplicates in both the storage and the response
        bool hasKid = jwkOut.isMember("kid"), checkedDuplicates = false;
        jsonForEach (target, jwkWithPerms) {
          const JSON::Value jwk = (*jwkWithPerms)[0u];
          if ((hasKid && jwk["kid"] == jwkOut["kid"]) || jwk.compareOnly(jwkOut, unique)) {
            jwkWithPerms.remove();
            if (checkedDuplicates) continue;
            jsonForEach (*response, jwkResWithPerms) {
              const JSON::Value jwkRes = (*jwkResWithPerms)[0u];
              if ((hasKid && jwk["kid"] == jwkRes["kid"]) || jwk.compareOnly(jwkRes, unique)) {
                jwkResWithPerms.remove();
                break;
              }
            }
            checkedDuplicates = true;
          }
        }

        // Do not write anything if this is a duplicate, otherwise start setting permissions
        prmOut.extend(prmIn, emptyset, {"input", "output", "admin", "stream"});

        // Append the short result to the target, this will be written to storage
        writeOut.append(jwkOut);
        writeOut.append(prmOut);
        target.append(writeOut);

        // Set default permissions if they are missing for the API response
        setDefaultPerms(prmOut);

        // Append the long result to the API response
        writeOut.null();
        writeOut.append(jwkOut);
        writeOut.append(prmOut);
        response->append(writeOut);
      }
    };

    /// Set the key(s) to be used with the JWTs; this replaces all stored keys
    if (Request.isMember("jwks")) {
      response = &Response["jwks"];
      // When the request is not an array just return the stored list
      if (!Request["jwks"].isArray() && Request["jwks"].asStringRef().find("://") == std::string::npos) {
        jsonForEachConst (out, it) {
          const JSON::Value jwk = (it->isArray() && it->size()) ? (*it)[0u] : *it;
          JSON::Value write, prm = (it->isArray() && it->size()) ? (*it)[1u] : JSON::EMPTY;
          setDefaultPerms(prm);
          write.append(jwk);
          write.append(prm);
          Response["jwks"].append(write);
        }
      } else {
        // Call the lambda with request as input and a new JSON array as output
        in = Request["jwks"];
        JSON::Value target;
        parseKeys(in, target);
        out = target;
      }
    }

    /// Remove keys by id, full jwk, url, or array, in an array either with or without permissions
    if (Request.isMember("deletejwks")) {
      in = Request["deletejwks"];
      response = &Response["deletejwks"];

      // If the object is a key with a type move it into an array
      if (in.isObject() && in.isMember("kty")) in.append((JSON::Value)in);

      // For URLs simply move the JSON into a JSON array, otherwise assume its a key id
      if (in.isString()) {
        if (in.asStringRef().find("://") != std::string::npos)
          in.append((JSON::Value)in);
        else
          in = JSON::fromString(R"([{"kid": ")" + in.asStringRef() + R"("}])");
      }

      // Iterate over the array and remove the first matching key, entries may be keys or key, perms
      if (in.isArray()) {
        jsonForEachConst (in, iter) {
          const JSON::Value & iterIn = (iter->isArray() && (*iter)[1u].size()) ? (*iter)[0u] : *iter;
          bool removed = false;
          // Key ids are UUIDs and if it is set in the object we just delete the key that matches it
          if (iterIn.isMember("kid") && iterIn["kid"].asStringRef().size()) {
            jsonForEach (out, iterOut) {
              if ((*iterOut).isMember("kid")) {
                if (iterIn["kid"] == (*iterOut)["kid"]) {
                  HIGH_MSG("Removing key with ID %s", iterIn["kid"].asStringRef().c_str());
                  iterOut.remove();
                  removed = true;
                  break;
                }
              } else if (iterOut->isArray() && (*iterOut)[0u].isMember("kid")) {
                if (iterIn["kid"] == (*iterOut)[0u]["kid"]) {
                  HIGH_MSG("Removing key-perms array with ID %s", iterIn["kid"].asStringRef().c_str());
                  iterOut.remove();
                  removed = true;
                  break;
                }
              }
            }
          } else {
            // Otherwise we search for full keys or URLs and only delete on an exact match
            jsonForEach (out, iterOut) {
              if (iterIn == (*iterOut) || (iterOut->isArray() && iterIn == (*iterOut)[0u])) {
                HIGH_MSG("Removing key object %s", iterIn.toString().c_str());
                iterOut.remove();
                removed = true;
                break;
              }
            }
          }
          // If this entry could not be removed add it to the response
          if (removed) response->append(iterIn);
        }
      }
    }

    /// Add keys to the keystore which is stored by the controller
    if (Request.isMember("addjwks")) {
      response = &Response["addjwks"];
      // Set response and always create an array if the request is not already one
      if (Request["addjwks"].isArray())
        in = Request["addjwks"];
      else
        in.append(Request["addjwks"]);
      parseKeys(in, out);
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
    if (Request["capabilities"].isString()){
      Response["capabilities"].null();
      const std::string & tmpFn = Request["capabilities"].asStringRef();
      jsonForEachConst(capabilities["inputs"], it){
        if (it->isMember("dynamic_capa")){
          std::string source = (*it)["source_match"].asStringRef();
          std::string front = source.substr(0, source.find('*'));
          std::string back = source.substr(source.find('*') + 1);
          if (tmpFn.size() >= front.size()+back.size() && tmpFn.substr(0, front.size()) == front && tmpFn.substr(tmpFn.size() - back.size()) == back){
            std::string arg_one = Util::getMyPath() + "MistIn" + it.key();
            char const *conn_args[] ={0, "--getcapa", 0, 0};
            conn_args[0] = arg_one.c_str();
            conn_args[2] = Request["capabilities"].asStringRef().c_str();
            configMutex.unlock();
            Response["capabilities"] = JSON::fromString(Util::Procs::getOutputOf((char **)conn_args));
            configMutex.lock();
            break;
          }
        }
      }
    }else{
      Controller::checkCapable(capabilities);
      Response["capabilities"] = capabilities;
    }
  }

  // Current load (partial data from capabilities)
  if (Request.isMember("load")) { Controller::checkCapable(Response["load"], true); }

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

  if (Request.isMember("streamkeys")) {
    JSON::Value & keys = Controller::Storage["streamkeys"];
    if (Request["streamkeys"].isObject()) {
      keys.null();
      jsonForEachConst (Request["streamkeys"], it) {
        if (it->isString()) { keys[it.key()] = it->asStringRef(); }
      }
    }
  }

  if (Request.isMember("streamkey_del")) {
    const JSON::Value & del = Request["streamkey_del"];
    JSON::Value & rep = Response["streamkey_del"]["deleted"];
    JSON::Value & keys = Controller::Storage["streamkeys"];
    if (del.isObject()) {
      jsonForEachConst (del, it) {
        if (keys.isMember(it.key()) && keys[it.key()].asStringRef() == it->asString()) {
          keys.removeMember(it.key());
          rep.append(it.key());
        }
      }
    } else if (del.isArray()) {
      jsonForEachConst (del, it) {
        if (it->isString() && keys.isMember(it->asStringRef())) {
          keys.removeMember(it->asStringRef());
          rep.append(it->asStringRef());
        }
      }
    } else if (del.isString()) {
      if (keys.isMember(del.asStringRef())) {
        keys.removeMember(del.asStringRef());
        rep.append(del.asStringRef());
      }
    } else {
      Response["streamkey_del"]["error"] = "streamkey_del must be a string, object or array";
    }
  }

  if (Request.isMember("streamkey_add")) {
    const JSON::Value & add = Request["streamkey_add"];
    if (add.isObject()) {
      JSON::Value & keys = Controller::Storage["streamkeys"];
      JSON::Value & rep = Response["streamkey_add"]["added"];
      jsonForEachConst (add, it) {
        if (it->isString() && (!keys.isMember(it.key()) || keys[it.key()].asStringRef() != it->asStringRef())) {
          keys[it.key()] = it->asStringRef();
          rep.append(it.key());
        }
      }
    } else {
      Response["streamkey_add"]["error"] = "streamkey_add must be an object";
    }
  }

  if (Request.isMember("streamkeys")) { Response["streamkeys"] = Controller::Storage["streamkeys"]; }

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
  if (Request.isMember("autoupdate")) { Controller::rollingUpdate(); }
  if (Request.isMember("abortupdate")) { Controller::abortUpdate(); }
  if (Request.isMember("checkupdate")) { Controller::updaterCheck(); }
  if (Request.isMember("update") || Request.isMember("checkupdate") || Request.isMember("autoupdate") ||
      Request.isMember("abortupdate")) {
    Controller::insertUpdateInfo(Response["update"]);
  }
#endif

  if (Request.isMember("version")){
    Response["version"]["version"] = PACKAGE_VERSION;
    Response["version"]["release"] = RELEASE;
    Response["version"]["date"] = __DATE__;
    Response["version"]["time"] = __TIME__;
  }

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
    std::lock_guard<std::mutex> guard(logMutex);
    if (!Controller::conf.is_active){return;}
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
    url.host = Controller::conf.boundServer.host();
    if (url.host == "::"){url.host = "[::1]";}
    if (url.host == "0.0.0.0"){url.host = "127.0.0.1";}
    url.setPort(Controller::conf.boundServer.port());
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

  if (Request.isMember("tag_stream")){
    if (Request["tag_stream"].isObject()){
      jsonForEach(Request["tag_stream"], it){
        if (it->isString()){
          Controller::stream_tag(it.key(), it->asStringRef());
        }else if (it->isArray()){
          jsonForEach(*it, jt){
            if (jt->isString()){
              Controller::stream_tag(it.key(), jt->asStringRef());
            }
          }
        }
      }
    }
  }

  if (Request.isMember("untag_stream")){
    if (Request["untag_stream"].isObject()){
      jsonForEach(Request["untag_stream"], it){
        if (it->isString()){
          Controller::stream_untag(it.key(), it->asStringRef());
        }else if (it->isArray()){
          jsonForEach(*it, jt){
            if (jt->isString()){
              Controller::stream_untag(it.key(), jt->asStringRef());
            }
          }
        }
      }
    }
  }

  if (Request.isMember("stream_tags")){
    JSON::Value & rT = Response["stream_tags"];
    if (Request["stream_tags"].isArray()){
      jsonForEach(Request["stream_tags"], it){
        if (it->isString()){
          std::set<std::string> tags = Controller::stream_tags(it->asStringRef());
          JSON::Value & tRef = rT[it->asStringRef()];
          for (std::set<std::string>::iterator ti = tags.begin(); ti != tags.end(); ++ti){tRef.append(*ti);}
        }
      }
    }else if (Request["stream_tags"].isObject()){
      jsonForEach(Request["stream_tags"], it){
        std::set<std::string> tags = Controller::stream_tags(it.key());
        JSON::Value & tRef = rT[it.key()];
        for (std::set<std::string>::iterator ti = tags.begin(); ti != tags.end(); ++ti){tRef.append(*ti);}
      }
    }else if (Request["stream_tags"].isString() && Request["stream_tags"].asStringRef().size()){
      std::set<std::string> tags = Controller::stream_tags(Request["stream_tags"].asStringRef());
      JSON::Value & tRef = rT[Request["stream_tags"].asStringRef()];
      for (std::set<std::string>::iterator ti = tags.begin(); ti != tags.end(); ++ti){tRef.append(*ti);}
    }else{
      JSON::Value nullPkt, resp;
      Controller::fillActive(nullPkt, resp);
      jsonForEach(resp, it){
        std::set<std::string> tags = Controller::stream_tags(it->asStringRef());
        JSON::Value & tRef = rT[it->asStringRef()];
        for (std::set<std::string>::iterator ti = tags.begin(); ti != tags.end(); ++ti){tRef.append(*ti);}
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
    if (stream.size()) {
      if (stream[0] == '#') {
        std::set<std::string> activeStreams = Controller::getActiveStreams(stream);
        jsonForEachConst (Storage["streams"], strmIt) {
          if (Controller::streamMatches(strmIt.key(), stream)) {
            activeStreams.insert(strmIt.key());
          }
        }
        if (activeStreams.size()) {
          for (std::set<std::string>::iterator jt = activeStreams.begin(); jt != activeStreams.end(); ++jt) {
            std::string streamname = *jt;
            std::string target_tmp = target;
            startPush(streamname, target_tmp);
          }
        }
      } else {
        Util::sanitizeName(stream);
        if (stream.size()) {
          if (*stream.rbegin() != '+') {
            startPush(stream, target);
          } else {
            std::set<std::string> activeStreams = Controller::getActiveStreams(stream);
            if (activeStreams.size()) {
              for (std::set<std::string>::iterator jt = activeStreams.begin();
                   jt != activeStreams.end(); ++jt) {
                std::string streamname = *jt;
                std::string target_tmp = target;
                startPush(streamname, target_tmp);
              }
            }
          }
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
    } else if (Request["push_stop"].isString()) {
      Controller::stopPush(Request["push_stop"].asStringRef());
    } else {
      Controller::stopPush(Request["push_stop"].asInt());
    }
  }

  // This `push_stop_graceful` command was added to allow outputs
  // to gracefully disconnect from their remote server. For
  // example, when used for an outgoing RTMP push, we will
  // correctly delete the stream before closing the socket.
  if (Request.isMember("push_stop_graceful")) {
    if (Request["push_stop_graceful"].isArray()) {
      jsonForEach (Request["push_stop_graceful"], it) { Controller::stopPushGraceful(it->asInt()); }
    } else if (Request["push_stop_graceful"].isString()) {
      Controller::stopPushGraceful(Request["push_stop_graceful"].asStringRef());
    } else {
      Controller::stopPushGraceful(Request["push_stop_graceful"].asInt());
    }
  }

  if (Request.isMember("push_auto_add")){Controller::addPush(Request["push_auto_add"], Response);}
  if (Request.isMember("push_auto_remove")){
    Controller::removePush(Request["push_auto_remove"]);
  }
  if (Request.isMember("push_auto_list") || Request.isMember("push_auto_add") || Request.isMember("push_auto_remove")){
    Response["auto_push"] = Controller::Storage["auto_push"];
    JSON::Value & R = Response["push_auto_list"];
    jsonForEachConst(Response["auto_push"], it){
      if (!it->isMember("stream") || !it->isMember("target")){continue;}
      JSON::Value tmp;
      tmp.append((*it)["stream"]);
      tmp.append((*it)["target"]);
      if (it->isMember("scheduletime") || it->isMember("completetime") || it->isMember("start_rule") || it->isMember("end_rule")){
        if (it->isMember("scheduletime")){
          tmp.append((*it)["scheduletime"]);
        }else{
          tmp.append(JSON::Value());
        }
        if (it->isMember("completetime") || it->isMember("start_rule") || it->isMember("end_rule")){
          if (it->isMember("completetime")){
            tmp.append((*it)["completetime"]);
          }else{
            tmp.append(JSON::Value());
          }
          if (it->isMember("start_rule") || it->isMember("end_rule")){
            if (it->isMember("start_rule")){
              tmp.append((*it)["start_rule"][0u]);
              tmp.append((*it)["start_rule"][1u]);
              tmp.append((*it)["start_rule"][2u]);
            }else{
              tmp.append(JSON::Value());
              tmp.append(JSON::Value());
              tmp.append(JSON::Value());
            }
            if (it->isMember("end_rule")){
              tmp.append((*it)["end_rule"][0u]);
              tmp.append((*it)["end_rule"][1u]);
              tmp.append((*it)["end_rule"][2u]);
            }
          }
        }
      }
      R.append(tmp);
    }

  }
  if (Request.isMember("push_settings")){
    Controller::pushSettings(Request["push_settings"], Response["push_settings"]);
  }

  // Variable related commands
  if (Request.isMember("variable_list")) { Response["variable_list"] = Storage["variables"]; }
  if (Request.isMember("variable_add")){Controller::addVariable(Request["variable_add"], Response["variable_list"]);}
  if (Request.isMember("variable_remove")){Controller::removeVariable(Request["variable_remove"], Response["variable_list"]);}

  // External writer related commands
  if (Request.isMember("external_writer_remove")){Controller::removeExternalWriter(Request["external_writer_remove"]);}
  if (Request.isMember("external_writer_add")){Controller::addExternalWriter(Request["external_writer_add"]);}
  if (Request.isMember("external_writer_remove") || Request.isMember("external_writer_add") || 
      Request.isMember("external_writer_list")){
    Controller::listExternalWriters(Response["external_writer_list"]);
  }

  if (Request.isMember("enumerate_sources")){
    if (!Request["enumerate_sources"].isString()){
      Response["enumerate_sources"].null();
    }else{
      jsonForEachConst(capabilities["inputs"], it){
        if (it->isMember("enum_static_prefix") && (*it)["enum_static_prefix"].asStringRef().size() <= Request["enumerate_sources"].asStringRef().size() && Request["enumerate_sources"].asStringRef().substr(0, (*it)["enum_static_prefix"].asStringRef().size()) == (*it)["enum_static_prefix"].asStringRef()){
          std::string arg_one = Util::getMyPath() + "MistIn" + it.key();
          char const *conn_args[] ={0, "--enumerate", 0, 0};
          conn_args[0] = arg_one.c_str();
          conn_args[2] = Request["enumerate_sources"].asStringRef().c_str();
          configMutex.unlock();
          Response["enumerate_sources"] = JSON::fromString(Util::Procs::getOutputOf((char **)conn_args));
          configMutex.lock();
          break;
        }
      }
    }
  }

  Controller::writeConfig();

  if (Request.isMember("save")){
    LOG_MSG("CONF", "Writing config to file on request through API");
    Controller::writeConfigToDisk(true);
  }

}
