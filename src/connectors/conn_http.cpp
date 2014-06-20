/// \file conn_http.cpp
/// Contains the main code for the HTTP Connector

#include <iostream>
#include <queue>
#include <set>
#include <sstream>

#include <ctime>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h> //
#include <getopt.h>

#include <mist/socket.h>
#include <mist/http_parser.h>
#include <mist/config.h>
#include <mist/stream.h>
#include <mist/timing.h>
#include <mist/auth.h>
#include <mist/procs.h>
#include <mist/tinythread.h>
#include <mist/defines.h>

#include "embed.js.h"

/*LTS-START*/
#ifndef SUPER_SECRET
#define SUPER_SECRET "PuddingBroodjes"
#endif
/*LTS-END*/

/// Holds everything unique to HTTP Connectors.
namespace Connector_HTTP {

  static inline void builPipedPart(JSON::Value & p, char * argarr[], int & argnum, JSON::Value & argset){
    for (JSON::ObjIter it = argset.ObjBegin(); it != argset.ObjEnd(); ++it){
      if (it->second.isMember("option") && p.isMember(it->first)){
        if (it->second.isMember("type")){
          if (it->second["type"].asStringRef() == "str" && !p[it->first].isString()){
            p[it->first] = p[it->first].asString();
          }
          if ((it->second["type"].asStringRef() == "uint" || it->second["type"].asStringRef() == "int") && !p[it->first].isInt()){
            p[it->first] = JSON::Value(p[it->first].asInt()).asString();
          }
        }
        if (p[it->first].asStringRef().size() > 0){
          argarr[argnum++] = (char*)(it->second["option"].c_str());
          argarr[argnum++] = (char*)(p[it->first].c_str());
        }
      }
    }
  }

  /// Class for keeping track of connections to connectors.
  class ConnConn{
    public:
      Socket::Connection * conn; ///< The socket of this connection
      unsigned int lastUse; ///< Seconds since last use of this connection.
      tthread::mutex inUse; ///< Mutex for this connection.
      /// Constructor that sets the socket and lastUse to 0.
      ConnConn(){
        conn = 0;
        lastUse = 0;
      }
      /// Constructor that sets lastUse to 0, but socket to s.
      ConnConn(Socket::Connection * s){
        conn = s;
        lastUse = 0;
      }
      /// Destructor that deletes the socket if non-null.
      ~ConnConn(){
        if (conn){
          conn->close();
          delete conn;
        }
        conn = 0;
      }
  };

  std::map<std::string, ConnConn *> connectorConnections; ///< Connections to connectors
  tthread::mutex connMutex; ///< Mutex for adding/removing connector connections.
  bool timeoutThreadStarted = false;
  tthread::mutex timeoutStartMutex; ///< Mutex for starting timeout thread.
  tthread::mutex timeoutMutex; ///< Mutex for timeout thread.
  tthread::thread * timeouter = 0; ///< Thread that times out connections to connectors.
  JSON::Value capabilities; ///< Holds a list of all HTTP connectors and their properties
  JSON::Value ServConf; /// < holds configuration, loads from file in main

  ///\brief Function run as a thread to timeout requests on the proxy.
  ///\param n A NULL-pointer
  void proxyTimeoutThread(void * n){
    n = 0; //prevent unused variable warning
    tthread::lock_guard<tthread::mutex> guard(timeoutMutex);
    timeoutThreadStarted = true;
    while (true){
      {
        tthread::lock_guard<tthread::mutex> guard(connMutex);
        if (connectorConnections.empty()){
          return;
        }
        std::map<std::string, ConnConn*>::iterator it;
        for (it = connectorConnections.begin(); it != connectorConnections.end(); it++){
          ConnConn* ccPointer = it->second;
          if ( !ccPointer->conn->connected() || ccPointer->lastUse++ > 15){
            if (ccPointer->inUse.try_lock()){
              connectorConnections.erase(it);
              ccPointer->inUse.unlock();
              delete ccPointer;
              it = connectorConnections.begin(); //get a valid iterator
              if (it == connectorConnections.end()){
                return;
              }
            }
          }
        }
      }
      usleep(1000000); //sleep 1 second and re-check
    }
  }

