#include "output_http_internal.h"

#include "flashPlayer.h"
#include "oldFlashPlayer.h"

#include <mist/encode.h>
#include <mist/jwt.h>
#include <mist/langcodes.h>
#include <mist/stream.h>
#include <mist/triggers.h>
#include <mist/url.h>
#include <mist/websocket.h>

#include <sys/stat.h>

bool includeZeroMatches = false;

namespace Mist{
  /// Helper function to find the protocol entry for a given port number
  std::string getProtocolForPort(uint16_t portNo){
    std::string ret;
    Util::DTSCShmReader rCapa(SHM_CAPA);
    DTSC::Scan conns = rCapa.getMember("connectors");
    Util::DTSCShmReader rProto(SHM_PROTO);
    DTSC::Scan prtcls = rProto.getScan();
    unsigned int pro_cnt = prtcls.getSize();
    for (unsigned int i = 0; i < pro_cnt; ++i){
      DTSC::Scan capa = conns.getMember(prtcls.getIndice(i).getMember("connector").asString());
      uint16_t port = prtcls.getIndice(i).getMember("port").asInt();
      // get the default port if none is set
      if (!port){
        port = capa.getMember("optional").getMember("port").getMember("default").asInt();
      }
      if (port == portNo){
        ret = capa.getMember("protocol").asString();
        break;
      }
    }
    if (ret.find(':') != std::string::npos){ret.erase(ret.find(':'));}
    return ret;
  }

  OutHTTP::OutHTTP(Socket::Connection &conn) : HTTPOutput(conn){
    stayConnected = false;
    thisError = "";
    // If this connection is a socket and not already connected to stdio, connect it to stdio.
    if (myConn.getPureSocket() != -1 && myConn.getSocket() != STDIN_FILENO && myConn.getSocket() != STDOUT_FILENO){
      std::string host = getConnectedHost();
      dup2(myConn.getSocket(), STDIN_FILENO);
      dup2(myConn.getSocket(), STDOUT_FILENO);
      myConn.open(STDOUT_FILENO, STDIN_FILENO);
      myConn.setHost(host);
    }
    if (config->getString("nostreamtext").size()){
      setenv("MIST_HTTP_nostreamtext", config->getString("nostreamtext").c_str(), 1);
    }
    if (config->getString("pubaddr").size()){
      std::string pubAddrs = config->getOption("pubaddr", true).toString();
      setenv("MIST_HTTP_pubaddr", pubAddrs.c_str(), 1);
    }
    if (config->getOption("wrappers", true).size() == 0 || config->getString("wrappers") == ""){
      JSON::Value &wrappers = config->getOption("wrappers", true);
      wrappers.shrink(0);
      jsonForEach(capa["optional"]["wrappers"]["allowed"], it){wrappers.append(*it);}
    }
  }

  OutHTTP::~OutHTTP(){}

  bool OutHTTP::listenMode(){return !(config->getString("ip").size());}

  void OutHTTP::onFail(const std::string &msg, bool critical){
    // If we are connected through WS, the websockethandler should return the error message
    if (stayConnected){
      thisError = msg;
      return;
    }
    if (responded){
      HTTPOutput::onFail(msg, critical);
      return;
    }
    std::string method = H.method;
    // send logo icon
    if (H.url.length() > 4 && H.url.substr(H.url.length() - 4, 4) == ".ico"){
      sendIcon(false);
      return;
    }
    if (H.url.length() > 6 && H.url.substr(H.url.length() - 5, 5) == ".html"){
      HTMLResponse(H, false);
      return;
    }
    if (H.url.size() >= 3 && H.url.substr(H.url.size() - 3) == ".js"){
      JSON::Value json_resp;
      json_resp["error"] = "Could not retrieve stream. Sorry.";
      json_resp["error_guru"] = msg;
      if (config->getString("nostreamtext") != ""){
        json_resp["on_error"] = config->getString("nostreamtext");
      }
      if (H.url.size() >= 5 && H.url.substr(0, 5) == "/json"){
        H.Clean();
        H.SetBody(json_resp.toString());
      }else{
        H.Clean();
        H.SetBody("if (!mistvideo){var mistvideo ={};}\nmistvideo['" + streamName +
                  "'] = " + json_resp.toString() + ";\n");
      }
      H.setCORSHeaders();
      if (method == "OPTIONS" || method == "HEAD"){
        H.SendResponse("200", "OK", myConn);
        responded = true;
        H.Clean();
        return;
      }
      H.SendResponse("200", "Stream not found", myConn);
      responded = true;
      H.Clean();
      return;
    }
    HTTPOutput::onFail(msg, critical);
  }

