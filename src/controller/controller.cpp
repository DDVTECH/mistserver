/// \page api API calls
/// The controller listens for commands through a JSON-based API. This page describes the API in full.
///
/// A default interface implementing this API as a single HTML page is included in the controller itself. This default interface will be send for invalid API requests, and is thus triggered by default when a browser attempts to access the API port directly.
/// The default API port is 4242 - but this can be changed through both the API and commandline parameters.
///
/// To send an API request, simply send a HTTP request to this port for any file, and include either a GET or POST parameter called `"command"`, containing a JSON object as payload. Nearly all members of the request object are optional, and described below.
/// A simple example request logging in to the system would look like this:
///
///     GET /api?command={"authorize":{"username":"test","password":"941d7b88b2312d4373aff526cf7b6114"}} HTTP/1.0
///
/// Or, when properly URL encoded:
///
///     GET /api?command=%7B%22authorize%22%3A%7B%22username%22%3A%22test%22%2C%22password%22%3A%22941d7b88b2312d4373aff526cf7b6114%22%7D%7D HTTP/1.0
///
/// The server is quite lenient about not URL encoding your strings, but it's a good idea to always do it, anyway.
/// See the `"authorize"` section below for more information about security and logging in.
///
/// As mentioned above, sending an invalid request will trigger a response containing the default interface. As you may not want to receive a big HTML page as response to an invalid request, requesting the file `"/api"` (as done in the example above) will force a JSON response, even when the request is invalid.
///
/// You may also include a `"callback"` or `"jsonp"` HTTP variable, to trigger JSONP compatibility mode. JSONP is useful for getting around the cross-domain scripting protection in most modern browsers. Developers creating non-JavaScript applications will most likely not want to use JSONP mode, though nothing is stopping you if you really want to.
///
/// \brief Listing of all controller API calls.

/// \file controller.cpp
/// Contains all code for the controller executable.


#include <stdio.h>
//#include <io.h>
#include <iostream>
#include <ctime>
#include <vector>
#include <sys/stat.h>
#include <mist/config.h>
#include <mist/socket.h>
#include <mist/http_parser.h>
#include <mist/procs.h>
#include <mist/auth.h>
#include <mist/timing.h>
#include <mist/converter.h>
#include <mist/stream.h>
#include <mist/defines.h>
#include "controller_storage.h"
#include "controller_connectors.h"
#include "controller_streams.h"
#include "controller_capabilities.h"
#include "controller_statistics.h"
/*LTS-START*/
#include "controller_updater.h"
#include "controller_limits.h"
/*LTS-END*/
#include "server.html.h"


#include <mist/tinythread.h>
#include <mist/shared_memory.h>


#define UPLINK_INTERVAL 30

#ifndef COMPILED_USERNAME
#define COMPILED_USERNAME ""
#define COMPILED_PASSWORD ""
#endif

///\brief Holds everything unique to the controller.
namespace Controller {
  Util::Config conf;
  
  Secure::Auth keychecker; ///< Checks key authorization.

  ///\brief A class storing information about a connected user.
  class ConnectedUser{
    public:
      Socket::Connection C;///<The socket through which the user is connected.
      HTTP::Parser H;///<The HTTP::Parser associated to this user, for the lsp.
      bool Authorized;///<Indicates whether the current user is authorized.
      bool clientMode;///<Indicates how to parse the commands.
      int logins;///<Keeps track of the amount of login-tries.
      std::string Username;///<The username of the user.
      
      ///\brief Construct a new user from a connection.
      ///\param c The socket through which the user is connected.
      ConnectedUser(Socket::Connection c){
        C = c;
        H.Clean();
        logins = 0;
        Authorized = false;
        clientMode = false;
      }
  };