  ///\brief Handles requests without associated handler.
  ///
  ///Displays a friendly error message.
  ///\param H The request to be handled.
  ///\param conn The connection to the client that issued the request.
  ///\return A timestamp indicating when the request was parsed.
  long long int proxyHandleUnsupported(HTTP::Parser & H, Socket::Connection & conn){
    H.Clean();
    H.SetHeader("Server", "mistserver/" PACKAGE_VERSION "/" + Util::Config::libver);
    H.SetBody(
        "<!DOCTYPE html><html><head><title>Unsupported Media Type</title></head><body><h1>Unsupported Media Type</h1>The server isn't quite sure what you wanted to receive from it.</body></html>");
    long long int ret = Util::getMS();
    conn.SendNow(H.BuildResponse("415", "Unsupported Media Type"));
    return ret;
  }

  ///\brief Handles requests that have timed out.
  ///
  ///Displays a friendly error message.
  ///\param H The request that was being handled upon timeout.
  ///\param conn The connection to the client that issued the request.
  ///\param msg The message to print to the client.
  ///\return A timestamp indicating when the request was parsed.
  long long int proxyHandleTimeout(HTTP::Parser & H, Socket::Connection & conn, std::string msg){
    H.Clean();
    H.SetHeader("Server", "mistserver/" PACKAGE_VERSION "/" + Util::Config::libver);
    H.SetBody(
        "<!DOCTYPE html><html><head><title>"+msg+"</title></head><body><h1>"+msg+"</h1>Though the server understood your request and attempted to handle it, somehow handling it took longer than it should. Your request has been cancelled - please try again later.</body></html>");
    long long int ret = Util::getMS();
    conn.SendNow(H.BuildResponse("504", msg));
    return ret;
  }
  
  /// Sorts the JSON::Value objects that hold source information by preference.
  struct sourceCompare {
    bool operator() (const JSON::Value& lhs, const JSON::Value& rhs) const {
      //first compare simultaneous tracks
      if (lhs["simul_tracks"].asInt() > rhs["simul_tracks"].asInt()){
        //more tracks = higher priority = true.
        return true;
      }
      if (lhs["simul_tracks"].asInt() < rhs["simul_tracks"].asInt()){
        //less tracks = lower priority = false
        return false;
      }
      //same amount of tracks - compare "hardcoded" priorities
      if (lhs["priority"].asInt() > rhs["priority"].asInt()){
        //higher priority = true.
        return true;
      }
      if (lhs["priority"].asInt() < rhs["priority"].asInt()){
        //lower priority = false
        return false;
      }
      //same priority - compare total matches
      if (lhs["total_matches"].asInt() > rhs["total_matches"].asInt()){
        //more matches = higher priority = true.
        return true;
      }
      if (lhs["total_matches"].asInt() < rhs["total_matches"].asInt()){
        //less matches = lower priority = false
        return false;
      }
      //also same amount of matches? just compare the URL then.
      return lhs["url"].asStringRef() < rhs["url"].asStringRef();
    }
  };
  
  void addSource(const std::string & rel, std::set<JSON::Value, sourceCompare> & sources, std::string & host, const std::string & port, JSON::Value & conncapa, unsigned int most_simul, unsigned int total_matches){
    JSON::Value tmp;
    tmp["type"] = conncapa["type"];
    tmp["relurl"] = rel;
    tmp["priority"] = conncapa["priority"];
    tmp["simul_tracks"] = most_simul;
    tmp["total_matches"] = total_matches;
    tmp["url"] = conncapa["handler"].asStringRef() + "://" + host + ":" + port + rel;
    sources.insert(tmp);
  }
  