  void OutHTTP::init(Util::Config *cfg){
    HTTPOutput::init(cfg);
    capa.removeMember("deps");
    capa["name"] = "HTTP";
    capa["friendly"] = "HTTP";
    capa["desc"] = "HTTP connection handler, provides all enabled HTTP-based outputs";
    capa["provides"] = "HTTP";
    capa["protocol"] = "http://";
    capa["url_rel"] = "/$.html";
    capa["codecs"][0u].null();
    capa["url_match"].append("/crossdomain.xml");
    capa["url_match"].append("/clientaccesspolicy.xml");
    capa["url_match"].append("/$.html");
    capa["url_match"].append("/favicon.ico");
    capa["url_match"].append("/$.smil");
    capa["url_match"].append("/info_$.js");
    capa["url_match"].append("/json_$.js");
    capa["url_match"].append("/player.js");
    capa["url_match"].append("/videojs.js");
    capa["url_match"].append("/dashjs.js");
    capa["url_match"].append("/flv.js");
    capa["url_match"].append("/hlsjs.js");
    capa["url_match"].append("/libde265.js");
    capa["url_match"].append("/skins/default.css");
    capa["url_match"].append("/skins/dev.css");
    capa["url_match"].append("/skins/videojs.css");
    capa["url_match"].append("/embed_$.js");
    capa["url_match"].append("/flashplayer.swf");
    capa["url_match"].append("/oldflashplayer.swf");
    capa["url_prefix"] = "/.well-known/";
    capa["optional"]["wrappers"]["name"] = "Active players";
    capa["optional"]["wrappers"]["help"] = "Which players are attempted and in what order.";
    capa["optional"]["wrappers"]["default"] = "";
    capa["optional"]["wrappers"]["type"] = "ord_multi_sel";
    capa["optional"]["wrappers"]["allowed"].append("html5");
    capa["optional"]["wrappers"]["allowed"].append("hlsjs");
    capa["optional"]["wrappers"]["allowed"].append("videojs");
    capa["optional"]["wrappers"]["allowed"].append("dashjs");
    capa["optional"]["wrappers"]["allowed"].append("wheprtc");
    capa["optional"]["wrappers"]["allowed"].append("webrtc");
    capa["optional"]["wrappers"]["allowed"].append("mews");
    capa["optional"]["wrappers"]["allowed"].append("rawws");
    capa["optional"]["wrappers"]["allowed"].append("flv");
    capa["optional"]["wrappers"]["allowed"].append("flash_strobe");
    capa["optional"]["wrappers"]["option"] = "--wrappers";
    capa["optional"]["wrappers"]["short"] = "w";
    capa["optional"]["certbot"]["name"] = "Certbot validation token";
    capa["optional"]["certbot"]["help"] = "Automatically set by the MistUtilCertbot authentication "
                                          "hook for certbot. Not intended to be set manually.";
    capa["optional"]["certbot"]["default"] = "";
    capa["optional"]["certbot"]["type"] = "str";
    capa["optional"]["certbot"]["option"] = "--certbot";
    capa["optional"]["certbot"]["short"] = "C";
    cfg->addConnectorOptions(8080, capa);
    /*LTS-START*/
    cfg->addOption("nostreamtext",
                   JSON::fromString("{\"arg\":\"string\", \"default\":\"\", "
                                    "\"short\":\"t\",\"long\":\"nostreamtext\",\"help\":\"Text or "
                                    "HTML to display when streams are unavailable.\"}"));
    capa["optional"]["nostreamtext"]["name"] = "Stream unavailable text";
    capa["optional"]["nostreamtext"]["help"] =
        "Text or HTML to display when streams are unavailable.";
    capa["optional"]["nostreamtext"]["default"] = "";
    capa["optional"]["nostreamtext"]["type"] = "str";
    capa["optional"]["nostreamtext"]["option"] = "--nostreamtext";
    /*LTS-END*/
    cfg->addOption("pubaddr",
                   JSON::fromString("{\"arg\":\"string\", \"default\":\"\", "
                                    "\"short\":\"A\",\"long\":\"public-address\",\"help\":\"Full "
                                    "public address this output is available as.\"}"));
    capa["optional"]["pubaddr"]["name"] = "Public address";
    capa["optional"]["pubaddr"]["help"] =
        "Full public address this output is available as, if being proxied";
    capa["optional"]["pubaddr"]["default"] = "";
    capa["optional"]["pubaddr"]["type"] = "inputlist";
    capa["optional"]["pubaddr"]["option"] = "--public-address";
  }

  /// Sorts the JSON::Value objects that hold source information by preference.
  struct sourceCompare{
    bool operator()(const JSON::Value &lhs, const JSON::Value &rhs) const{
      // first compare simultaneous tracks
      if (lhs["simul_tracks"].asInt() > rhs["simul_tracks"].asInt()){
        // more tracks = higher priority = true.
        return true;
      }
      if (lhs["simul_tracks"].asInt() < rhs["simul_tracks"].asInt()){
        // less tracks = lower priority = false
        return false;
      }
      // same amount of tracks - compare "hardcoded" priorities
      if (lhs["priority"].asInt() > rhs["priority"].asInt()){
        // higher priority = true.
        return true;
      }
      if (lhs["priority"].asInt() < rhs["priority"].asInt()){
        // lower priority = false
        return false;
      }
      // same priority - compare total matches
      if (lhs["total_matches"].asInt() > rhs["total_matches"].asInt()){
        // more matches = higher priority = true.
        return true;
      }
      if (lhs["total_matches"].asInt() < rhs["total_matches"].asInt()){
        // less matches = lower priority = false
        return false;
      }
      // also same amount of matches? just compare the URL then.
      return lhs["url"].asStringRef() < rhs["url"].asStringRef();
    }
  };

  void addSources(std::string &streamname, std::set<JSON::Value, sourceCompare> &sources, HTTP::URL url,
                  JSON::Value &conncapa, JSON::Value &strmMeta, const std::string &useragent, bool metaEverywhere){
    url.path += "/";
    if (strmMeta.isMember("live") && conncapa.isMember("exceptions") &&
        conncapa["exceptions"].isObject() && conncapa["exceptions"].size()){
      jsonForEach(conncapa["exceptions"], ex){
        if (ex.key() == "live"){
          if (!Util::checkException(*ex, useragent)){return;}
        }
      }
    }
    bool allowBFrames = true;
    if (conncapa.isMember("methods")){
      jsonForEach(conncapa["methods"], mthd){
        if (mthd->isMember("nobframes") && (*mthd)["nobframes"]){
          allowBFrames = false;
          break;
        }
      }
    }
    const std::string &rel = conncapa["url_rel"].asStringRef();
    unsigned int most_simul = 0;
    unsigned int total_matches = 0;
    if (conncapa.isMember("codecs") && conncapa["codecs"].size() > 0){
      jsonForEach(conncapa["codecs"], it){
        unsigned int simul = 0;
        if ((*it).size() > 0){
          jsonForEach((*it), itb){
            unsigned int matches = 0;
            if ((*itb).size() > 0){
              jsonForEach((*itb), itc){
                const std::string &strRef = (*itc).asStringRef();
                bool byType = false;
                uint8_t shift = 0;
                if (strRef[shift] == '@'){
                  byType = true;
                  ++shift;
                }
                if (strRef[shift] == '+'){
                  ++shift;
                }
                jsonForEach(strmMeta["tracks"], trit){
                  if ((!byType && (*trit)["codec"].asStringRef() == strRef.substr(shift)) ||
                      (byType && (*trit)["type"].asStringRef() == strRef.substr(shift)) ||
                      strRef.substr(shift) == "*"){
                    if (metaEverywhere && (*trit)["type"] == "meta"){continue;}
                    if (allowBFrames || !(trit->isMember("bframes") && (*trit)["bframes"])){
                      matches++;
                      total_matches++;
                      if (conncapa.isMember("exceptions") && conncapa["exceptions"].isObject() &&
                          conncapa["exceptions"].size()){
                        jsonForEach(conncapa["exceptions"], ex){
                          if (ex.key() == "codec:" + strRef.substr(shift)){
                            if (!Util::checkException(*ex, useragent)){
                              matches--;
                              total_matches--;
                            }
                            break;
                          }
                        }
                      }
                    }
                  }
                }
              }
            }
            if (matches){simul++;}
          }
        }

        // Simulate support for all metadata tracks in every protocol
        if (metaEverywhere){
          size_t matches = 0;
          jsonForEach(strmMeta["tracks"], trit){
            if ((*trit)["type"] == "meta"){++matches;}
          }
          if (matches){
            total_matches += matches;
            ++simul;
          }
        }

        if (simul > most_simul){most_simul = simul;}
      }
    }
    if (conncapa.isMember("methods") && conncapa["methods"].size() > 0){
      std::string relurl;
      size_t found = rel.find('$');
      if (found != std::string::npos){
        relurl = rel.substr(1, found - 1) + Encodings::URL::encode(streamname) + rel.substr(found + 1);
      }else{
        relurl = "";
      }
      jsonForEach(conncapa["methods"], it){
        if (it->isMember("url_rel")){
          size_t foundb = (*it)["url_rel"].asStringRef().find('$');
          if (foundb != std::string::npos){
            relurl = (*it)["url_rel"].asStringRef().substr(1, foundb - 1) +
                     Encodings::URL::encode(streamname) + (*it)["url_rel"].asStringRef().substr(foundb + 1);
          }
        }
        if (!strmMeta.isMember("live") || !it->isMember("nolive")){
          bool isSSL = false;
          if (url.protocol == "https" || url.protocol == "wss"){isSSL = true;}
          if (it->isMember("handler")){
            url.protocol = (*it)["handler"].asStringRef() + (isSSL?"s":"");
          }
          JSON::Value tmp;
          tmp["type"] = (*it)["type"];
          if (it->isMember("hrn")){tmp["hrn"] = (*it)["hrn"];}
          tmp["relurl"] = relurl;
          tmp["priority"] = (*it)["priority"];
          if ((*it).isMember("player_url")){
            tmp["player_url"] = (*it)["player_url"].asStringRef();
          }
          if (conncapa.isMember("cnf") && conncapa["cnf"].isMember("iceservers")){tmp["RTCIceServers"] = conncapa["cnf"]["iceservers"];}
          tmp["simul_tracks"] = most_simul;
          tmp["total_matches"] = total_matches;
          tmp["url"] = url.link(relurl).getUrl();
          sources.insert(tmp);
        }
      }
    }
  }

