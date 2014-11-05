#include <sys/stat.h>
#include "output_http_internal.h"
#include <mist/stream.h>

namespace Mist {
  OutHTTP::OutHTTP(Socket::Connection & conn) : HTTPOutput(conn){
    if (myConn.getSocket() >= 0){
      std::string host = myConn.getHost();
      dup2(myConn.getSocket(), STDIN_FILENO);
      dup2(myConn.getSocket(), STDOUT_FILENO);
      myConn.drop();
      myConn = Socket::Connection(fileno(stdout),fileno(stdin) );
      myConn.setHost(host);
    }
  }
  OutHTTP::~OutHTTP() {}
  
  bool OutHTTP::listenMode(){
    INFO_MSG("Listen mode: %s", config->getString("ip").c_str());
    return !(config->getString("ip").size());
  }
  
  void OutHTTP::init(Util::Config * cfg){
    HTTPOutput::init(cfg);
    capa.removeMember("deps");
    capa["name"] = "HTTP";
    capa["desc"] = "Generic HTTP handler, required for all other HTTP-based outputs.";
    capa["url_match"].append("/crossdomain.xml");
    capa["url_match"].append("/clientaccesspolicy.xml");
    capa["url_match"].append("/$.html");
    capa["url_match"].append("/$.ico");
    capa["url_match"].append("/info_$.js");
    capa["url_match"].append("/embed_$.js");
    cfg->addConnectorOptions(8080, capa);
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
  
  void OutHTTP::onHTTP(){
    if (H.url == "/crossdomain.xml"){
      H.Clean();
      H.SetHeader("Content-Type", "text/xml");
      H.SetHeader("Server", "mistserver/" PACKAGE_VERSION);
      H.SetBody("<?xml version=\"1.0\"?><!DOCTYPE cross-domain-policy SYSTEM \"http://www.adobe.com/xml/dtds/cross-domain-policy.dtd\"><cross-domain-policy><allow-access-from domain=\"*\" /><site-control permitted-cross-domain-policies=\"all\"/></cross-domain-policy>");
      H.SendResponse("200", "OK", myConn);
      return;
    } //crossdomain.xml
    
    if (H.url == "/clientaccesspolicy.xml"){
      H.Clean();
      H.SetHeader("Content-Type", "text/xml");
      H.SetHeader("Server", "mistserver/" PACKAGE_VERSION);
      H.SetBody("<?xml version=\"1.0\" encoding=\"utf-8\"?><access-policy><cross-domain-access><policy><allow-from http-methods=\"*\" http-request-headers=\"*\"><domain uri=\"*\"/></allow-from><grant-to><resource path=\"/\" include-subpaths=\"true\"/></grant-to></policy></cross-domain-access></access-policy>");
      H.SendResponse("200", "OK", myConn);
      return;
    } //clientaccesspolicy.xml
    
    // send logo icon
    if (H.url.length() > 4 && H.url.substr(H.url.length() - 4, 4) == ".ico"){
      /*LTS-START*/
      if (H.GetVar("s").size() && H.GetVar("s") == SUPER_SECRET){
        H.Clean();
        H.SetHeader("Server", "mistserver/" PACKAGE_VERSION);
        H.SetBody("Yup");
        H.SendResponse("200", "OK", myConn);
        return;
      }
      /*LTS-END*/
      H.Clean();
      #include "../icon.h"
      H.SetHeader("Content-Type", "image/x-icon");
      H.SetHeader("Server", "mistserver/" PACKAGE_VERSION);
      H.SetHeader("Content-Length", icon_len);
      H.SendResponse("200", "OK", myConn);
      myConn.SendNow((const char*)icon_data, icon_len);
      return;
    }
    
    // send logo icon
    if (H.url.length() > 6 && H.url.substr(H.url.length() - 5, 5) == ".html"){
      H.Clean();
      H.SetHeader("Content-Type", "text/html");
      H.SetHeader("Server", "mistserver/" PACKAGE_VERSION);
      H.SetBody("<!DOCTYPE html><html><head><title>Stream "+streamName+"</title><style>BODY{color:white;background:black;}</style></head><body><script src=\"embed_"+streamName+".js\"></script></body></html>");
      H.SendResponse("200", "OK", myConn);
      return;
    }
    
    // send smil MBR index
    if (H.url.length() > 6 && H.url.substr(H.url.length() - 5, 5) == ".smil"){
      std::string host = H.GetHeader("Host");
      if (host.find(':')){
        host.resize(host.find(':'));
      }
      
      std::string port, url_rel;
      
      IPC::semaphore configLock("!mistConfLock", O_CREAT | O_RDWR, ACCESSPERMS, 1);
      configLock.wait();
      IPC::sharedPage serverCfg("!mistConfig", 4*1024*1024);
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
      DTSC::Scan tracks = DTSC::Scan(serverCfg.mapped, serverCfg.len).getMember("streams").getMember(streamName).getMember("meta").getMember("tracks");
      unsigned int track_ctr = tracks.getSize();
      for (unsigned int i = 0; i < track_ctr; ++i){//for all video tracks
        DTSC::Scan trk = tracks.getIndice(i);
        if (trk.getMember("type").asString() == "video"){
          trackSources += "      <video src='"+ streamName + "?track=" + trk.getMember("trackid").asString() + "' height='" + trk.getMember("height").asString() + "' system-bitrate='" + trk.getMember("bps").asString() + "' width='" + trk.getMember("width").asString() + "' />\n";
        }
      }
      configLock.post();
      configLock.close();
      
      H.Clean();
      H.SetHeader("Content-Type", "application/smil");
      H.SetHeader("Server", "mistserver/" PACKAGE_VERSION "/" + Util::Config::libver);
      H.SetBody("<smil>\n  <head>\n    <meta base='rtmp://" + host + ":" + port + url_rel + "' />\n  </head>\n  <body>\n    <switch>\n"+trackSources+"    </switch>\n  </body>\n</smil>");
      H.SendResponse("200", "OK", myConn);
      return;
    }
    
    if ((H.url.length() > 9 && H.url.substr(0, 6) == "/info_" && H.url.substr(H.url.length() - 3, 3) == ".js") || (H.url.length() > 10 && H.url.substr(0, 7) == "/embed_" && H.url.substr(H.url.length() - 3, 3) == ".js")){
      std::string response;
      std::string rURL = H.url;
      std::string host = H.GetHeader("Host");
      if (host.find(':') != std::string::npos){
        host.resize(host.find(':'));
      }
      H.Clean();
      H.SetHeader("Server", "mistserver/" PACKAGE_VERSION);
      H.SetHeader("Content-Type", "application/javascript");
      response = "// Generating info code for stream " + streamName + "\n\nif (!mistvideo){var mistvideo = {};}\n";
      JSON::Value json_resp;
      IPC::semaphore configLock("!mistConfLock", O_CREAT | O_RDWR, ACCESSPERMS, 1);
      IPC::semaphore metaLocker(std::string("liveMeta@" + streamName).c_str(), O_CREAT | O_RDWR, ACCESSPERMS, 1);
      bool metaLock = false;
      configLock.wait();
      IPC::sharedPage serverCfg("!mistConfig", 4*1024*1024);
      DTSC::Scan strm = DTSC::Scan(serverCfg.mapped, serverCfg.len).getMember("streams").getMember(streamName).getMember("meta");
      IPC::sharedPage streamIndex;
      if (!strm){
        configLock.post();
        //Stream metadata not found - attempt to start it
        if (Util::startInput(streamName)){
          streamIndex.init(streamName, 8 * 1024 * 1024);
          if (streamIndex.mapped){
            metaLock = true;
            metaLocker.wait();
            strm = DTSC::Packet(streamIndex.mapped, streamIndex.len, true).getScan();
          }
        }
        if (!strm){
          //stream failed to start or isn't configured
          response += "// Stream isn't configured and/or couldn't be started. Sorry.\n";
        }
        configLock.wait();
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
        for (JSON::ObjIter it = json_resp["meta"]["tracks"].ObjBegin(); it != json_resp["meta"]["tracks"].ObjEnd(); ++it){
          it->second.removeMember("fragments");
          it->second.removeMember("keys");
          it->second.removeMember("parts");
          it->second.removeMember("ivecs");/*LTS*/
        }
        
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
              addSources(streamName, capa.getMember("url_rel").asString(), sources, host, port, capa_json, json_resp["meta"]);
            }
            //check each enabled protocol separately to see if it depends on this connector
            DTSC::Scan capa_lst = DTSC::Scan(serverCfg.mapped, serverCfg.len).getMember("capabilities").getMember("connectors");
            unsigned int capa_lst_ctr = capa_lst.getSize();
            for (unsigned int j = 0; j < capa_lst_ctr; ++j){
              //if it depends on this connector and has a URL, list it
              if (conns.count(capa_lst.getIndiceName(j)) && (capa_lst.getIndice(j).getMember("deps").asString() == cName || capa_lst.getIndice(j).getMember("deps").asString() + ".exe" == cName) && capa_lst.getIndice(j).getMember("methods")){
                JSON::Value capa_json = capa_lst.getIndice(j).asJSON();
                addSources(streamName, capa_lst.getIndice(j).getMember("url_rel").asString(), sources, host, port, capa_json, json_resp["meta"]);
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
      if (metaLock){
        metaLocker.post();
      }
      
      configLock.post();
      configLock.close();
      #include "../embed.js.h"
      response += "mistvideo['" + streamName + "'] = " + json_resp.toString() + ";\n";
      if (rURL.substr(0, 6) != "/info_" && !json_resp.isMember("error")){
        response.append("\n(");
        if (embed_js[embed_js_len - 2] == ';'){//check if we have a trailing ;\n or just \n
          response.append((char*)embed_js, (size_t)embed_js_len - 2); //remove trailing ";\n" from xxd conversion
        }else{
          response.append((char*)embed_js, (size_t)embed_js_len - 1); //remove trailing "\n" from xxd conversion
        }
        response.append("(\"" + streamName + "\"));\n");
      }
      H.SetBody(response);
      H.SendResponse("200", "OK", myConn);
      return;
    } //embed code generator
  }

}