  void addSources(std::string & streamname, const std::string & rel, std::set<JSON::Value, sourceCompare> & sources, std::string & host, const std::string & port, JSON::Value & conncapa, JSON::Value & strmMeta){
    unsigned int most_simul = 0;
    unsigned int total_matches = 0;
    if (conncapa.isMember("codecs") && conncapa["codecs"].size() > 0){
      for (JSON::ArrIter it = conncapa["codecs"].ArrBegin(); it != conncapa["codecs"].ArrEnd(); it++){
        unsigned int simul = 0;
        if ((*it).size() > 0){
          for (JSON::ArrIter itb = (*it).ArrBegin(); itb != (*it).ArrEnd(); itb++){
            unsigned int matches = 0;
            if ((*itb).size() > 0){
              for (JSON::ArrIter itc = (*itb).ArrBegin(); itc != (*itb).ArrEnd(); itc++){
                for (JSON::ObjIter trit = strmMeta["tracks"].ObjBegin(); trit != strmMeta["tracks"].ObjEnd(); trit++){
                  if (trit->second["codec"].asStringRef() == (*itc).asStringRef()){
                    matches++;
                    total_matches++;
                  }
                }
              }
            }
            if (matches){
              simul++;
            }
          }
        }
        if (simul > most_simul){
          most_simul = simul;
        }
      }
    }
    if (conncapa.isMember("methods") && conncapa["methods"].size() > 0){
      std::string relurl;
      size_t found = rel.find('$');
      if (found != std::string::npos){
        relurl = rel.substr(0, found) + streamname + rel.substr(found+1);
      }else{
        relurl = "/";
      }
      for (JSON::ArrIter it = conncapa["methods"].ArrBegin(); it != conncapa["methods"].ArrEnd(); it++){
        if (!strmMeta.isMember("live") || !it->isMember("nolive")){
          addSource(relurl, sources, host, port, *it, most_simul, total_matches);
        }
      }
    }
  }
  

