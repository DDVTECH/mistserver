#include <dirent.h> //for browse API call
#include <sys/stat.h> //for browse API call
#include <mist/http_parser.h>
#include <mist/auth.h>
#include <mist/stream.h>
#include <mist/config.h>
#include <mist/defines.h>
#include <mist/timing.h>
#include <mist/procs.h>
#include "controller_api.h"
#include "controller_storage.h"
#include "controller_streams.h"
#include "controller_connectors.h"
#include "controller_capabilities.h"
#include "controller_statistics.h"
/*LTS-START*/
#include "controller_updater.h"
#include "controller_limits.h"
#include "controller_push.h"
#include "controller_license.h"
/*LTS-END*/

///\brief Checks an authorization request for a given user.
///\param Request The request to be parsed.
///\param Response The location to store the generated response.
///\param conn The user to be checked for authorization.
///\return True on successfull authorization, false otherwise.
///
/// \api
/// To login, an `"authorize"` request must be sent. Since HTTP does not use persistent connections, you are required to re-sent authentication with every API request made. To prevent plaintext sending of the password, a random challenge string is sent first, and then the password is hashed together with this challenge string to create a one-time-use string to login with.
/// If the user is not authorized, this request is the only request the server will respond to until properly authorized.
/// `"authorize"` requests take the form of:
/// ~~~~~~~~~~~~~~~{.js}
/// {
///   //username to login as
///   "username": "test",
///   //hash of password to login with. Send empty value when no challenge for the hash is known yet.
///   //When the challenge is known, the value to be used here can be calculated as follows:
///   //   MD5( MD5("secret") + challenge)
///   //Where "secret" is the plaintext password.
///   "password": ""
/// }
/// ~~~~~~~~~~~~~~~
/// and are responded to as:
/// ~~~~~~~~~~~~~~~{.js}
/// {
///   //current login status. Either "OK", "CHALL", "NOACC" or "ACC_MADE".
///   "status": "CHALL",
///   //Random value to be used in hashing the password.
///   "challenge": "abcdef1234567890"
/// }
/// ~~~~~~~~~~~~~~~
/// The challenge string is sent for all statuses, except `"NOACC"`, where it is left out.
/// A status of `"OK"` means you are currently logged in and have access to all other API requests.
/// A status of `"CHALL"` means you are not logged in, and a challenge has been provided to login with.
/// A status of `"NOACC"` means there are no valid accounts to login with. In this case - and ONLY in this case - it is possible to create a initial login through the API itself. To do so, send a request as follows:
/// ~~~~~~~~~~~~~~~{.js}
/// {
///   //username to create, as plain text
///   "new_username": "test",
///   //password to set, as plain text
///   "new_password": "secret"
/// }
/// ~~~~~~~~~~~~~~~
/// Please note that this is NOT secure. At all. Never use this mechanism over a public network!
/// A status of `"ACC_MADE"` indicates the account was created successfully and can now be used to login as normal.
bool Controller::authorize(JSON::Value & Request, JSON::Value & Response, Socket::Connection & conn){
  #ifdef NOAUTH
  Response["authorize"]["status"] = "OK";
  return true;
  #endif
  time_t Time = time(0);
  tm * TimeInfo = localtime( &Time);
  std::stringstream Date;
  std::string retval;
  Date << TimeInfo->tm_mday << "-" << TimeInfo->tm_mon << "-" << TimeInfo->tm_year + 1900;
  std::string Challenge = Secure::md5(Date.str().c_str() + conn.getHost());
  if (Request.isMember("authorize") && Request["authorize"]["username"].asString() != ""){
    std::string UserID = Request["authorize"]["username"];
    if (Storage["account"].isMember(UserID)){
      if (Secure::md5(Storage["account"][UserID]["password"].asString() + Challenge) == Request["authorize"]["password"].asString()){
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
  //the following is used to add the first account through the LSP
  if (!Storage["account"]){
    Response["authorize"]["status"] = "NOACC";
    if (Request["authorize"]["new_username"] && Request["authorize"]["new_password"]){
      //create account
      Controller::Log("CONF", "Created account " + Request["authorize"]["new_username"].asString() + " through API");
      Controller::Storage["account"][Request["authorize"]["new_username"].asString()]["password"] = Secure::md5(Request["authorize"]["new_password"].asString());
      Response["authorize"]["status"] = "ACC_MADE";
    }else{
      Response["authorize"].removeMember("challenge");
    }
  }
  return false;
}//Authorize

/// Handles a single incoming API connection.
/// Assumes the connection is unauthorized and will allow for 4 requests without authorization before disconnecting.
int Controller::handleAPIConnection(Socket::Connection & conn){
  //set up defaults
  unsigned int logins = 0;
  bool authorized = false;
  HTTP::Parser H;
  //while connected and not past login attempt limit
  while (conn && logins < 4){
    if ((conn.spool() || conn.Received().size()) && H.Read(conn)){
      //Catch prometheus requests
      if (Controller::prometheus.size()){
        if (H.url == "/"+Controller::prometheus){
          handlePrometheus(H, conn, PROMETHEUS_TEXT);
          continue;
        }
        if (H.url == "/"+Controller::prometheus+".json"){
          handlePrometheus(H, conn, PROMETHEUS_JSON);
          continue;
        }
      }
      JSON::Value Response;
      JSON::Value Request = JSON::fromString(H.GetVar("command"));
      //invalid request? send the web interface, unless requested as "/api"
      if ( !Request.isObject() && H.url != "/api" && H.url != "/api2"){
        #include "server.html.h"
        H.Clean();
        H.SetHeader("Content-Type", "text/html");
        H.SetHeader("X-Info", "To force an API response, request the file /api");
        H.SetHeader("Server", "MistServer/" PACKAGE_VERSION);
        H.SetHeader("Content-Length", server_html_len);
        H.SetHeader("X-UA-Compatible","IE=edge;chrome=1");
        H.SendResponse("200", "OK", conn);
        conn.SendNow(server_html, server_html_len);
        H.Clean();
        break;
      }
      if (H.url == "/api2"){
        Request["minimal"] = true;
      }
      {//lock the config mutex here - do not unlock until done processing
        tthread::lock_guard<tthread::mutex> guard(configMutex);
        //Are we local and not forwarded? Instant-authorized.
        if (!authorized && !H.hasHeader("X-Real-IP") && conn.isLocal()){
          INFO_MSG("Local API access automatically authorized");
          authorized = true;
        }
        //if already authorized, do not re-check for authorization
        if (authorized && Storage["account"]){
          Response["authorize"]["status"] = "OK";
        }else{
          authorized |= authorize(Request, Response, conn);
        }
        if (authorized){
          handleAPICommands(Request, Response);
        }else{//unauthorized
          Util::sleep(1000);//sleep a second to prevent bruteforcing 
          logins++;
        }
        Controller::checkServerLimits(); /*LTS*/
      }//config mutex lock
      //send the response, either normally or through JSONP callback.
      std::string jsonp = "";
      if (H.GetVar("callback") != ""){
        jsonp = H.GetVar("callback");
      }
      if (H.GetVar("jsonp") != ""){
        jsonp = H.GetVar("jsonp");
      }
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
    }//if HTTP request received
  }//while connected
  return 0;
}

void Controller::handleUDPAPI(void * np){
  Socket::UDPConnection uSock(true);
  if (!uSock.bind(4242, "localhost")){
    FAIL_MSG("Could not open local API UDP socket - not all functionality will be available");
    return;
  }
  Util::Procs::socketList.insert(uSock.getSock());
  while (Controller::conf.is_active){
    if (uSock.Receive()){
      INFO_MSG("UDP API: %s", uSock.data);
      JSON::Value Request = JSON::fromString(uSock.data, uSock.data_len);
      Request["minimal"] = true;
      JSON::Value Response;
      if (Request.isObject()){
        handleAPICommands(Request, Response);
      }else{
        WARN_MSG("Invalid API command received over UDP: %s", uSock.data);
      }
    }else{
      Util::sleep(500);
    }
  }
}

/// Local-only helper function that checks for duplicate protocols and removes them
static void removeDuplicateProtocols(){
  JSON::Value & P = Controller::Storage["config"]["protocols"];
  jsonForEach(P, it){
    it->removeNullMembers();
  }
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

void Controller::handleAPICommands(JSON::Value & Request, JSON::Value & Response){
  //Parse config and streams from the request.
  if (Request.isMember("config") && Request["config"].isObject()){
    const JSON::Value & in = Request["config"];
    JSON::Value & out = Controller::Storage["config"];
    if (in.isMember("debug")){
      out["debug"] = in["debug"];
      if (Util::Config::printDebugLevel != (out["debug"].isInt()?out["debug"].asInt():DEBUG)){
        Util::Config::printDebugLevel = (out["debug"].isInt()?out["debug"].asInt():DEBUG);
        INFO_MSG("Debug level set to %u", Util::Config::printDebugLevel);
      }
    }
    if (in.isMember("protocols")){
      out["protocols"] = in["protocols"];
      removeDuplicateProtocols();
    }
    if (in.isMember("controller")){
      out["controller"] = in["controller"];
    }
    if (in.isMember("serverid")){
      out["serverid"] = in["serverid"];
    }
    if (in.isMember("triggers")){
      out["triggers"] = in["triggers"];
      if (!out["triggers"].isObject()){
        out.removeMember("triggers");
      }else{
        jsonForEach(out["triggers"], it){
          if (it->isArray()){
            jsonForEach((*it), jt){
              jt->removeNullMembers();
            }
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
  }
  if (Request.isMember("streams")){
    Controller::CheckStreams(Request["streams"], Controller::Storage["streams"]);
  }
  if (Request.isMember("addstream")){
    Controller::AddStreams(Request["addstream"], Controller::Storage["streams"]);
  }
  if (Request.isMember("deletestream")){
    //if array, delete all elements
    //if object, delete all entries
    //if string, delete just the one
    if (Request["deletestream"].isString()){
      Controller::deleteStream(Request["deletestream"].asStringRef(), Controller::Storage["streams"]); 
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
        if (add){
          newProtocols.append(*it);
        }
      }
      Controller::Storage["config"]["protocols"] = newProtocols;
    }
    if (Request["deleteprotocol"].isObject()){
      JSON::Value newProtocols;
      jsonForEach(Controller::Storage["config"]["protocols"], it){
        if (!(*it).compareExcept(Request["deleteprotocol"], ignores)){
          newProtocols.append(*it);
        }
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
          (*it) = Request["updateprotocol"][1u];
        }
      }
      removeDuplicateProtocols();
    }else{
      FAIL_MSG("Cannot parse updateprotocol call: needs to be in the form [A, B]");
    }
  }

  if (Request.isMember("capabilities")){
    Controller::checkCapable(capabilities);
    Response["capabilities"] = capabilities;
  }


  if(Request.isMember("browse")){                    
    if(Request["browse"] == ""){
      Request["browse"] = ".";
    }
    DIR *dir;
    struct dirent *ent;
    struct stat filestat;
    char* rpath = realpath(Request["browse"].asString().c_str(),0);
    if(rpath == NULL){
      Response["browse"]["path"].append(Request["browse"].asString());
    }else{
      Response["browse"]["path"].append(rpath);//Request["browse"].asString());
      if ((dir = opendir (Request["browse"].asString().c_str())) != NULL) {
        while ((ent = readdir (dir)) != NULL) {
          if(strcmp(ent->d_name,".")!=0 && strcmp(ent->d_name,"..")!=0 ){
            std::string filepath = Request["browse"].asString() + "/" + std::string(ent->d_name);
            if (stat( filepath.c_str(), &filestat )) continue;
            if (S_ISDIR( filestat.st_mode)){
              Response["browse"]["subdirectories"].append(ent->d_name);
            }else{
              Response["browse"]["files"].append(ent->d_name);
            }
          }
        }
        closedir (dir);
      }
    }
    free(rpath);
  }

  if (Request.isMember("save")){
    Controller::Log("CONF", "Writing config to file on request through API");
    Controller::writeConfigToDisk();
  }

  if (Request.isMember("ui_settings")){
    if (Request["ui_settings"].isObject()){
      Storage["ui_settings"] = Request["ui_settings"];
    }
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
  if (Request.isMember("autoupdate")){
    Controller::checkUpdates();
  }
  if (Request.isMember("update") || Request.isMember("checkupdate") || Request.isMember("autoupdate")){
    Controller::insertUpdateInfo(Response["update"]);
  }
  #endif
  /*LTS-END*/
  if (!Request.isMember("minimal") || Request.isMember("streams") || Request.isMember("addstream") || Request.isMember("deletestream")){
    if (!Request.isMember("streams") && (Request.isMember("addstream") || Request.isMember("deletestream"))){
      Response["streams"]["incomplete list"] = 1ll;
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
  //sent current configuration, if not minimal or was changed/requested
  if (!Request.isMember("minimal") || Request.isMember("config")){
    Response["config"] = Controller::Storage["config"];
    Response["config"]["iid"] = instanceId;
    Response["config"]["version"] = PACKAGE_VERSION;
    /*LTS-START*/
    #ifdef LICENSING
    Response["config"]["license"] = getLicense();
    #endif
    /*LTS-END*/
    //add required data to the current unix time to the config, for syncing reasons
    Response["config"]["time"] = Util::epoch();
    if ( !Response["config"].isMember("serverid")){
      Response["config"]["serverid"] = "";
    }
  }
  //sent any available logs and statistics
  /// 
  /// \api
  /// `"log"` responses are always sent, and cannot be requested:
  /// ~~~~~~~~~~~~~~~{.js}
  /// [
  ///   [
  ///     1398978357, //unix timestamp of this log message
  ///     "CONF", //shortcode indicating the type of log message
  ///     "Starting connector: {\"connector\":\"HTTP\"}" //string containing the log message itself
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
    //clear log if requested
    if (Request.isMember("clearstatlogs")){
      Controller::Storage["log"].null();
    }
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
    Controller::fillActive(Request["active_streams"], Response["active_streams"], true);
  }
  if (Request.isMember("stats_streams")){
    Controller::fillActive(Request["stats_streams"], Response["stats_streams"]);
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
      jsonForEach(Request["stop_sessions"], it){
        Controller::sessions_shutdown(it);
      }
    }else{
      Controller::sessions_shutdown(Request["stop_sessions"].asStringRef());
    }
  }

  if (Request.isMember("stop_sessid")){
    if (Request["stop_sessid"].isArray() || Request["stop_sessid"].isObject()){
      jsonForEach(Request["stop_sessid"], it){
        Controller::sessId_shutdown(it->asStringRef());
      }
    }else{
      Controller::sessId_shutdown(Request["stop_sessid"].asStringRef());
    }
  }

  if (Request.isMember("stop_tag")){
    if (Request["stop_tag"].isArray() || Request["stop_tag"].isObject()){
      jsonForEach(Request["stop_tag"], it){
        Controller::tag_shutdown(it->asStringRef());
      }
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
      if (activeStreams.size()){
        for (std::map<std::string, unsigned int>::iterator jt = activeStreams.begin(); jt != activeStreams.end(); ++jt){
          if (jt->first.substr(0, stream.size()) == stream){
            std::string streamname = jt->first;
            std::string target_tmp = target;
            startPush(streamname, target_tmp);
          }
        }
      }
    }
  }

  if (Request.isMember("push_list")){
    Controller::listPush(Response["push_list"]);
  }
  
  if (Request.isMember("push_stop")){
    if (Request["push_stop"].isArray()){
      jsonForEach(Request["push_stop"], it){
        Controller::stopPush(it->asInt());
      }
    }else{
      Controller::stopPush(Request["push_stop"].asInt());
    }
  }

  if (Request.isMember("push_auto_add")){
    Controller::addPush(Request["push_auto_add"]);
  }

  if (Request.isMember("push_auto_remove")){
    if (Request["push_auto_remove"].isArray()){
      jsonForEach(Request["push_auto_remove"], it){
        Controller::removePush(*it);
      }
    }else{
      Controller::removePush(Request["push_auto_remove"]);
    }
  }

  if (Request.isMember("push_auto_list")){
    Response["push_auto_list"] = Controller::Storage["autopushes"];
  }

  if (Request.isMember("push_settings")){
    Controller::pushSettings(Request["push_settings"], Response["push_settings"]);
  }


  Controller::configChanged = true;
}

