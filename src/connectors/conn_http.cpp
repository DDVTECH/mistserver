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
#include <mist/dtsc.h>
#include <mist/shared_memory.h>

#include "embed.js.h"


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
  IPC::sharedPage serverCfg; ///< Contains server configuration and capabilities

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
      long long int ret = Util::getMS();
      H.SendResponse("200", "OK", conn);
      conn.SendNow((const char*)icon_data, icon_len);
      return ret;
    }

    // send logo icon
    if (url.length() > 6 && url.substr(url.length() - 5, 5) == ".html"){
      std::string streamname = url.substr(1, url.length() - 6);
      Util::sanitizeName(streamname);
      H.Clean();
      H.SetHeader("Content-Type", "text/html");
      H.SetHeader("Server", "mistserver/" PACKAGE_VERSION "/" + Util::Config::libver);
      H.SetBody("<!DOCTYPE html><html><head><title>Stream "+streamname+"</title><style>BODY{color:white;background:black;}</style></head><body><script src=\"embed_"+streamname+".js\"></script></body></html>");
      long long int ret = Util::getMS();
      H.SendResponse("200", "OK", conn);
      return ret;
    }
    
    // send smil MBR index
    if (url.length() > 6 && url.substr(url.length() - 5, 5) == ".smil"){
      std::string streamname = url.substr(1, url.length() - 6);
      Util::sanitizeName(streamname);
      
      std::string host = H.GetHeader("Host");
      if (host.find(':')){
        host.resize(host.find(':'));
      }
      
      std::string port, url_rel;
      
      IPC::semaphore configLock("!mistConfLock", O_CREAT | O_RDWR, ACCESSPERMS, 1);
      configLock.wait();
      DTSC::Scan prtcls = DTSC::Scan(serverCfg.mapped, serverCfg.len).getMember("config").getMember("protocols");
      DTSC::Scan capa = DTSC::Scan(serverCfg.mapped, serverCfg.len).getMember("capabilities").getMember("connectors").getMember("RTMP");
      unsigned int pro_cnt = prtcls.getSize();
      for (unsigned int i = 0; i < pro_cnt; ++i){
        if (prtcls.getIndice(i).getMember("connector").asString() != "RTMP"){
          continue;
        }
        port = prtcls.getIndice(i).getMember("port").asString();
        //get the default port if none is set
        if (!port.size()){
          port = capa.getMember("optional").getMember("port").getMember("default").asString();
        }
        //extract url
        url_rel = capa.getMember("url_rel").asString();
        if (url_rel.find('$')){
          url_rel.resize(url_rel.find('$'));
        }
      }

      std::string trackSources;//this string contains all track sources for MBR smil
      DTSC::Scan tracks = DTSC::Scan(serverCfg.mapped, serverCfg.len).getMember("streams").getMember(streamname).getMember("meta").getMember("tracks");
      unsigned int track_ctr = tracks.getSize();
      for (unsigned int i = 0; i < track_ctr; ++i){//for all video tracks
        DTSC::Scan trk = tracks.getIndice(i);
        if (trk.getMember("type").asString() == "video"){
          trackSources += "      <video src='"+ streamname + "?track=" + trk.getMember("trackid").asString() + "' height='" + trk.getMember("height").asString() + "' system-bitrate='" + trk.getMember("bps").asString() + "' width='" + trk.getMember("width").asString() + "' />\n";
        }
      }
      configLock.post();
      configLock.close();

      H.Clean();
      H.SetHeader("Content-Type", "application/smil");
      H.SetHeader("Server", "mistserver/" PACKAGE_VERSION "/" + Util::Config::libver);
      H.SetBody("<smil>\n  <head>\n    <meta base='rtmp://" + host + ":" + port + url_rel + "' />\n  </head>\n  <body>\n    <switch>\n"+trackSources+"    </switch>\n  </body>\n</smil>");
      long long int ret = Util::getMS();
      H.SendResponse("200", "OK", conn);
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
      Util::sanitizeName(streamname);
      
      std::string response;
      std::string host = H.GetHeader("Host");
      if (host.find(':') != std::string::npos){
        host.resize(host.find(':'));
      }
      H.Clean();
      H.SetHeader("Server", "mistserver/" PACKAGE_VERSION "/" + Util::Config::libver);
      H.SetHeader("Content-Type", "application/javascript");
      response = "// Generating info code for stream " + streamname + "\n\nif (!mistvideo){var mistvideo = {};}\n";
      JSON::Value json_resp;
      IPC::semaphore configLock("!mistConfLock", O_CREAT | O_RDWR, ACCESSPERMS, 1);
      configLock.wait();
      DTSC::Scan strm = DTSC::Scan(serverCfg.mapped, serverCfg.len).getMember("streams").getMember(streamname).getMember("meta");
      IPC::sharedPage streamIndex;
      if (!strm){
        //Stream metadata not found - attempt to start it
        if (Util::startInput(streamname)){
          streamIndex.init(streamname, 8 * 1024 * 1024);
          if (streamIndex.mapped){
            strm = DTSC::Packet(streamIndex.mapped, streamIndex.len, true).getScan();
          }
        }
        if (!strm){
          //stream failed to start or isn't configured
          response += "// Stream isn't configured and/or couldn't be started. Sorry.\n";
        }
      }
      DTSC::Scan prots = DTSC::Scan(serverCfg.mapped, serverCfg.len).getMember("config").getMember("protocols");
      if (strm && prots){
        DTSC::Scan trcks = strm.getMember("tracks");
        unsigned int trcks_ctr = trcks.getSize();
        for (unsigned int i = 0; i < trcks_ctr; ++i){
          if (trcks.getIndice(i).getMember("width").asInt() > json_resp["width"].asInt()){
            json_resp["width"] = trcks.getIndice(i).getMember("width").asInt();
          }
          if (trcks.getIndice(i).getMember("height").asInt() > json_resp["height"].asInt()){
            json_resp["height"] = trcks.getIndice(i).getMember("height").asInt();
          }
        }
        if (json_resp["width"].asInt() < 1 || json_resp["height"].asInt() < 1){
          json_resp["width"] = 640ll;
          json_resp["height"] = 480ll;
        }
        if (strm.getMember("vod")){
          json_resp["type"] = "vod";
        }
        if (strm.getMember("live")){
          json_resp["type"] = "live";
        }

        // show ALL the meta datas!
        json_resp["meta"] = strm.asJSON();

        //create a set for storing source information
        std::set<JSON::Value, sourceCompare> sources;

        //find out which connectors are enabled
        std::set<std::string> conns;
        unsigned int prots_ctr = prots.getSize();
        for (unsigned int i = 0; i < prots_ctr; ++i){
          conns.insert(prots.getIndice(i).getMember("connector").asString());
        }
        //loop over the connectors.
        for (unsigned int i = 0; i < prots_ctr; ++i){
          std::string cName = prots.getIndice(i).getMember("connector").asString();
          DTSC::Scan capa = DTSC::Scan(serverCfg.mapped, serverCfg.len).getMember("capabilities").getMember("connectors").getMember(cName);
          //if the connector has a port,
          if (capa.getMember("optional").getMember("port")){
            //get the default port if none is set
            std::string port = prots.getIndice(i).getMember("port").asString();
            if (!port.size()){
              port = capa.getMember("optional").getMember("port").getMember("default").asString();
            }
            //and a URL - then list the URL
            if (capa.getMember("url_rel")){
              JSON::Value capa_json = capa.asJSON();
              addSources(streamname, capa.getMember("url_rel").asString(), sources, host, port, capa_json, json_resp["meta"]);
            }
            //check each enabled protocol separately to see if it depends on this connector
            DTSC::Scan capa_lst = DTSC::Scan(serverCfg.mapped, serverCfg.len).getMember("capabilities").getMember("connectors");
            unsigned int capa_lst_ctr = capa_lst.getSize();
            for (unsigned int j = 0; j < capa_lst_ctr; ++j){
              //if it depends on this connector and has a URL, list it
              if (conns.count(capa_lst.getIndiceName(j)) && (capa_lst.getIndice(j).getMember("deps").asString() == cName || capa_lst.getIndice(j).getMember("deps").asString() + ".exe" == cName) && capa_lst.getIndice(j).getMember("methods")){
                JSON::Value capa_json = capa_lst.getIndice(j).asJSON();
                addSources(streamname, capa_lst.getIndice(j).getMember("url_rel").asString(), sources, host, port, capa_json, json_resp["meta"]);
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
      configLock.post();
      configLock.close();
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
      H.SendResponse("200", "OK", conn);
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

    IPC::semaphore configLock("!mistConfLock", O_CREAT | O_RDWR, ACCESSPERMS, 1);
    configLock.wait();
    DTSC::Scan prots = DTSC::Scan(serverCfg.mapped, serverCfg.len).getMember("config").getMember("protocols");
    unsigned int prots_ctr = prots.getSize();
    
    for (unsigned int i=0; i < prots_ctr; ++i){
      if (prots.getIndice(i).getMember("connector").asString() == connector) {
        id =  i;
        break;  	//pick the first protocol in the list that matches the connector 
      }
    }
    if (id == -1) {
      DEBUG_MSG(DLVL_ERROR, "No connector found for: %s", connector.c_str());
      configLock.post();
      configLock.close();
      return -1;
    }

    DEBUG_MSG(DLVL_HIGH, "Connector found: %s", connector.c_str());
    //build arguments for starting output process

    std::string temphost=conn.getHost();
    std::string tempstream=H.GetVar("stream");
    std::string debuglevel = JSON::Value((long long)Util::Config::printDebugLevel).asString();

    std::string tmparg;
    tmparg = Util::getMyPath() + std::string("MistOut") + connector;
    struct stat buf;
    if (::stat(tmparg.c_str(), &buf) != 0){
      tmparg = Util::getMyPath() + std::string("MistConn") + connector;
    }

    int argnum = 0;
    argarr[argnum++] = (char*)tmparg.c_str();
    JSON::Value p = prots.getIndice(id).asJSON();
    JSON::Value pipedCapa = DTSC::Scan(serverCfg.mapped, serverCfg.len).getMember("capabilities").getMember("connectors").getMember(connector).asJSON();
    configLock.post();
    configLock.close();
    argarr[argnum++] = (char*)"-i";
    argarr[argnum++] = (char*)(temphost.c_str());
    argarr[argnum++] = (char*)"-s";
    argarr[argnum++] = (char*)(tempstream.c_str());
    //set the debug level if non-default
    if (Util::Config::printDebugLevel != DEBUG){
      argarr[argnum++] = (char*)"--debug";
      argarr[argnum++] = (char*)(debuglevel.c_str());
    }
    if (pipedCapa.isMember("required")){builPipedPart(p, argarr, argnum, pipedCapa["required"]);}
    if (pipedCapa.isMember("optional")){builPipedPart(p, argarr, argnum, pipedCapa["optional"]);}
    
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
    
    //loop over the connectors
    IPC::semaphore configLock("!mistConfLock", O_CREAT | O_RDWR, ACCESSPERMS, 1);
    configLock.wait();
    DTSC::Scan capa = DTSC::Scan(serverCfg.mapped, serverCfg.len).getMember("capabilities").getMember("connectors");
    unsigned int capa_ctr = capa.getSize();
    for (unsigned int i = 0; i < capa_ctr; ++i){
      DTSC::Scan c = capa.getIndice(i);
      //if it depends on HTTP and has a match or prefix...
      if (c.getMember("deps").asString() == "HTTP" && (c.getMember("url_match") || c.getMember("url_prefix"))){
        //if there is a matcher, try to match
        if (c.getMember("url_match")){
          std::string m = c.getMember("url_match").asString();
          size_t found = m.find('$');
          if (found != std::string::npos){
            if (m.substr(0, found) == url.substr(0, found) && m.substr(found+1) == url.substr(url.size() - (m.size() - found) + 1)){
              //it matched - handle it now
              std::string streamname = url.substr(found, url.size() - m.size() + 1);
              Util::sanitizeName(streamname);
              H.SetVar("stream", streamname);
              configLock.post();
              configLock.close();
              return capa.getIndiceName(i);
            }
          }
        }
        //if there is a prefix, try to match
        if (c.getMember("url_prefix")){
          std::string m = c.getMember("url_prefix").asString();
          size_t found = m.find('$');
          if (found != std::string::npos){
            size_t found_suf = url.find(m.substr(found+1), found);
            if (m.substr(0, found) == url.substr(0, found) && found_suf != std::string::npos){
              //it matched - handle it now
              std::string streamname = url.substr(found, found_suf - found);
              Util::sanitizeName(streamname);
              H.SetVar("stream", streamname);
              configLock.post();
              configLock.close();
              return capa.getIndiceName(i);
            }
          }
        }
      }
    }
    configLock.post();
    configLock.close();
    
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
            Client.Clean();
            conn.Received().clear();
            conn.spool();
            Client.Read(conn);
            if (handler == "internal"){
              proxyHandleInternal(Client, conn);
            }else{
              proxyHandleUnsupported(Client, conn);
            }
          }else{
            proxyHandleThroughConnector(Client, conn, handler);
            if (conn.connected()){
              FAIL_MSG("Request %d (%s) failed - no connector started", conn.getSocket(), handler.c_str());
            }
            break;
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
  capa["desc"] = "Enables the generic HTTP listener, required by all other HTTP protocols. Needs other HTTP protocols enabled to do much of anything.";
  capa["deps"] = "";
  conf.addConnectorOptions(8080, capa);
  conf.parseArgs(argc, argv);
  if (conf.getBool("json")){
    std::cout << capa.toString() << std::endl;
    return -1;
  }
  Connector_HTTP::serverCfg.init("!mistConfig", 4*1024*1024);
  return conf.serveThreadedSocket(Connector_HTTP::proxyHandleHTTPConnection);
}