  ///\brief Handles requests within the proxy.
  ///
  ///Currently supported urls:
  /// - /crossdomain.xml
  /// - /clientaccesspolicy.xml
  /// - *.ico (for favicon)
  /// - /info_[streamname].js (stream info)
  /// - /embed_[streamname].js (embed info)
  ///
  ///Unsupported urls default to proxyHandleUnsupported( ).
  ///\param H The request to be handled.
  ///\param conn The connection to the client that issued the request.
  ///\return A timestamp indicating when the request was parsed.
  long long int proxyHandleInternal(HTTP::Parser & H, Socket::Connection & conn){

    std::string url = H.getUrl();

    if (url == "/crossdomain.xml"){
      H.Clean();
      H.SetHeader("Content-Type", "text/xml");
      H.SetHeader("Server", "mistserver/" PACKAGE_VERSION "/" + Util::Config::libver);
      H.SetBody(
          "<?xml version=\"1.0\"?><!DOCTYPE cross-domain-policy SYSTEM \"http://www.adobe.com/xml/dtds/cross-domain-policy.dtd\"><cross-domain-policy><allow-access-from domain=\"*\" /><site-control permitted-cross-domain-policies=\"all\"/></cross-domain-policy>");
      long long int ret = Util::getMS();
      conn.SendNow(H.BuildResponse("200", "OK"));
      return ret;
    } //crossdomain.xml

    if (url == "/clientaccesspolicy.xml"){
      H.Clean();
      H.SetHeader("Content-Type", "text/xml");
      H.SetHeader("Server", "mistserver/" PACKAGE_VERSION "/" + Util::Config::libver);
      H.SetBody(
          "<?xml version=\"1.0\" encoding=\"utf-8\"?><access-policy><cross-domain-access><policy><allow-from http-methods=\"*\" http-request-headers=\"*\"><domain uri=\"*\"/></allow-from><grant-to><resource path=\"/\" include-subpaths=\"true\"/></grant-to></policy></cross-domain-access></access-policy>");
      long long int ret = Util::getMS();
      conn.SendNow(H.BuildResponse("200", "OK"));
      return ret;
    } //clientaccesspolicy.xml
    
    // send logo icon
    if (url.length() > 4 && url.substr(url.length() - 4, 4) == ".ico"){
      /*LTS-START*/
      if (H.GetVar("s") == SUPER_SECRET){
        H.Clean();
        H.SetHeader("Server", "mistserver/" PACKAGE_VERSION "/" + Util::Config::libver);
        H.SetBody("Yup");
        long long int retv = Util::getMS();
        H.SendResponse("200", "OK", conn);
        return retv;
      }
      /*LTS-END*/
      H.Clean();
#include "icon.h"
      H.SetHeader("Content-Type", "image/x-icon");
      H.SetHeader("Server", "mistserver/" PACKAGE_VERSION "/" + Util::Config::libver);
      H.SetHeader("Content-Length", icon_len);
      H.SetBody("");
      long long int ret = Util::getMS();
      conn.SendNow(H.BuildResponse("200", "OK"));
      conn.SendNow((const char*)icon_data, icon_len);
      return ret;
    }

    // send logo icon
    if (url.length() > 6 && url.substr(url.length() - 5, 5) == ".html"){
      std::string streamname = url.substr(1, url.length() - 6);
      Util::Stream::sanitizeName(streamname);
      H.Clean();
      H.SetHeader("Content-Type", "text/html");
      H.SetHeader("Server", "mistserver/" PACKAGE_VERSION "/" + Util::Config::libver);
      H.SetBody("<!DOCTYPE html><html><head><title>Stream "+streamname+"</title><style>BODY{color:white;background:black;}</style></head><body><script src=\"embed_"+streamname+".js\"></script></body></html>");
      long long int ret = Util::getMS();
      conn.SendNow(H.BuildResponse("200", "OK"));
      return ret;
    }
    
    // send smil MBR index
    if (url.length() > 6 && url.substr(url.length() - 5, 5) == ".smil"){
      std::string streamname = url.substr(1, url.length() - 6);
      Util::Stream::sanitizeName(streamname);
      JSON::Value ServConf = JSON::fromFile(Util::getTmpFolder() + "streamlist");
      
      std::string host = H.GetHeader("Host");
      if (host.find(':')){
        host.resize(host.find(':'));
      }
      
      std::string port, url_rel;
      
      for (JSON::ArrIter it = ServConf["config"]["protocols"].ArrBegin(); it != ServConf["config"]["protocols"].ArrEnd(); it++){
        const std::string & cName = ( *it)["connector"].asStringRef();
        if (cName != "RTMP"){continue;}
        //if we have the RTMP port,
        if (capabilities.isMember(cName) && capabilities[cName].isMember("optional") && capabilities[cName]["optional"].isMember("port")){
          //get the default port if none is set
          if (( *it)["port"].asInt() == 0){
            port = capabilities[cName]["optional"]["port"]["default"].asString();
          }
          //extract url
          if (capabilities[cName].isMember("url_rel")){
            url_rel = capabilities[cName]["url_rel"].asString();
            if (url_rel.find('$')){
              url_rel.resize(url_rel.find('$'));
            }
          }
        }
      }

      std::string trackSources;//this string contains all track sources for MBR smil
      for (JSON::ObjIter it = ServConf["streams"][streamname]["meta"]["tracks"].ObjBegin(); it != ServConf["streams"][streamname]["meta"]["tracks"].ObjEnd(); it++){//for all tracks
        if (it->second.isMember("type") && it->second["type"].asString() == "video"){
          trackSources += "      <video src='"+ streamname + "?track=" + it->second["trackid"].asString() + "' height='" + it->second["height"].asString() + "' system-bitrate='" + it->second["bps"].asString() + "' width='" + it->second["width"].asString() + "' />\n";
        }
      }

      H.Clean();
      H.SetHeader("Content-Type", "application/smil");
      H.SetHeader("Server", "mistserver/" PACKAGE_VERSION "/" + Util::Config::libver);
      H.SetBody("<smil>\n  <head>\n    <meta base='rtmp://" + host + ":" + port + url_rel + "' />\n  </head>\n  <body>\n    <switch>\n"+trackSources+"    </switch>\n  </body>\n</smil>");
      long long int ret = Util::getMS();
      conn.SendNow(H.BuildResponse("200", "OK"));
      return ret;
    }
    
    if ((url.length() > 9 && url.substr(0, 6) == "/info_" && url.substr(url.length() - 3, 3) == ".js")
        || (url.length() > 10 && url.substr(0, 7) == "/embed_" && url.substr(url.length() - 3, 3) == ".js")){
      std::string streamname;
      if (url.substr(0, 6) == "/info_"){
        streamname = url.substr(6, url.length() - 9);
      }else{
        streamname = url.substr(7, url.length() - 10);
      }
      Util::Stream::sanitizeName(streamname);
      //JSON::Value ServConf = JSON::fromFile(Util::getTmpFolder() + "streamlist");
      std::string response;
      std::string host = H.GetHeader("Host");
      if (host.find(':')){
        host.resize(host.find(':'));
      }
      H.Clean();
      H.SetHeader("Server", "mistserver/" PACKAGE_VERSION "/" + Util::Config::libver);
      H.SetHeader("Content-Type", "application/javascript");
      response = "// Generating info code for stream " + streamname + "\n\nif (!mistvideo){var mistvideo = {};}\n";
      JSON::Value json_resp;
      if (ServConf["streams"].isMember(streamname) && ServConf["config"]["protocols"].size() > 0){
        if (ServConf["streams"][streamname]["meta"].isMember("tracks") && ServConf["streams"][streamname]["meta"]["tracks"].size() > 0){
          for (JSON::ObjIter it = ServConf["streams"][streamname]["meta"]["tracks"].ObjBegin(); it != ServConf["streams"][streamname]["meta"]["tracks"].ObjEnd(); it++){
            if (it->second.isMember("width") && it->second["width"].asInt() > json_resp["width"].asInt()){
              json_resp["width"] = it->second["width"].asInt();
            }
            if (it->second.isMember("height") && it->second["height"].asInt() > json_resp["height"].asInt()){
              json_resp["height"] = it->second["height"].asInt();
            }
          }
        }
        if (json_resp["width"].asInt() < 1 || json_resp["height"].asInt() < 1){
          json_resp["width"] = 640ll;
          json_resp["height"] = 480ll;
        }
        if (ServConf["streams"][streamname]["meta"].isMember("vod")){
          json_resp["type"] = "vod";
        }
        if (ServConf["streams"][streamname]["meta"].isMember("live")){
          json_resp["type"] = "live";
        }

        // show ALL the meta datas!
        json_resp["meta"] = ServConf["streams"][streamname]["meta"];

        //create a set for storing source information
        std::set<JSON::Value, sourceCompare> sources;

        //find out which connectors are enabled
        std::set<std::string> conns;
        for (JSON::ArrIter it = ServConf["config"]["protocols"].ArrBegin(); it != ServConf["config"]["protocols"].ArrEnd(); it++){
          conns.insert(( *it)["connector"].asStringRef());
        }
        //loop over the connectors.
        for (JSON::ArrIter it = ServConf["config"]["protocols"].ArrBegin(); it != ServConf["config"]["protocols"].ArrEnd(); it++){
          const std::string & cName = ( *it)["connector"].asStringRef();
          //if the connector has a port,
          if (capabilities.isMember(cName) && capabilities[cName].isMember("optional") && capabilities[cName]["optional"].isMember("port")){
            //get the default port if none is set
            if (( *it)["port"].asInt() == 0){
              ( *it)["port"] = capabilities[cName]["optional"]["port"]["default"];
            }
            //and a URL - then list the URL
            if (capabilities[cName].isMember("url_rel")){
              addSources(streamname, capabilities[cName]["url_rel"].asStringRef(), sources, host, ( *it)["port"].asString(), capabilities[cName], ServConf["streams"][streamname]["meta"]);
            }
            //check each enabled protocol separately to see if it depends on this connector
            for (JSON::ObjIter oit = capabilities.ObjBegin(); oit != capabilities.ObjEnd(); oit++){
              //if it depends on this connector and has a URL, list it
              if (conns.count(oit->first) && (oit->second["deps"].asStringRef() == cName || oit->second["deps"].asStringRef() + ".exe" == cName) && oit->second.isMember("methods")){
                addSources(streamname, oit->second["url_rel"].asStringRef(), sources, host, ( *it)["port"].asString(), oit->second, ServConf["streams"][streamname]["meta"]);
              }
            }
          }
        }
        
        //loop over the added sources, add them to json_resp["sources"]
        for (std::set<JSON::Value, sourceCompare>::iterator it = sources.begin(); it != sources.end(); it++){
          if ((*it)["simul_tracks"].asInt() > 0){
            json_resp["source"].append(*it);
          }
        }
      }else{
        json_resp["error"] = "The specified stream is not available on this server.";
      }
      response += "mistvideo['" + streamname + "'] = " + json_resp.toString() + ";\n";
      if (url.substr(0, 6) != "/info_" && !json_resp.isMember("error")){
        response.append("\n(");
        if (embed_js[embed_js_len - 2] == ';'){//check if we have a trailing ;\n or just \n
          response.append((char*)embed_js, (size_t)embed_js_len - 2); //remove trailing ";\n" from xxd conversion
        }else{
          response.append((char*)embed_js, (size_t)embed_js_len - 1); //remove trailing "\n" from xxd conversion
        }
        response.append("(\"" + streamname + "\"));\n");
      }
      H.SetBody(response);
      long long int ret = Util::getMS();
      conn.SendNow(H.BuildResponse("200", "OK"));
      return ret;
    } //embed code generator

    return proxyHandleUnsupported(H, conn); //anything else doesn't get handled
  }

