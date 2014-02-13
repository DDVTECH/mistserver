/// \file controller.cpp
/// Contains all code for the controller executable.

#include <stdio.h>
//#include <io.h>
#include <iostream>
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
#include "controller_storage.h"
#include "controller_connectors.h"
#include "controller_streams.h"
#include "controller_capabilities.h"
#include "server.html.h"

#define UPLINK_INTERVAL 30

#ifndef COMPILED_USERNAME
#define COMPILED_USERNAME ""
#define COMPILED_PASSWORD ""
#endif

///\brief Holds everything unique to the controller.
namespace Controller {

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
  void CheckConfig(JSON::Value & in, JSON::Value & out){
    for (JSON::ObjIter jit = in.ObjBegin(); jit != in.ObjEnd(); jit++){
      if (jit->first == "version" || jit->first == "time"){
        continue;
      }
      if (out.isMember(jit->first)){
        if (jit->second != out[jit->first]){
          Log("CONF", std::string("Updated configuration value ") + jit->first);
        }
      }else{
        Log("CONF", std::string("New configuration value ") + jit->first);
      }
    }
    if (out["config"]["basepath"].asString()[out["config"]["basepath"].asString().size() - 1] == '/'){
      out["config"]["basepath"] = out["config"]["basepath"].asString().substr(0, out["config"]["basepath"].asString().size() - 1);
    }
    for (JSON::ObjIter jit = out.ObjBegin(); jit != out.ObjEnd(); jit++){
      if (jit->first == "version" || jit->first == "time"){
        continue;
      }
      if ( !in.isMember(jit->first)){
        Log("CONF", std::string("Deleted configuration value ") + jit->first);
      }
    }
    out = in;
  }

  ///\brief Parse received statistics.
  ///\param stats The statistics to be parsed.
  void CheckStats(JSON::Value & stats){
    long long int currTime = Util::epoch();
    for (JSON::ObjIter jit = stats.ObjBegin(); jit != stats.ObjEnd(); jit++){
      if (currTime - lastBuffer[jit->first] > 120){
        stats.removeMember(jit->first);
        return;
      }else{
        if (jit->second.isMember("curr") && jit->second["curr"].size() > 0){
          for (JSON::ObjIter u_it = jit->second["curr"].ObjBegin(); u_it != jit->second["curr"].ObjEnd(); ++u_it){
            if (u_it->second.isMember("now") && u_it->second["now"].asInt() < currTime - 3){
              jit->second["log"].append(u_it->second);
              jit->second["curr"].removeMember(u_it->first);
              if ( !jit->second["curr"].size()){
                break;
              }
              u_it = jit->second["curr"].ObjBegin();
            }
          }
        }
      }
    }
  }
} //Controller namespace

