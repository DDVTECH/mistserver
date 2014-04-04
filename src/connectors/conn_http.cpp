/// \file conn_http.cpp
/// Contains the main code for the HTTP Connector

#include <iostream>
#include <queue>
#include <set>

#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
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


/// Holds everything unique to HTTP Connectors.
namespace Connector_HTTP {

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
    
    if ((url.length() > 9 && url.substr(0, 6) == "/info_" && url.substr(url.length() - 3, 3) == ".js")
        || (url.length() > 10 && url.substr(0, 7) == "/embed_" && url.substr(url.length() - 3, 3) == ".js")){
      std::string streamname;
      if (url.substr(0, 6) == "/info_"){
        streamname = url.substr(6, url.length() - 9);
      }else{
        streamname = url.substr(7, url.length() - 10);
      }
      Util::Stream::sanitizeName(streamname);
      JSON::Value ServConf = JSON::fromFile(Util::getTmpFolder() + "streamlist");
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

  ///\brief Handles requests by dispatching them to the corresponding connector.
  ///\param H The request to be handled.
  ///\param conn The connection to the client that issued the request.
  ///\param connector The type of connector to be invoked.
  ///\return A timestamp indicating when the request was parsed.
  long long int proxyHandleThroughConnector(HTTP::Parser & H, Socket::Connection & conn, std::string & connector){
    //create a unique ID based on a hash of the user agent and host, followed by the stream name and connector
    std::string uid = Secure::md5(H.GetHeader("User-Agent") + conn.getHost()) + "_" + H.GetVar("stream") + "_" + connector;
    H.SetHeader("X-Stream", H.GetVar("stream"));
    H.SetHeader("X-UID", uid); //add the UID to the headers before copying
    H.SetHeader("X-Origin", conn.getHost()); //add the UID to the headers before copying
    std::string request = H.BuildRequest(); //copy the request for later forwarding to the connector
    std::string orig_url = H.getUrl();
    H.Clean();

    ConnConn * myCConn = 0;
    unsigned int counter = 0;
    //loop until a connection is available/created
    while (!myCConn){
      //lock the connection mutex before trying anything
      connMutex.lock();
      //check if a connection exists, and if not create one
      if ( !connectorConnections.count(uid)){
        connectorConnections[uid] = new ConnConn(new Socket::Connection(Util::getTmpFolder() + connector));
        connectorConnections[uid]->conn->setBlocking(false); //do not block on spool() with no data
        if (!connectorConnections[uid]->conn->spool() && !connectorConnections[uid]->conn){
          //unlock the connection mutex before exiting
          connMutex.unlock();
          DEBUG_MSG(DLVL_FAIL, "Created new connection (%s) failed - aborting request!", uid.c_str());
          return Util::getMS();
        }
        DEBUG_MSG(DLVL_HIGH, "Created new connection %s", uid.c_str());
      }
      
      //attempt to lock the mutex for this connection
      if (connectorConnections[uid]->inUse.try_lock()){
        myCConn = connectorConnections[uid];
        //if the connection is dead, delete it and re-loop
        if (!myCConn->conn->spool() && !myCConn->conn->connected()){
          counter++;
          DEBUG_MSG(DLVL_HIGH, "Resetting existing connection %s", uid.c_str());
          connectorConnections.erase(uid);
          myCConn->inUse.unlock();
          delete myCConn;
          myCConn = 0;
          if (counter++ > 2){
            connMutex.unlock();
            DEBUG_MSG(DLVL_FAIL, "Created new connection (%s) failed - aborting request!", uid.c_str());
            return Util::getMS();
          }
        }else{
          DEBUG_MSG(DLVL_HIGH, "Using active connection %s", uid.c_str());
        }
      }
      //unlock the connection mutex before sleeping
      connMutex.unlock();
      //no connection yet? wait for 0.1 second and try again
      if ( !myCConn){
        Util::sleep(100);
      }
    }
    
    //we now have a locked, working connection
    
    {//start a new timeout thread, if neccesary
      tthread::lock_guard<tthread::mutex> guard(timeoutStartMutex);
      if (timeoutMutex.try_lock()){
        if (timeouter){
          timeouter->join();
          delete timeouter;
        }
        timeoutThreadStarted = false;
        timeouter = new tthread::thread(Connector_HTTP::proxyTimeoutThread, 0);
        timeoutMutex.unlock();
        while (!timeoutThreadStarted){Util::sleep(10);}
      }
    }

    //forward the original request
    myCConn->conn->SendNow(request);
    myCConn->lastUse = 0;
    unsigned int timeout = 0;
    unsigned int retries = 0;
    //set to only read headers
    H.headerOnly = true;
    //wait for a response
    while (myCConn->conn->connected() && conn.connected()){
      conn.spool();
        //check if the whole header was received
      if (myCConn->conn->spool() && H.Read(*(myCConn->conn))){
          //208 means the fragment is too new, retry in 3s
          if (H.url == "208"){
            while (myCConn->conn->Received().size() > 0){
              myCConn->conn->Received().get().clear();
            }
            retries++;
            if (retries >= 10){
              DEBUG_MSG(DLVL_HIGH, "Cancelled connection %s, because of 208 status repeated 10 times", uid.c_str());
              myCConn->conn->close();
              myCConn->inUse.unlock();
              //unset to only read headers
              H.headerOnly = false;
              return proxyHandleTimeout(H, conn, "Timeout: fragment too new");
            }
            myCConn->lastUse = 0;
            timeout = 0;
            Util::sleep(3000);
            myCConn->conn->SendNow(request);
            H.Clean();
            continue;
          }
          break; //continue down below this while loop
      }
      //keep trying unless the timeout triggers
      if (timeout++ > 4000){
        DEBUG_MSG(DLVL_HIGH, "Canceled connection %s, 4s timeout", uid.c_str());
        myCConn->conn->close();
        myCConn->inUse.unlock();
        //unset to only read headers
        H.headerOnly = false;
        return proxyHandleTimeout(H, conn, "Gateway timeout while waiting for response");
      }else{
        Util::sleep(100);
      }
    }
    //unset to only read headers
    H.headerOnly = false;
    if ( !myCConn->conn->connected() || !conn.connected()){
      //failure, disconnect and sent error to user
      myCConn->conn->close();
      myCConn->inUse.unlock();
      return proxyHandleTimeout(H, conn, "Gateway connection dropped");
    }else{
      long long int ret = Util::getMS();
      //success, check type of response
      if (H.GetHeader("MistMultiplex") != "No" && (H.GetHeader("Content-Length") != "" || H.GetHeader("Transfer-Encoding") == "chunked")){
        //known length - simply re-send the request with added headers and continue
        DEBUG_MSG(DLVL_HIGH, "Proxying %s - known length or chunked transfer encoding", uid.c_str());
        H.SetHeader("X-UID", uid);
        H.SetHeader("Server", "mistserver/" PACKAGE_VERSION "/" + Util::Config::libver);
        H.body = "";
        H.Proxy(*(myCConn->conn), conn);
        if (!conn.connected()){
          DEBUG_MSG(DLVL_HIGH, "Incoming connection to %s dropped, killing off connector", uid.c_str());
          myCConn->conn->close();
        }
        myCConn->inUse.unlock();
      }else{
        DEBUG_MSG(DLVL_HIGH, "Handing off %s - one-time connection", uid.c_str());
        //unknown length
        H.SetHeader("X-UID", uid);
        H.SetHeader("Server", "mistserver/" PACKAGE_VERSION "/" + Util::Config::libver);
        conn.SendNow(H.BuildResponse(H.url, H.method));
        //switch out the connection for an empty one - it makes no sense to keep these globally
        Socket::Connection * myConn = myCConn->conn;
        myCConn->conn = new Socket::Connection();
        myCConn->inUse.unlock();
        long long int last_data_time = Util::getMS();
        //continue sending data from this socket and keep it permanently in use
        while (myConn->connected() && conn.connected()){
          if (myConn->Received().size() || myConn->spool()){
            //forward any and all incoming data directly without parsing
            conn.SendNow(myConn->Received().get());
            myConn->Received().get().clear();
            last_data_time = Util::getMS();
          }else{
            Util::sleep(30);
            //if no data for 5000ms, cancel the connection
            if (Util::getMS() - last_data_time > 5000){
              break;
            }
          }
        }
        myConn->close();
        delete myConn;
        conn.close();
      }
      return ret;
    }
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
              return oit->second["socket"];
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
              return oit->second["socket"];
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
        if (conn.spool() && Client.Read(conn)){
          std::string handler = proxyGetHandleType(Client);
          DEBUG_MSG(DLVL_HIGH, "Received request: %s (%d) => %s (%s)", Client.getUrl().c_str(), conn.getSocket(), handler.c_str(), Client.GetVar("stream").c_str());
          #if DEBUG >= DLVL_HIGH
          long long int startms = Util::getMS();
          long long int midms = 0;
          #define MID_BENCH midms =
          #else
          #define MID_BENCH 
          #endif
          bool closeConnection = false;
          if (Client.GetHeader("Connection") == "close"){
            closeConnection = true;
          }

          if (handler == "none" || handler == "internal"){
            if (handler == "internal"){
              MID_BENCH proxyHandleInternal(Client, conn);
            }else{
              MID_BENCH proxyHandleUnsupported(Client, conn);
            }
          }else{
            MID_BENCH proxyHandleThroughConnector(Client, conn, handler);
          }
          #if DEBUG >= DLVL_HIGH
          long long int nowms = Util::getMS();
          DEBUG_MSG(DLVL_HIGH, "Completed request %d (%s) in %d ms (processing) / %d ms (transfer)", conn.getSocket(), handler.c_str(), (midms - startms), (nowms - midms));
          #endif
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

  if (Connector_HTTP::timeouter){
    Connector_HTTP::timeouter->detach();
    delete Connector_HTTP::timeouter;
  }
  return 0;
} //main