  ///\brief Handles requests by starting a corresponding output process.
  ///\param H The request to be handled
  ///\param conn The connection to the client that issued the request.
  ///\param connector The type of connector to be invoked.
  ///\return -1 on failure, else 0.
  long long int proxyHandleThroughConnector(HTTP::Parser & H, Socket::Connection & conn, std::string & connector){
    static unsigned long long int confUpdateTime=0;
    static tthread::mutex updateLock;
    if( Util::bootSecs() -confUpdateTime > 10 ){
       tthread::lock_guard<tthread::mutex> guard(updateLock);  
       if( Util::bootSecs() -confUpdateTime > 10 ){
         Connector_HTTP::ServConf = JSON::fromFile(Util::getTmpFolder() + "streamlist");
         confUpdateTime=Util::bootSecs();
       }
    }
    
    //create a unique ID based on a hash of the user agent and host, followed by the stream name and connector
    std::stringstream uidtemp;
    /// \todo verify the correct formation of the uid
    uidtemp << Secure::md5(H.GetHeader("User-Agent") + conn.getHost()) << "_" << H.GetVar("stream") << "_" << connector;
    std::string uid = uidtemp.str();

    //fdIn and fdOut are connected to conn.sock 
    int fdIn = conn.getSocket();
    int fdOut = conn.getSocket();

	//taken from CheckProtocols (controller_connectors.cpp)
    char * argarr[20];
    for (int i=0; i<20; i++){argarr[i] = 0;}
	int id = -1;
		
	for (unsigned int i=0; i < ServConf["config"]["protocols"].size(); ++i){
	  if ( ServConf["config"]["protocols"][i]["connector"].asStringRef() == connector ) {
		id =  i;
		break;  	//pick the first protocol in the list that matches the connector 
      }
	}
	if (id == -1) {
		DEBUG_MSG(DLVL_ERROR, "No connector found for: %s", connector.c_str());
		return -1;
	}

	DEBUG_MSG(DLVL_HIGH, "Connector found: %s", connector.c_str());
    //build arguments for starting output process
    
    std::string temphost=conn.getHost();
    std::string tempstream=H.GetVar("stream");
   // buildPipedArguments( , (char **)&argarr, Connector_HTTP::capabilities, temphost, tempstream);
	int argnum = 0;
    
    std::string tmparg;
    tmparg = Util::getMyPath() + std::string("MistOut") + ServConf["config"]["protocols"][id]["connector"].asStringRef();
    struct stat buf;
    if (::stat(tmparg.c_str(), &buf) != 0){
      tmparg = Util::getMyPath() + std::string("MistConn") + ServConf["config"]["protocols"][id]["connector"].asStringRef();
    }
    
    argarr[argnum++] = (char*)tmparg.c_str();
    JSON::Value & p = ServConf["config"]["protocols"][id];
    JSON::Value & pipedCapa = capabilities[p["connector"].asStringRef()];
    /// \todo why is the if(pipedCapa) line not working (nothing is added to argarr)??
    if (pipedCapa.isMember("required")){builPipedPart(p, argarr, argnum, pipedCapa["required"]);}
    if (pipedCapa.isMember("optional")){builPipedPart(p, argarr, argnum, pipedCapa["optional"]);}
	
    argarr[argnum++] = (char*)"-i";
    argarr[argnum++] = (char*)(temphost.c_str());
    argarr[argnum++] = (char*)"-s";
    argarr[argnum++] = (char*)(tempstream.c_str());
	
    //for (unsigned int i=0; i<20; i++){
    //std::cerr << "argv["<<i<< "] " << argarr[i] <<std::endl;
    //}
	
	//std::cerr << "p: " << p.toPrettyString() <<std::endl;
	//std::cerr << "pipedCapa: " << pipedCapa.toPrettyString() <<std::endl;
	//std::cerr << "capa: " << capabilities.toPrettyString() <<std::endl;
	
	int tempint = fileno(stderr);
    ///start output process, fdIn and fdOut are connected to conn.sock
	Util::Procs::StartPiped(argarr, & fdIn, & fdOut, & tempint);
	conn.drop();
	return 0;
  }