  void OutHTTP::HTMLResponse(const HTTP::Parser & req, bool headersOnly){
    HTTPOutput::respondHTTP(req, headersOnly);
    HTTP::URL fullURL(req.GetHeader("Host"));
    if (!fullURL.protocol.size()){fullURL.protocol = getProtocolForPort(fullURL.getPort());}
    if (config->getString("pubaddr") != ""){
      HTTP::URL altURL(config->getString("pubaddr"));
      fullURL.protocol = altURL.protocol;
      if (altURL.host.size()){fullURL.host = altURL.host;}
      fullURL.port = altURL.port;
      fullURL.path = altURL.path;
    }
    if (mistPath.size()){fullURL = mistPath;}
    std::string uAgent = req.GetHeader("User-Agent");

    JSON::Value opts;
    opts["host"] = fullURL.getUrl();
    opts["urlappend"] = req.allVars();
    opts["fillSpace"] = true;

    // These parameters are checked if they are passed at all (unset is also accepted)
    // This check works by comparing the global unset empty string pointer against the variables string pointer:
    // If they match, it was not passed at all. If they mismatch, it was passed either with or without a value.
    if (req.GetVar("dev").data() != req.GetVar("").data()) { opts["skin"] = "dev"; }
    if (req.GetVar("muted").data() != req.GetVar("").data()) { opts["muted"] = true; }
    if (req.GetVar("nounix").data() != req.GetVar("").data()) { opts["useDateTime"] = false; }
    if (req.GetVar("nocatchup").data() != req.GetVar("").data()) { opts["liveCatchup"] = false; }

    // These parameters must have a non-empty value set to take effect
    if (req.GetVar("forceType").size()) { opts["forceType"] = req.GetVar("forceType"); }
    if (req.GetVar("forcetype").size()) { opts["forceType"] = req.GetVar("forcetype"); }
    if (req.GetVar("forcePlayer").size()) { opts["forcePlayer"] = req.GetVar("forcePlayer"); }
    if (req.GetVar("forceplayer").size()) { opts["forcePlayer"] = req.GetVar("forceplayer"); }
    if (req.GetVar("autoplay").size()) {
      if ((req.GetVar("autoplay") == "false") || (req.GetVar("autoplay") == "0")) { opts["autoplay"] = false; }
    }

    std::string seekTo = "";
    if (req.GetVar("t").size()){
      uint64_t autoSeekTime = 0;
      std::string sTime = req.GetVar("t");
      unsigned long long h = 0, m = 0, s = 0;
      autoSeekTime = JSON::Value(sTime).asInt();
      if (sscanf(sTime.c_str(), "%llum%llus", &m, &s) == 2){autoSeekTime = m * 60 + s;}
      if (sscanf(sTime.c_str(), "%llu:%llu", &m, &s) == 2){autoSeekTime = m * 60 + s;}
      if (sscanf(sTime.c_str(), "%lluh%llum%llus", &h, &m, &s) == 3){
        autoSeekTime = h * 3600 + m * 60 + s;
      }
      if (sscanf(sTime.c_str(), "%llu:%llu:%llu", &h, &m, &s) == 3){
        autoSeekTime = h * 3600 + m * 60 + s;
      }
      if (autoSeekTime){
        seekTo = "var f = function(){if (mv.reference && mv.reference.player && "
                 "mv.reference.player.api){mv.reference.player.api.currentTime = " +
                 JSON::Value(autoSeekTime).toString() +
                 ";}this.removeEventListener(\"initialized\",f);}; document.getElementById(\"" +
                 streamName + "\").addEventListener(\"initialized\",f);";
      }
    }

    H.Clean();
    H.SetHeader("Content-Type", "text/html");
    H.SetHeader("X-UA-Compatible", "IE=edge");
    if (headersOnly){
      H.SendResponse("200", "OK", myConn);
      responded = true;
      H.Clean();
      return;
    }

    std::string hlsUrl = "/hls/" + streamName + "/index.m3u8";
    std::string mp4Url = "/" + streamName + ".mp4";

    H.SetBody("<!DOCTYPE html><html><head><title>" + streamName +
              "</title><meta name=\"viewport\" content=\"width=device-width, "
              "initial-scale=1\"><style>html{margin:0;padding:0;display:table;width:100%;height:"
              "100%;}body{color:white;background:#0f0f0f;margin:0;padding:0;display:table-cell;"
              "vertical-align:middle;text-align:center}body>div>div{text-align:left;}</style></"
              "head><body><div class=mistvideo id=\"" +
              streamName + "\"><noscript><video controls autoplay><source src=\"" + hlsUrl +
              "\" type=\"application/vnd.apple.mpegurl\"><source src=\"" + mp4Url + "\" type=\"video/mp4\"><a href=\"" +
              hlsUrl + "\">Click here to play the video [Apple]</a><br><a href=\"" + mp4Url +
              "\">Click here to play the video [MP4]</a></video></noscript><script "
              "src=\"player.js\"></script><script>var mv={reference:false}; var opts=" +
              opts.toString() + ";opts.target=document.getElementById('" + streamName +
              "');opts.MistVideoObject = mv;mistPlay('" + streamName + "',opts);" + seekTo + "</script></div></body></html>");
    H.SendResponse("200", "OK", myConn);
    responded = true;
    H.Clean();
  }

