#include <dirent.h> //for browse API call
#include <sys/stat.h> //for browse API call
#include <mist/http_parser.h>
#include <mist/auth.h>
#include <mist/config.h>
#include <mist/defines.h>
#include <mist/timing.h>
#include "controller_api.h"
#include "controller_storage.h"
#include "controller_streams.h"
#include "controller_connectors.h"
#include "controller_capabilities.h"
#include "controller_statistics.h"

///\brief Check the submitted configuration and handle things accordingly.
///\param in The new configuration.
///\param out The location to store the resulting configuration.
///
/// \api
/// `"config"` requests take the form of:
/// ~~~~~~~~~~~~~~~{.js}
/// {
///   "controller": { //controller settings
///     "interface": null, //interface to listen on. Defaults to all interfaces.
///     "port": 4242, //port to listen on. Defaults to 4242.
///     "username": null //username to drop privileges to. Defaults to root.
///   },
///   "protocols": [ //enabled connectors / protocols
///     {
///       "connector": "HTTP" //Name of the connector to enable
///       //any required and/or optional settings may be given here as "name": "value" pairs inside this object.
///     },
///     //above structure repeated for all enabled connectors / protocols
///   ],
///   "serverid": "", //human-readable server identifier, optional.
/// }
/// ~~~~~~~~~~~~~~~
/// and are responded to as:
/// ~~~~~~~~~~~~~~~{.js}
/// {
///   "controller": { //controller settings
///     "interface": null, //interface to listen on. Defaults to all interfaces.
///     "port": 4242, //port to listen on. Defaults to 4242.
///     "username": null //username to drop privileges to. Defaults to root.
///   },
///   "protocols": [ //enabled connectors / protocols
///     {
///       "connector": "HTTP" //Name of the connector to enable
///       //any required and/or optional settings may be given here as "name": "value" pairs inside this object.
///       "online": 1 //boolean value indicating if the executable is running or not
///     },
///     //above structure repeated for all enabled connectors / protocols
///   ],
///   "serverid": "", //human-readable server identifier, as configured.
///   "time": 1398982430, //current unix time
///   "version": "2.0.2/8.0.1-23-gfeb9322/Generic_64" //currently running server version string
/// }
/// ~~~~~~~~~~~~~~~
void Controller::checkConfig(JSON::Value & in, JSON::Value & out){
  out = in;
  if (out["basepath"].asString()[out["basepath"].asString().size() - 1] == '/'){
    out["basepath"] = out["basepath"].asString().substr(0, out["basepath"].asString().size() - 1);
  }
  if (out.isMember("debug")){
    if (Util::Config::printDebugLevel != out["debug"].asInt()){
      Util::Config::printDebugLevel = out["debug"].asInt();
      INFO_MSG("Debug level set to %u", Util::Config::printDebugLevel);
    }
  }
}

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
    if (Request["authorize"]["password"].asString() != "" && Secure::md5(Storage["account"][UserID]["password"].asString()) != Request["authorize"]["password"].asString()){
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
      JSON::Value Response;
      JSON::Value Request = JSON::fromString(H.GetVar("command"));
      //invalid request? send the web interface, unless requested as "/api"
      if ( !Request.isObject() && H.url != "/api"){
        #include "server.html.h"
        H.Clean();
        H.SetHeader("Content-Type", "text/html");
        H.SetHeader("X-Info", "To force an API response, request the file /api");
        H.SetHeader("Server", "mistserver/" PACKAGE_VERSION "/" + Util::Config::libver + "/" RELEASE);
        H.SetHeader("Content-Length", server_html_len);
        H.SendResponse("200", "OK", conn);
        conn.SendNow(server_html, server_html_len);
        H.Clean();
        break;
      }
      {//lock the config mutex here - do not unlock until done processing
        tthread::lock_guard<tthread::mutex> guard(configMutex);
        //if already authorized, do not re-check for authorization
        if (authorized){
          Response["authorize"]["status"] = "OK";
        }else{
          authorized |= authorize(Request, Response, conn);
        }
        if (authorized){
          //Parse config and streams from the request.
          if (Request.isMember("config")){
            Controller::checkConfig(Request["config"], Controller::Storage["config"]);
            Controller::CheckProtocols(Controller::Storage["config"]["protocols"], capabilities);
          }
          if (Request.isMember("streams")){
            Controller::CheckStreams(Request["streams"], Controller::Storage["streams"]);
          }
          if (Request.isMember("capabilities")){
            Controller::checkCapable(capabilities);
            Response["capabilities"] = capabilities;
          }
          /// \todo Re-enable conversion API at some point.
          /*
          if (Request.isMember("conversion")){
            if (Request["conversion"].isMember("encoders")){
              Response["conversion"]["encoders"] = myConverter.getEncoders();
            }
            if (Request["conversion"].isMember("query")){
              if (Request["conversion"]["query"].isMember("path")){
                Response["conversion"]["query"] = myConverter.queryPath(Request["conversion"]["query"]["path"].asString());
              }else{
                Response["conversion"]["query"] = myConverter.queryPath("./");
              }
            }
            if (Request["conversion"].isMember("convert")){
              for (JSON::ObjIter it = Request["conversion"]["convert"].ObjBegin(); it != Request["conversion"]["convert"].ObjEnd(); it++){
                myConverter.startConversion(it->first,it->second);
                Controller::Log("CONV","Conversion " + it->second["input"].asString() + " to " + it->second["output"].asString() + " started.");
              }
            }
            if (Request["conversion"].isMember("status") || Request["conversion"].isMember("convert")){
              if (Request["conversion"].isMember("clear")){
                myConverter.clearStatus();
              }
              Response["conversion"]["status"] = myConverter.getStatus();
            }
          }
          */

          /// This takes a "browse" request, and fills in the response data.
          /// 
          /// \api
          /// `"browse"` requests take the form of:
          /// ~~~~~~~~~~~~~~~{.js}
          ///   //A string, containing the path for which to discover contents. Empty means current working directory.
          ///   "/tmp/example"
          /// ~~~~~~~~~~~~~~~
          /// and are responded to as:
          /// ~~~~~~~~~~~~~~~{.js}
          /// [
          ///   //The folder path
          ///   "path":"/tmp/example"
          ///   //An array of strings showing all files 
          ///   "files":
          ///     ["file1.dtsc",
          ///      "file2.mp3",
          ///      "file3.exe"
          ///     ]
          ///   //An array of strings showing all subdirectories
          ///   "subdirectories":[
          ///   "folder1"
          ///   ]
          /// ]
          /// ~~~~~~~~~~~~~~~
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

          /// 
          /// \api
          /// `"save"` requests are always empty:
          /// ~~~~~~~~~~~~~~~{.js}
          /// {}
          /// ~~~~~~~~~~~~~~~
          /// Sending this request will cause the controller to write out its currently active configuration to the configuration file it was loaded from (the default being `./config.json`).
          /// 
          if (Request.isMember("save")){
            if( Controller::WriteFile(Controller::conf.getString("configFile"), Controller::Storage.toString())){
              Controller::Log("CONF", "Config written to file on request through API");
            }else{
              Controller::Log("ERROR", "Config " + Controller::conf.getString("configFile") + " could not be written");
            }
          }
          //sent current configuration, no matter if it was changed or not
          Response["config"] = Controller::Storage["config"];
          Response["config"]["version"] = PACKAGE_VERSION "/" + Util::Config::libver + "/" RELEASE;
          Response["streams"] = Controller::Storage["streams"];
          //add required data to the current unix time to the config, for syncing reasons
          Response["config"]["time"] = Util::epoch();
          if ( !Response["config"].isMember("serverid")){
            Response["config"]["serverid"] = "";
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
          {
            tthread::lock_guard<tthread::mutex> guard(logMutex);
            Response["log"] = Controller::Storage["log"];
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
        }else{//unauthorized
          Util::sleep(1000);//sleep a second to prevent bruteforcing 
          logins++;
        }
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
      H.SetHeader("Access-Control-Allow-Origin", "*");
      H.SetHeader("Access-Control-Allow-Methods", "GET, POST");
      H.SetHeader("Access-Control-Allow-Headers", "Content-Type, X-Requested-With");
      H.SetHeader("Access-Control-Allow-Credentials", "true");
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
