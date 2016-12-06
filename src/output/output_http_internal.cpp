#include <sys/stat.h>
#include "output_http_internal.h"
#include <mist/stream.h>
#include <mist/encode.h>
#include <mist/langcodes.h>
#include "flashPlayer.h"
#include "oldFlashPlayer.h"

namespace Mist {
  OutHTTP::OutHTTP(Socket::Connection & conn) : HTTPOutput(conn){
    if (myConn.getPureSocket() >= 0){
      std::string host = getConnectedHost();
      dup2(myConn.getSocket(), STDIN_FILENO);
      dup2(myConn.getSocket(), STDOUT_FILENO);
      myConn.drop();
      myConn = Socket::Connection(fileno(stdout),fileno(stdin) );
      myConn.setHost(host);
    }
    if (config->getOption("wrappers",true).size() == 0 || config->getString("wrappers") == ""){
      JSON::Value & wrappers = config->getOption("wrappers",true);
      wrappers.shrink(0);
      jsonForEach(capa["optional"]["wrappers"]["allowed"],it){
        wrappers.append(*it);
      }
    }
  }

  OutHTTP::~OutHTTP() {}
  
  bool OutHTTP::listenMode(){
    return !(config->getString("ip").size());
  }
  
  void OutHTTP::onFail(){
    INFO_MSG("Failing: %s", H.url.c_str());
    if (H.url.size() >= 3 && H.url.substr(H.url.size() - 3) == ".js"){
      if (H.url.size() >= 5 && H.url.substr(0, 5) == "/json"){
          H.Clean();
          H.SetBody("{\"error\":\"Could not retrieve stream. Sorry.\"}\n");
        }else{
          H.Clean();
          H.SetBody("if (!mistvideo){var mistvideo = {};}\nmistvideo['" + streamName + "'] = {\"error\":\"Could not retrieve stream. Sorry.\"};\n");
        }
        H.SendResponse("200", "Stream not found", myConn);
    }else{
      HTTPOutput::onFail();
    }
    Output::onFail();
  }

  /// We assume it's ready to play if there is at least one track available
  bool OutHTTP::isReadyForPlay() {
    return myMeta.tracks.size();
  }
  