  ///\brief Determines the type of connector to be used for handling a request.
  ///\param H The request to be handled..
  ///\return A string indicating the type of connector.
  ///Possible values are:
  /// - "none" The request is not supported.
  /// - "internal" The request should be handled by the proxy itself.
  /// - anything else: The request should be dispatched to a connector on the named socket.
  std::string proxyGetHandleType(HTTP::Parser & H){
    std::string url = H.getUrl();
    
    //loop over the connectors
    for (JSON::ObjIter oit = capabilities.ObjBegin(); oit != capabilities.ObjEnd(); oit++){
      //if it depends on HTTP and has a match or prefix...
      if (oit->second["deps"].asStringRef() == "HTTP" && oit->second.isMember("socket") && (oit->second.isMember("url_match") || oit->second.isMember("url_prefix"))){
        //if there is a matcher, try to match
        if (oit->second.isMember("url_match")){
          size_t found = oit->second["url_match"].asStringRef().find('$');
          if (found != std::string::npos){
            if (oit->second["url_match"].asStringRef().substr(0, found) == url.substr(0, found) && oit->second["url_match"].asStringRef().substr(found+1) == url.substr(url.size() - (oit->second["url_match"].asStringRef().size() - found) + 1)){
              //it matched - handle it now
              std::string streamname = url.substr(found, url.size() - oit->second["url_match"].asStringRef().size() + 1);
              Util::Stream::sanitizeName(streamname);
              H.SetVar("stream", streamname);             
              return oit->first;
            }
          }
        }
        //if there is a prefix, try to match
        if (oit->second.isMember("url_prefix")){
          size_t found = oit->second["url_prefix"].asStringRef().find('$');
          if (found != std::string::npos){
            size_t found_suf = url.find(oit->second["url_prefix"].asStringRef().substr(found+1), found);
            if (oit->second["url_prefix"].asStringRef().substr(0, found) == url.substr(0, found) && found_suf != std::string::npos){
              //it matched - handle it now
              std::string streamname = url.substr(found, found_suf - found);
              Util::Stream::sanitizeName(streamname);
              H.SetVar("stream", streamname);
              return oit->first;
            }
          }
        }
      }
    }
  
    if (url.length() > 4){
      std::string ext = url.substr(url.length() - 4, 4);
      if (ext == ".ico"){
        return "internal";
      }
      if (url.length() > 6 && url.substr(url.length() - 5, 5) == ".html"){
        return "internal";
      }
      if (url.length() > 6 && url.substr(url.length() - 5, 5) == ".smil"){
        return "internal";
      }
    }
    if (url == "/crossdomain.xml"){
      return "internal";
    }
    if (url == "/clientaccesspolicy.xml"){
      return "internal";
    }
    if (url.length() > 10 && url.substr(0, 7) == "/embed_" && url.substr(url.length() - 3, 3) == ".js"){
      return "internal";
    }
    if (url.length() > 9 && url.substr(0, 6) == "/info_" && url.substr(url.length() - 3, 3) == ".js"){
      return "internal";
    }
    return "none";
  }

