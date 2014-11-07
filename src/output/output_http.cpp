#include <sys/stat.h>
#include "output_http.h"
#include <mist/stream.h>
#include <mist/checksum.h>

namespace Mist {
  HTTPOutput::HTTPOutput(Socket::Connection & conn) : Output(conn) {
    if (config->getString("ip").size()){
      myConn.setHost(config->getString("ip"));
    }
    if (config->getString("streamname").size()){
      streamName = config->getString("streamname");
    }
  }
  
  void HTTPOutput::init(Util::Config * cfg){
    Output::init(cfg);
    capa["deps"] = "HTTP";
    capa["forward"]["streamname"]["name"] = "Stream";
    capa["forward"]["streamname"]["help"] = "What streamname to serve.";
    capa["forward"]["streamname"]["type"] = "str";
    capa["forward"]["streamname"]["option"] = "--stream";
    capa["forward"]["ip"]["name"] = "IP";
    capa["forward"]["ip"]["help"] = "IP of forwarded connection.";
    capa["forward"]["ip"]["type"] = "str";
    capa["forward"]["ip"]["option"] = "--ip";
    cfg->addOption("streamname", JSON::fromString("{\"arg\":\"string\",\"short\":\"s\",\"long\":\"stream\",\"help\":\"The name of the stream that this connector will transmit.\"}"));
    cfg->addOption("ip", JSON::fromString("{\"arg\":\"string\",\"short\":\"I\",\"long\":\"ip\",\"help\":\"IP address of connection on stdio.\"}"));
    cfg->addBasicConnectorOptions(capa);
    config = cfg;
  }
  
  void HTTPOutput::onFail(){
    H.Clean(); //make sure no parts of old requests are left in any buffers
    H.SetBody("Stream not found. Sorry, we tried.");
    H.SendResponse("404", "Stream not found", myConn);
    Output::onFail();
  }
  
  bool isMatch(const std::string & url, const std::string & m, std::string & streamname){
    size_t found = m.find('$');
    if (found != std::string::npos){
      if (m.substr(0, found) == url.substr(0, found) && m.substr(found+1) == url.substr(url.size() - (m.size() - found) + 1)){
        streamname = url.substr(found, url.size() - m.size() + 1);
        return true;
      }
    }
    return (url == m);
  }
  
  bool isPrefix(const std::string & url, const std::string & m, std::string & streamname){
    size_t found = m.find('$');
    if (found != std::string::npos){
      size_t found_suf = url.find(m.substr(found+1), found);
      if (m.substr(0, found) == url.substr(0, found) && found_suf != std::string::npos){
        streamname = url.substr(found, found_suf - found);
        return true;
      }
    }
    return false;
  }
  
