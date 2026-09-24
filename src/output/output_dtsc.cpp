#include "output_dtsc.h"
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <mist/auth.h>
#include <mist/bitfields.h>
#include <mist/defines.h>
#include <mist/stream.h>
#include <mist/triggers.h>
#include <mist/http_parser.h>
#include <sys/stat.h>

class crtAndKey {
  public:
    crtAndKey() {
      mbedtls_x509_crt_init(&crt);
      mbedtls_pk_init(&key);
    }
    mbedtls_x509_crt crt;
    mbedtls_pk_context key;
    std::string crtFile, keyFile;
};
std::deque<crtAndKey> srvcerts;

static int cert_callback(void *p_info, mbedtls_ssl_context *ssl, const unsigned char *name, size_t name_len) {
  std::string sniName((char *)name, name_len);

  for (auto & it : srvcerts) {
    std::string subject((char *)it.crt.subject.val.p, it.crt.subject.val.len);
    if (sniName != subject) {
      bool found = false;
      mbedtls_asn1_sequence *cur = &it.crt.subject_alt_names;
      while (cur) {
        if (sniName == std::string((char *)cur->buf.p, cur->buf.len)) {
          found = true;
          break;
        }
        if (cur->buf.len && cur->buf.p[0] == '*' && sniName.size() >= cur->buf.len) {
          if (sniName.substr(sniName.size() - (cur->buf.len - 1)) == std::string((char *)cur->buf.p + 1, cur->buf.len - 1)) {
            if (sniName.substr(0, sniName.size() - (cur->buf.len - 1)).find('.') == std::string::npos) {
              found = true;
              break;
            }
          }
        }
        cur = cur->next;
      }
      if (!found) { continue; }
    }
    MEDIUM_MSG("Matched %s to (%s, %s)!", sniName.c_str(), it.crtFile.c_str(), it.keyFile.c_str());

    int r = mbedtls_ssl_set_hs_own_cert(ssl, &(it.crt), &(it.key));
    if (r) { WARN_MSG("Could not set certificate!"); }
    return r;
  }
  WARN_MSG("Could not find matching certificate for %s; using default certificate instead", sniName.c_str());
  int r = mbedtls_ssl_set_hs_own_cert(ssl, &(srvcerts.begin()->crt), &(srvcerts.begin()->key));
  if (r) { WARN_MSG("Could not set certificate!"); }
  return r;
}

