#include "output_http.h"
#include <mist/checksum.h>
#include <mist/encode.h>
#include <mist/langcodes.h>
#include <mist/stream.h>
#include <mist/util.h>
#include <mist/url.h>
#include <set>
#include <sys/stat.h>

namespace Mist{
  HTTPOutput::HTTPOutput(Socket::Connection &conn) : Output(conn){
    webSock = 0;
    idleInterval = 0;
    idleLast = 0;
    if (config->getString("ip").size()){myConn.setHost(config->getString("ip"));}
    if (config->getString("prequest").size()){
      myConn.Received().prepend(config->getString("prequest"));
    }
    config->activate();
  }

  HTTPOutput::~HTTPOutput(){
    if (webSock){
      delete webSock;
      webSock = 0;
    }
  }

  void HTTPOutput::init(Util::Config *cfg){
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
    capa["forward"]["ip"]["name"] = "Previous request";
    capa["forward"]["ip"]["help"] =
        "Data to pretend arrived on the socket before parsing the socket.";
    capa["forward"]["ip"]["type"] = "str";
    capa["forward"]["ip"]["option"] = "--prequest";
    cfg->addOption("streamname", JSON::fromString("{\"arg\":\"string\",\"short\":\"s\",\"long\":"
                                                  "\"stream\",\"help\":\"The name of the stream "
                                                  "that this connector will transmit.\"}"));
    cfg->addOption("ip", JSON::fromString("{\"arg\":\"string\",\"short\":\"I\",\"long\":\"ip\","
                                          "\"help\":\"IP address of connection on stdio.\"}"));
    cfg->addOption("prequest", JSON::fromString("{\"arg\":\"string\",\"short\":\"R\",\"long\":"
                                                "\"prequest\",\"help\":\"Data to pretend arrived "
                                                "on the socket before parsing the socket.\"}"));
    cfg->addBasicConnectorOptions(capa);
  }

  void HTTPOutput::onFail(const std::string &msg, bool critical){
    if (!webSock && !isRecording() && !responded){
      H.Clean(); // make sure no parts of old requests are left in any buffers
      H.SetHeader("Server", APPIDENT);
      H.setCORSHeaders();
      H.SetBody("Could not retrieve stream: " + msg);
      H.SendResponse("404", "Error opening stream", myConn);
      responded = true;
    }
    Output::onFail(msg, critical);
  }

  bool isMatch(const std::string &url, const std::string &m, std::string &streamname){
    size_t found = m.find('$');
    if (found != std::string::npos){
      if (url.size() < m.size()){return false;}
      if (m.substr(0, found) == url.substr(0, found) &&
          m.substr(found + 1) == url.substr(url.size() - (m.size() - found) + 1)){
        if (url.substr(found, url.size() - m.size() + 1).find('/') != std::string::npos){
          return false;
        }
        streamname = url.substr(found, url.size() - m.size() + 1);
        return true;
      }
    }
    return (url == m);
  }

  bool isPrefix(const std::string &url, const std::string &m, std::string &streamname){
    size_t found = m.find('$');
    if (found != std::string::npos){
      if (url.size() < m.size()){return false;}
      size_t found_suf = url.find(m.substr(found + 1), found);
      if (m.substr(0, found) == url.substr(0, found) && found_suf != std::string::npos){
        if (url.substr(found, found_suf - found).find('/') != std::string::npos){return false;}
        streamname = url.substr(found, found_suf - found);
        return true;
      }
    }else{
      return (url.substr(0, m.size()) == m);
    }
    return false;
  }