  JSON::Value OutHTTP::getStatusJSON(std::string &reqHost, const std::string &useragent, bool metaEverywhere){
    JSON::Value json_resp;
    if (config->getString("nostreamtext") != ""){
      json_resp["on_error"] = config->getString("nostreamtext");
    }
    // Make note of any defaultStream-based redirection
    if (origStreamName.size() && origStreamName != streamName){
      json_resp["redirected"].append(origStreamName);
      json_resp["redirected"].append(streamName);
    }
    uint8_t streamStatus = Util::getStreamStatus(streamName);
    uint8_t streamStatusPerc = Util::getStreamStatusPercentage(streamName);
    if (streamStatus != STRMSTAT_READY){
      // If we haven't rewritten the stream name yet to a fallback, attempt to do so
      if (origStreamName == streamName){
        // If stream is configured, use fallback stream setting, if set.
        JSON::Value strCnf = Util::getStreamConfig(streamName);
        if (strCnf && strCnf["fallback_stream"].asStringRef().size()){
          std::string defStrm = strCnf["fallback_stream"].asStringRef();
          std::string newStrm = defStrm;
          Util::streamVariables(newStrm, streamName, "");
          if (streamName != newStrm){
            INFO_MSG("Switching to configured fallback stream '%s' -> '%s'", defStrm.c_str(), newStrm.c_str());
            origStreamName = streamName;
            streamName = newStrm;
            Util::setStreamName(streamName);
            reconnect();
            return getStatusJSON(reqHost, useragent, metaEverywhere);
          }
        }

        //global fallback stream
        JSON::Value defStrmJson = Util::getGlobalConfig("defaultStream");
        std::string defStrm = defStrmJson.asString();
        if (Triggers::shouldTrigger("DEFAULT_STREAM", streamName)){
          std::string payload = defStrm + "\n" + streamName + "\n" + getConnectedHost() + "\n" +
                                capa["name"].asStringRef() + "\n" + reqUrl;
          // The return value is ignored, because the response (defStrm in this case) tells us what to do next, if anything.
          Triggers::doTrigger("DEFAULT_STREAM", payload, streamName, false, defStrm);
        }
        if (defStrm.size()){
          std::string newStrm = defStrm;
          Util::streamVariables(newStrm, streamName, "");
          if (streamName != newStrm){
            INFO_MSG("Falling back to default stream '%s' -> '%s'", defStrm.c_str(), newStrm.c_str());
            origStreamName = streamName;
            streamName = newStrm;
            Util::setStreamName(streamName);
            reconnect();
            return getStatusJSON(reqHost, useragent, metaEverywhere);
          }
        }
        origStreamName.clear(); // no fallback, don't check again
      }
      switch (streamStatus){
      case STRMSTAT_OFF: json_resp["error"] = "Stream is offline"; break;
      case STRMSTAT_INIT: json_resp["error"] = "Stream is initializing"; break;
      case STRMSTAT_BOOT: json_resp["error"] = "Stream is booting"; break;
      case STRMSTAT_WAIT: json_resp["error"] = "Stream is waiting for data"; break;
      case STRMSTAT_SHUTDOWN: json_resp["error"] = "Stream is shutting down"; break;
      case STRMSTAT_INVALID: json_resp["error"] = "Stream status is invalid?!"; break;
      default: json_resp["error"] = "Stream status is unknown?!"; break;
      }
      if (streamStatusPerc){json_resp["perc"] = ((double)streamStatusPerc)/2.55;}
      return json_resp;
    }
    initialize();
    if (!myConn || !M){return json_resp;}

    json_resp["selver"] = 2;

    // Mention what the server supports in terms of optional/versioned capabilities
    json_resp["capa"]["selver"] = 2;
#ifdef WITH_DATACHANNELS
    json_resp["capa"]["datachannels"] = true;
#endif
#ifdef SSL
    json_resp["capa"]["ssl"] = true;
#else
    json_resp["capa"]["ssl"] = false;
#endif

    bool hasVideo = false;
    std::set<size_t> validTracks = M.getValidTracks();
    for (std::set<size_t>::iterator it = validTracks.begin(); it != validTracks.end(); it++){
      if (M.getType(*it) == "video"){
        hasVideo = true;
        if (M.getWidth(*it) > json_resp["width"].asInt()){json_resp["width"] = M.getWidth(*it);}
        if (M.getHeight(*it) > json_resp["height"].asInt()){
          json_resp["height"] = M.getHeight(*it);
        }
      }
    }
    if (json_resp["width"].asInt() < 1 || json_resp["height"].asInt() < 1){
      json_resp["width"] = 640;
      json_resp["height"] = (hasVideo ? 480 : 20);
    }
    json_resp["type"] = (M.getLive() ? "live" : "vod");
    if (M.getLive()){
      if (M.getUTCOffset()){
        json_resp["unixoffset"] = M.getUTCOffset();
      }else{
        json_resp["unixoffset"] = M.getBootMsOffset() + (Util::unixMS() - Util::bootMS());
      }
    }else{
      if (M.getUTCOffset()){
        json_resp["unixoffset"] = M.getUTCOffset();
      }
    }

    // show ALL the meta datas!
    M.toJSON(json_resp["meta"], true);
    jsonForEach(json_resp["meta"]["tracks"], it){
      if (it->isMember("lang")){
        (*it)["language"] = Encodings::ISO639::decode((*it)["lang"].asStringRef());
      }
    }
    json_resp["meta"].removeMember("source");

    // Get sources/protocols information
    Util::DTSCShmReader rCapa(SHM_CAPA);
    DTSC::Scan connectors = rCapa.getMember("connectors");
    Util::DTSCShmReader rProto(SHM_PROTO);
    DTSC::Scan prots = rProto.getScan();
    if (!prots || !connectors){
      json_resp["error"] = "Server configuration unavailable at this time.";
      return json_resp;
    }

    // create a set for storing source information
    std::set<JSON::Value, sourceCompare> sources;

    // find out which connectors are enabled
    std::set<std::string> conns;
    unsigned int prots_ctr = prots.getSize();
    for (unsigned int i = 0; i < prots_ctr; ++i){
      conns.insert(prots.getIndice(i).getMember("connector").asString());
    }
    // loop over the connectors.
    for (unsigned int i = 0; i < prots_ctr; ++i){
      std::string cName = prots.getIndice(i).getMember("connector").asString();
      DTSC::Scan capa = connectors.getMember(cName);
      // if the connector has a port,
      if (capa.getMember("optional").getMember("port") && !capa.getMember("provides_dependency")){
        HTTP::URL outURL(reqHost);
        // get the default port if none is set
        outURL.port = prots.getIndice(i).getMember("port").asString();
        if (!outURL.port.size()){
          outURL.port = capa.getMember("optional").getMember("port").getMember("default").asString();
        }
        outURL.protocol = capa.getMember("protocol").asString();
        if (outURL.protocol.find(':') != std::string::npos){
          outURL.protocol.erase(outURL.protocol.find(':'));
        }
        JSON::Value pubAddrs;
        pubAddrs.append("");
        if (prots.getIndice(i).hasMember("pubaddr") &&
            prots.getIndice(i).getMember("pubaddr").getType() == DTSC_STR){
          if (prots.getIndice(i).getMember("pubaddr").asString().size()){
            pubAddrs[0u] = prots.getIndice(i).getMember("pubaddr").asString();
          }
        }
        if (prots.getIndice(i).hasMember("pubaddr") &&
            prots.getIndice(i).getMember("pubaddr").getType() == DTSC_ARR){
          pubAddrs = prots.getIndice(i).getMember("pubaddr").asJSON();
          if (!pubAddrs.size()){pubAddrs.append("");}
        }
        if (mistPath.size()){
          pubAddrs.null();
          pubAddrs.append(mistPath);
        }
        // and a URL - then list the URL
        JSON::Value capa_json = capa.asJSON();
        capa_json["cnf"] = prots.getIndice(i).asJSON();
        if (capa.getMember("url_rel") || capa.getMember("methods")){
          jsonForEach(pubAddrs, jit){
            HTTP::URL altURL = outURL;
            if (jit->asString().size()){altURL = jit->asString();}
            if (!altURL.host.size()){altURL.host = outURL.host;}
            if (!altURL.protocol.size()){altURL.protocol = outURL.protocol;}
            addSources(streamName, sources, altURL, capa_json, json_resp["meta"], useragent, metaEverywhere);
          }
        }
        // Make note if this connector can be depended upon by other connectors
        if (capa.getMember("provides")){
          std::string cProv = capa.getMember("provides").asString();
          // if this connector can be depended upon by other connectors, loop over the rest
          // check each enabled protocol separately to see if it depends on this connector
          unsigned int capa_lst_ctr = connectors.getSize();
          for (unsigned int j = 0; j < capa_lst_ctr; ++j){
            // if it depends on this connector and has a URL, list it
            if (conns.count(connectors.getIndiceName(j)) &&
                connectors.getIndice(j).getMember("deps").asString() == cProv &&
                connectors.getIndice(j).getMember("methods")){
              JSON::Value subcapa_json = connectors.getIndice(j).asJSON();
              subcapa_json["cnf"] = prots.getIndice(i).asJSON();
              for (unsigned int k = 0; k < prots_ctr; ++k){
                if (prots.getIndice(k).getMember("connector").asString() == connectors.getIndiceName(j)){
                  subcapa_json["cnf"].extend(prots.getIndice(k).asJSON());
                  break;
                }
              }

              jsonForEach(pubAddrs, jit){
                HTTP::URL altURL = outURL;
                if (jit->asString().size()){altURL = jit->asString();}
                if (!altURL.host.size()){altURL.host = outURL.host;}
                if (!altURL.protocol.size()){altURL.protocol = outURL.protocol;}
                addSources(streamName, sources, altURL, subcapa_json, json_resp["meta"], useragent, metaEverywhere);
              }
            }
          }
        }
      }
    }

    // loop over the added sources, add them to json_resp["sources"]
    for (std::set<JSON::Value, sourceCompare>::iterator it = sources.begin(); it != sources.end(); it++){
      if (includeZeroMatches || (*it)["simul_tracks"].asInt() > 0){
        if (Comms::tknMode & 0x04){
          JSON::Value tmp;
          tmp = (*it);
          tmp["url"] = tmp["url"].asStringRef() + "?tkn=" + tkn;
          tmp["relurl"] = tmp["relurl"].asStringRef() + "?tkn=" + tkn;
          json_resp["source"].append(tmp);
        }else{
          json_resp["source"].append(*it);
        }
      }
    }
    return json_resp;
  }