namespace Mist{
  OutDTSC::OutDTSC(Socket::Connection & conn, Util::Config & _cfg, JSON::Value & _capa) : Output(conn, _cfg, _capa) {
#ifdef SSL
    if (setupTLS()) { myConn.sslAccept(&sslConf, &ctr_drbg); }
#endif
    JSON::Value prep;
    if (!config->getBool("syncmode")) { setSyncMode(false); }
    isSyncReceiver = false;
    lastActive = Util::epoch();
    if (config->getString("target").size()){
      streamName = config->getString("streamname");
      pushUrl = HTTP::URL(config->getString("target"));
      if (pushUrl.protocol != "dtsc") { return; }

      setSyncMode(JSON::Value(targetParams["sync"]).asBool());

      if (!pushUrl.path.size()){pushUrl.path = streamName;}
      INFO_MSG("About to push stream %s out. Host: %s, port: %d, target stream: %s", streamName.c_str(),
               pushUrl.host.c_str(), pushUrl.getPort(), pushUrl.path.c_str());
      myConn.close();
      myConn.Received().clear();
      myConn.open(pushUrl.host, pushUrl.getPort(), true);
      initialize();
      initialSeek();
      if (!myConn){
        onFail("Could not start push, aborting", true);
        return;
      }
      prep["cmd"] = "push";
      prep["version"] = APPIDENT;
      prep["stream"] = pushUrl.path;
      std::map<std::string, std::string> args;
      HTTP::parseVars(pushUrl.args, args);
      if (args.count("pass")){prep["password"] = args["pass"];}
      if (args.count("pw")){prep["password"] = args["pw"];}
      if (args.count("password")){prep["password"] = args["password"];}
      if (pushUrl.pass.size()){prep["password"] = pushUrl.pass;}
      if (getSyncMode()) { prep["sync"] = true; }
      sendCmd(prep);
      wantRequest = true;
      parseData = true;
      return;
    }

    setBlocking(true);
    prep["cmd"] = "hi";
    prep["version"] = APPIDENT;
    prep["pack_method"] = 2;
    salt = Secure::md5("mehstuff" + JSON::Value((uint64_t)time(0)).asString());
    prep["salt"] = salt;
    /// \todo Make this securererer.
    sendCmd(prep);
  }

#ifdef SSL
  bool OutDTSC::setupTLS() {
    isTLSEnabled = false;
    // No cert or key? Non-SSL mode.
    if (config->getOption("cert", true).size() < 2 || config->getOption("key", true).size() < 2){
      INFO_MSG("No cert or key set, regular RTMP mode");
      return false;
    }

    INFO_MSG("Cert and key set, RTMPS mode");

    // Declare and set up all required mbedtls structures
    int ret;
    mbedtls_ssl_config_init(&sslConf);
    mbedtls_entropy_init(&entropy);
    mbedtls_pk_init(&pkey);
    mbedtls_x509_crt_init(&srvcert);
    mbedtls_ctr_drbg_init(&ctr_drbg);
    isTLSEnabled = true;

    mbedtls_debug_set_threshold(3);
    mbedtls_ssl_conf_dbg(&sslConf, [](void *ctx, int level, const char *file, int line, const char *str) {
      const int lvl = level + 5;
      if (Util::printDebugLevel >= lvl) {
        fprintf(stderr, "%.8s|%.30s|%d|%.100s:%d|%.200s|TLS: %s\n", DBG_LVL_LIST[lvl], MIST_PROG, getpid(), file, line,
                Util::streamName, str);
      }
    }, 0);

    // seed the rng
    if ((ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                                     (const unsigned char *)APPNAME, strlen(APPNAME))) != 0){
      FAIL_MSG("Could not seed the random number generator!");
    }

    // Read certificate chain(s) from cmdline option(s)
    bool ignoreKeys = false;
    JSON::Value certs = config->getOption("cert", true);
    jsonForEach (certs, it) {
      const std::string & cFile = it->asStringRef();
      if (!cFile.size()) { continue; } // Ignore empty entries (default is empty)
      srvcerts.push_back(crtAndKey());
      crtAndKey & srvcert = srvcerts.back();
      if (cFile[0] == '[') {
        ignoreKeys = true;
        JSON::Value crtCnf(PARSEJSON, cFile);
        jsonForEachConst (crtCnf, jt) {
          if (!jt->asStringRef().size()) { continue; }
          if (jt.num() + 1 != crtCnf.size()) {
            if (!srvcert.crtFile.size()) { srvcert.crtFile = jt->asStringRef(); }
            ret = mbedtls_x509_crt_parse_file(&srvcert.crt, jt->asStringRef().c_str());
            if (ret) { WARN_MSG("Could not load any certificates from file: %s", jt->asStringRef().c_str()); }
          } else {
            srvcert.keyFile = jt->asStringRef();
            ret = mbedtls_pk_parse_keyfile(&(srvcert.key), jt->asStringRef().c_str(), NULL
#if MBEDTLS_VERSION_MAJOR > 2
                                           ,
                                           mbedtls_ctr_drbg_random, &ctr_drbg
#endif
            );
            if (ret) { WARN_MSG("Could not load any keys from file: %s", jt->asStringRef().c_str()); }
          }
        }
        continue;
      }
      srvcert.crtFile = cFile;
      ret = mbedtls_x509_crt_parse_file(&srvcert.crt, cFile.c_str());
      if (ret != 0) { WARN_MSG("Could not load any certificates from file: %s", cFile.c_str()); }
    }