/// the following function is a simple check if the user wants to proceed to fix (y), ignore (n) or abort on (a) a question
char yna(std::string user_input){
  if(user_input == "y" || user_input == "Y"){
    return 'y';
  }else if(user_input == "n" || user_input == "N"){
    return 'n';
  }else if(user_input == "a" || user_input == "A"){
    return 'a';
  }else{
    return 'x';//when no valid option is found, yna returns x
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
  Util::Config conf = Util::Config(argv[0], PACKAGE_VERSION " / " RELEASE);
  conf.addOption("listen_port", stored_port);
  conf.addOption("listen_interface", stored_interface);
  conf.addOption("username", stored_user);
  conf.addOption("daemonize",
      JSON::fromString(
          "{\"long\":\"daemon\", \"short\":\"d\", \"default\":1, \"long_off\":\"nodaemon\", \"short_off\":\"n\", \"help\":\"Turns deamon mode on (-d) or off (-n). -d (default) runs quietly in background, -n enables verbose in foreground.\"}"));
  conf.addOption("account",
      JSON::fromString(
          "{\"long\":\"account\", \"short\":\"a\", \"arg\":\"string\" \"default\":\"\", \"help\":\"A username:password string to create a new account with.\"}"));
  conf.addOption("configFile",
      JSON::fromString(
          "{\"long\":\"config\", \"short\":\"c\", \"arg\":\"string\" \"default\":\"config.json\", \"help\":\"Specify a config file other than default.\"}"));
  conf.addOption("uplink",
      JSON::fromString(
          "{\"default\":\"\", \"arg\":\"string\", \"help\":\"MistSteward uplink host and port.\", \"short\":\"U\", \"long\":\"uplink\"}"));
  conf.addOption("uplink-name",
      JSON::fromString(
          "{\"default\":\"" COMPILED_USERNAME "\", \"arg\":\"string\", \"help\":\"MistSteward uplink username.\", \"short\":\"N\", \"long\":\"uplink-name\"}"));
  conf.addOption("uplink-pass",
      JSON::fromString(
          "{\"default\":\"" COMPILED_PASSWORD "\", \"arg\":\"string\", \"help\":\"MistSteward uplink password.\", \"short\":\"P\", \"long\":\"uplink-pass\"}"));
  conf.parseArgs(argc, argv);

  //Input custom config here
  Controller::Storage = JSON::fromFile(conf.getString("configFile"));

  //check for port, interface and username in arguments
  //if they are not there, take them from config file, if there
  if (conf.getOption("listen_port", true).size() <= 1){
    if (Controller::Storage["config"]["controller"]["port"]){
      conf.getOption("listen_port") = Controller::Storage["config"]["controller"]["port"];
    }
  }
  if (conf.getOption("listen_interface", true).size() <= 1){
    if (Controller::Storage["config"]["controller"]["interface"]){
      conf.getOption("listen_interface") = Controller::Storage["config"]["controller"]["interface"];
    }
  }
  if (conf.getOption("username", true).size() <= 1){
    if (Controller::Storage["config"]["controller"]["username"]){
      conf.getOption("username") = Controller::Storage["config"]["controller"]["username"];
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
      arg_one = Util::getMyPath() + (*it);
      conn_args[0] = arg_one.c_str();
      capabilities["connectors"][(*it).substr(8)] = JSON::fromString(Util::Procs::getOutputOf((char**)conn_args));
      if (capabilities["connectors"][(*it).substr(8)].size() < 1){
        capabilities["connectors"].removeMember((*it).substr(8));
      }
    }
  }
  
  createAccount(conf.getString("account"));
  
  /// User friendliness input added at this line
  if (isatty(fileno(stdin))){
    //check for username
    if ( !Controller::Storage.isMember("account") || Controller::Storage["account"].size() < 1){
      std::string in_string = "";
      while(yna(in_string) == 'x'){
        std::cerr << "Account not set, do you want to create an account? (y)es, (n)o, (a)bort: ";
        std::getline(std::cin, in_string);
        if (yna(in_string) == 'y'){
          //create account
          std::string usr_string = "";
          while(!(Controller::Storage.isMember("account") && Controller::Storage["account"].size() > 0)){
            std::cerr << "Please type in the username, a colon and a password in the following format; username:password" << std::endl << ": ";
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
        std::cerr << "Protocols not set, do you want to enable default protocols? (y)es, (n)o, (a)bort: ";
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
      std::cerr << "No streams configured, remember to set up streams through local settings page on port " << conf.getInteger("listen_port") << " or using the API." << std::endl;
    }
  }
  
  std::string uplink_addr = conf.getString("uplink");
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
  Socket::Server API_Socket = Socket::Server(conf.getInteger("listen_port"), conf.getString("listen_interface"), true);
  Socket::Server Stats_Socket = Socket::Server(Util::getTmpFolder() + "statistics", true);
  conf.activate();
  Socket::Connection Incoming;
  std::vector<Controller::ConnectedUser> users;
  std::vector<Socket::Connection> buffers;
  JSON::Value Request;
  JSON::Value Response;
  std::string jsonp;
  Controller::ConnectedUser * uplink = 0;
  Controller::Log("CONF", "Controller started");
  conf.activate();
  
  //Create a converter class and automatically load in all encoders.
  Converter::Converter myConverter;
  
  while (API_Socket.connected() && conf.is_active){
    Util::sleep(10);//sleep for 10 ms - prevents 100% CPU time

    if (Util::epoch() - processchecker > 10){
      processchecker = Util::epoch();
      Controller::CheckProtocols(Controller::Storage["config"]["protocols"], capabilities);
      Controller::CheckAllStreams(Controller::Storage["streams"]);
      Controller::CheckStats(Controller::Storage["statistics"]);
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
        Response["statistics"] = Controller::Storage["statistics"];
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
    Incoming = Stats_Socket.accept(true);
    if (Incoming.connected()){
      buffers.push_back(Incoming);
    }
    if (buffers.size() > 0){
      for (std::vector<Socket::Connection>::iterator it = buffers.begin(); it != buffers.end(); it++){
        if ( !it->connected()){
          it->close();
          buffers.erase(it);
          break;
        }
        if (it->spool()){
          while (it->Received().size()){
            it->Received().get().resize(it->Received().get().size() - 1);
            Request = JSON::fromString(it->Received().get());
            it->Received().get().clear();
            if (Request.isMember("buffer")){
              std::string thisbuffer = Request["buffer"];
              Controller::lastBuffer[thisbuffer] = Util::epoch();
              //if metadata is available, store it
              if (Request.isMember("meta")){
                Controller::Storage["streams"][thisbuffer]["meta"] = Request["meta"];
              }
              if (Controller::Storage["streams"][thisbuffer].isMember("updated")){
                Controller::Storage["streams"][thisbuffer].removeMember("updated");
                if (Controller::Storage["streams"][thisbuffer].isMember("cut")){
                  it->SendNow("c"+Controller::Storage["streams"][thisbuffer]["cut"].asString()+"\n");
                }else{
                  it->SendNow("c0\n");
                }
                if (Controller::Storage["streams"][thisbuffer].isMember("DVR")){
                  it->SendNow("d"+Controller::Storage["streams"][thisbuffer]["DVR"].asString()+"\n");
                }else{
                  it->SendNow("d20000\n");
                }
                if (Controller::Storage["streams"][thisbuffer].isMember("source") && Controller::Storage["streams"][thisbuffer]["source"].asStringRef().substr(0, 7) == "push://"){
                  it->SendNow("s"+Controller::Storage["streams"][thisbuffer]["source"].asStringRef().substr(7)+"\n");
                }else{
                  it->SendNow("s127.0.01\n");
                }
              }
              if (Request.isMember("totals")){
                Controller::Storage["statistics"][thisbuffer]["curr"] = Request["curr"];
                std::string nowstr = Request["totals"]["now"].asString();
                Controller::Storage["statistics"][thisbuffer]["totals"][nowstr] = Request["totals"];
                Controller::Storage["statistics"][thisbuffer]["totals"][nowstr].removeMember("now");
                Controller::Storage["statistics"][thisbuffer]["totals"].shrink(600); //limit to 10 minutes of data
                for (JSON::ObjIter jit = Request["log"].ObjBegin(); jit != Request["log"].ObjEnd(); jit++){
                  Controller::Storage["statistics"][thisbuffer]["log"].append(jit->second);
                  Controller::Storage["statistics"][thisbuffer]["log"].shrink(1000); //limit to 1000 users per buffer
                }
              }
            }
            if (Request.isMember("vod")){
              std::string thisfile = Request["vod"]["filename"];
              for (JSON::ObjIter oit = Controller::Storage["streams"].ObjBegin(); oit != Controller::Storage["streams"].ObjEnd(); ++oit){
                if ((oit->second.isMember("source") && oit->second["source"].asString() == thisfile)
                    || (oit->second.isMember("channel") && oit->second["channel"]["URL"].asString() == thisfile)){
                  Controller::lastBuffer[oit->first] = Util::epoch();
                  if (Request["vod"].isMember("meta")){
                    Controller::Storage["streams"][oit->first]["meta"] = Request["vod"]["meta"];
                  }
                  JSON::Value sockit = (long long int)it->getSocket();
                  std::string nowstr = Request["vod"]["now"].asString();
                  Controller::Storage["statistics"][oit->first]["curr"][sockit.asString()] = Request["vod"];
                  Controller::Storage["statistics"][oit->first]["curr"][sockit.asString()].removeMember("meta");
                  JSON::Value nowtotal;
                  for (JSON::ObjIter u_it = Controller::Storage["statistics"][oit->first]["curr"].ObjBegin();
                      u_it != Controller::Storage["statistics"][oit->first]["curr"].ObjEnd(); ++u_it){
                    nowtotal["up"] = nowtotal["up"].asInt() + u_it->second["up"].asInt();
                    nowtotal["down"] = nowtotal["down"].asInt() + u_it->second["down"].asInt();
                    nowtotal["count"] = nowtotal["count"].asInt() + 1;
                  }
                  Controller::Storage["statistics"][oit->first]["totals"][nowstr] = nowtotal;
                  Controller::Storage["statistics"][oit->first]["totals"].shrink(600);
                }
              }
            }
            if (Request.isMember("ctrl_log") && Request["ctrl_log"].size() > 0){
              for (JSON::ArrIter it = Request["ctrl_log"].ArrBegin(); it != Request["ctrl_log"].ArrEnd(); it++){
                Controller::Log((*it)[0u], (*it)[1u]);
              }
            }
          }
        }
      }
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
                    Response["statistics"] = Controller::Storage["statistics"];
                    Response["authorize"]["username"] = conf.getString("uplink-name");
                    Controller::checkCapable(capabilities);
                    Response["capabilities"] = capabilities;
                    Controller::Log("UPLK", "Responding to login challenge: " + Request["authorize"]["challenge"].asString());
                    Response["authorize"]["password"] = Secure::md5(conf.getString("uplink-pass") + Request["authorize"]["challenge"].asString());
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
                  Controller::Storage["statistics"].null();
                }
              }
            }else{
              Request = JSON::fromString(it->H.GetVar("command"));
              if ( !Request.isObject() && it->H.url != "/api"){
                it->H.Clean();
                it->H.SetHeader("Content-Type", "text/html");
                it->H.SetHeader("X-Info", "To force an API response, request the file /api");
                it->H.SetHeader("Server", "mistserver/" PACKAGE_VERSION "/" + Util::Config::libver + "/" RELEASE);
                it->H.SetBody(std::string((char*)server_html, (size_t)server_html_len));
                it->C.Send(it->H.BuildResponse("200", "OK"));
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
                  if (Request.isMember("save")){
                    if( Controller::WriteFile(conf.getString("configFile"), Controller::Storage.toString())){
                      Controller::Log("CONF", "Config written to file on request through API");
                    }else{
                      Controller::Log("ERROR", "Config " + conf.getString("configFile") + " could not be written");
                    }
                  }
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
                  Response["log"] = Controller::Storage["log"];
                  Response["statistics"] = Controller::Storage["statistics"];
                  //clear log and statistics if requested
                  if (Request.isMember("clearstatlogs")){
                    Controller::Storage["log"].null();
                    Controller::Storage["statistics"].null();
                  }
                  
                }
                jsonp = "";
                if (it->H.GetVar("callback") != ""){
                  jsonp = it->H.GetVar("callback");
                }
                if (it->H.GetVar("jsonp") != ""){
                  jsonp = it->H.GetVar("jsonp");
                }
                it->H.Clean();
                it->H.SetHeader("Content-Type", "text/javascript");
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
  if (!conf.is_active){
    Controller::Log("CONF", "Controller shutting down because of user request (received shutdown signal)");
  }
  if (!API_Socket.connected()){
    Controller::Log("CONF", "Controller shutting down because of socket problem (API port closed)");
  }
  API_Socket.close();
  if ( !Controller::WriteFile(conf.getString("configFile"), Controller::Storage.toString())){
    std::cerr << "Error writing config " << conf.getString("configFile") << std::endl;
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
  return 0;
}