  void OutHTTP::respondHTTP(const HTTP::Parser & req, bool headersOnly){
    // Streamname may be a JWT, replace it with the subject field and set tkn to the JWT
    if (JWT::isJWS(streamName)) {
      JWT::JWS jws(streamName, true);
      if (jws && !jws.hasWildcard() && jws.checkClaims("", getConnectedBinHost())) {
        tkn = streamName;
        streamName = jws.getPayload()["sub"].asStringRef();
      }
    }

    // Sanitize the streamname (also if it was contained in a JWT)
    Util::sanitizeName(streamName);
    Util::setStreamName(streamName);

    origStreamName = streamName;
    includeZeroMatches = req.GetVar("inclzero").size();

    if (req.GetHeader("X-Mst-Path").size()){mistPath = req.GetHeader("X-Mst-Path");}

    // Handle certbot validations
    if (req.url.substr(0, 28) == "/.well-known/acme-challenge/"){
      std::string cbToken = req.url.substr(28);
      jsonForEach(config->getOption("certbot", true), it){
        if (it->asStringRef().substr(0, cbToken.size() + 1) == cbToken + ":"){
          H.SetHeader("Content-Type", "text/plain");
          H.SetHeader("Server", APPIDENT);
          H.setCORSHeaders();
          H.SetBody(it->asStringRef().substr(cbToken.size() + 1));
          H.SendResponse("200", "OK", myConn);
          responded = true;
          H.Clean();
          return;
        }
      }
      H.SetHeader("Content-Type", "text/plain");
      H.setCORSHeaders();
      H.SetBody("No matching validation found for token '" + cbToken + "'");
      H.SendResponse("404", "Not found", myConn);
      responded = true;
      H.Clean();
      return;
    }

    if (req.url == "/crossdomain.xml"){
      H.SetHeader("Content-Type", "text/xml");
      H.SetHeader("Server", APPIDENT);
      H.setCORSHeaders();
      if (headersOnly){
        H.SendResponse("200", "OK", myConn);
        responded = true;
        H.Clean();
        return;
      }
      H.SetBody("<?xml version=\"1.0\"?><!DOCTYPE cross-domain-policy SYSTEM "
                "\"http://www.adobe.com/xml/dtds/"
                "cross-domain-policy.dtd\"><cross-domain-policy><allow-access-from domain=\"*\" "
                "/><site-control permitted-cross-domain-policies=\"all\"/></cross-domain-policy>");
      H.SendResponse("200", "OK", myConn);
      responded = true;
      H.Clean();
      return;
    }// crossdomain.xml

    if (req.url == "/clientaccesspolicy.xml"){
      H.SetHeader("Content-Type", "text/xml");
      H.SetHeader("Server", APPIDENT);
      H.setCORSHeaders();
      if (headersOnly){
        H.SendResponse("200", "OK", myConn);
        responded = true;
        H.Clean();
        return;
      }
      H.SetBody(
          "<?xml version=\"1.0\" "
          "encoding=\"utf-8\"?><access-policy><cross-domain-access><policy><allow-from "
          "http-methods=\"*\" http-request-headers=\"*\"><domain "
          "uri=\"*\"/></allow-from><grant-to><resource path=\"/\" "
          "include-subpaths=\"true\"/></grant-to></policy></cross-domain-access></access-policy>");
      H.SendResponse("200", "OK", myConn);
      responded = true;
      H.Clean();
      return;
    }// clientaccesspolicy.xml

    if (req.url == "/flashplayer.swf"){
      H.SetHeader("Content-Type", "application/x-shockwave-flash");
      H.SetHeader("Server", APPIDENT);
      H.SetBody((const char *)FlashMediaPlayback_101_swf, FlashMediaPlayback_101_swf_len);
      H.SendResponse("200", "OK", myConn);
      responded = true;
      return;
    }
    if (req.url == "/oldflashplayer.swf"){
      H.SetHeader("Content-Type", "application/x-shockwave-flash");
      H.SetHeader("Server", APPIDENT);
      H.SetBody((const char *)FlashMediaPlayback_swf, FlashMediaPlayback_swf_len);
      H.SendResponse("200", "OK", myConn);
      responded = true;
      return;
    }
    // send logo icon
    if (req.url.length() > 4 && req.url.substr(req.url.length() - 4, 4) == ".ico"){
      sendIcon(headersOnly);
      return;
    }

    // send generic HTML page
    if (req.url.length() > 6 && req.url.substr(req.url.length() - 5, 5) == ".html"){
      HTMLResponse(req, headersOnly);
      return;
    }

    // send smil MBR index
    if (req.url.length() > 6 && req.url.substr(req.url.length() - 5, 5) == ".smil"){
      HTTPOutput::respondHTTP(req, headersOnly);
      std::string reqHost = HTTP::URL(req.GetHeader("Host")).host;
      std::string port, url_rel;
      std::string trackSources; // this string contains all track sources for MBR smil
      {
        Util::DTSCShmReader rProto(SHM_PROTO);
        DTSC::Scan prtcls = rProto.getScan();
        Util::DTSCShmReader rCapa(SHM_CAPA);
        DTSC::Scan capa = rCapa.getMember("connectors").getMember("RTMP");
        unsigned int pro_cnt = prtcls.getSize();
        for (unsigned int i = 0; i < pro_cnt; ++i){
          if (prtcls.getIndice(i).getMember("connector").asString() != "RTMP"){continue;}
          port = prtcls.getIndice(i).getMember("port").asString();
          // get the default port if none is set
          if (!port.size()){
            port = capa.getMember("optional").getMember("port").getMember("default").asString();
          }
          // extract url
          url_rel = capa.getMember("url_rel").asString();
          if (url_rel.find('$')){url_rel.resize(url_rel.find('$'));}
        }

        initialize();
        if (!myConn){return;}
        std::set<size_t> validTracks = M.getValidTracks();
        for (std::set<size_t>::iterator it = validTracks.begin(); it != validTracks.end(); it++){
          if (M.getType(*it) == "video"){
            trackSources += "      <video src='" + streamName + "?track=" + JSON::Value((uint64_t)*it).asString() +
                            "' height='" + JSON::Value(M.getHeight(*it)).asString() +
                            "' system-bitrate='" + JSON::Value(M.getBps(*it)).asString() +
                            "' width='" + JSON::Value(M.getWidth(*it)).asString() + "' />\n";
          }
        }
      }

      H.SetHeader("Content-Type", "application/smil");
      if (headersOnly){
        H.SendResponse("200", "OK", myConn);
        responded = true;
        H.Clean();
        return;
      }
      H.SetBody("<smil>\n  <head>\n    <meta base='rtmp://" + reqHost + ":" + port + url_rel +
                "' />\n  </head>\n  <body>\n    <switch>\n" + trackSources + "    </switch>\n  </body>\n</smil>");
      H.SendResponse("200", "OK", myConn);
      responded = true;
      H.Clean();
      return;
    }

    if ((req.url.length() > 9 && req.url.substr(0, 6) == "/info_" && req.url.substr(req.url.length() - 3, 3) == ".js") ||
        (req.url.length() > 9 && req.url.substr(0, 6) == "/json_" && req.url.substr(req.url.length() - 3, 3) == ".js")){
      HTTPOutput::respondHTTP(req, headersOnly);
      if (websocketHandler(req, headersOnly)){return;}
      bool metaEverywhere = req.GetVar("metaeverywhere").size();
      std::string reqHost = HTTP::URL(req.GetHeader("Host")).host;
      std::string useragent = req.GetVar("ua");
      if (!useragent.size()){useragent = req.GetHeader("User-Agent");}
      std::string response;
      std::string rURL = req.url;
      if (rURL.substr(0, 6) != "/json_"){
        H.SetHeader("Content-Type", "application/javascript");
      }else{
        H.SetHeader("Content-Type", "application/json");
      }
      if (headersOnly){
        H.SendResponse("200", "OK", myConn);
        responded = true;
        H.Clean();
        return;
      }
      response = "// Generating info code for stream " + streamName + "\n\nif (!mistvideo){var mistvideo ={};}\n";
      JSON::Value json_resp = getStatusJSON(reqHost, useragent, metaEverywhere);
      if (rURL.substr(0, 6) != "/json_"){
        response += "mistvideo['" + streamName + "'] = " + json_resp.toString() + ";\n";
      }else{
        response = json_resp.toString();
      }
      H.SetBody(response);
      H.SendResponse("200", "OK", myConn);
      responded = true;
      H.Clean();
      return;
    }// embed code generator

    if ((req.url == "/player.js") || ((req.url.substr(0, 7) == "/embed_") && (req.url.length() > 10) &&
                                    (req.url.substr(H.url.length() - 3, 3) == ".js"))){
      HTTP::URL fullURL(req.GetHeader("Host"));
      if (!fullURL.protocol.size()){fullURL.protocol = getProtocolForPort(fullURL.getPort());}
      if (config->getString("pubaddr") != ""){
        HTTP::URL altURL(config->getString("pubaddr"));
        fullURL.protocol = altURL.protocol;
        if (altURL.host.size()){fullURL.host = altURL.host;}
        fullURL.port = altURL.port;
        fullURL.path = altURL.path;
      }
      if (mistPath.size()){fullURL = mistPath;}
      std::string response;
      std::string rURL = req.url;

      if ((rURL.substr(0, 7) == "/embed_") && (rURL.length() > 10) &&
          (rURL.substr(rURL.length() - 3, 3) == ".js")){
        HTTPOutput::respondHTTP(req, headersOnly);
      }

      H.SetHeader("Server", APPIDENT);
      H.setCORSHeaders();
      H.SetHeader("Content-Type", "application/javascript; charset=utf-8");
      if (headersOnly){
        H.SendResponse("200", "OK", myConn);
        responded = true;
        H.Clean();
        return;
      }

      response.append("if (typeof mistoptions == 'undefined'){mistoptions ={};}\nif (!('host' "
                      "in mistoptions)){mistoptions.host = '" +
                      fullURL.getUrl() + "';}\n");

#include "player.js.h"
      response.append((char *)player_js, (size_t)player_js_len);

      jsonForEach(config->getOption("wrappers", true), it){
        bool used = false;
        if (it->asStringRef() == "html5"){
#include "html5.js.h"
          response.append((char *)html5_js, (size_t)html5_js_len);
          used = true;
        }
        if (it->asStringRef() == "flash_strobe"){
#include "flash_strobe.js.h"
          response.append((char *)flash_strobe_js, (size_t)flash_strobe_js_len);
          used = true;
        }
        if (it->asStringRef() == "dashjs"){
#include "dashjs.js.h"
          response.append((char *)dash_js, (size_t)dash_js_len);
          used = true;
        }
        if (it->asStringRef() == "videojs"){
#include "videojs.js.h"
          response.append((char *)video_js, (size_t)video_js_len);
          used = true;
        }
        if (it->asStringRef() == "wheprtc") {
#include "wheprtc.js.h"
          response.append((char *)wheprtc_js, (size_t)wheprtc_js_len);
          used = true;
        }
        if (it->asStringRef() == "webrtc"){
#include "webrtc.js.h"
          response.append((char *)webrtc_js, (size_t)webrtc_js_len);
          used = true;
        }
        if (it->asStringRef() == "mews"){
#include "mews.js.h"
          response.append((char *)mews_js, (size_t)mews_js_len);
          used = true;
        }
        if (it->asStringRef() == "rawws"){
#include "rawws.js.h"
          response.append((char *)rawws_js, (size_t)rawws_js_len);
          used = true;
        }
        if (it->asStringRef() == "flv"){
#include "flv.js.h"
          response.append((char *)flv_js, (size_t)flv_js_len);
          used = true;
        }
        if (it->asStringRef() == "hlsjs"){
          #include "hlsjs.js.h"
          response.append((char*)hlsjs_js, (size_t)hlsjs_js_len);
          used = true;
        }
        if (!used){WARN_MSG("Unknown player type: %s", it->asStringRef().c_str());}
      }

      if ((rURL.substr(0, 7) == "/embed_") && (rURL.length() > 10) &&
          (rURL.substr(rURL.length() - 3, 3) == ".js")){
        response.append("var container = document.createElement(\"div\");\ncontainer.id = \"" +
                        streamName + "\";\ndocument.write(container.outerHTML);\nmistPlay(\"" +
                        streamName + "\",{target:document.getElementById(\"" + streamName + "\")});");
      }

      H.SetBody(response);
      H.SendResponse("200", "OK", myConn);
      responded = true;
      H.Clean();
      return;
    }

    if (req.url.substr(0, 7) == "/skins/"){
      std::string response;
      std::string url = req.url;
      H.SetHeader("Server", APPIDENT);
      H.setCORSHeaders();
      H.SetHeader("Content-Type", "text/css");
      if (headersOnly){
        H.SendResponse("200", "OK", myConn);
        responded = true;
        H.Clean();
        return;
      }

      if (url == "/skins/default.css"){
#include "skin_default.css.h"
        response.append((char *)skin_default_css, (size_t)skin_default_css_len);
      }else if (url == "/skins/dev.css"){
#include "skin_dev.css.h"
        response.append((char *)skin_dev_css, (size_t)skin_dev_css_len);
      }else if (url == "/skins/videojs.css"){
#include "skin_videojs.css.h"
        response.append((char *)skin_videojs_css, (size_t)skin_videojs_css_len);
      }else{
        H.SetBody("Unknown stylesheet: " + url);
        H.SendResponse("404", "Unknown stylesheet", myConn);
        responded = true;
        H.Clean();
        return;
      }

      H.SetBody(response);
      H.SendResponse("200", "OK", myConn);
      responded = true;
      H.Clean();
      return;
    }
    if (req.url == "/videojs.js"){
      std::string response;
      H.SetHeader("Server", APPIDENT);
      H.setCORSHeaders();
      H.SetHeader("Content-Type", "application/javascript");
      if (headersOnly){
        H.SendResponse("200", "OK", myConn);
        responded = true;
        H.Clean();
        return;
      }

#include "player_video.js.h"
      response.append((char *)player_video_js, (size_t)player_video_js_len);

      H.SetBody(response);
      H.SendResponse("200", "OK", myConn);
      responded = true;
      H.Clean();
      return;
    }
    if (req.url == "/dashjs.js"){
      std::string response;
      H.SetHeader("Server", APPIDENT);
      H.setCORSHeaders();
      H.SetHeader("Content-Type", "application/javascript");
      if (headersOnly){
        H.SendResponse("200", "OK", myConn);
        responded = true;
        H.Clean();
        return;
      }

#include "player_dash_lic.js.h"
      response.append((char *)player_dash_lic_js, (size_t)player_dash_lic_js_len);
#include "player_dash.js.h"
      response.append((char *)player_dash_js, (size_t)player_dash_js_len);

      H.SetBody(response);
      H.SendResponse("200", "OK", myConn);
      responded = true;
      H.Clean();
      return;
    }

    if (req.url == "/flv.js"){
      std::string response;
      H.SetHeader("Server", "MistServer/" PACKAGE_VERSION);
      H.setCORSHeaders();
      H.SetHeader("Content-Type", "application/javascript");
      if (headersOnly){
        H.SendResponse("200", "OK", myConn);
        H.Clean();
        return;
      }

#include "player_flv.js.h"
      response.append((char *)player_flv_js, (size_t)player_flv_js_len);

      H.SetBody(response);
      H.SendResponse("200", "OK", myConn);
      H.Clean();
      return;
    }
    if (req.url == "/hlsjs.js"){
      std::string response;
      H.SetHeader("Server", "MistServer/" PACKAGE_VERSION);
      H.setCORSHeaders();
      H.SetHeader("Content-Type", "application/javascript");
      if (headersOnly){
        H.SendResponse("200", "OK", myConn);
        H.Clean();
        return;
      }

      #include "player_hlsjs.js.h"
      response.append((char*)player_hlsjs_js, (size_t)player_hlsjs_js_len);

      H.SetBody(response);
      H.SendResponse("200", "OK", myConn);
      H.Clean();
      return;
    }
    if (req.url == "/libde265.js"){
      std::string response;
      H.Clean();
      H.SetHeader("Server", "MistServer/" PACKAGE_VERSION);
      H.setCORSHeaders();
      H.SetHeader("Content-Type", "application/javascript");
      if (headersOnly){
        H.SendResponse("200", "OK", myConn);
        H.Clean();
        return;
      }

      #include "player_libde265.js.h"
      response.append((char*)player_libde265_js, (size_t)player_libde265_js_len);

      H.SetBody(response);
      H.SendResponse("200", "OK", myConn);
      H.Clean();
      return;
    }
  }