    if (!ignoreKeys) {
      auto crtIt = srvcerts.begin();
      // Read key from cmdline option
      JSON::Value keys = config->getOption("key", true);
      jsonForEach (keys, it) {
        if (!it->asStringRef().size()) { continue; } // Ignore empty entries (default is empty)
        if (crtIt == srvcerts.end()) { break; }
        crtIt->keyFile = it->asStringRef();
        ret = mbedtls_pk_parse_keyfile(&(crtIt->key), it->asStringRef().c_str(), NULL
#if MBEDTLS_VERSION_MAJOR > 2
                                       ,
                                       mbedtls_ctr_drbg_random, &ctr_drbg
#endif
        );
        if (ret != 0) { WARN_MSG("Could not load any keys from file: %s", config->getString("key").c_str()); }
        ++crtIt;
      }
    }

    if ((ret = mbedtls_ssl_config_defaults(&sslConf, MBEDTLS_SSL_IS_SERVER, MBEDTLS_SSL_TRANSPORT_STREAM,
                                           MBEDTLS_SSL_PRESET_DEFAULT)) != 0){
      FAIL_MSG("SSL config defaults failed");
      return false;
    }
    mbedtls_ssl_conf_rng(&sslConf, mbedtls_ctr_drbg_random, &ctr_drbg);
    mbedtls_ssl_conf_ca_chain(&sslConf, srvcert.next, NULL);
    if ((ret = mbedtls_ssl_conf_own_cert(&sslConf, &srvcert, &pkey)) != 0){
      FAIL_MSG("SSL config own certificate failed");
      return false;
    }
    mbedtls_ssl_conf_sni(&sslConf, cert_callback, (void *)&srvcerts);
    return true;
  }