  ///\brief Function run as a thread to handle a single HTTP connection.
  ///\param conn A Socket::Connection indicating the connection to th client.
  int proxyHandleHTTPConnection(Socket::Connection & conn){
    conn.setBlocking(false); //do not block on conn.spool() when no data is available
    HTTP::Parser Client;
    while (conn.connected()){
        //conn.peek reads data without removing it from pipe
        if (conn.peek() && Client.Read(conn)){
          std::string handler = proxyGetHandleType(Client);
          DEBUG_MSG(DLVL_HIGH, "Received request: %s (%d) => %s (%s)", Client.getUrl().c_str(), conn.getSocket(), handler.c_str(), Client.GetVar("stream").c_str());
 
          bool closeConnection = false;
          if (Client.GetHeader("Connection") == "close"){
            closeConnection = true;
          }

          if (handler == "none" || handler == "internal"){
            if (handler == "internal"){
              proxyHandleInternal(Client, conn);
            }else{
              proxyHandleUnsupported(Client, conn);
            }
          }else{
            proxyHandleThroughConnector(Client, conn, handler);
          }
          DEBUG_MSG(DLVL_HIGH, "Completed request %d (%s) ", conn.getSocket(), handler.c_str());
          if (closeConnection){
            break;
          }
          Client.Clean(); //clean for any possible next requests
      }else{
        Util::sleep(10); //sleep 10ms
      }
    }
    //close and remove the connection
    conn.close();
    return 0;
  }

} //Connector_HTTP namespace