  /// - anything else: The request should be dispatched to a connector on the named socket.
  std::string HTTPOutput::getHandler(){
    std::string url = H.getUrl();
    // check the current output first, the most common case
    if (capa.isMember("url_match") || capa.isMember("url_prefix")){
      bool match = false;
      std::string streamname;
      // if there is a matcher, try to match
      if (capa.isMember("url_match")){
        if (capa["url_match"].isArray()){
          jsonForEach(capa["url_match"], it){
            match |= isMatch(url, it->asStringRef(), streamname);
          }
        }
        if (capa["url_match"].isString()){
          match |= isMatch(url, capa["url_match"].asStringRef(), streamname);
        }
      }
      // if there is a prefix, try to match
      if (capa.isMember("url_prefix")){
        if (capa["url_prefix"].isArray()){
          jsonForEach(capa["url_prefix"], it){
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

    // loop over the connectors
    Util::DTSCShmReader rCapa(SHM_CAPA);
    DTSC::Scan capa = rCapa.getMember("connectors");
    unsigned int capa_ctr = capa.getSize();
    for (unsigned int i = 0; i < capa_ctr; ++i){
      DTSC::Scan c = capa.getIndice(i);
      // if it depends on HTTP and has a match or prefix...
      if ((c.getMember("name").asString() == "HTTP" || c.getMember("deps").asString() == "HTTP") &&
          (c.getMember("url_match") || c.getMember("url_prefix"))){
        bool match = false;
        std::string streamname;
        // if there is a matcher, try to match
        if (c.getMember("url_match")){
          if (c.getMember("url_match").getSize()){
            for (unsigned int j = 0; j < c.getMember("url_match").getSize(); ++j){
              match |= isMatch(url, c.getMember("url_match").getIndice(j).asString(), streamname);
            }
          }else{
            match |= isMatch(url, c.getMember("url_match").asString(), streamname);
          }
        }
        // if there is a prefix, try to match
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
          return capa.getIndiceName(i);
        }
      }
    }
    return "";
  }

  void HTTPOutput::requestHandler(){
    // Handle onIdle function caller, if needed
    if (idleInterval && (Util::bootMS() > idleLast + idleInterval)){
      onIdle();
      idleLast = Util::bootMS();
    }
    // Handle websockets
    if (webSock){
      if (webSock->readFrame()){
        onWebsocketFrame();
        idleLast = Util::bootMS();
        return;
      }
      if (!isBlocking && !parseData){Util::sleep(100);}
      return;
    }

    //Attempt to read a HTTP request, regardless of data being available
    bool sawRequest = false;
    while (H.Read(myConn)){
      sawRequest = true;
      std::string handler = getHandler();
      if (handler != capa["name"].asStringRef() || H.GetVar("stream") != streamName){
        INFO_MSG("Received request: %s => %s (%s)", H.getUrl().c_str(), handler.c_str(), H.GetVar("stream").c_str());
      }else{
        MEDIUM_MSG("Received request: %s => %s (%s)", H.getUrl().c_str(), handler.c_str(), H.GetVar("stream").c_str());
      }
      if (!handler.size()){
        H.Clean();
        H.SetHeader("Server", APPIDENT);
        H.setCORSHeaders();
        H.SetBody("<!DOCTYPE html><html><head><title>Unsupported Media "
                  "Type</title></head><body><h1>Unsupported Media Type</h1>The server isn't quite "
                  "sure what you wanted to receive from it.</body></html>");
        H.SendResponse("415", "Unsupported Media Type", myConn);
        myConn.close();
        return;
      }

      tkn.clear();
      // Read the session token
      if (Comms::tknMode & 0x01){
        // Get session token from the request url
        if (H.GetVar("tkn") != ""){
          tkn = H.GetVar("tkn");
        } else if (H.GetVar("sid") != ""){
          tkn = H.GetVar("sid");
        } else if (H.GetVar("sessId") != ""){
          tkn = H.GetVar("sessId");
        }
      }
      if ((Comms::tknMode & 0x02) && !tkn.size()){
        // Get session token from the request cookie
        std::map<std::string, std::string> storage;
        const std::string koekjes = H.GetHeader("Cookie");
        HTTP::parseVars(koekjes, storage, "; ");
        if (storage.count("tkn")){
          tkn = storage.at("tkn");
        }
      }
      // Generate a session token if it is being sent as a cookie or url parameter and we couldn't read one
      if (!tkn.size() && Comms::tknMode > 3){
        const std::string newTkn = UA + JSON::Value(getpid()).asString();
        tkn = JSON::Value(checksum::crc32(0, newTkn.data(), newTkn.size())).asString();
        HIGH_MSG("Generated tkn '%s'", tkn.c_str());
      }

      //Check if we need to change binary and/or reconnect
      if (handler != capa["name"].asStringRef() || H.GetVar("stream") != streamName || (statComm && (statComm.getHost() != getConnectedBinHost() || statComm.getTkn() != tkn))){
        MEDIUM_MSG("Switching from %s (%s) to %s (%s)", capa["name"].asStringRef().c_str(),
                   streamName.c_str(), handler.c_str(), H.GetVar("stream").c_str());
        streamName = H.GetVar("stream");
        userSelect.clear();
        if (statComm){statComm.setStatus(COMM_STATUS_DISCONNECT | statComm.getStatus());}
        reConnector(handler);
        onFail("Server error - could not start connector", true);
        return;
      }

      /*LTS-START*/
      {
        HTTP::URL qUrl("http://"+H.GetHeader("Host")+"/"+H.url + H.allVars());
        if (!qUrl.host.size()){qUrl.host = myConn.getBoundAddress();}
        if (!qUrl.port.size() && config->hasOption("port")){qUrl.port = config->getOption("port").asString();}
        reqUrl = qUrl.getUrl();
      }
      /*LTS-END*/
      if (H.hasHeader("User-Agent")){UA = H.GetHeader("User-Agent");}

      if (H.GetVar("audio") != ""){targetParams["audio"] = H.GetVar("audio");}
      if (H.GetVar("video") != ""){targetParams["video"] = H.GetVar("video");}
      if (H.GetVar("subtitle") != ""){targetParams["subtitle"] = H.GetVar("subtitle");}
      if (H.GetVar("start") != ""){targetParams["start"] = H.GetVar("start");}
      if (H.GetVar("stop") != ""){targetParams["stop"] = H.GetVar("stop");}
      if (H.GetVar("startunix") != ""){targetParams["startunix"] = H.GetVar("startunix");}
      if (H.GetVar("stopunix") != ""){targetParams["stopunix"] = H.GetVar("stopunix");}
      // allow setting of max lead time through buffer variable.
      // max lead time is set in MS, but the variable is in integer seconds for simplicity.
      if (H.GetVar("buffer") != ""){
        maxSkipAhead = JSON::Value(H.GetVar("buffer")).asInt() * 1000;
      }
      // allow setting of play back rate through rate variable or custom HTTP header.
      // play back rate is set in MS per second, but the variable is a simple multiplier.
      if (H.GetVar("rate") != ""){
        long long int multiplier = JSON::Value(H.GetVar("rate")).asInt();
        if (multiplier){
          realTime = 1000 / multiplier;
        }else{
          realTime = 0;
        }
      }
      if (H.GetHeader("X-Mist-Rate") != ""){
        long long int multiplier = JSON::Value(H.GetHeader("X-Mist-Rate")).asInt();
        if (multiplier){
          realTime = 1000 / multiplier;
        }else{
          realTime = 0;
        }
      }

      // Handle upgrade to websocket if the output supports it
      std::string upgradeHeader = H.GetHeader("Upgrade");
      Util::stringToLower(upgradeHeader);
      if (doesWebsockets() && upgradeHeader == "websocket"){
        INFO_MSG("Switching to Websocket mode");
        setBlocking(false);
        preWebsocketConnect();
        HTTP::Parser req = H;
        H.Clean();
        webSock = new HTTP::Websocket(myConn, req, H);
        if (!(*webSock)){
          delete webSock;
          webSock = 0;
          return;
        }
        onWebsocketConnect();
        H.Clean();
        return;
      }
      responded = false;
      preHTTP();
      if (!myConn){return;}
      onHTTP();
      idleLast = Util::bootMS();
      // Prevent the clean as well as the loop when we're in the middle of handling a request now
      if (!wantRequest){return;}
      H.Clean();
    }
    // If we can't read anything more and we're non-blocking, sleep some.
    if (!sawRequest && !myConn.spool() && !isBlocking && !parseData){Util::sleep(100);}
  }

  /// Default HTTP handler.
  /// Only takes care of OPTIONS and HEAD, saving the original request, and calling respondHTTP
  void HTTPOutput::onHTTP(){
    const HTTP::Parser reqH = H;
    bool headersOnly = (reqH.method == "OPTIONS" || reqH.method == "HEAD");
    H.Clean();
    respondHTTP(reqH, headersOnly);
  }

  /// Default implementation of preHTTP simply calls initialize and selectDefaultTracks.
  void HTTPOutput::preHTTP(){
    initialize();
    selectDefaultTracks();
  }

  /// Sets common HTTP headers. Virtual, so child classes can/will implement further behaviour if needed.
  /// Child classes are suggested to call the parent implementation and then add their own logic afterwards.
  void HTTPOutput::respondHTTP(const HTTP::Parser & req, bool headersOnly){
    //We generally want the CORS headers to be set for all responses
    H.setCORSHeaders();
    H.SetHeader("Server", APPIDENT);
    if (tkn.size()){
      if (Comms::tknMode & 0x08){
        std::stringstream cookieHeader;
        cookieHeader << "tkn=" << tkn << "; Max-Age=" << SESS_TIMEOUT;
        H.SetHeader("Set-Cookie", cookieHeader.str()); 
      }
    }
    //Set attachment header to force download, if applicable
    if (req.GetVar("dl").size()){
      //If we want to download, and the string contains a dot, use as-is.
      std::string dl = req.GetVar("dl");
      //Without a dot, we use the last segment of the bare URL
      if (dl.find('.') == std::string::npos){
        dl = HTTP::URL(req.url).getFilePath();
        size_t lSlash = dl.rfind('/');
        if (lSlash != std::string::npos){dl = dl.substr(lSlash+1);}
      }
      H.SetHeader("Content-Disposition", "attachment; filename=" + Encodings::URL::encode(dl) + ";");
      //Force max download speed when downloading
      realTime = 0;
    }
    //If there is a method defined, and the first method has a type with two slashes in it,
    //assume the last two sections are the content type.
    if (capa.isMember("methods") && capa["methods"].isArray() && capa["methods"].size() && capa["methods"][0u].isMember("type")){
      const std::string & cType = capa["methods"][0u]["type"].asStringRef();
      size_t fSlash = cType.find('/');
      if (fSlash != std::string::npos && cType.rfind('/') != fSlash){
        H.SetHeader("Content-Type", cType.substr(fSlash+1));
      }
    }
  }

  static inline void builPipedPart(JSON::Value &p, char *argarr[], int &argnum, const JSON::Value &argset){
    jsonForEachConst(argset, it){
      if (it->isMember("option") && p.isMember(it.key())){
        if (!it->isMember("type")){
          if (JSON::Value(p[it.key()]).asBool()){
            argarr[argnum++] = (char *)((*it)["option"].c_str());
          }
          continue;
        }
        if ((*it)["type"].asStringRef() == "inputlist" && p[it.key()].isArray()){
          jsonForEach(p[it.key()], iVal){
            (*iVal) = iVal->asString();
            argarr[argnum++] = (char *)((*it)["option"].c_str());
            argarr[argnum++] = (char *)((*iVal).c_str());
          }
          continue;
        }
        if ((*it)["type"].asStringRef() == "uint" || (*it)["type"].asStringRef() == "int" ||
            (*it)["type"].asStringRef() == "debug"){
          p[it.key()] = JSON::Value(p[it.key()].asInt()).asString();
        }else{
          p[it.key()] = p[it.key()].asString();
        }
        if (p[it.key()].asStringRef().size() > 0){
          argarr[argnum++] = (char *)((*it)["option"].c_str());
          argarr[argnum++] = (char *)(p[it.key()].c_str());
        }else{
          argarr[argnum++] = (char *)((*it)["option"].c_str());
        }
      }
    }
  }

  ///\brief Handles requests by starting a corresponding output process.
  ///\param connector The type of connector to be invoked.
  void HTTPOutput::reConnector(std::string &connector){
    // Clear tkn in order to deal with reverse proxies
    tkn = "";
    // taken from CheckProtocols (controller_connectors.cpp)
    char *argarr[32];
    for (int i = 0; i < 32; i++){argarr[i] = 0;}
    int id = -1;
    JSON::Value pipedCapa;
    JSON::Value p; // properties of protocol

    {
      Util::DTSCShmReader rProto(SHM_PROTO);
      DTSC::Scan prots = rProto.getScan();
      unsigned int prots_ctr = prots.getSize();

      if (connector == "HTTP" || connector == "HTTP.exe"){
        // restore from values in the environment, regardless of configged settings
        if (getenv("MIST_HTTP_nostreamtext")){
          p["nostreamtext"] = getenv("MIST_HTTP_nostreamtext");
        }
        if (getenv("MIST_HTTP_pubaddr")){
          std::string pubAddrs = getenv("MIST_HTTP_pubaddr");
          p["pubaddr"] = JSON::fromString(pubAddrs);
        }
      }else{
        // find connector in config
        for (unsigned int i = 0; i < prots_ctr; ++i){
          if (prots.getIndice(i).getMember("connector").asString() == connector){
            id = i;
            break; // pick the first protocol in the list that matches the connector
          }
        }
        if (id == -1){
          connector = connector + ".exe";
          for (unsigned int i = 0; i < prots_ctr; ++i){
            if (prots.getIndice(i).getMember("connector").asString() == connector){
              id = i;
              break; // pick the first protocol in the list that matches the connector
            }
          }
          if (id == -1){
            connector = connector.substr(0, connector.size() - 4);
            ERROR_MSG("No connector found for: %s", connector.c_str());
            return;
          }
        }
        // read options from found connector
        p = prots.getIndice(id).asJSON();
      }

      HIGH_MSG("Connector found: %s", connector.c_str());
      Util::DTSCShmReader rCapa(SHM_CAPA);
      DTSC::Scan capa = rCapa.getMember("connectors");
      pipedCapa = capa.getMember(connector).asJSON();
    }
    // build arguments for starting output process
    std::string tmparg = Util::getMyPath() + std::string("MistOut") + connector;
    std::string tmpPrequest;
    if (H.url.size()){tmpPrequest = H.BuildRequest();}
    int argnum = 0;
    argarr[argnum++] = (char *)tmparg.c_str();
    std::string temphost = getConnectedHost();
    std::string debuglevel = JSON::Value(Util::printDebugLevel).asString();
    argarr[argnum++] = (char *)"--ip";
    argarr[argnum++] = (char *)(temphost.c_str());
    argarr[argnum++] = (char *)"--stream";
    argarr[argnum++] = (char *)(streamName.c_str());
    argarr[argnum++] = (char *)"--prequest";
    argarr[argnum++] = (char *)(tmpPrequest.c_str());
    // set the debug level if non-default
    if (Util::printDebugLevel != DEBUG){
      argarr[argnum++] = (char *)"--debug";
      argarr[argnum++] = (char *)(debuglevel.c_str());
    }
    if (pipedCapa.isMember("required")){builPipedPart(p, argarr, argnum, pipedCapa["required"]);}
    if (pipedCapa.isMember("optional")){builPipedPart(p, argarr, argnum, pipedCapa["optional"]);}

    /// start new/better process
    execv(argarr[0], argarr);
  }

  /*LTS-START*/
  std::string HTTPOutput::getConnectedHost(){
    std::string host = Output::getConnectedHost();
    std::string xRealIp = H.GetHeader("X-Real-IP");

    if (!xRealIp.size() || !isTrustedProxy(host)){
      static bool msg = false;
      if (xRealIp.size() && !msg && xRealIp != host){
        WARN_MSG("Host %s is attempting to act as a proxy, but not trusted", host.c_str());
        msg = true;
      }
      return host;
    }
    return xRealIp;
  }
  std::string HTTPOutput::getConnectedBinHost(){
    // Do first check with connected host because of simplicity
    std::string host = Output::getConnectedHost();
    std::string xRealIp = H.GetHeader("X-Real-IP");

    if (!xRealIp.size() || !isTrustedProxy(host)){
      static bool msg = false;
      if (xRealIp.size() && !msg && xRealIp != host){
        WARN_MSG("Host %s is attempting to act as a proxy, but not trusted", host.c_str());
        msg = true;
      }
      return Output::getConnectedBinHost();
    }

    Socket::Connection binConn;
    binConn.setHost(xRealIp);
    return binConn.getBinHost();
  }

  bool HTTPOutput::isTrustedProxy(const std::string &ip){
    static std::set<std::string> trustedProxies;
    if (!trustedProxies.size()){
      trustedProxies.insert("localhost");

      IPC::sharedPage rPage(SHM_PROXY, 0, false, false);
      if (rPage){
        Util::RelAccX rAcc(rPage.mapped);
        std::string trustedList(rAcc.getPointer("proxy_data"), rAcc.getSize("proxy_data"));
        size_t pos = 0;
        size_t endPos;
        while (pos != std::string::npos){
          endPos = trustedList.find(" ", pos);
          trustedProxies.insert(trustedList.substr(pos, endPos - pos));
          pos = endPos;
          if (pos != std::string::npos){pos++;}
        }
      }
    }
    std::string binIp = Socket::getBinForms(ip);
    for (std::set<std::string>::iterator it = trustedProxies.begin(); it != trustedProxies.end(); ++it){
      if (Socket::isBinAddress(binIp, *it)){return true;}
    }
    return false;
  }
  /*LTS-END*/

  /// Parses a "Range: " header, setting byteStart and byteEnd.
  /// Assumes byteStart and byteEnd are initialized to their minimum respectively maximum values
  /// when the function is called. On error, byteEnd is set to zero and the function return false.
  bool HTTPOutput::parseRange(std::string header, uint64_t &byteStart, uint64_t &byteEnd){
    if (header.size() < 6 || header.substr(0, 6) != "bytes="){
      byteEnd = 0;
      WARN_MSG("Invalid range header: %s", header.c_str());
      return false;
    }
    header.erase(0, 6);
    // Do parsing of the rest of the header...
    if (header.size() && header[0] == '-'){
      // negative range = count from end
      byteStart = 0;
      for (unsigned int i = 1; i < header.size(); ++i){
        if (header[i] >= '0' && header[i] <= '9'){
          byteStart *= 10;
          byteStart += header[i] - '0';
          continue;
        }
        break;
      }
      if (byteStart > byteEnd){
        // entire file if starting before byte zero
        byteStart = 0;
      }else{
        // start byteStart bytes before byteEnd
        byteStart = byteEnd - byteStart;
      }
      MEDIUM_MSG("Range request: %" PRIu64 "-%" PRIu64 " (%s)", byteStart, byteEnd, header.c_str());
      return true;
    }

    // Positive range
    long long size = byteEnd;
    byteEnd = 0;
    byteStart = 0;
    unsigned int i = 0;
    for (; i < header.size(); ++i){
      if (header[i] >= '0' && header[i] <= '9'){
        byteStart *= 10;
        byteStart += header[i] - '0';
        continue;
      }
      break;
    }
    if (header[i] != '-'){
      WARN_MSG("Invalid range header: %s", header.c_str());
      byteEnd = 0;
      return false;
    }
    ++i;
    if (i < header.size()){
      for (; i < header.size(); ++i){
        if (header[i] >= '0' && header[i] <= '9'){
          byteEnd *= 10;
          byteEnd += header[i] - '0';
          continue;
        }
        break;
      }
      if (byteEnd > size){byteEnd = size;}
    }else{
      byteEnd = size;
    }
    MEDIUM_MSG("Range request: %" PRIu64 "-%" PRIu64 " (%s)", byteStart, byteEnd, header.c_str());
    return true;
  }

}// namespace Mist