  /// - anything else: The request should be dispatched to a connector on the named socket.
  std::string HTTPOutput::getHandler(){
    std::string url = H.getUrl();
    //check the current output first, the most common case
    if (capa.isMember("url_match") || capa.isMember("url_prefix")){
      bool match = false;
      std::string streamname;
      //if there is a matcher, try to match
      if (capa.isMember("url_match")){
        if (capa["url_match"].isArray()){
          for (JSON::ArrIter it = capa["url_match"].ArrBegin(); it != capa["url_match"].ArrEnd(); ++it){
            match |= isMatch(url, it->asStringRef(), streamname);
          }
        }
        if (capa["url_match"].isString()){
          match |= isMatch(url, capa["url_match"].asStringRef(), streamname);
        }
      }
      //if there is a prefix, try to match
      if (capa.isMember("url_prefix")){
        if (capa["url_prefix"].isArray()){
          for (JSON::ArrIter it = capa["url_prefix"].ArrBegin(); it != capa["url_prefix"].ArrEnd(); ++it){
            match |= isPrefix(url, it->asStringRef(), streamname);
          }
        }
        if (capa["url_prefix"].isString()){
          match |= isPrefix(url, capa["url_prefix"].asStringRef(), streamname);
        }
      }
      if (match){
        if (streamname.size()){
          Util::sanitizeName(streamname);
          H.SetVar("stream", streamname);
        }
        return capa["name"].asStringRef();
      }
    }
    
    //loop over the connectors
    IPC::semaphore configLock("!mistConfLock", O_CREAT | O_RDWR, ACCESSPERMS, 1);
    configLock.wait();
    IPC::sharedPage serverCfg("!mistConfig", 4*1024*1024);
    DTSC::Scan capa = DTSC::Scan(serverCfg.mapped, serverCfg.len).getMember("capabilities").getMember("connectors");
    unsigned int capa_ctr = capa.getSize();
    for (unsigned int i = 0; i < capa_ctr; ++i){
      DTSC::Scan c = capa.getIndice(i);
      //if it depends on HTTP and has a match or prefix...
      if ((c.getMember("name").asString() == "HTTP" || c.getMember("deps").asString() == "HTTP") && (c.getMember("url_match") || c.getMember("url_prefix"))){
        bool match = false;
        std::string streamname;
        //if there is a matcher, try to match
        if (c.getMember("url_match")){
          if (c.getMember("url_match").getSize()){
            for (unsigned int j = 0; j < c.getMember("url_match").getSize(); ++j){
              match |= isMatch(url, c.getMember("url_match").getIndice(j).asString(), streamname);
            }
          }else{
            match |= isMatch(url, c.getMember("url_match").asString(), streamname);
          }
        }
        //if there is a prefix, try to match
        if (c.getMember("url_prefix")){
          if (c.getMember("url_prefix").getSize()){
            for (unsigned int j = 0; j < c.getMember("url_prefix").getSize(); ++j){
              match |= isPrefix(url, c.getMember("url_prefix").getIndice(j).asString(), streamname);
            }
          }else{
            match |= isPrefix(url, c.getMember("url_prefix").asString(), streamname);
          }
        }
        if (match){
          if (streamname.size()){
            Util::sanitizeName(streamname);
            H.SetVar("stream", streamname);
          }
          configLock.post();
          configLock.close();
          return capa.getIndiceName(i);
        }
      }
    }
    configLock.post();
    configLock.close();
    return "";
  }
  
  void HTTPOutput::requestHandler(){
    if (myConn.Received().size() && myConn.spool()){
      DEBUG_MSG(DLVL_DONTEVEN, "onRequest");
      onRequest();
    }else{
      if (!myConn.Received().size()){
        if (myConn.peek() && H.Read(myConn)){
          std::string handler = getHandler();
          DEBUG_MSG(DLVL_MEDIUM, "Received request: %s => %s (%s)", H.getUrl().c_str(), handler.c_str(), H.GetVar("stream").c_str());
          if (!handler.size()){
            H.Clean();
            H.SetHeader("Server", "mistserver/" PACKAGE_VERSION);
            H.SetBody("<!DOCTYPE html><html><head><title>Unsupported Media Type</title></head><body><h1>Unsupported Media Type</h1>The server isn't quite sure what you wanted to receive from it.</body></html>");
            H.SendResponse("415", "Unsupported Media Type", myConn);
            myConn.close();
            return;
          }
          if (handler != capa["name"].asStringRef() || H.GetVar("stream") != streamName){
            DEBUG_MSG(DLVL_MEDIUM, "Switching from %s (%s) to %s (%s)", capa["name"].asStringRef().c_str(), streamName.c_str(), handler.c_str(), H.GetVar("stream").c_str());
            streamName = H.GetVar("stream");
            playerConn.finish();
            statsPage.finish();
            reConnector(handler);
            H.Clean();
            if (myConn.connected()){
              FAIL_MSG("Request failed - no connector started");
              myConn.close();
            }
            return;
          }else{
            H.Clean();
            myConn.Received().clear();
            myConn.spool();
            DEBUG_MSG(DLVL_DONTEVEN, "onRequest");
            onRequest();
          }
        }else{
          H.Clean();
          if (myConn.Received().size()){
            myConn.Received().clear();
            myConn.spool();
            DEBUG_MSG(DLVL_DONTEVEN, "onRequest");
            onRequest();
          }
        }
      }else{
        if (!isBlocking && !parseData){
          Util::sleep(500);
        }
      }
    }
  }
  