  void OutHTTP::sendIcon(bool headersOnly){
#include "../icon.h"
    H.SetHeader("Content-Type", "image/x-icon");
    H.SetHeader("Server", APPIDENT);
    H.SetHeader("Content-Length", icon_len);
    H.setCORSHeaders();
    if (headersOnly){
      H.SendResponse("200", "OK", myConn);
      responded = true;
      H.Clean();
      return;
    }
    H.SendResponse("200", "OK", myConn);
    responded = true;
    myConn.SendNow((const char *)icon_data, icon_len);
    H.Clean();
  }

  bool OutHTTP::websocketHandler(const HTTP::Parser & req, bool headersOnly){
    stayConnected = true;
    std::string reqHost = HTTP::URL(req.GetHeader("Host")).host;
    bool metaEverywhere = req.GetVar("metaeverywhere").size();
    if (req.GetHeader("X-Mst-Path").size()){mistPath = req.GetHeader("X-Mst-Path");}
    std::string useragent = req.GetVar("ua");
    if (!useragent.size()){useragent = req.GetHeader("User-Agent");}
    std::string upgradeHeader = req.GetHeader("Upgrade");
    Util::stringToLower(upgradeHeader);
    if (upgradeHeader != "websocket"){return false;}
    HTTP::Websocket ws(myConn, req, H);
    if (!ws){return false;}
    setBlocking(false);
    // start the stream, if needed
    Util::sanitizeName(streamName);
    if (!Util::streamAlive(streamName)){Util::startInput(streamName, "", true, false);}

    char pageName[NAME_BUFFER_SIZE];
    std::string currStreamName;
    currStreamName = streamName;
    snprintf(pageName, NAME_BUFFER_SIZE, SHM_STREAM_STATE, streamName.c_str());
    IPC::sharedPage streamStatus(pageName, 2, false, false);
    uint8_t prevState, newState, pingCounter = 0;
    uint8_t prevStatePerc = 0, newStatePerc = 0;
    std::set<size_t> prevTracks;
    prevState = newState = STRMSTAT_INVALID;
    while (keepGoing()){
      if (!streamStatus || !streamStatus.exists()){streamStatus.init(pageName, 2, false, false);}
      if (!streamStatus){
        newState = STRMSTAT_OFF;
        newStatePerc = 0;
      }else{
        newState = streamStatus.mapped[0];
        if (streamStatus.len > 1){newStatePerc = streamStatus.mapped[1];}
      }

      if (meta){meta.reloadReplacedPagesIfNeeded();}
      if (newState != prevState || (newState == STRMSTAT_READY && M.getValidTracks() != prevTracks) || (newState != STRMSTAT_READY && newStatePerc != prevStatePerc)){
        if (newState == STRMSTAT_READY){
          thisError = "";
          reconnect();
          prevTracks = M.getValidTracks();
        }else{
          disconnect();
        }
        JSON::Value resp;
        // Check if we have an error message set
        if (thisError == ""){
          resp = getStatusJSON(reqHost, useragent, metaEverywhere);
        }else{
          resp["error"] = "Could not retrieve stream. Sorry.";
          resp["error_guru"] = thisError;
          if (config->getString("nostreamtext") != ""){
            resp["on_error"] = config->getString("nostreamtext");
          }
        }
        if (currStreamName != streamName){
          currStreamName = streamName;
          snprintf(pageName, NAME_BUFFER_SIZE, SHM_STREAM_STATE, streamName.c_str());
          streamStatus.close();
        }
        ws.sendFrame(resp.toString());
        prevState = newState;
        prevStatePerc = newStatePerc;
      }else{
        if (newState == STRMSTAT_READY){stats();}
        if (ws.readFrame(true)){
          onWebsocketFrame();
        }else{
          Util::sleep(250);
        }
        if ((++pingCounter % 40) == 0){ws.sendFrame("", 0, 0x9);}
      }
    }
    return true;
  }

}// namespace Mist