int main(int argc, char ** argv){
  Util::Config conf(argv[0], PACKAGE_VERSION);
  JSON::Value capa;
  capa["optional"]["debug"]["name"] = "debug";
  capa["optional"]["debug"]["help"] = "The debug level at which messages need to be printed.";
  capa["optional"]["debug"]["option"] = "--debug";
  capa["optional"]["debug"]["type"] = "uint";
  Connector_HTTP::ServConf = JSON::fromFile(Util::getTmpFolder() + "streamlist");
  capa["desc"] = "Enables the generic HTTP listener, required by all other HTTP protocols. Needs other HTTP protocols enabled to do much of anything.";
  capa["deps"] = "";
  conf.addConnectorOptions(8080, capa);
  conf.parseArgs(argc, argv);
  if (conf.getBool("json")){
    std::cout << capa.toString() << std::endl;
    return -1;
  }
  
  //list available protocols and report about them
  std::deque<std::string> execs;
  Util::getMyExec(execs);
  std::string arg_one;
  char const * conn_args[] = {0, "-j", 0};
  for (std::deque<std::string>::iterator it = execs.begin(); it != execs.end(); it++){
    if ((*it).substr(0, 8) == "MistConn"){
      arg_one = Util::getMyPath() + (*it);
      conn_args[0] = arg_one.c_str();
      Connector_HTTP::capabilities[(*it).substr(8)] = JSON::fromString(Util::Procs::getOutputOf((char**)conn_args));
      if (Connector_HTTP::capabilities[(*it).substr(8)].size() < 1){
        Connector_HTTP::capabilities.removeMember((*it).substr(8));
      }
    }
    if ((*it).substr(0, 7) == "MistOut"){
      arg_one = Util::getMyPath() + (*it);
      conn_args[0] = arg_one.c_str();
      Connector_HTTP::capabilities[(*it).substr(7)] = JSON::fromString(Util::Procs::getOutputOf((char**)conn_args));
      if (Connector_HTTP::capabilities[(*it).substr(7)].size() < 1){
        Connector_HTTP::capabilities.removeMember((*it).substr(7));
      }
    }
  }
  
  return conf.serveThreadedSocket(Connector_HTTP::proxyHandleHTTPConnection);
} //main