  void HTTPOutput::onRequest(){
    while (H.Read(myConn)){
      std::string ua = H.GetHeader("User-Agent");
      crc = checksum::crc32(0, ua.data(), ua.size());
      INFO_MSG("Received request %s", H.getUrl().c_str());
      if (H.GetVar("audio") != ""){
        selectedTracks.insert(JSON::Value(H.GetVar("audio")).asInt());
      }
      if (H.GetVar("video") != ""){
        selectedTracks.insert(JSON::Value(H.GetVar("video")).asInt());
      }
      onHTTP();
      H.Clean();
    }
  }
  
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
  
  ///\brief Handles requests by starting a corresponding output process.
  ///\param H The request to be handled
  ///\param conn The connection to the client that issued the request.
  ///\param connector The type of connector to be invoked.
  void HTTPOutput::reConnector(std::string & connector){
    //taken from CheckProtocols (controller_connectors.cpp)
    char * argarr[20];
    for (int i=0; i<20; i++){argarr[i] = 0;}
    int id = -1;
    
    IPC::semaphore configLock("!mistConfLock", O_CREAT | O_RDWR, ACCESSPERMS, 1);
    configLock.wait();
    IPC::sharedPage serverCfg("!mistConfig", 4*1024*1024);
    DTSC::Scan prots = DTSC::Scan(serverCfg.mapped, serverCfg.len).getMember("config").getMember("protocols");
    unsigned int prots_ctr = prots.getSize();
    
    for (unsigned int i=0; i < prots_ctr; ++i){
      if (prots.getIndice(i).getMember("connector").asString() == connector) {
        id =  i;
        break;    //pick the first protocol in the list that matches the connector 
      }
    }
    if (id == -1) {
      connector = connector + ".exe";
      for (unsigned int i=0; i < prots_ctr; ++i){
        if (prots.getIndice(i).getMember("connector").asString() == connector) {
          id =  i;
          break;    //pick the first protocol in the list that matches the connector 
        }
      }
      if (id == -1) {
        connector = connector.substr(0, connector.size() - 4);
        DEBUG_MSG(DLVL_ERROR, "No connector found for: %s", connector.c_str());
        configLock.post();
        configLock.close();
        return;
      }
    }
    
    DEBUG_MSG(DLVL_HIGH, "Connector found: %s", connector.c_str());
    //build arguments for starting output process
    
    std::string temphost=myConn.getHost();
    std::string debuglevel = JSON::Value((long long)Util::Config::printDebugLevel).asString();
    std::string tmparg = Util::getMyPath() + std::string("MistOut") + connector;
    
    int argnum = 0;
    argarr[argnum++] = (char*)tmparg.c_str();
    JSON::Value p = prots.getIndice(id).asJSON();
    JSON::Value pipedCapa = DTSC::Scan(serverCfg.mapped, serverCfg.len).getMember("capabilities").getMember("connectors").getMember(connector).asJSON();
    configLock.post();
    configLock.close();
    argarr[argnum++] = (char*)"-i";
    argarr[argnum++] = (char*)(temphost.c_str());
    argarr[argnum++] = (char*)"-s";
    argarr[argnum++] = (char*)(streamName.c_str());
    //set the debug level if non-default
    if (Util::Config::printDebugLevel != DEBUG){
      argarr[argnum++] = (char*)"--debug";
      argarr[argnum++] = (char*)(debuglevel.c_str());
    }
    if (pipedCapa.isMember("required")){builPipedPart(p, argarr, argnum, pipedCapa["required"]);}
    if (pipedCapa.isMember("optional")){builPipedPart(p, argarr, argnum, pipedCapa["optional"]);}
    
    ///start new/better process
    execv(argarr[0], argarr);
  }
  
}