  void OutHTTP::init(Util::Config * cfg){
    HTTPOutput::init(cfg);
    capa.removeMember("deps");
    capa["name"] = "HTTP";
    capa["desc"] = "Generic HTTP handler, required for all other HTTP-based outputs.";
    capa["provides"] = "HTTP";
    capa["protocol"] = "http://";
    capa["url_match"].append("/crossdomain.xml");
    capa["url_match"].append("/clientaccesspolicy.xml");
    capa["url_match"].append("/$.html");
    capa["url_match"].append("/$.ico");
    capa["url_match"].append("/$.smil");
    capa["url_match"].append("/info_$.js");
    capa["url_match"].append("/json_$.js");
    capa["url_match"].append("/player.js");
    capa["url_match"].append("/player.css");
    capa["url_match"].append("/embed_$.js");
    capa["url_match"].append("/flashplayer.swf");
    capa["url_match"].append("/oldflashplayer.swf");
    capa["optional"]["wrappers"]["name"] = "Active players";
    capa["optional"]["wrappers"]["help"] = "Which players are attempted and in what order.";
    capa["optional"]["wrappers"]["default"] = "";
    capa["optional"]["wrappers"]["type"] = "ord_multi_sel";
    /*capa["optional"]["wrappers"]["allowed"].append("theoplayer");
    capa["optional"]["wrappers"]["allowed"].append("jwplayer");*/
    capa["optional"]["wrappers"]["allowed"].append("html5");
    capa["optional"]["wrappers"]["allowed"].append("videojs");
    capa["optional"]["wrappers"]["allowed"].append("dashjs");
    //capa["optional"]["wrappers"]["allowed"].append("polytrope"); //currently borked
    capa["optional"]["wrappers"]["allowed"].append("flash_strobe");
    capa["optional"]["wrappers"]["allowed"].append("silverlight");
    capa["optional"]["wrappers"]["option"] = "--wrappers";
    capa["optional"]["wrappers"]["short"] = "w";
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
  
  void addSource(const std::string & rel, std::set<JSON::Value, sourceCompare> & sources, std::string & host, const std::string & port, JSON::Value & conncapa, unsigned int most_simul, unsigned int total_matches, const std::string & protocol){
    JSON::Value tmp;
    tmp["type"] = conncapa["type"];
    tmp["relurl"] = rel;
    tmp["priority"] = conncapa["priority"];
    if (conncapa.isMember("player_url")){tmp["player_url"] = conncapa["player_url"].asStringRef();}
    tmp["simul_tracks"] = most_simul;
    tmp["total_matches"] = total_matches;
    tmp["url"] = protocol + host + ":" + port + rel;
    sources.insert(tmp);
  }
  

  void addSources(std::string & streamname, std::set<JSON::Value, sourceCompare> & sources, std::string & host, const std::string & port, JSON::Value & conncapa, JSON::Value & strmMeta, const std::string httpHost, JSON::Value * mainconn_capa = 0){
    const std::string & rel = conncapa["url_rel"].asStringRef();
    unsigned int most_simul = 0;
    unsigned int total_matches = 0;
    if (conncapa.isMember("codecs") && conncapa["codecs"].size() > 0){
      jsonForEach(conncapa["codecs"], it) {
        unsigned int simul = 0;
        if ((*it).size() > 0){
          jsonForEach((*it), itb) {
            unsigned int matches = 0;
            if ((*itb).size() > 0){
              jsonForEach((*itb), itc) {
                jsonForEach(strmMeta["tracks"], trit) {
                  if ((*trit)["codec"].asStringRef() == (*itc).asStringRef()){
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
        relurl = rel.substr(0, found) + Encodings::URL::encode(streamname) + rel.substr(found+1);
      }else{
        relurl = "/";
      }
      std::string protocol;
      if (mainconn_capa && mainconn_capa->isMember("protocol")){
        protocol = (*mainconn_capa)["protocol"].asStringRef();
      }
      jsonForEach(conncapa["methods"], it) {
        if (it->isMember("url_rel")){
          size_t foundb = (*it)["url_rel"].asStringRef().find('$');
          if (foundb != std::string::npos){
            relurl = (*it)["url_rel"].asStringRef().substr(0, foundb) + Encodings::URL::encode(streamname) + (*it)["url_rel"].asStringRef().substr(foundb+1);
          }
        }
        if (!strmMeta.isMember("live") || !it->isMember("nolive")){
          if (!protocol.size() && it->isMember("handler")){
            protocol = (*it)["handler"].asStringRef() + "://";
          }
          addSource(relurl, sources, host, port, *it, most_simul, total_matches, protocol);
        }
      }
    }
  }
  
  void OutHTTP::onHTTP(){
    std::string method = H.method;
    
    if (H.url == "/crossdomain.xml"){
      H.Clean();
      H.SetHeader("Content-Type", "text/xml");
      H.SetHeader("Server", "MistServer/" PACKAGE_VERSION);
      H.setCORSHeaders();
      if(method == "OPTIONS" || method == "HEAD"){
        H.SendResponse("200", "OK", myConn);
        H.Clean();
        return;
      }
      H.SetBody("<?xml version=\"1.0\"?><!DOCTYPE cross-domain-policy SYSTEM \"http://www.adobe.com/xml/dtds/cross-domain-policy.dtd\"><cross-domain-policy><allow-access-from domain=\"*\" /><site-control permitted-cross-domain-policies=\"all\"/></cross-domain-policy>");
      H.SendResponse("200", "OK", myConn);
      H.Clean();
      return;
    } //crossdomain.xml
    
    if (H.url == "/clientaccesspolicy.xml"){
      H.Clean();
      H.SetHeader("Content-Type", "text/xml");
      H.SetHeader("Server", "MistServer/" PACKAGE_VERSION);
      H.setCORSHeaders();
      if(method == "OPTIONS" || method == "HEAD"){
        H.SendResponse("200", "OK", myConn);
        H.Clean();
        return;
      }
      H.SetBody("<?xml version=\"1.0\" encoding=\"utf-8\"?><access-policy><cross-domain-access><policy><allow-from http-methods=\"*\" http-request-headers=\"*\"><domain uri=\"*\"/></allow-from><grant-to><resource path=\"/\" include-subpaths=\"true\"/></grant-to></policy></cross-domain-access></access-policy>");
      H.SendResponse("200", "OK", myConn);
      H.Clean();
      return;
    } //clientaccesspolicy.xml
    
    if (H.url == "/flashplayer.swf"){
      H.Clean();
      H.SetHeader("Content-Type", "application/x-shockwave-flash");
      H.SetHeader("Server", "MistServer/" PACKAGE_VERSION);
      H.SetBody((const char*)FlashMediaPlayback_101_swf, FlashMediaPlayback_101_swf_len);
      H.SendResponse("200", "OK", myConn);
      return;
    }
    if (H.url == "/oldflashplayer.swf"){
      H.Clean();
      H.SetHeader("Content-Type", "application/x-shockwave-flash");
      H.SetHeader("Server", "MistServer/" PACKAGE_VERSION);
      H.SetBody((const char *)FlashMediaPlayback_swf, FlashMediaPlayback_swf_len);
      H.SendResponse("200", "OK", myConn);
      return;

    }
    // send logo icon
    if (H.url.length() > 4 && H.url.substr(H.url.length() - 4, 4) == ".ico"){
      H.Clean();
      #include "../icon.h"
      H.SetHeader("Content-Type", "image/x-icon");
      H.SetHeader("Server", "MistServer/" PACKAGE_VERSION);
      H.SetHeader("Content-Length", icon_len);
      H.setCORSHeaders();
      if(method == "OPTIONS" || method == "HEAD"){
        H.SendResponse("200", "OK", myConn);
        H.Clean();
        return;
      }
      H.SendResponse("200", "OK", myConn);
      myConn.SendNow((const char*)icon_data, icon_len);
      H.Clean();
      return;
    }
    
    // send generic HTML page
    if (H.url.length() > 6 && H.url.substr(H.url.length() - 5, 5) == ".html"){
      std::string fullHost = H.GetHeader("Host");
      std::string uAgent = H.GetHeader("User-Agent");
      H.Clean();
      H.SetHeader("Content-Type", "text/html");
      H.SetHeader("Server", "MistServer/" PACKAGE_VERSION);
      H.setCORSHeaders();
      if(method == "OPTIONS" || method == "HEAD"){
        H.SendResponse("200", "OK", myConn);
        H.Clean();
        return;
      }
      
      std::string hlsUrl = "/hls/"+streamName+"/index.m3u8";
      std::string mp4Url = "/"+streamName+".mp4";
      
      H.SetBody("<!DOCTYPE html><html><head><title>"+streamName+"</title><style>body{color:white;background:black;}</style></head><body><div class=mistvideo id=\""+streamName+"\"><noscript><video controls autoplay><source src=\""+hlsUrl+"\" type=\"application/vnd.apple.mpegurl\"><source src=\""+mp4Url+"\" type=\"video/mp4\"><a href=\""+hlsUrl+"\">Click here to play the video [Apple]</a><br><a href=\""+mp4Url+"\">Click here to play the video [MP4]</a></video></noscript><script src=\"/player.js\"></script><script>mistPlay('"+streamName+"',{host:'//"+fullHost+"',target:document.getElementById('"+streamName+"')})</script></div></body></html>");
      if ((uAgent.find("iPad") != std::string::npos) || (uAgent.find("iPod") != std::string::npos) || (uAgent.find("iPhone") != std::string::npos)) {
        H.SetHeader("Location",hlsUrl);
        H.SendResponse("307", "HLS redirect", myConn);
        return;
      }
      H.SendResponse("200", "OK", myConn);
      return;
    }
    
    // send smil MBR index
    if (H.url.length() > 6 && H.url.substr(H.url.length() - 5, 5) == ".smil"){
      std::string host = H.GetHeader("Host");
      if (host.rfind(':') != std::string::npos && *host.rbegin() != ']'){
        host.resize(host.rfind(':'));
      }
      
      std::string port, url_rel;
      
      IPC::semaphore configLock(SEM_CONF, O_CREAT | O_RDWR, ACCESSPERMS, 1);
      configLock.wait();
      IPC::sharedPage serverCfg(SHM_CONF, DEFAULT_CONF_PAGE_SIZE);
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
      H.SetHeader("Server", "MistServer/" PACKAGE_VERSION);
      H.setCORSHeaders();
      if(method == "OPTIONS" || method == "HEAD"){
        H.SendResponse("200", "OK", myConn);
        H.Clean();
        return;
      }
      H.SetBody("<smil>\n  <head>\n    <meta base='rtmp://" + host + ":" + port + url_rel + "' />\n  </head>\n  <body>\n    <switch>\n"+trackSources+"    </switch>\n  </body>\n</smil>");
      H.SendResponse("200", "OK", myConn);
      H.Clean();
      return;
    }
    
    if ((H.url.length() > 9 && H.url.substr(0, 6) == "/info_" && H.url.substr(H.url.length() - 3, 3) == ".js") || (H.url.length() > 10 && H.url.substr(0, 7) == "/embed_" && H.url.substr(H.url.length() - 3, 3) == ".js") || (H.url.length() > 9 && H.url.substr(0, 6) == "/json_" && H.url.substr(H.url.length() - 3, 3) == ".js")){
      std::string fullHost = H.GetHeader("Host");
      std::string response;
      std::string rURL = H.url;
      std::string host = H.GetHeader("Host");
      if (host.rfind(':') != std::string::npos && *host.rbegin() != ']'){
        host.resize(host.rfind(':'));
      }
      H.Clean();
      H.SetHeader("Server", "MistServer/" PACKAGE_VERSION);
      H.setCORSHeaders();
      if (rURL.substr(0, 6) != "/json_"){
        H.SetHeader("Content-Type", "application/javascript");
      }else{
        H.SetHeader("Content-Type", "application/json");
      }
      if(method == "OPTIONS" || method == "HEAD"){
        H.SendResponse("200", "OK", myConn);
        H.Clean();
        return;
      }
      response = "// Generating info code for stream " + streamName + "\n\nif (!mistvideo){var mistvideo = {};}\n";
      JSON::Value json_resp;
      IPC::semaphore configLock(SEM_CONF, O_CREAT | O_RDWR, ACCESSPERMS, 1);
      static char liveSemName[NAME_BUFFER_SIZE];
      snprintf(liveSemName, NAME_BUFFER_SIZE, SEM_LIVE, streamName.c_str());
      IPC::semaphore metaLocker(liveSemName, O_CREAT | O_RDWR, ACCESSPERMS, 1);
      bool metaLock = false;
      configLock.wait();
      IPC::sharedPage serverCfg(SHM_CONF, DEFAULT_CONF_PAGE_SIZE);
      DTSC::Scan strm = DTSC::Scan(serverCfg.mapped, serverCfg.len).getMember("streams").getMember(streamName).getMember("meta");
      IPC::sharedPage streamIndex;
      if (!strm){
        configLock.post();
        //Stream metadata not found - attempt to start it
        if (Util::startInput(streamName)){
          char pageId[NAME_BUFFER_SIZE];
          snprintf(pageId, NAME_BUFFER_SIZE, SHM_STREAM_INDEX, streamName.c_str());
          streamIndex.init(pageId, DEFAULT_STRM_PAGE_SIZE);
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
        jsonForEach(json_resp["meta"]["tracks"], it) {
          if (it->isMember("lang")){
            (*it)["language"] = Encodings::ISO639::decode((*it)["lang"].asStringRef());
          }
          it->removeMember("fragments");
          it->removeMember("keys");
          it->removeMember("keysizes");
          it->removeMember("parts");
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
            JSON::Value capa_json = capa.asJSON();
            if (capa.getMember("url_rel") || capa.getMember("methods")){
              addSources(streamName, sources, host, port, capa_json, json_resp["meta"], fullHost);
            }
            //Make note if this connector can be depended upon by other connectors
            if (capa.getMember("provides")){
              std::string cProv = capa.getMember("provides").asString();
              //if this connector can be depended upon by other connectors, loop over the rest
              //check each enabled protocol separately to see if it depends on this connector
              DTSC::Scan capa_lst = DTSC::Scan(serverCfg.mapped, serverCfg.len).getMember("capabilities").getMember("connectors");
              unsigned int capa_lst_ctr = capa_lst.getSize();
              for (unsigned int j = 0; j < capa_lst_ctr; ++j){
                //if it depends on this connector and has a URL, list it
                if (conns.count(capa_lst.getIndiceName(j)) && capa_lst.getIndice(j).getMember("deps").asString() == cProv && capa_lst.getIndice(j).getMember("methods")){
                  JSON::Value subcapa_json = capa_lst.getIndice(j).asJSON();
                  addSources(streamName, sources, host, port, subcapa_json, json_resp["meta"], fullHost, &capa_json);
                }
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
      if (rURL.substr(0, 6) != "/json_"){
        response += "mistvideo['" + streamName + "'] = " + json_resp.toString() + ";\n";
      }else{
        response = json_resp.toString();
      }
      if (rURL.substr(0, 7) == "/embed_" && !json_resp.isMember("error")){
        #include "embed.js.h"
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
      H.Clean();
      return;
    } //embed code generator
    
    
    if (H.url == "/player.js"){
      std::string fullHost = H.GetHeader("Host");
      std::string response;
      std::string rURL = H.url;
      std::string host = H.GetHeader("Host");
      if (host.rfind(':') != std::string::npos && *host.rbegin() != ']'){
        host.resize(host.rfind(':'));
      }
      H.Clean();
      H.SetHeader("Server", "MistServer/" PACKAGE_VERSION);
      H.setCORSHeaders();
      H.SetHeader("Content-Type", "application/javascript");
      if(method == "OPTIONS" || method == "HEAD"){
        H.SendResponse("200", "OK", myConn);
        H.Clean();
        return;
      }
      
      response.append("if (typeof mistoptions == 'undefined') { mistoptions = {}; }\nif (!('host' in mistoptions)) { mistoptions.host = 'http://"+fullHost+"'; }\n");
      #include "core.js.h"
      response.append((char*)core_js, (size_t)core_js_len);
      jsonForEach(config->getOption("wrappers",true),it){
        bool used = false;
        if (it->asStringRef() == "html5"){
          #include "html5.js.h"
          response.append((char*)html5_js, (size_t)html5_js_len);
          used = true;
        }
        if (it->asStringRef() == "flash_strobe"){
          #include "flash_strobe.js.h"
          response.append((char*)flash_strobe_js, (size_t)flash_strobe_js_len);
          used = true;
        }
        if (it->asStringRef() == "silverlight"){
          #include "silverlight.js.h"
          response.append((char*)silverlight_js, (size_t)silverlight_js_len);
          used = true;
        }
        if (it->asStringRef() == "theoplayer"){
          #include "theoplayer.js.h"
          response.append((char*)theoplayer_js, (size_t)theoplayer_js_len);
          used = true;
        }
        if (it->asStringRef() == "jwplayer"){
          #include "jwplayer.js.h"
          response.append((char*)jwplayer_js, (size_t)jwplayer_js_len);
          used = true;
        }
        if (it->asStringRef() == "polytrope"){
          #include "polytrope.js.h"
          response.append((char*)polytrope_js, (size_t)polytrope_js_len);
          used = true;
        }
        if (it->asStringRef() == "dashjs"){
          #include "playerdash.js.h"
          response.append((char*)playerdash_js, (size_t)playerdash_js_len);
          #include "dashjs.js.h"
          response.append((char*)dash_js, (size_t)dash_js_len);
          used = true;
        }
        if (it->asStringRef() == "videojs"){
          #include "playervideo.js.h"
          response.append((char*)playervideo_js, (size_t)playervideo_js_len);
          #include "videojs.js.h"
          response.append((char*)video_js, (size_t)video_js_len);
          used = true;
        }
        if (!used) {
          WARN_MSG("Unknown player type: %s",it->asStringRef().c_str());
        }
      }
      
      H.SetBody(response);
      H.SendResponse("200", "OK", myConn);
      H.Clean();
      return;
      
    }
    
    if (H.url == "/player.css"){
      std::string response;
      H.Clean();
      H.SetHeader("Server", "MistServer/" PACKAGE_VERSION);
      H.setCORSHeaders();
      H.SetHeader("Content-Type", "text/css");
      if (method == "OPTIONS" || method == "HEAD"){
        H.SendResponse("200", "OK", myConn);
        H.Clean();
        return;
      }
      
      #include "mist.css.h"
      response.append((char*)mist_css, (size_t)mist_css_len);
      
      H.SetBody(response);
      H.SendResponse("200", "OK", myConn);
      H.Clean();
      return;
    }
  }
}