#endif

  OutDTSC::~OutDTSC(){
#ifdef SSL
    if (isTLSEnabled) {
      // Free all the mbedtls structures
      mbedtls_x509_crt_free(&srvcert);
      mbedtls_pk_free(&pkey);
      mbedtls_ssl_config_free(&sslConf);
      mbedtls_ctr_drbg_free(&ctr_drbg);
      mbedtls_entropy_free(&entropy);
      isTLSEnabled = false;
    }
#endif
  }

  bool OutDTSC::isFileTarget() {
    if (!isRecording()) { return false; }
    pushUrl = HTTP::URL(config->getString("target"));
    if (pushUrl.protocol == "dtsc") { return false; }
    return true;
  }

  void OutDTSC::stats(bool force){
    unsigned long long int now = Util::epoch();
    if (now - lastActive > 1 && !pushing){
      JSON::Value prep;
      prep["cmd"] = "ping";
      sendCmd(prep);
      lastActive = now;
    }
    Output::stats(force);
  }

  void OutDTSC::sendCmd(const JSON::Value &data){
    MEDIUM_MSG("Sending DTCM: %s", data.toString().c_str());
    myConn.SendNow("DTCM");
    char sSize[4] ={0, 0, 0, 0};
    Bit::htobl(sSize, data.packedSize());
    myConn.SendNow(sSize, 4);
    data.sendTo(myConn);
  }

  void OutDTSC::sendOk(const std::string &msg){
    JSON::Value err;
    err["cmd"] = "ok";
    err["msg"] = msg;
    sendCmd(err);
  }

  void OutDTSC::init(Util::Config *cfg, JSON::Value & capa) {
    Output::init(cfg, capa);
    capa["name"] = "DTSC";
    capa["friendly"] = "DTSC";
    capa["desc"] = "Real time streaming over DTSC (proprietary protocol for efficient inter-server streaming)";
    capa["deps"] = "";
    capa["codecs"][0u][0u].append("+*");
    cfg->addStandardPushCapabilities(capa);
    capa["push_urls"].append("dtsc://*");
    capa["incoming_push_url"] = "dtsc://$host:$port/$stream?pass=$password";
    capa["sort"] = "sort";

    capa["url_rel"] = "/$";

    { // playback method
      JSON::Value & M = capa["methods"].append();
      M["hrn"] = "DTSC";
      M["handler"] = "dtsc";
      M["type"] = "dtsc";
      M["latency"] = 0;
      M["bw"].fromString("[0, 0, 14]");
      M["control"] = 10;
      M["stability"] = 10;
      M["cpu_server"] = 10;
      M["permissibility"] = 7;
    }

    capa["optional"]["syncmode"]["name"] = "Default sync mode";
    capa["optional"]["syncmode"]["help"] = "0 for async (default), 1 for sync";
    capa["optional"]["syncmode"]["option"] = "--syncmode";
    capa["optional"]["syncmode"]["short"] = "x";
    capa["optional"]["syncmode"]["type"] = "uint";
    capa["optional"]["syncmode"]["default"] = 0;

#ifdef SSL
    capa["optional"]["cert"]["name"] = "Certificate";
    capa["optional"]["cert"]["help"] =
      "Path to the file(s) containing certificate chain(s). When multiple chains are used make sure to "
      "provide their matching keys in the same order.";
    capa["optional"]["cert"]["option"] = "--cert";
    capa["optional"]["cert"]["short"] = "C";
    capa["optional"]["cert"]["default"] = "";
    capa["optional"]["cert"]["type"] = "inputlist";
    capa["optional"]["cert"]["input"]["type"] = "browse";
    capa["optional"]["cert"]["sort"] = "aab";
    capa["optional"]["key"]["name"] = "Key";
    capa["optional"]["key"]["help"] =
      "Path to private key for SSL. When multiple are used make sure they are in order matching the certificates.";
    capa["optional"]["key"]["option"] = "--key";
    capa["optional"]["key"]["short"] = "k";
    capa["optional"]["key"]["default"] = "";
    capa["optional"]["key"]["type"] = "inputlist";
    capa["optional"]["key"]["input"]["type"] = "browse";
    capa["optional"]["key"]["sort"] = "aac";
#endif

    JSON::Value opt;
    opt["arg"] = "string";
    opt["default"] = "";
    opt["arg_num"] = 1;
    opt["help"] = "Target DTSC URL to push out towards.";
    cfg->addOption("target", opt);
    cfg->addOption("streamname", R"-({
      "arg":"string",
      "short":"s",
      "long":"stream",
      "help":"The name of the stream that this connector will transmit."
    })-");

    opt.null();
    opt["arg"] = "string";
    opt["default"] = "";
    opt["short"] = "U";
    opt["long"] = "pushurl";
    opt["help"] = "Target DTSC URL to pretend pushing out towards, when not actually connected to another host";
    cfg->addOption("pushurl", opt);

    cfg->addConnectorOptions(4200, capa);
  }

  std::string OutDTSC::getStatsName(){return (pushing ? "INPUT:DTSC" : "OUTPUT:DTSC");}

  void OutDTSC::sendNext(){
    DTSC::Packet p(thisPacket, thisIdx+1);
    if (M.hasEmbeddedFrames(thisIdx)) { p.genericFill(thisTime, 0, thisIdx + 1, thisData, thisDataLen, 0, true); }
    myConn.setBlocking(true);
    myConn.SendNow(p.getData(), p.getDataLen());
    myConn.setBlocking(false);
    lastActive = Util::epoch();
    // If selectable tracks changed, set sentHeader to false to force it to send init data
    static uint64_t lastMeta = 0;
    if (Util::epoch() > lastMeta + 5){
      lastMeta = Util::epoch();
      if (selectDefaultTracks()){
        INFO_MSG("Track selection changed - resending headers and continuing");
        sentHeader = false;
        return;
      }
    }
  }

  void OutDTSC::sendHeader(){
    if (!sentHeader && config->getString("pushurl").size()) {
      pushUrl = HTTP::URL(config->getString("pushurl"));
      if (pushUrl.protocol != "dtsc") {
        WARN_MSG("Invalid push URL format, must start with dtsc:// - %s", config->getString("pushURI").c_str());
      } else {
        if (!pushUrl.path.size()) { pushUrl.path = streamName; }
        INFO_MSG("About to push stream %s out. target stream: %s", streamName.c_str(), pushUrl.path.c_str());
        JSON::Value prep;
        prep["cmd"] = "push";
        prep["version"] = APPIDENT;
        prep["stream"] = pushUrl.path;
        std::map<std::string, std::string> args;
        HTTP::parseVars(pushUrl.args, args);
        if (args.count("pass")) { prep["password"] = args["pass"]; }
        if (args.count("pw")) { prep["password"] = args["pw"]; }
        if (args.count("password")) { prep["password"] = args["password"]; }
        if (pushUrl.pass.size()) { prep["password"] = pushUrl.pass; }
        if (getSyncMode()) { prep["sync"] = true; }
        sendCmd(prep);
      }
    }
    sentHeader = true;
    std::set<size_t> selectedTracks;
    for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end(); it++){
      selectedTracks.insert(it->first);
    }
    M.send(myConn, true, selectedTracks, true);
    if (M.getLive()){realTime = 0;}
  }

  void OutDTSC::onFail(const std::string &msg, bool critical){
    JSON::Value err;
    err["cmd"] = "error";
    err["msg"] = msg;
    sendCmd(err);
    Output::onFail(msg, critical);
  }

  void OutDTSC::onRequest(){
    while (myConn.Received().available(8)){
      if (myConn.Received().copy(4) == "DTCM"){
        // Command message
        std::string toRec = myConn.Received().copy(8);
        unsigned long rSize = Bit::btohl(toRec.c_str() + 4);
        if (!myConn.Received().available(8 + rSize)){return;}// abort - not enough data yet
        myConn.Received().remove(8);
        std::string dataPacket = myConn.Received().remove(rSize);
        DTSC::Scan dScan((char *)dataPacket.data(), rSize);
        HIGH_MSG("Received DTCM: %s", dScan.asJSON().toString().c_str());
        if (dScan.getMember("cmd").asString() == "ok"){
          INFO_MSG("Remote OK: %s", dScan.getMember("msg").asString().c_str());
          if (dScan.getMember("msg").asString() == "pong"){
            if (selectDefaultTracks()){
              INFO_MSG("Track selection changed while idle - resending headers");
              sentHeader = false;
            }
          }
          continue;
        }
        if (dScan.getMember("cmd").asString() == "push"){
          handlePush(dScan);
          continue;
        }
        if (dScan.getMember("cmd").asString() == "play"){
          handlePlay(dScan);
          continue;
        }
        if (dScan.getMember("cmd").asString() == "ping"){
          sendOk("Pong!");
          continue;
        }
        if (dScan.getMember("cmd").asString() == "ok"){
          INFO_MSG("Ok: %s", dScan.getMember("msg").asString().c_str());
          continue;
        }
        if (dScan.getMember("cmd").asString() == "hi"){
          INFO_MSG("Connected to server running version %s", dScan.getMember("version").asString().c_str());
          continue;
        }
        if (dScan.getMember("cmd").asString() == "error"){
          ERROR_MSG("%s", dScan.getMember("msg").asString().c_str());
          continue;
        }
        if (dScan.getMember("cmd").asString() == "reset"){
          userSelect.clear();
          sendOk("Internal state reset");
          continue;
        }
        if (dScan.getMember("cmd").asString() == "check_key_duration"){
          size_t idx = dScan.getMember("id").asInt() - 1;
          size_t dur = dScan.getMember("duration").asInt();
          if (!M.trackValid(idx)){
            ERROR_MSG("Cannot check key duration %zu for track %zu: not valid", dur, idx);
            return;
          }
          uint32_t longest_key = 0;
          // Note: specifically uses `keys` instead of `getKeys` since we want _all_ data, regardless of limiting
          DTSC::Keys Mkeys(M.keys(idx));
          uint32_t firstKey = Mkeys.getFirstValid();
          uint32_t endKey = Mkeys.getEndValid();
          for (uint32_t k = firstKey; k+1 < endKey; k++){
            uint64_t kDur = Mkeys.getDuration(k);
            if (kDur > longest_key){longest_key = kDur;}
          }
          if (dur > longest_key*1.2){
            onFail("Key duration mismatch; disconnecting "+myConn.getHost()+" to recover ("+JSON::Value(longest_key).asString()+" -> "+JSON::Value((uint64_t)dur).asString()+")", true);
            return;
          }else{
            sendOk("Key duration matches upstream");
          }
          continue;
        }
        WARN_MSG("Unhandled DTCM command: '%s'", dScan.getMember("cmd").asString().c_str());
      }else if (myConn.Received().copy(4) == "DTSC"){
        // Header packet
        if (!isPushing()){
          onFail("DTSC_HEAD ignored: you are not cleared for pushing data!", true);
          return;
        }
        std::string toRec = myConn.Received().copy(8);
        unsigned long rSize = Bit::btohl(toRec.c_str() + 4);
        if (!myConn.Received().available(8 + rSize)){return;}// abort - not enough data yet
        std::string dataPacket = myConn.Received().remove(8 + rSize);
        DTSC::Packet metaPack(dataPacket.data(), dataPacket.size());
        DTSC::Scan metaScan = metaPack.getScan();
        meta.reloadReplacedPagesIfNeeded();
        size_t prevTracks = meta.getValidTracks().size();

        size_t tNum = metaScan.getMember("tracks").getSize();
        for (int i = 0; i < tNum; i++){
          DTSC::Scan trk = metaScan.getMember("tracks").getIndice(i);
          size_t trackID = trk.getMember("trackid").asInt();
          if (meta.trackIDToIndex(trackID, getpid()) == INVALID_TRACK_ID){
            MEDIUM_MSG("Adding track: %s", trk.asJSON().toString().c_str());
            meta.addTrackFrom(trk);
          }else{
            HIGH_MSG("Already had track: %s", trk.asJSON().toString().c_str());
          }
        }
        meta.reloadReplacedPagesIfNeeded();
        // Unix Time at zero point of a stream
        if (metaScan.hasMember("unixzero")){
          meta.setBootMsOffset(metaScan.getMember("unixzero").asInt() - Util::unixMS() + Util::bootMS());
        }else{
          MEDIUM_MSG("No member \'unixzero\' found in DTSC::Scan. Calculating locally.");
          int64_t lastMs = 0;
          std::set<size_t> tracks = M.getValidTracks();
          for (std::set<size_t>::iterator it = tracks.begin(); it != tracks.end(); it++){
            if (M.getLastms(*it) > lastMs){
              lastMs = M.getLastms(*it);
            }
          }
          meta.setBootMsOffset(Util::bootMS() - lastMs);
        }
        std::stringstream rep;
        rep << "DTSC_HEAD parsed, we went from " << prevTracks << " to " << meta.getValidTracks().size() << " tracks. Bring on those data packets!";
        sendOk(rep.str());
      }else if (myConn.Received().copy(4) == "DTP2"){
        if (!isPushing()){
          onFail("DTSC_V2 ignored: you are not cleared for pushing data!", true);
          return;
        }
        // Data packet
        std::string toRec = myConn.Received().copy(8);
        unsigned long rSize = Bit::btohl(toRec.c_str() + 4);
        if (!myConn.Received().available(8 + rSize)){return;}// abort - not enough data yet
        std::string dataPacket = myConn.Received().remove(8 + rSize);
        DTSC::Packet inPack(dataPacket.data(), dataPacket.size(), true);
        size_t tid = M.trackIDToIndex(inPack.getTrackId(), getpid());
        if (tid == INVALID_TRACK_ID){
          //WARN_MSG("Received data for unknown track: %zu", inPack.getTrackId());
          onFail("DTSC_V2 received for a track that was not announced in a header!", true);
          return;
        }
        if (!userSelect.count(tid)){
          userSelect[tid].reload(streamName, tid, COMM_STATUS_SOURCE);
        }
        char *data;
        size_t dataLen;
        inPack.getString("data", data, dataLen);
        uint64_t packTime = inPack.getTime();
        bufferLivePacket(packTime, inPack.getInt("offset"), tid, data, dataLen, inPack.getInt("bpos"), inPack.getFlag("keyframe"));

        // If we're receiving packets in sync, we now know until 1ms ago all tracks are up-to-date
        if (isSyncReceiver && packTime) {
          std::map<size_t, Comms::Users>::iterator uIt;
          for (uIt = userSelect.begin(); uIt != userSelect.end(); ++uIt) {
            if (uIt->first == tid) { continue; }
            if (M.getNowms(uIt->first) < packTime - 1) { meta.setNowms(uIt->first, packTime - 1); }
          }
        }

      }else{
        // Invalid
        onFail("Invalid packet header received. Aborting.", true);
        return;
      }
    }
  }

  void OutDTSC::handlePlay(DTSC::Scan &dScan){
    streamName = dScan.getMember("stream").asString();
    // Put all arguments except for cmd and stream in the targetParams
    dScan.forEachMember([this](const DTSC::Scan & m, const std::string & name) {
      if (name == "cmd" || name == "stream") { return; }
      switch (m.getType()) {
        case DTSC_INT: targetParams[name] = m.asInt(); break;
        case DTSC_STR: targetParams[name] = m.asString(); break;
        default: break;
      }
    });
    HTTP::URL qUrl;
    qUrl.protocol = "dtsc";
    qUrl.host = myConn.getBoundAddress();
    qUrl.port = config->getOption("port").asString();
    qUrl.path = streamName;
    qUrl.args = HTTP::argStr(targetParams, false);
    reqUrl = qUrl.getUrl();
    if (targetParams.count("sync")) {
      setSyncMode(JSON::Value(targetParams["sync"]).asBool());
      if (getSyncMode()) {
        JSON::Value prep;
        prep["cmd"] = "sync";
        prep["sync"] = 1;
        sendCmd(prep);
      }
    }
    parseData = true;
    INFO_MSG("Handled play from %s for %s", myConn.getHost().c_str(), reqUrl.c_str());
    setBlocking(false);
  }

  void OutDTSC::handlePush(DTSC::Scan &dScan){
    streamName = dScan.getMember("stream").asString();
    std::string passString = dScan.getMember("password").asString();
    std::map<std::string, std::string> args;
    dScan.forEachMember([&args](const DTSC::Scan & m, const std::string & name) {
      if (name == "cmd" || name == "stream" || name == "password") { return; }
      switch (m.getType()) {
        case DTSC_INT: args[name] = std::to_string(m.asInt()); break;
        case DTSC_STR: args[name] = m.asString(); break;
        default: break;
      }
    });
    HTTP::URL qUrl;
    qUrl.protocol = "dtsc";
    qUrl.host = myConn.getBoundAddress();
    qUrl.port = config->getOption("port").asString();
    qUrl.path = streamName;
    qUrl.pass = passString;
    qUrl.args = HTTP::argStr(args);
    reqUrl = qUrl.getUrl();
    if (checkStreamKey()) {
      if (!streamName.size()) {
        onFinish();
        return;
      }
    } else {
      if (Triggers::shouldTrigger("PUSH_REWRITE")) {
        std::string payload = reqUrl + "\n" + getConnectedHost() + "\n" + streamName;
        std::string newStream = streamName;
        Triggers::doTrigger("PUSH_REWRITE", payload, "", false, newStream);
        if (!newStream.size()) {
          FAIL_MSG("Push from %s to URL %s rejected - PUSH_REWRITE trigger blanked the URL", getConnectedHost().c_str(),
                   reqUrl.c_str());
          Util::logExitReason(ER_TRIGGER, "Push from %s to URL %s rejected - PUSH_REWRITE trigger blanked the URL",
                              getConnectedHost().c_str(), reqUrl.c_str());
          onFail("Push not allowed - rejected by trigger");
          return;
        } else {
          streamName = newStream;
        }
      }
      if (!allowPush(passString)) {
        onFail("Push not allowed - stream and/or password incorrect", true);
        return;
      }
    }
    if (dScan.getMember("sync").asBool()) { isSyncReceiver = true; }
    sendOk("You're cleared for pushing! DTSC_HEAD please?");
  }

}// namespace Mist