  ///\brief Checks an authorization request for a given user.
  ///\param Request The request to be parsed.
  ///\param Response The location to store the generated response.
  ///\param conn The user to be checked for authorization.
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
  void Authorize(JSON::Value & Request, JSON::Value & Response, ConnectedUser & conn){
    time_t Time = time(0);
    tm * TimeInfo = localtime( &Time);
    std::stringstream Date;
    std::string retval;
    Date << TimeInfo->tm_mday << "-" << TimeInfo->tm_mon << "-" << TimeInfo->tm_year + 1900;
    std::string Challenge = Secure::md5(Date.str().c_str() + conn.C.getHost());
    if (Request.isMember("authorize")){
      std::string UserID = Request["authorize"]["username"];
      if (Storage["account"].isMember(UserID)){
        if (Secure::md5(Storage["account"][UserID]["password"].asString() + Challenge) == Request["authorize"]["password"].asString()){
          Response["authorize"]["status"] = "OK";
          conn.Username = UserID;
          conn.Authorized = true;
          return;
        }
      }
      if (UserID != ""){
        if (Request["authorize"]["password"].asString() != ""
            && Secure::md5(Storage["account"][UserID]["password"].asString()) != Request["authorize"]["password"].asString()){
          Log("AUTH", "Failed login attempt " + UserID + " @ " + conn.C.getHost());
        }
      }
      conn.logins++;
    }
    conn.Username = "";
    conn.Authorized = false;
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
    return;
  }

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
  void CheckConfig(JSON::Value & in, JSON::Value & out){
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

} //Controller namespace

/// the following function is a simple check if the user wants to proceed to fix (y), ignore (n) or abort on (a) a question
char yna(std::string & user_input){
  switch (user_input[0]){
    case 'y': case 'Y':
      return 'y';
      break;
    case 'n': case 'N':
      return 'n';
      break;
    case 'a': case 'A':
      return 'a';
      break;
    default:
      return 'x';
      break;
  }
}

/// createAccount accepts a string in the form of username:account
/// and creates an account.
void createAccount (std::string account){
  if (account.size() > 0){
    size_t colon = account.find(':');
    if (colon != std::string::npos && colon != 0 && colon != account.size()){
      std::string uname = account.substr(0, colon);
      std::string pword = account.substr(colon + 1, std::string::npos);
      Controller::Log("CONF", "Created account " + uname + " through commandline option");
      Controller::Storage["account"][uname]["password"] = Secure::md5(pword);
    }
  }
  if ( !Controller::Storage["config"].isMember("basePath")){
    Controller::Storage["config"]["basePath"] = Util::getMyPath();
  }
}

///\brief The main entry point for the controller.
int main(int argc, char ** argv){
  
  Controller::Storage = JSON::fromFile("config.json");
  JSON::Value stored_port = JSON::fromString("{\"long\":\"port\", \"short\":\"p\", \"arg\":\"integer\", \"help\":\"TCP port to listen on.\"}");
  stored_port["default"] = Controller::Storage["config"]["controller"]["port"];
  if ( !stored_port["default"]){
    stored_port["default"] = 4242;
  }
  JSON::Value stored_interface =
      JSON::fromString(
          "{\"long\":\"interface\", \"short\":\"i\", \"arg\":\"string\", \"help\":\"Interface address to listen on, or 0.0.0.0 for all available interfaces.\"}");
  stored_interface["default"] = Controller::Storage["config"]["controller"]["interface"];
  if ( !stored_interface["default"]){
    stored_interface["default"] = "0.0.0.0";
  }
  JSON::Value stored_user = JSON::fromString(
      "{\"long\":\"username\", \"short\":\"u\", \"arg\":\"string\", \"help\":\"Username to transfer privileges to, default is root.\"}");
  stored_user["default"] = Controller::Storage["config"]["controller"]["username"];
  if ( !stored_user["default"]){
    stored_user["default"] = "root";
  }
  Controller::conf = Util::Config(argv[0], PACKAGE_VERSION " / " RELEASE);
  Controller::conf.addOption("listen_port", stored_port);
  Controller::conf.addOption("listen_interface", stored_interface);
  Controller::conf.addOption("username", stored_user);
  Controller::conf.addOption("daemonize",
      JSON::fromString(
          "{\"long\":\"daemon\", \"short\":\"d\", \"default\":0, \"long_off\":\"nodaemon\", \"short_off\":\"n\", \"help\":\"Turns deamon mode on (-d) or off (-n). -d runs quietly in background, -n (default) enables verbose in foreground.\"}"));
  Controller::conf.addOption("account",
      JSON::fromString(
          "{\"long\":\"account\", \"short\":\"a\", \"arg\":\"string\" \"default\":\"\", \"help\":\"A username:password string to create a new account with.\"}"));
  Controller::conf.addOption("logfile",
      JSON::fromString(
          "{\"long\":\"logfile\", \"short\":\"L\", \"arg\":\"string\" \"default\":\"\",\"help\":\"Redirect all standard output to a log file, provided with an argument\"}"));
  Controller::conf.addOption("configFile",
      JSON::fromString(
          "{\"long\":\"config\", \"short\":\"c\", \"arg\":\"string\" \"default\":\"config.json\", \"help\":\"Specify a config file other than default.\"}"));
  Controller::conf.addOption("uplink",
      JSON::fromString(
          "{\"default\":\"\", \"arg\":\"string\", \"help\":\"MistSteward uplink host and port.\", \"short\":\"U\", \"long\":\"uplink\"}"));
  Controller::conf.addOption("uplink-name",
      JSON::fromString(
          "{\"default\":\"" COMPILED_USERNAME "\", \"arg\":\"string\", \"help\":\"MistSteward uplink username.\", \"short\":\"N\", \"long\":\"uplink-name\"}"));
  Controller::conf.addOption("uplink-pass",
      JSON::fromString(
          "{\"default\":\"" COMPILED_PASSWORD "\", \"arg\":\"string\", \"help\":\"MistSteward uplink password.\", \"short\":\"P\", \"long\":\"uplink-pass\"}"));
  Controller::conf.addOption("update", JSON::fromString("{\"default\":0, \"help\":\"Check for and install updates before starting.\", \"short\":\"D\", \"long\":\"update\"}")); /*LTS*/
  Controller::conf.parseArgs(argc, argv);
  if(Controller::conf.getString("logfile")!= ""){
    //open logfile, dup stdout to logfile
    int output = open(Controller::conf.getString("logfile").c_str(),O_APPEND|O_CREAT|O_WRONLY,S_IRWXU);
    if(output < 0){
      DEBUG_MSG(DLVL_ERROR, "Could not redirect output to %s: %s",Controller::conf.getString("logfile").c_str(),strerror(errno));
      return 7;
    }else{
      dup2(output,STDOUT_FILENO);
      dup2(output,STDERR_FILENO);
      time_t rawtime;
      struct tm * timeinfo;
      char buffer [25];
      time (&rawtime);
      timeinfo = localtime (&rawtime);
      strftime (buffer,25,"%c",timeinfo);
      std::cerr << std::endl << std::endl <<"!----MistServer Started at " << buffer << " ----!"  << std::endl;
    }
  }
  //Input custom config here
  Controller::Storage = JSON::fromFile(Controller::conf.getString("configFile"));
  
  {
    //spawn thread that reads stderr of process
    int pipeErr[2];
    if (pipe(pipeErr) >= 0){
      dup2(pipeErr[1], STDERR_FILENO);//cause stderr to write to the pipe
      close(pipeErr[1]);//close the unneeded pipe file descriptor
      tthread::thread msghandler(Controller::handleMsg, (void*)(((char*)0) + pipeErr[0]));
      msghandler.detach();
    }
  }
  
  
  if (Controller::conf.getOption("debug",true).size() > 1){
    Controller::Storage["config"]["debug"] = Controller::conf.getInteger("debug");
  }
  if (Controller::Storage.isMember("config") && Controller::Storage["config"].isMember("debug")){
    Util::Config::printDebugLevel = Controller::Storage["config"]["debug"].asInt();
  }
  //check for port, interface and username in arguments
  //if they are not there, take them from config file, if there
  if (Controller::conf.getOption("listen_port", true).size() <= 1){
    if (Controller::Storage["config"]["controller"]["port"]){
      Controller::conf.getOption("listen_port") = Controller::Storage["config"]["controller"]["port"];
    }
  }
  if (Controller::conf.getOption("listen_interface", true).size() <= 1){
    if (Controller::Storage["config"]["controller"]["interface"]){
      Controller::conf.getOption("listen_interface") = Controller::Storage["config"]["controller"]["interface"];
    }
  }
  if (Controller::conf.getOption("username", true).size() <= 1){
    if (Controller::Storage["config"]["controller"]["username"]){
      Controller::conf.getOption("username") = Controller::Storage["config"]["controller"]["username"];
    }
  }
  JSON::Value capabilities;
  //list available protocols and report about them
  std::deque<std::string> execs;
  Util::getMyExec(execs);
  std::string arg_one;
  char const * conn_args[] = {0, "-j", 0};
  for (std::deque<std::string>::iterator it = execs.begin(); it != execs.end(); it++){
    if ((*it).substr(0, 8) == "MistConn"){
      //skip if an MistOut already existed - MistOut takes precedence!
      if (capabilities["connectors"].isMember((*it).substr(8))){
        continue;
      }
      arg_one = Util::getMyPath() + (*it);
      conn_args[0] = arg_one.c_str();
      capabilities["connectors"][(*it).substr(8)] = JSON::fromString(Util::Procs::getOutputOf((char**)conn_args));
      if (capabilities["connectors"][(*it).substr(8)].size() < 1){
        capabilities["connectors"].removeMember((*it).substr(8));
      }
    }
    if ((*it).substr(0, 7) == "MistOut"){
      arg_one = Util::getMyPath() + (*it);
      conn_args[0] = arg_one.c_str();
      capabilities["connectors"][(*it).substr(7)] = JSON::fromString(Util::Procs::getOutputOf((char**)conn_args));
      if (capabilities["connectors"][(*it).substr(7)].size() < 1){
        capabilities["connectors"].removeMember((*it).substr(7));
      }
    }
  }
  
  createAccount(Controller::conf.getString("account"));
  
  /// User friendliness input added at this line
  if (isatty(fileno(stdin)) && Controller::conf.getString("logfile") == ""){
    //check for username
    if ( !Controller::Storage.isMember("account") || Controller::Storage["account"].size() < 1){
      std::string in_string = "";
      while(yna(in_string) == 'x'){
        std::cout << "Account not set, do you want to create an account? (y)es, (n)o, (a)bort: ";
        std::cout.flush();
        std::getline(std::cin, in_string);
        if (yna(in_string) == 'y'){
          //create account
          std::string usr_string = "";
          while(!(Controller::Storage.isMember("account") && Controller::Storage["account"].size() > 0)){
            std::cout << "Please type in the username, a colon and a password in the following format; username:password" << std::endl << ": ";
            std::cout.flush();
            std::getline(std::cin, usr_string);
            createAccount(usr_string);
          }
        }else if(yna(in_string) == 'a'){
          //abort controller startup
          return 0;
        }
      }
    }
    //check for protocols
    if ( !Controller::Storage.isMember("config") || !Controller::Storage["config"].isMember("protocols") || Controller::Storage["config"]["protocols"].size() < 1){
      std::string in_string = "";
      while(yna(in_string) == 'x'){
        std::cout << "Protocols not set, do you want to enable default protocols? (y)es, (n)o, (a)bort: ";
        std::cout.flush();
        std::getline(std::cin, in_string);
        if (yna(in_string) == 'y'){
          //create protocols
          for (JSON::ObjIter it = capabilities["connectors"].ObjBegin(); it != capabilities["connectors"].ObjEnd(); it++){
            if ( !it->second.isMember("required")){
              JSON::Value newProtocol;
              newProtocol["connector"] = it->first;
              Controller::Storage["config"]["protocols"].append(newProtocol);
            }
          }
        }else if(yna(in_string) == 'a'){
          //abort controller startup
          return 0;
        }
      }
    }
    //check for streams
    if ( !Controller::Storage.isMember("streams") || Controller::Storage["streams"].size() < 1){
      std::cout << "No streams configured, remember to set up streams through local settings page on port " << Controller::conf.getInteger("listen_port") << " or using the API." << std::endl;
    }
  }
  
  std::string uplink_addr = Controller::conf.getString("uplink");
  std::string uplink_host = "";
  int uplink_port = 0;
  if (uplink_addr.size() > 0){
    size_t colon = uplink_addr.find(':');
    if (colon != std::string::npos && colon != 0 && colon != uplink_addr.size()){
      uplink_host = uplink_addr.substr(0, colon);
      uplink_port = atoi(uplink_addr.substr(colon + 1, std::string::npos).c_str());
      Controller::Log("CONF",
          "Connection to uplink enabled on host " + uplink_host + " and port " + uplink_addr.substr(colon + 1, std::string::npos));
    }
  }

  time_t lastuplink = 0;
  time_t processchecker = 0;
  time_t updatechecker = 0; /*LTS*/
  Socket::Server API_Socket = Socket::Server(Controller::conf.getInteger("listen_port"), Controller::conf.getString("listen_interface"), true);
  Socket::Server Stats_Socket = Socket::Server(Util::getTmpFolder() + "statistics", true);
  Socket::Connection Incoming;
  std::vector<Controller::ConnectedUser> users;
  std::vector<Socket::Connection> buffers;
  JSON::Value Request;
  JSON::Value Response;
  std::string jsonp;
  Controller::ConnectedUser * uplink = 0;
  Controller::Log("CONF", "Controller started");
  Controller::conf.activate();

  /*LTS-START*/
  if (Controller::conf.getBool("update")){
    Controller::CheckUpdates();
  }
  /*LTS-END*/
  //Create a converter class and automatically load in all encoders.
  Converter::Converter myConverter;
  
  tthread::thread statsThread(Controller::SharedMemStats, &Controller::conf);
  
  while (API_Socket.connected() && Controller::conf.is_active){
    Util::sleep(10);//sleep for 10 ms - prevents 100% CPU time
    
    /*LTS-START*/
    if (Util::epoch() - updatechecker > 3600){
      updatechecker = Util::epoch();
      Controller::CheckUpdateInfo();
    }
    /*LTS-END*/

    if (Util::epoch() - processchecker > 5){
      processchecker = Util::epoch();
      Controller::CheckProtocols(Controller::Storage["config"]["protocols"], capabilities);
      Controller::CheckAllStreams(Controller::Storage["streams"]);
      myConverter.updateStatus();
    }
    if (uplink_port && Util::epoch() - lastuplink > UPLINK_INTERVAL){
      lastuplink = Util::epoch();
      bool gotUplink = false;
      if (users.size() > 0){
        for (std::vector<Controller::ConnectedUser>::iterator it = users.end() - 1; it >= users.begin(); it--){
          if ( !it->C.connected()){
            it->C.close();
            users.erase(it);
            break;
          }
          if (it->clientMode){
            uplink = & *it;
            gotUplink = true;
          }
        }
      }
      if ( !gotUplink){
        Incoming = Socket::Connection(uplink_host, uplink_port, true);
        if (Incoming.connected()){
          users.push_back((Controller::ConnectedUser)Incoming);
          users.back().clientMode = true;
          uplink = &users.back();
          gotUplink = true;
        }
      }
      if (gotUplink){
        Response.null(); //make sure no data leaks from previous requests
        Response["config"] = Controller::Storage["config"];
        Response["streams"] = Controller::Storage["streams"];
        Response["log"] = Controller::Storage["log"];
        /// \todo Put this back in, someway, somehow...
        //Response["statistics"] = Controller::Storage["statistics"];
        Response["now"] = (unsigned int)lastuplink;
        uplink->H.Clean();
        uplink->H.SetBody("command=" + HTTP::Parser::urlencode(Response.toString()));
        uplink->H.BuildRequest();
        uplink->H.SendResponse("200", "OK", uplink->C);
        uplink->H.Clean();
        //Controller::Log("UPLK", "Sending server data to uplink.");
      }else{
        Controller::Log("UPLK", "Could not connect to uplink.");
      }
    }

    Incoming = API_Socket.accept(true);
    if (Incoming.connected()){
      users.push_back((Controller::ConnectedUser)Incoming);
    }
    if (users.size() > 0){
      for (std::vector<Controller::ConnectedUser>::iterator it = users.begin(); it != users.end(); it++){
        if ( !it->C.connected() || it->logins > 3){
          it->C.close();
          users.erase(it);
          break;
        }
        if (it->C.spool() || it->C.Received().size()){
          if (it->H.Read(it->C)){
            Response.null(); //make sure no data leaks from previous requests
            if (it->clientMode){
              // In clientMode, requests are reversed. These are connections we initiated to GearBox.
              // They are assumed to be authorized, but authorization to gearbox is still done.
              // This authorization uses the compiled-in or commandline username and password (account).
              Request = JSON::fromString(it->H.body);
              if (Request["authorize"]["status"] != "OK"){
                if (Request["authorize"].isMember("challenge")){
                  it->logins++;
                  if (it->logins > 2){
                    Controller::Log("UPLK", "Max login attempts passed - dropping connection to uplink.");
                    it->C.close();
                  }else{
                    Response["config"] = Controller::Storage["config"];
                    Response["streams"] = Controller::Storage["streams"];
                    Response["log"] = Controller::Storage["log"];
                    /// \todo Put this back in, someway, somehow...
                    //Response["statistics"] = Controller::Storage["statistics"];
                    Response["authorize"]["username"] = Controller::conf.getString("uplink-name");
                    Response["update"] = Controller::updates; /*LTS*/
                    Controller::checkCapable(capabilities);
                    Response["capabilities"] = capabilities;
                    Controller::Log("UPLK", "Responding to login challenge: " + Request["authorize"]["challenge"].asString());
                    Response["authorize"]["password"] = Secure::md5(Controller::conf.getString("uplink-pass") + Request["authorize"]["challenge"].asString());
                    it->H.Clean();
                    it->H.SetBody("command=" + HTTP::Parser::urlencode(Response.toString()));
                    it->H.BuildRequest();
                    it->H.SendResponse("200", "OK", it->C);
                    it->H.Clean();
                    Controller::Log("UPLK", "Attempting login to uplink.");
                  }
                }
              }else{
                if (Request.isMember("config")){
                  Controller::CheckConfig(Request["config"], Controller::Storage["config"]);
                  Controller::CheckProtocols(Controller::Storage["config"]["protocols"], capabilities);
                }
                if (Request.isMember("streams")){
                  Controller::CheckStreams(Request["streams"], Controller::Storage["streams"]);
                  Controller::CheckAllStreams(Controller::Storage["streams"]);
                }
                if (Request.isMember("clearstatlogs")){
                  Controller::Storage["log"].null();
                }
                /*LTS-START*/
                if (Request.isMember("autoupdate")){
                  Controller::CheckUpdates();
                }
                if (Request.isMember("checkupdate")){
                  Controller::updates = Controller::CheckUpdateInfo();
                }
                /*LTS-END*/
              }
            }else{
              Request = JSON::fromString(it->H.GetVar("command"));
              if ( !Request.isObject() && it->H.url != "/api"){
                it->H.Clean();
                it->H.SetHeader("Content-Type", "text/html");
                it->H.SetHeader("X-Info", "To force an API response, request the file /api");
                it->H.SetHeader("Server", "mistserver/" PACKAGE_VERSION "/" + Util::Config::libver + "/" RELEASE);
                it->H.SetHeader("Content-Length", server_html_len);
                it->H.SendResponse("200", "OK", it->C);
                it->C.SendNow(server_html, server_html_len);
                it->H.Clean();
              }else{
                Authorize(Request, Response, ( *it));
                if (it->Authorized){
                  //Parse config and streams from the request.
                  if (Request.isMember("config")){
                    Controller::CheckConfig(Request["config"], Controller::Storage["config"]);
                    Controller::CheckProtocols(Controller::Storage["config"]["protocols"], capabilities);
                  }
                  if (Request.isMember("streams")){
                    Controller::CheckStreams(Request["streams"], Controller::Storage["streams"]);
                    Controller::CheckAllStreams(Controller::Storage["streams"]);
                  }
                  /*LTS-START*/
                  /// 
                  /// \api
                  /// `"addstream"` requests take the form of:
                  /// ~~~~~~~~~~~~~~~{.js}
                  /// {
                  ///   "streamname": {
                  ///     //Stream configuration - see the "streams" call for details on this format.
                  ///   }
                  ///   /// Optionally, repeat for more streams.
                  /// }
                  /// ~~~~~~~~~~~~~~~
                  /// These requests will add new streams or update existing streams with the same names, without touching other streams. In other words, this call can be used for incremental updates to the stream list instead of complete updates, like the "streams" call.
                  /// 
                  if (Request.isMember("addstream")){
                    Controller::AddStreams(Request["addstream"], Controller::Storage["streams"]);
                    Controller::CheckAllStreams(Controller::Storage["streams"]);
                  }
                  /// 
                  /// \api
                  /// `"deletestream"` requests take the form of:
                  /// ~~~~~~~~~~~~~~~{.js}
                  /// {
                  ///   "streamname": {} //any contents in this object are ignored
                  ///   /// Optionally, repeat for more streams.
                  /// }
                  /// ~~~~~~~~~~~~~~~
                  /// OR
                  /// ~~~~~~~~~~~~~~~{.js}
                  /// [
                  ///   "streamname",
                  ///   /// Optionally, repeat for more streams.
                  /// ]
                  /// ~~~~~~~~~~~~~~~
                  /// OR
                  /// ~~~~~~~~~~~~~~~{.js}
                  /// "streamname"
                  /// ~~~~~~~~~~~~~~~
                  /// These requests will remove the named stream(s), without touching other streams. In other words, this call can be used for incremental updates to the stream list instead of complete updates, like the "streams" call.
                  ///
                  if (Request.isMember("deletestream")){
                    //if array, delete all elements
                    //if object, delete all entries
                    //if string, delete just the one
                    if (Request["deletestream"].isString()){
                      Controller::Storage["streams"].removeMember(Request["deletestream"].asStringRef());
                    }
                    if (Request["deletestream"].isArray()){
                      for (JSON::ArrIter it = Request["deletestream"].ArrBegin(); it != Request["deletestream"].ArrEnd(); ++it){
                        Controller::Storage["streams"].removeMember(it->asString());
                      }
                    }
                    if (Request["deletestream"].isObject()){
                      for (JSON::ObjIter it = Request["deletestream"].ObjBegin(); it != Request["deletestream"].ObjEnd(); ++it){
                        Controller::Storage["streams"].removeMember(it->first);
                      }
                    }
                    Controller::CheckAllStreams(Controller::Storage["streams"]);
                  }
                  /*LTS-END*/
                  if (Request.isMember("capabilities")){
                    Controller::checkCapable(capabilities);
                    Response["capabilities"] = capabilities;
                  }
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
                  /*LTS-START*/
                  Response["LTS"] = 1;
                  if (Request.isMember("autoupdate")){
                    Controller::CheckUpdates();
                  }
                  if (Request.isMember("checkupdate")){
                    Controller::updates = Controller::CheckUpdateInfo();
                  }
                  if (Request.isMember("update") || Request.isMember("checkupdate")){
                    Response["update"] = Controller::updates;
                  }
                  /*LTS-END*/
                  //sent current configuration, no matter if it was changed or not
                  //Response["streams"] = Storage["streams"];
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
                  Response["log"] = Controller::Storage["log"];
                  //clear log and statistics if requested
                  if (Request.isMember("clearstatlogs")){
                    Controller::Storage["log"].null();
                  }
                  if (Request.isMember("clients")){
                    Controller::fillClients(Request["clients"], Response["clients"]);
                  }
                  if (Request.isMember("totals")){
                    Controller::fillTotals(Request["totals"], Response["totals"]);
                  }
                  
                }
                Controller::checkServerLimits(); /*LTS*/
                jsonp = "";
                if (it->H.GetVar("callback") != ""){
                  jsonp = it->H.GetVar("callback");
                }
                if (it->H.GetVar("jsonp") != ""){
                  jsonp = it->H.GetVar("jsonp");
                }
                it->H.Clean();
                it->H.SetHeader("Content-Type", "text/javascript");
                it->H.SetHeader("Access-Control-Allow-Origin", "*");
                it->H.SetHeader("Access-Control-Allow-Methods", "GET, POST");
                it->H.SetHeader("Access-Control-Allow-Headers", "Content-Type, X-Requested-With");
                it->H.SetHeader("Access-Control-Allow-Credentials", "true");
                
                if (jsonp == ""){
                  it->H.SetBody(Response.toString() + "\n\n");
                }else{
                  it->H.SetBody(jsonp + "(" + Response.toString() + ");\n\n");
                }
                it->H.SendResponse("200", "OK", it->C);
                it->H.Clean();
              }
            }
          }
        }
      }
    }
  }
  /*LTS-START*/
  if (Controller::restarting){
    Controller::Log("CONF", "Controller restarting for update");
  }
  /*LTS-END*/
  if (!Controller::conf.is_active){
    Controller::Log("CONF", "Controller shutting down because of user request (received shutdown signal)");
  }
  if (!API_Socket.connected()){
    Controller::Log("CONF", "Controller shutting down because of socket problem (API port closed)");
  }
  Controller::conf.is_active = false;
  API_Socket.close();
  statsThread.join();
  if ( !Controller::WriteFile(Controller::conf.getString("configFile"), Controller::Storage.toString())){
    std::cerr << "Error writing config " << Controller::conf.getString("configFile") << std::endl;
    Controller::Storage.removeMember("log");
    for (JSON::ObjIter it = Controller::Storage["streams"].ObjBegin(); it != Controller::Storage["streams"].ObjEnd(); it++){
      it->second.removeMember("meta");
    }
    std::cerr << "**Config**" << std::endl;
    std::cerr << Controller::Storage.toString() << std::endl;
    std::cerr << "**End config**" << std::endl;
  }
  Util::Procs::StopAll();
  std::cout << "Killed all processes, wrote config to disk. Exiting." << std::endl;
  /*LTS-START*/
  if (Controller::restarting){
    std::string myFile = Util::getMyPath() + "MistController";
    execvp(myFile.c_str(), argv);
    std::cout << "Error restarting: " << strerror(errno) << std::endl;
  }
  /*LTS-END*/
  return 0;
}
