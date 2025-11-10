#include "output_rtmp.h"
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <mist/auth.h>
#include <mist/bitfields.h>
#include <mist/defines.h>
#include <mist/encode.h>
#include <mist/http_parser.h>
#include <mist/stream.h>
#include <mist/triggers.h>
#include <mist/util.h>
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

const char * trackType(char ID){
  if (ID == 8){return "audio";}
  if (ID == 9){return "video";}
  if (ID == 18){return "metadata";}
  return "unknown";
}

// Handles "soft close" behaviour, where the connection is gracefully closed on the next data stall
bool softClose = false;
void hup_handler(int signum, siginfo_t *sigInfo, void *ignore) {
  softClose = true;
}
void setHupHandler() {
  struct sigaction new_action;
  new_action.sa_sigaction = hup_handler;
  sigemptyset(&new_action.sa_mask);
  new_action.sa_flags = SA_SIGINFO;
  sigaction(SIGHUP, &new_action, NULL);
}

namespace Mist {
  OutRTMP::OutRTMP(Socket::Connection &conn) : Output(conn){
#ifdef SSL
    if (setupTLS()) { myConn.sslAccept(&sslConf, &ctr_drbg); }
#endif
    didPublish = false;
    lastErrCheck = 0;
    amfErr = 0;
    rtmpErr = 0;
    lastSilence = 0;
    hasSilence = false;
    lastAudioInserted = 0;
    hasCustomAudio = false;
    customAudioSize = 0;
    customAudioIterator = 0;
    currentFrameTimestamp = 0;
    lastAck = Util::bootSecs();
    setRtmpOffset = false;
    rtmpOffset = 0;
    lastSend = 0;
    authAttempts = 0;
    didReceiveDeleteStream = false;
    maxbps = config->getInteger("maxkbps") * 128;
    conn.Received().splitter.clear();
    //Switch realtime tracking system to mode where it never skips ahead, but only changes playback speed
    maxSkipAhead = 0;
    if (config->getString("target").size() && config->getString("target") != "-") {
      startPushOut("");
    } else {
      setBlocking(true);
      while (!conn.Received().available(1537) && conn.connected() && config->is_active){
        conn.spool();
      }
      if (!conn || !config->is_active){return;}
      RTMPStream::handshake_in.append(conn.Received().remove(1537));
      RTMPStream::rec_cnt += 1537;

      if (RTMPStream::doHandshake()){
        conn.SendNow(RTMPStream::handshake_out);
        while (!conn.Received().available(1536) && conn.connected() && config->is_active){
          conn.spool();
        }
        conn.Received().remove(1536);
        RTMPStream::rec_cnt += 1536;
        HIGH_MSG("Handshake success");
      }else{
        MEDIUM_MSG("Handshake fail (this is not a problem, usually)");
      }
      setBlocking(false);
    }
  }

  OutRTMP::~OutRTMP() {
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

#ifdef SSL
  bool OutRTMP::setupTLS() {
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
      const int lvl = (level == 1) ? 4 : level + 6;
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
        const JSON::Value crtCnf = JSON::fromString(cFile);
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

  void OutRTMP::startPushOut(const char *args) {
    didPublish = false;
    parseData = false;

    RTMPStream::chunk_rec_max = 128;
    RTMPStream::chunk_snd_max = 128;
    RTMPStream::rec_window_size = 2500000;
    RTMPStream::snd_window_size = 2500000;
    RTMPStream::rec_window_at = 0;
    RTMPStream::snd_window_at = 0;
    RTMPStream::rec_cnt = 0;
    RTMPStream::snd_cnt = 0;

    RTMPStream::lastsend.clear();
    RTMPStream::lastrecv.clear();

    pushUrl = HTTP::URL(config->getString("target"));
    streamOut = streamName;

    // For a push you can specify the `host` query parameter via
    // the target URL. When specified we will use as this host as
    // the host to which we connect using the socket. We will
    // still use the host from the `rtmp://<host>:<port>/` URL
    // that you've defined for the push target. This allows us to
    // connect with a differrent host then we use for the `tcUrl`
    // AMF field (see below).
    std::string remoteHost = pushUrl.host;
    if (targetParams.count("host")) { remoteHost = targetParams["host"]; }

    std::string app = Encodings::URL::encode(pushUrl.path, "/:=@[]");
    if (pushUrl.args.size()) { app += "?" + pushUrl.args; }

    size_t slash = app.rfind('/');
    if (slash != std::string::npos) {
      streamOut = app.substr(slash + 1, std::string::npos);
      app = app.substr(0, slash);
      if (!streamOut.size()) { streamOut = streamName; }
    }

    // For a push, you can add the `?app=<name>` to change the
    // RTMP application name (rtmp://host:port/<app>/stream)
    if (targetParams.count("app")) { app = targetParams["app"]; }

    // You can specify `?stream=<name>` to change the RTMP stream
    // key, (rmtp://host:port/app/<stream>)
    if (targetParams.count("stream")) { streamOut = targetParams["stream"]; }

    if (pushUrl.getPort() == 1935) {
      targetParams["tcUrl"] = pushUrl.protocol + "://" + pushUrl.host + "/" + app;
    } else {
      targetParams["tcUrl"] = pushUrl.protocol + "://" + pushUrl.host + ":" + std::to_string(pushUrl.getPort()) + "/" + app;
    }
    targetParams["stream"] = streamOut;
    targetParams["app"] = app;

    INFO_MSG("About to push stream %s out. Host: %s, port: %d, app: %s, stream: %s", streamName.c_str(),
             remoteHost.c_str(), pushUrl.getPort(), app.c_str(), streamOut.c_str());

    myConn.setHost(remoteHost);
    initialize();
    initialSeek();

    myConn.close();
    myConn.Received().clear();

    if (pushUrl.protocol == "rtmp") {
      myConn.open(remoteHost, pushUrl.getPort(), false);
#ifdef SSL
    } else if (pushUrl.protocol == "rtmps") {
      myConn.open(remoteHost, pushUrl.getPort(), false, true, pushUrl.host);
#endif
    } else {
      Util::logExitReason(ER_FORMAT_SPECIFIC, "protocol not supported: %s", pushUrl.protocol.c_str());
      return;
    }

    if (!myConn){
      Util::logExitReason(ER_FORMAT_SPECIFIC, "could not connect to %s:%d: %s!", remoteHost.c_str(), pushUrl.getPort(),
                          myConn.getError().c_str());
      return;
    }

    // do handshake
    myConn.SendNow("\003", 1); // protocol version. Always 3
    char *temp = (char *)malloc(3072);
    if (!temp){
      myConn.close();
      Util::logExitReason(ER_MEMORY, "could not allocate buffer for RTMP handshake");
      return;
    }

    *((uint32_t *)temp) = 0;                         // time zero
    *(((uint32_t *)(temp + 4))) = 0; // version 0
    for (int i = 8; i < 3072; ++i){
      temp[i] = FILLER_DATA[i % sizeof(FILLER_DATA)];
    }//"random" data

    // Calculate the SHA265 digest over the data, insert it in the "secret" location
    size_t digest_pos = (temp[9] + temp[10] + temp[11] + temp[12]) % 728 + 12;
    // Copy data except for the 32 bytes where the digest is stored
    Util::ResizeablePointer digest_data;
    digest_data.append(temp+1, digest_pos);
    digest_data.append(temp+1+digest_pos+32, 1504-digest_pos);
    Secure::hmac_shabin(digest_data, digest_data.size(), "Genuine Adobe Flash Player 001", 30, temp + 1 + digest_pos, Secure::SHA256);

    myConn.SendNow(temp, 1536);
    while (!myConn.Received().available(1537) && myConn.connected() && config->is_active){
      myConn.spool();
    }
    if (!myConn || !config->is_active){
      WARN_MSG("Lost connection while waiting for S0/S1 packets!");
      return;
    }
    // Send back copy of S1 (S0 is the first byte, skip it)
    Util::ResizeablePointer s0s1;
    myConn.Received().remove(s0s1, 1537);
    myConn.SendNow(s0s1 + 1, 1536);
    free(temp);
    setBlocking(true);
    while (!myConn.Received().available(1536) && myConn.connected() && config->is_active){
      myConn.spool();
    }
    if (!myConn || !config->is_active) {
      Util::logExitReason(ER_FORMAT_SPECIFIC, "connection closed during RTMP handshake");
      return;
    }
    myConn.Received().remove(1536);
    RTMPStream::rec_cnt += 3073;
    RTMPStream::snd_cnt += 3073;
    setBlocking(false);
    VERYHIGH_MSG("Push out handshake completed");
    setHupHandler(); // Install SIGHUP handler

    AMF::Object amfReply;
    amfReply.addContent("connect"); // command
    amfReply.addContent(1.0); // transaction ID
    AMF::Object *opts = amfReply.addContent(AMF::AMF0_OBJECT); // options
    opts->addContent("app", app + args);
    opts->addContent("type", "nonprivate");
    opts->addContent("flashVer", "FMLE/3.0 (compatible; " APPNAME ")");
    opts->addContent("tcUrl", targetParams["tcUrl"] + args);
    opts->addContent("capsEx", 15.0);
    {
      AMF::Object *ccList = opts->addContent("fourCcList", AMF::AMF0_STRICT_ARRAY);
      ccList->addContent("av01");
      ccList->addContent("avc1");
      ccList->addContent("hvc1");
      ccList->addContent("vp08");
      ccList->addContent("vp09");
      ccList->addContent("ac-3");
      ccList->addContent("ec-3");
      ccList->addContent("Opus");
      ccList->addContent(".mp3");
      ccList->addContent("fLaC");
      ccList->addContent("mp4a");
    }
    {
      AMF::Object *ccList = opts->addContent("audioFourCcInfoMap", AMF::AMF0_OBJECT);
      ccList->addContent("ac-3", 7.0);
      ccList->addContent("ec-3", 7.0);
      ccList->addContent("Opus", 7.0);
      ccList->addContent(".mp3", 7.0);
      ccList->addContent("fLaC", 7.0);
      ccList->addContent("mp4a", 7.0);
    }
    {
      AMF::Object *ccList = opts->addContent("videoFourCcInfoMap", AMF::AMF0_OBJECT);
      ccList->addContent("av01", 7.0);
      ccList->addContent("avc1", 7.0);
      ccList->addContent("hvc1", 7.0);
      ccList->addContent("vp08", 7.0);
      ccList->addContent("vp09", 7.0);
    }
    sendCommand(amfReply, 20, 0);

    RTMPStream::chunk_snd_max = 65536;                                 // 64KiB
    myConn.SendNow(RTMPStream::SendCTL(1, RTMPStream::chunk_snd_max)); // send chunk size max (msg 1)

    pushState = "connect";
    HIGH_MSG("Waiting for server to acknowledge connect request...");
  }

  bool OutRTMP::listenMode(){return !(config->getString("target").size());}

  bool OutRTMP::onFinish(){

    MEDIUM_MSG("Finishing stream %s, %s", streamName.c_str(), myConn ? "while connected" : "already disconnected");

    // When the query parameter `graceless` has been set for an
    // outgoing push target we directly close the socket. This
    // means we'll never send the `deleteStream` command.
    if (targetParams.count("graceless")) { myConn.close(); }

    if (!myConn) { return false; }

    if (isRecording()) {
      AMF::Object amfReply;
      amfReply.addContent("deleteStream"); // status reply
      amfReply.addContent(6.0); // transaction ID
      amfReply.addContent(AMF::AMF0_NULL); // null - command info
      amfReply.addContent(1.0); // No clue. But OBS sends this, too.
      sendCommand(amfReply, 20, 1);
      myConn.close();
      return false;
    }

    myConn.SendNow(RTMPStream::SendUSR(1, 1)); // send UCM StreamEOF (1), stream 1
    {
      AMF::Object amfReply;
      amfReply.addContent("onStatus"); // status reply
      amfReply.addContent(0.0); // transaction ID
      amfReply.addContent(AMF::AMF0_NULL); // null - command info
      AMF::Object *info = amfReply.addContent(AMF::AMF0_OBJECT); // info
      info->addContent("level", "status");
      info->addContent("code", "NetStream.Play.Stop");
      info->addContent("description", "Stream stopped");
      info->addContent("details", "DDV");
      info->addContent("clientid", 1337.0);
      sendCommand(amfReply, 20, 1);
    }
    {
      AMF::Object amfReply;
      amfReply.addContent("onStatus"); // status reply
      amfReply.addContent(0.0); // transaction ID
      amfReply.addContent(AMF::AMF0_NULL); // null - command info
      AMF::Object *info = amfReply.addContent(AMF::AMF0_OBJECT); // info
      info->addContent("level", "status");
      info->addContent("code", "NetStream.Play.UnpublishNotify");
      info->addContent("description", "Stream stopped");
      info->addContent("clientid", 1337.0);
      sendCommand(amfReply, 20, 1);
    }

    myConn.close();

    return false;
  }

  void OutRTMP::init(Util::Config *cfg){
    Output::init(cfg);
    capa["name"] = "RTMP";
    capa["friendly"] = "RTMP";
    capa["desc"] = "Real time streaming over Adobe RTMP";
    capa["sort"] = "sort";
    capa["deps"] = "";
    capa["url_rel"] = "/play/$";
    capa["incoming_push_url"] = "rtmp://$host:$port/$password/$stream";
    capa["codecs"][0u][0u].append("H264");
    capa["codecs"][0u][0u].append("H263");
    capa["codecs"][0u][0u].append("VP6");
    capa["codecs"][0u][0u].append("VP6Alpha");
    capa["codecs"][0u][0u].append("ScreenVideo2");
    capa["codecs"][0u][0u].append("ScreenVideo1");
    capa["codecs"][0u][0u].append("JPEG");
    capa["codecs"][0u][1u].append("AAC");
    capa["codecs"][0u][1u].append("MP3");
    capa["codecs"][0u][1u].append("Speex");
    capa["codecs"][0u][1u].append("Nellymoser");
    capa["codecs"][0u][1u].append("PCM");
    capa["codecs"][0u][1u].append("ADPCM");
    capa["codecs"][0u][1u].append("ALAW");
    capa["codecs"][0u][1u].append("ULAW");
    capa["codecs"][0u][2u].append("JSON");

    /// Enhanced RTMP codec list
    capa["codecs"][1u] = capa["codecs"][0u];
    capa["codecs"][1u][0u].append("AV1");
    capa["codecs"][1u][0u].append("HEVC");
    capa["codecs"][1u][0u].append("VP8");
    capa["codecs"][1u][0u].append("VP9");
    capa["codecs"][1u][1u].append("opus");
    capa["codecs"][1u][1u].append("AC3");
    capa["codecs"][1u][1u].append("EAC-3");
    capa["codecs"][1u][1u].append("FLAC");

    capa["methods"][0u]["handler"] = "rtmp";
    capa["methods"][0u]["type"] = "flash/10";
    capa["methods"][0u]["hrn"] = "RTMP";
    capa["methods"][0u]["priority"] = 7;
    capa["methods"][0u]["player_url"] = "/flashplayer.swf";
    capa["optional"]["acceptable"]["name"] = "Acceptable connection types";
    capa["optional"]["acceptable"]["help"] =
        "Whether to allow only incoming pushes (2), only outgoing pulls (1), or both (0, default)";
    capa["optional"]["acceptable"]["option"] = "--acceptable";
    capa["optional"]["acceptable"]["short"] = "T";
    capa["optional"]["acceptable"]["default"] = 0;
    capa["optional"]["acceptable"]["type"] = "select";
    capa["optional"]["acceptable"]["select"][0u][0u] = 0;
    capa["optional"]["acceptable"]["select"][0u][1u] =
        "Allow both incoming and outgoing connections";
    capa["optional"]["acceptable"]["select"][1u][0u] = 1;
    capa["optional"]["acceptable"]["select"][1u][1u] = "Allow only outgoing connections";
    capa["optional"]["acceptable"]["select"][2u][0u] = 2;
    capa["optional"]["acceptable"]["select"][2u][1u] = "Allow only incoming connections";
    capa["optional"]["acceptable"]["sort"] = "aaa";
    capa["optional"]["maxkbps"]["name"] = "Max. kbps";
    capa["optional"]["maxkbps"]["help"] =
        "Maximum bitrate to allow in the ingest direction, in kilobits per second.";
    capa["optional"]["maxkbps"]["option"] = "--maxkbps";
    capa["optional"]["maxkbps"]["short"] = "K";
    capa["optional"]["maxkbps"]["default"] = 0;
    capa["optional"]["maxkbps"]["type"] = "uint";

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

    cfg->addConnectorOptions(1935, capa);
    config = cfg;
    config->addStandardPushCapabilities(capa);
    capa["push_urls"].append("rtmp://*");
    capa["push_urls"].append("rtmps://*");

    JSON::Value & pp = capa["push_parameters"];

    // By setting the `graceless` option we will never perfrom a
    // gracefull disconnect. This means that when `onFinish()` is
    // called we directly .close() the socket connection w/o
    // sending a `deleteStream` AMF message. For some media
    // servers, not sending the `deleteStream` message, means
    // that we can re-open the socket and continue sending media
    // to the same `tcUrl`/stream.
    pp["graceless"]["name"] = "Graceless disconnect";
    pp["graceless"]["help"] = "When set, we will directly close the RTMP connection when shutting down the stream. "
                              "This means we don't send a `deleteStream` AMF command to the receiving media server.";
    pp["graceless"]["type"] = "bool";
    pp["graceless"]["format"] = "set_or_unset"; // when not checked do not set at all so `graceless=false` does not do what you expect
    pp["graceless"]["sort"] = "1a"; // alphanumerical sorting: determines the position in the web interface form.

    // Specify the <host> part for the `tcUrl` that we use when
    // connecting with the remote media server, see
    // https://en.wikipedia.org/wiki/Real-Time_Messaging_Protocol
    pp["host"]["name"] = "Host";
    pp["host"]["help"] =
      "Specify the host to which we should connect. When set, we will open a connection to this host instead of the "
      "host specified in the RTMP target url as defined above. We will stil use the host value of the RTMP target URL "
      "when for the `tcUrl` that is send to the remote server. This option is relevant when you want to manually "
      "specify which server to connect to when dealing with CDNs.";
    pp["host"]["type"] = "string";
    pp["host"]["sort"] = "1b";

    // Specify the <app> separately for the url rtmp://host:port/<app>/stream
    pp["app"]["name"] = "Stream app";
    pp["app"]["help"] =
      "The value that we should use for the app, this is used to as the rtmp://host:port/&lt;app&gt;/stream. ";
    pp["app"]["type"] = "string";
    pp["app"]["sort"] = "1c";

    // Specify the <stream> separately for the url rtmp://host:port/app/<stream>
    pp["stream"]["name"] = "Stream key";
    pp["stream"]["help"] = "The value that we should use for the stream key, this is used to as the "
                           "<code>rtmp://host:port/app/&lt;stream&gt;. ";
    pp["stream"]["type"] = "string";
    pp["stream"]["sort"] = "1d";

    JSON::Value opt;
    opt["arg"] = "string";
    opt["default"] = "";
    opt["arg_num"] = 1;
    opt["help"] = "Target RTMP URL to push out towards.";
    cfg->addOption("target", opt);
    cfg->addOption("streamname", JSON::fromString("{\"arg\":\"string\",\"short\":\"s\",\"long\":"
                                                  "\"stream\",\"help\":\"The name of the stream to "
                                                  "push out, when pushing out.\"}"));
  }

  void OutRTMP::sendSilence(uint64_t timestamp){
    /* 
    Byte 1:
      SoundFormat     = 4 bits    : AAC   = 10  = 1010
      SoundRate       = 2 bits    : 44Khz = 3   = 11
      SoundSize       = 1 bit     : always 1
      SoundType       = 1 bit     : always 1 (Stereo) for AAC
    
    Byte 2->:
      SoundData
      Since it is an AAC stream SoundData is an AACAUDIODATA object:
        AACPacketType = 8 bits     : always 1 (0 indicates sequence header which is sent in sendHeader)
        Data[N times] = 8 bits     : Raw AAC frame data
        
    tmpData: 10101111 00000001 = af 01 = \257 \001 + raw AAC silence
    */
    const char * tmpData = "\257\001!\020\004`\214\034";
    
    size_t data_len = 8;

    char rtmpheader[] ={0,                // byte 0 = cs_id | ch_type
                         0,    0, 0,      // bytes 1-3 = timestamp
                         0,    0, 0,      // bytes 4-6 = length
                         0x08,            // byte 7 = msg_type_id
                         1,    0, 0, 0,   // bytes 8-11 = msg_stream_id = 1
                         0,    0, 0, 0};  // bytes 12-15 = extended timestamp
                         
    bool allow_short = RTMPStream::lastsend.count(4);
    RTMPStream::Chunk &prev = RTMPStream::lastsend[4];
    uint8_t chtype = 0x00;
    size_t header_len = 12;
    bool time_is_diff = false;
    if (allow_short && (prev.cs_id == 4)){
      if (prev.msg_stream_id == 1){
        chtype = 0x40;
        header_len = 8; // do not send msg_stream_id
        if (data_len == prev.len && rtmpheader[7] == prev.msg_type_id){
          chtype = 0x80;
          header_len = 4; // do not send len and msg_type_id
          if (timestamp == prev.timestamp){
            chtype = 0xC0;
            header_len = 1; // do not send timestamp
          }
        }
        // override - we always sent type 0x00 if the timestamp has decreased since last chunk in this channel
        if (timestamp < prev.timestamp){
          chtype = 0x00;
          header_len = 12;
        }else{
          // store the timestamp diff instead of the whole timestamp
          timestamp -= prev.timestamp;
          time_is_diff = true;
        }
      }
    }

    // update previous chunk variables
    prev.cs_id = 4;
    prev.msg_stream_id = 1;
    prev.len = data_len;
    prev.msg_type_id = 0x08;
    if (time_is_diff){
      prev.timestamp += timestamp;
    }else{
      prev.timestamp = timestamp;
    }

    // cs_id and ch_type
    rtmpheader[0] = chtype | 4;
    // data length, 3 bytes
    rtmpheader[4] = (data_len >> 16) & 0xff;
    rtmpheader[5] = (data_len >> 8) & 0xff;
    rtmpheader[6] = data_len & 0xff;
    // timestamp, 3 bytes
    if (timestamp >= 0x00ffffff){
      // send extended timestamp
      rtmpheader[1] = 0xff;
      rtmpheader[2] = 0xff;
      rtmpheader[3] = 0xff;
      rtmpheader[header_len++] = (timestamp >> 24) & 0xff;
      rtmpheader[header_len++] = (timestamp >> 16) & 0xff;
      rtmpheader[header_len++] = (timestamp >> 8) & 0xff;
      rtmpheader[header_len++] = timestamp & 0xff;
    }else{
      // regular timestamp
      rtmpheader[1] = (timestamp >> 16) & 0xff;
      rtmpheader[2] = (timestamp >> 8) & 0xff;
      rtmpheader[3] = timestamp & 0xff;
    }

    // send the packet
    myConn.setBlocking(true);
    myConn.SendNow(rtmpheader, header_len);
    myConn.SendNow(tmpData, data_len);
    RTMPStream::snd_cnt += header_len+data_len; // update the sent data counter
    myConn.setBlocking(false);
  }
  
  // Gets next ADTS frame and loops back to 0 is EOF is reached
  void OutRTMP::calcNextFrameInfo(){ 
    // Set iterator to start of next frame
    customAudioIterator += currentFrameInfo.getCompleteSize();
    // Loop the audio
    if (customAudioIterator >= customAudioSize)
      customAudioIterator = 0;

    // Confirm syncword (= FFF)
    if (customAudioFile[customAudioIterator] != 0xFF || ( customAudioFile[customAudioIterator + 1] & 0xF0) != 0xF0 ){
      WARN_MSG("Invalid sync word at start of header. Will probably read garbage...");
    }

    uint64_t frameSize = (((customAudioFile[customAudioIterator + 3] & 0x03) << 11) 
                        | ( customAudioFile[customAudioIterator + 4] << 3)
                        |(( customAudioFile[customAudioIterator + 5] >> 5) & 0x07));
    aac::adts adtsPack(customAudioFile + customAudioIterator, frameSize);
    if (!adtsPack){
      WARN_MSG("Could not parse ADTS package. Will probably read garbage..."); 
    }
    // Update internal variables
    currentFrameInfo = adtsPack;
    currentFrameTimestamp += (adtsPack.getSampleCount() * 1000) / adtsPack.getFrequency();
  }
  
  // Sends FLV audio tag + raw AAC data
  void OutRTMP::sendLoopedAudio(uint64_t untilTimestamp){
    // ADTS frame can be invalid if there is metadata or w/e in the input file
    if ( !currentFrameInfo ){
      if (customAudioIterator == 0){
        ERROR_MSG("Input .AAC file is invalid!");
        return;
      }
      // Re-init currentFrameInfo
      WARN_MSG("File contains invalid ADTS frame. Resetting filePos to 0 and throwing this data...");
      customAudioSize = customAudioIterator;
      customAudioIterator = 0;
      // NOTE that we do not reset the timestamp to prevent eternal loops
      
      // Confirm syncword (= FFF)
      if (customAudioFile[customAudioIterator] != 0xFF || ( customAudioFile[customAudioIterator + 1] & 0xF0) != 0xF0 ){
        WARN_MSG("Invalid sync word at start of header. Invalid input file!");
        return;
      }

      uint64_t frameSize = (((customAudioFile[customAudioIterator + 3] & 0x03) << 11) | ( customAudioFile[customAudioIterator + 4] << 3) | (( customAudioFile[customAudioIterator + 5] >> 5) & 0x07));
      aac::adts adtsPack(customAudioFile + customAudioIterator, frameSize);
      if (!adtsPack){
        WARN_MSG("Could not parse ADTS package. Invalid input file!");
        return;
      }
      currentFrameInfo = adtsPack;
    }
    
    // Keep parsing ADTS frames until we reach a frame which starts in the future
    while (currentFrameTimestamp < untilTimestamp){  
      // Init RTMP header info
      char rtmpheader[] ={0,              // byte 0 = cs_id | ch_type
                          0,    0, 0,     // bytes 1-3 = timestamp
                          0,    0, 0,     // bytes 4-6 = length
                          0x08,           // byte 7 = msg_type_id
                          1,    0, 0, 0,  // bytes 8-11 = msg_stream_id = 1
                          0,    0, 0, 0}; // bytes 12-15 = extended timestamp
                          
      // Separate timestamp since we store Δtimestamps
      uint64_t rtmpTimestamp = currentFrameTimestamp;
      // Since we have to prepend an FLV audio tag, increase size by 2 bytes
      uint64_t aacPacketSize = currentFrameInfo.getPayloadSize() + 2;
      // If there is a previous sent package, we do not need to send all data
      bool allow_short = RTMPStream::lastsend.count(4);
      RTMPStream::Chunk &prev = RTMPStream::lastsend[4];
      // Defines the type of header. Only the 2 most significant bits are counted:
      //  0x00 = 000.. = 12 byte header
      //  0x40 = 010.. = 8 byte header, leave out message ID if it's the same as prev
      //  0x80 = 100.. = 4 byte header, above + leave out msg type and size if the packets are all the same size and type
      //  0xC0 = 110.. = 1 byte header, above + leave out timestamp as well
      uint8_t chtype = 0x00;
      size_t header_len = 12;
      bool time_is_diff = false;
      if (allow_short && (prev.cs_id == 4)){
        if (prev.msg_stream_id == 1){
          chtype = 0x40;
          header_len = 8;
          if (aacPacketSize == prev.len && rtmpheader[7] == prev.msg_type_id){
            chtype = 0x80;
            header_len = 4;
            if (rtmpTimestamp == prev.timestamp){
              chtype = 0xC0;
              header_len = 1;
            }
          }
          // override - we always sent type 0x00 if the timestamp has decreased since last chunk in this channel
          if (rtmpTimestamp < prev.timestamp){
            chtype = 0x00;
            header_len = 12;
          }else{
            // store the timestamp diff instead of the whole timestamp
            rtmpTimestamp -= prev.timestamp;
            time_is_diff = true;
          }
        }
      }

      // Update previous chunk variables
      prev.cs_id = 4;
      prev.msg_stream_id = 1;
      prev.len = aacPacketSize;
      prev.msg_type_id = 0x08;
      if (time_is_diff){
        prev.timestamp += rtmpTimestamp;
      }else{
        prev.timestamp = rtmpTimestamp;
      }

      // Now fill in type...
      rtmpheader[0] = chtype | 4;
      // data length...
      rtmpheader[4] = (aacPacketSize >> 16) & 0xff;
      rtmpheader[5] = (aacPacketSize >> 8) & 0xff;
      rtmpheader[6] = aacPacketSize & 0xff;
      // and timestamp (3 bytes unless extended)
      if (rtmpTimestamp >= 0x00ffffff){
        rtmpheader[1] = 0xff;
        rtmpheader[2] = 0xff;
        rtmpheader[3] = 0xff;
        rtmpheader[header_len++] = (rtmpTimestamp >> 24) & 0xff;
        rtmpheader[header_len++] = (rtmpTimestamp >> 16) & 0xff;
        rtmpheader[header_len++] = (rtmpTimestamp >> 8) & 0xff;
        rtmpheader[header_len++] = rtmpTimestamp & 0xff;
      }else{
        rtmpheader[1] = (rtmpTimestamp >> 16) & 0xff;
        rtmpheader[2] = (rtmpTimestamp >> 8) & 0xff;
        rtmpheader[3] = rtmpTimestamp & 0xff;
      }

      // Send RTMP packet containing header only
      myConn.setBlocking(true);
      myConn.SendNow(rtmpheader, header_len);
      // Prepend FLV AAC audio tag
      char *tmpData = (char*)malloc(aacPacketSize);
      const char *tmpBuf = currentFrameInfo.getPayload();
      // Prepend FLV Audio tag: always 10101111 00000001 + raw AAC
      tmpData[0] = '\257';
      tmpData[1] = '\001';
      for (int i = 2; i < aacPacketSize; i++)
        tmpData[i] = tmpBuf[i-2];

      myConn.SendNow(tmpData, aacPacketSize);
      // Update internal variables
      RTMPStream::snd_cnt += header_len+aacPacketSize;
      myConn.setBlocking(false);
      
      // get next ADTS frame for new raw AAC data
      calcNextFrameInfo();
    }
  }
  
  
  void OutRTMP::sendNext(){
    if (pushState == "ReconnectRequest" && thisIdx == getMainSelectedTrack()) {
      if (M.getType(thisIdx) != "video" || thisPacket.getFlag("keyframe")) {
        INFO_MSG("At next keyframe (timestamp %" PRIu64 ") - reconnecting as requested!", thisTime);
        targetParams["start"] = std::to_string(thisTime);
        startPushOut("");
      }
    }

    //Every 5s, check if the track selection should change in live streams, and do it.
    if (M.getLive()){
      static uint64_t lastMeta = 0;
      if (Util::epoch() > lastMeta + 5){
        lastMeta = Util::epoch();
        if (selectDefaultTracks()){
          INFO_MSG("Track selection changed - resending headers and continuing");
          sentHeader = false;
          return;
        }
      }
      if (liveSeek()){return;}
    }

    uint64_t timestamp = thisTime - rtmpOffset;
    // make sure we don't go negative
    if (rtmpOffset > (int64_t)thisTime) {
      timestamp = 0;
      rtmpOffset = (int64_t)thisTime;
    }

    // Send silence packets if needed
    if (hasSilence){
      // If there's more than 15s of skip, skip audio as well
      if (timestamp > 15000 && lastAudioInserted < timestamp - 15000){
        lastAudioInserted = timestamp - 30;
      }
      // convert time to packet counter
      uint64_t currSilence = ((lastAudioInserted*44100+512000)/1024000)+1;
      uint64_t silentTime = currSilence*1024000/44100;
      // keep sending silent packets until we've caught up to the current timestamp
      while (silentTime < timestamp){
        sendSilence(silentTime);
        lastAudioInserted = silentTime;
        silentTime = (++currSilence)*1024000/44100;
      }
    }

    // NOTE hier ergens fixen dat de audio ook stopt als video wegvalt
    
    // Send looped audio if needed
    if (hasCustomAudio){
      // If there's more than 15s of skip, skip audio as well
      if (timestamp > 15000 && lastAudioInserted < timestamp - 15000){
        lastAudioInserted = timestamp - 30;
      }
      // keep sending silent packets until we've caught up to the current timestamp
      sendLoopedAudio(timestamp);
      lastAudioInserted = timestamp;
    }

    if (!M.trackLoaded(thisIdx)) { return; }
    std::string type = M.getType(thisIdx);
    std::string codec = M.getCodec(thisIdx);

    // byte 0 = cs_id | ch_type
    // bytes 1-3 = timestamp
    // bytes 4-6 = length
    // byte 7 = msg_type_id
    // bytes 8-11 = msg_stream_id = 1
    // bytes 12-15 = extended timestamp
    char rtmpheader[] = {0, 0, 0, 0, 0, 0, 0, 0x12, 1, 0, 0, 0, 0, 0, 0, 0};
    char dataheader[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    size_t dheader_len = 0;
    static Util::ResizeablePointer swappy;
    char *tmpData = 0;   // pointer to raw media data
    size_t data_len = 0; // length of processed media data
    thisPacket.getString("data", tmpData, data_len);

    // set msg_type_id
    if (type == "video"){
      dheader_len = 1; // 4 bits frame type, 4 bits codec ID
      rtmpheader[7] = 0x09;
      int64_t offset = thisPacket.getInt("offset");

      auto mapped = vidMultiMap.find(thisIdx);
      if (mapped != vidMultiMap.end()) {
        dheader_len = offset ? 10 : 7; // add fourcc, and optionally 3 bytes 24-bit offset
        dataheader[0] = 0x86; // 0x80 = VideoHeaderEx present, multitrack
        // 0x00 = OneTrack, plus either CodedFrames or CodedFramesx:
        dataheader[1] = offset ? 1 : 3; // 1 = CodedFrames, 3 = CodedFramesx (implied zero offset)
        if (codec == "AV1") { memcpy(dataheader + 2, "av01", 4); }
        if (codec == "VP8") { memcpy(dataheader + 2, "vp08", 4); }
        if (codec == "VP9") { memcpy(dataheader + 2, "vp09", 4); }
        if (codec == "HEVC") { memcpy(dataheader + 2, "hvc1", 4); }
        if (codec == "H264") { memcpy(dataheader + 2, "avc1", 4); }
        dataheader[6] = mapped->second; // trackId
        if (offset){
          // 3 bytes int24 offset
          dataheader[7] = (offset >> 16) & 0xFF;
          dataheader[8] = (offset >> 8) & 0xFF;
          dataheader[9] = offset & 0xFF;
        }
      } else {
        if (codec == "H264") {
          dheader_len = 5;
          dataheader[0] = 7; // codec ID 7 = H264
          dataheader[1] = 1; // AVCPacketType 1 = NALU
          if (offset) {
            // 3 bytes int24 offset
            dataheader[2] = (offset >> 16) & 0xFF;
            dataheader[3] = (offset >> 8) & 0xFF;
            dataheader[4] = offset & 0xFF;
          }
        }
        if (codec == "AV1" || codec == "VP8" || codec == "VP9" || codec == "HEVC") {
          dheader_len = offset ? 8 : 5; // add fourcc, and optionally 3 bytes 24-bit offset
          dataheader[0] = 0x80; // 0x80 = VideoHeaderEx present,
          dataheader[0] |= offset ? 1 : 3; // 1 = CodedFrames, 3 = CodedFramesx (implied zero offset)
          if (codec == "AV1") { memcpy(dataheader + 1, "av01", 4); }
          if (codec == "VP8") { memcpy(dataheader + 1, "vp08", 4); }
          if (codec == "VP9") { memcpy(dataheader + 1, "vp09", 4); }
          if (codec == "HEVC") { memcpy(dataheader + 1, "hvc1", 4); }
          if (offset) {
            // 3 bytes int24 offset
            dataheader[5] = (offset >> 16) & 0xFF;
            dataheader[6] = (offset >> 8) & 0xFF;
            dataheader[7] = offset & 0xFF;
          }
        }
        if (codec == "H263") {
          dataheader[0] = 2; // codec ID 2 = H263
        }
      }
      // Set upper nibble (frame type): 1 = key frame, 2 = inter frame
      dataheader[0] |= (thisPacket.getFlag("keyframe") ? 0x10 : 0x20);
      // 3 = disposable frame; OR is still okay since the above line only sets 1 or 2, and 3 is both of those bits set
      if (thisPacket.getFlag("disposableframe")) { dataheader[0] |= 0x30; }
    }

    if (type == "audio") {
      rtmpheader[7] = 0x08;
      auto mapped = audMultiMap.find(thisIdx);
      if (mapped != audMultiMap.end()) {
        dheader_len = 7; // add fourcc
        dataheader[0] = 0x95; // 0x90 = AudioHeaderEx present, 5 = Multitrack
        dataheader[1] = 5; // OneTrack, CodedFrames
        if (codec == "opus") { memcpy(dataheader + 2, "Opus", 4); }
        if (codec == "AC3") { memcpy(dataheader + 2, "ac-3", 4); }
        if (codec == "FLAC") { memcpy(dataheader + 2, "fLaC", 4); }
        if (codec == "AAC") { memcpy(dataheader + 2, "mp4a", 4); }
        dataheader[6] = mapped->second; // trackId
      } else {
        dheader_len = 1; // 4 bits sound format, 2 bits rate, 1 bit size, 1 bit channels
        uint32_t rate = M.getRate(thisIdx);
        bool useLowerNibble = true;
        if (codec == "AAC") {
          dheader_len = 2; // AAC has an extra byte of header signalling media data vs init data
          dataheader[0] = 0xA0; // Format 10 = AAC
          dataheader[1] = 1; // raw AAC data, not sequence header
        }
        if (codec == "opus" || codec == "AC3" || codec == "FLAC") {
          useLowerNibble = false; // Disable filling of the lower nibble - it carries the frame type instead
          dheader_len = 5; // add fourcc
          dataheader[0] = 0x91; // 0x90 = AudioHeaderEx present, 1 = Coded frames
          if (codec == "opus") { memcpy(dataheader + 1, "Opus", 4); }
          if (codec == "AC3") { memcpy(dataheader + 1, "ac-3", 4); }
          if (codec == "FLAC") { memcpy(dataheader + 1, "fLaC", 4); }
        }
        if (codec == "MP3") {
          if (rate == 8000) {
            dataheader[0] = 0xE0; // Format 14 = 8kHz MP3
          } else {
            dataheader[0] = 0x20; // Format 2 = MP3
          }
        }
        if (codec == "ADPCM") { dataheader[0] = 0x10; } // Format 1 = ADPCM
        if (codec == "PCM") {
          // We store PCM big-endian, but FLV wants it little-endian or platform-endian.
          // Since platform-endian is the path of insanity, we always send big-endian.
          if (M.getSize(thisIdx) == 16 && swappy.allocate(data_len)) {
            for (uint32_t i = 0; i < data_len; i += 2) {
              swappy[i] = tmpData[i + 1];
              swappy[i + 1] = tmpData[i];
            }
            tmpData = swappy;
          }
          dataheader[0] = 0x30; // Format 3 = Little-endian PCM
        }
        if (codec == "Nellymoser") {
          // Format 4 = Nellymoser 16kHz
          // Format 5 = Nellymoser 8kHz
          // Format 6 = Nellymoser
          dataheader[0] |= (rate == 8000 ? 0x50 : (rate == 16000 ? 0x40 : 0x60));
        }
        if (codec == "ALAW") { dataheader[0] |= 0x70; } // Format 7 = ALAW
        if (codec == "ULAW") { dataheader[0] |= 0x80; } // Format 8 = ULAW
        if (codec == "Speex") { dataheader[0] |= 0xB0; } // Format 11 = Speex

        if (useLowerNibble) {
          // 2 bits rate: 0 = 5.5Khz, 1 = 11Khz, 2 = 22Khz, 3 = 44Khz (according to spec, anyway)
          // We actually use:
          // - 0 for under 11025 Hz
          // - 1 for under 22050 Hz
          // - 2 for under 44100 Hz
          // - 3 for 44100 Hz and higher
          if (rate >= 44100) {
            dataheader[0] |= 0x0C;
          } else if (rate >= 22050) {
            dataheader[0] |= 0x08;
          } else if (rate >= 11025) {
            dataheader[0] |= 0x04;
          }
          // 1 bit size: 0 = 8-bit, 1 = 16-bit (we transmit anything that isn't 8 as 16)
          if (M.getSize(thisIdx) != 8) { dataheader[0] |= 0x02; }
          // 1 bit channel: 0 = mono, 1 = stereo (we transmit anything > 1 as stereo)
          if (M.getChannels(thisIdx) > 1) { dataheader[0] |= 0x01; }
        }
      }
    }

    if (type == "meta") {
      rtmpheader[7] = 0x12;

      static std::string amfData;
      AMF::Object amf = AMF::fromJSON(tmpData, data_len);
      amfData = amf.Pack();

      tmpData = (char *)amfData.c_str();
      data_len = amfData.size();
    }

    data_len += dheader_len;

    bool allow_short = RTMPStream::lastsend.count(4);
    RTMPStream::Chunk &prev = RTMPStream::lastsend[4];
    uint8_t chtype = 0x00;
    size_t header_len = 12;
    bool time_is_diff = false;
    if (allow_short && (prev.cs_id == 4)){
      if (prev.msg_stream_id == 1){
        chtype = 0x40;
        header_len = 8; // do not send msg_stream_id
        if (data_len == prev.len && rtmpheader[7] == prev.msg_type_id){
          chtype = 0x80;
          header_len = 4; // do not send len and msg_type_id
          if (timestamp == prev.timestamp){
            chtype = 0xC0;
            header_len = 1; // do not send timestamp
          }
        }
        // override - we always sent type 0x00 if the timestamp has decreased since last chunk in this channel
        if (timestamp < prev.timestamp){
          chtype = 0x00;
          header_len = 12;
        }else{
          // store the timestamp diff instead of the whole timestamp
          timestamp -= prev.timestamp;
          time_is_diff = true;
        }
      }
    }

    // update previous chunk variables
    prev.cs_id = 4;
    prev.msg_stream_id = 1;
    prev.len = data_len;
    prev.msg_type_id = rtmpheader[7];
    if (time_is_diff){
      prev.timestamp += timestamp;
    }else{
      prev.timestamp = timestamp;
    }

    // cs_id and ch_type
    rtmpheader[0] = chtype | 4;
    // data length, 3 bytes
    rtmpheader[4] = (data_len >> 16) & 0xff;
    rtmpheader[5] = (data_len >> 8) & 0xff;
    rtmpheader[6] = data_len & 0xff;
    // timestamp, 3 bytes
    if (timestamp >= 0x00ffffff){
      // send extended timestamp
      rtmpheader[1] = 0xff;
      rtmpheader[2] = 0xff;
      rtmpheader[3] = 0xff;
      rtmpheader[header_len++] = (timestamp >> 24) & 0xff;
      rtmpheader[header_len++] = (timestamp >> 16) & 0xff;
      rtmpheader[header_len++] = (timestamp >> 8) & 0xff;
      rtmpheader[header_len++] = timestamp & 0xff;
    }else{
      // regular timestamp
      rtmpheader[1] = (timestamp >> 16) & 0xff;
      rtmpheader[2] = (timestamp >> 8) & 0xff;
      rtmpheader[3] = timestamp & 0xff;
    }

    // send the header
    myConn.setBlocking(true);
    myConn.SendNow(rtmpheader, header_len);
    RTMPStream::snd_cnt += header_len; // update the sent data counter
    // set the header's first byte to the "continue" type chunk, for later use
    rtmpheader[0] = 0xC4;
    if (timestamp >= 0x00ffffff){
      rtmpheader[1] = (timestamp >> 24) & 0xff;
      rtmpheader[2] = (timestamp >> 16) & 0xff;
      rtmpheader[3] = (timestamp >> 8) & 0xff;
      rtmpheader[4] = timestamp & 0xff;
    }

    // sent actual data - never send more than chunk_snd_max at a time
    // interleave blocks of max chunk_snd_max bytes with 0xC4 bytes to indicate continue
    size_t len_sent = 0;
    while (len_sent < data_len){
      size_t to_send = std::min(data_len - len_sent, RTMPStream::chunk_snd_max);
      if (!len_sent){
        myConn.SendNow(dataheader, dheader_len);
        RTMPStream::snd_cnt += dheader_len; // update the sent data counter
        to_send -= dheader_len;
        len_sent += dheader_len;
      }
      myConn.SendNow(tmpData + len_sent - dheader_len, to_send);
      len_sent += to_send;
      if (len_sent < data_len){
        if (timestamp >= 0x00ffffff){
          myConn.SendNow(rtmpheader, 5);
          RTMPStream::snd_cnt += 5; // update the sent data counter
        }else{
          myConn.SendNow(rtmpheader, 1);
          RTMPStream::snd_cnt += 1; // update the sent data counter
        }
      }
    }
    myConn.setBlocking(false);
    lastSend = Util::bootMS();
  }

  void OutRTMP::sendHeader(){
    FLV::Tag tag;
    std::set<size_t> selectedTracks, vidTracks, audTracks;
    // Will contain the full audio=<> parameter in it, which should be CSV's of
    // {path, url, filename, silent, silence}1..*
    std::string audioParameterBuffer;
    // Current parameter we're parsing
    std::string audioParameter;
    // Indicates position where the previous parameter ended
    int prevPos = 0;
    // Used to read a custom AAC file 
    HTTP::URIReader inAAC;
    char *tempBuffer;
    size_t bytesRead;

    for (const auto & it : userSelect) {
      selectedTracks.insert(it.first);
      const std::string & type = M.getType(it.first);
      if (type == "video") {
        vidTracks.insert(it.first);
      } else if (type == "audio") {
        audTracks.insert(it.first);
      }
    }

    // Parse manual video/audio mapping overrides
    std::set<uint8_t> vidUsed, audUsed;
    vidMultiMap.clear();
    audMultiMap.clear();
    if (targetParams.count("vidmap")) {
      JSON::Value vidMap = JSON::fromString(targetParams["vidmap"]);
      jsonForEachConst (vidMap, it) {
        int k = atoi(it.key().c_str());
        if (vidTracks.count(k)) {
          vidMultiMap[k] = it->asInt();
          vidUsed.insert(it->asInt());
        }
      }
    }
    if (targetParams.count("audmap")) {
      JSON::Value audMap = JSON::fromString(targetParams["audmap"]);
      jsonForEachConst (audMap, it) {
        int k = atoi(it.key().c_str());
        if (audTracks.count(k)) {
          audMultiMap[k] = it->asInt();
          audUsed.insert(it->asInt());
        }
      }
    }

    // Check if tracks need to be mapped
    if (vidTracks.size() > 1) {
      bool firstTrack = true;
      for (const size_t T : vidTracks) {
        if (!vidMultiMap.count(T)) {
          // The first unmapped track need not be mapped
          if (firstTrack) {
            firstTrack = false;
            continue;
          }
          for (uint8_t i = 1; i < 255; ++i) {
            if (!vidUsed.count(i)) {
              vidUsed.insert(i);
              vidMultiMap[T] = i;
              break;
            }
          }
        }
      }
    }
    if (Util::printDebugLevel >= DLVL_INFO) {
      if (audMultiMap.size()) {
        JSON::Value mapping;
        for (const auto & it : vidMultiMap) { mapping[std::to_string(it.first)] = it.second; }
        INFO_MSG("%zu video tracks: E-RTMP mapping: %s", vidTracks.size(), mapping.toString().c_str());
      }
    }
    if (audTracks.size() > 1) {
      bool firstTrack = true;
      for (const size_t T : audTracks) {
        if (!audMultiMap.count(T)) {
          // The first unmapped track need not be mapped
          if (firstTrack) {
            firstTrack = false;
            continue;
          }
          for (uint8_t i = 1; i < 255; ++i) {
            if (!audUsed.count(i)) {
              audUsed.insert(i);
              audMultiMap[T] = i;
              break;
            }
          }
        }
      }
    }
    if (Util::printDebugLevel >= DLVL_INFO) {
      if (audMultiMap.size()) {
        JSON::Value mapping;
        for (const auto & it : audMultiMap) { mapping[std::to_string(it.first)] = it.second; }
        INFO_MSG("%zu audio tracks: E-RTMP mapping: %s", audTracks.size(), mapping.toString().c_str());
      }
    }

    //GO-RTMP can't handle the complexity of our metadata, but also doesn't need it... so let's not.
    if (UA != "GO-RTMP/0,0,0,0"){
      tag.DTSCMetaInit(meta, selectedTracks);
    }
    if (tag.len){
      tag.tagTime(currentTime() - rtmpOffset);
      myConn.SendNow(RTMPStream::SendMedia(tag));
    }

    for (const size_t T : vidTracks) {
      auto mapped = vidMultiMap.find(T);
      if (tag.DTSCVideoInit(meta.getCodec(T), meta.getInit(T), mapped == vidMultiMap.end() ? -1 : mapped->second)) {
        tag.tagTime(currentTime() - rtmpOffset);
        myConn.SendNow(RTMPStream::SendMedia(tag));
      }
    }
    for (const size_t T : audTracks) {
      auto mapped = audMultiMap.find(T);
      if (tag.DTSCAudioInit(meta.getCodec(T), meta.getRate(T), meta.getSize(T), meta.getChannels(T), meta.getInit(T),
                            mapped == audMultiMap.end() ? -1 : mapped->second)) {
        tag.tagTime(currentTime() - rtmpOffset);
        myConn.SendNow(RTMPStream::SendMedia(tag));
      }
    }
    // Insert silent init data if audio set to silent or loop a custom AAC file
    audioParameterBuffer = targetParams["audio"];
    HIGH_MSG("audioParameterBuffer: %s", audioParameterBuffer.c_str());
    // Read until we find a , or end of audioParameterBuffer
    for (std::string::size_type i = 0; i < audioParameterBuffer.size(); i++){
      if ( (audioParameterBuffer[i] == ',') || i + 1 == (audioParameterBuffer.size()) ){
        // If end of buffer reached, take entire string
        if (i + 1 == audioParameterBuffer.size()){i++;}
        // Get audio parameter
        audioParameter = audioParameterBuffer.substr(prevPos, i - prevPos);
        HIGH_MSG("Parsing audio parameter %s", audioParameter.c_str());
        // Inc i to skip the ,
        i++; 
        prevPos = i;
        
        if (audioParameter == "silence" || audioParameter == "silent"){
          hasSilence = true;
          INFO_MSG("Filling audio track with silence");
          break;
        }
        // Else parse AAC track(s) until we find one which works for us
        else{
          inAAC.open(audioParameter);
          if (inAAC && !inAAC.isEOF()){
              inAAC.readAll(tempBuffer, bytesRead);
              customAudioSize = bytesRead;
              // Copy to buffer since inAAC will be closed soon...
              customAudioFile = (char*)malloc(bytesRead);
              memcpy(customAudioFile, tempBuffer, bytesRead);
              hasCustomAudio = true;
              customAudioIterator = 0;
              break;
          }
          else{
            INFO_MSG("Could not parse audio parameter %s. Skipping...", audioParameter.c_str());
          }
        }
      }
    }
    if (hasSilence && tag.DTSCAudioInit("AAC", 44100, 32, 2, std::string("\022\020V\345\000", 5))){
      // InitData contains AudioSpecificConfig:
      //                   \022       \020       V        \345     \000
      //   12 10 56 e5 0 = 00010-010  0-0010-000 01010110 11100101 00000000
      //                   Type -sample-chnl-000
      //                   AACLC-44100 -2   -000
      INFO_MSG("Inserting silence track init data");
      tag.tagTime(currentTime() - rtmpOffset);
      myConn.SendNow(RTMPStream::SendMedia(tag));
    }
    if (hasCustomAudio){
      // Get first frame in order to init the audio track correctly
      // Confirm syncword (= FFF)
      if (customAudioFile[customAudioIterator] != 0xFF || ( customAudioFile[customAudioIterator + 1] & 0xF0) != 0xF0 ){
        WARN_MSG("Invalid sync word at start of header. Invalid input file!");
        return;
      }
      // Calculate the starting position of the next frame
      uint64_t frameSize = (((customAudioFile[customAudioIterator + 3] & 0x03) << 11) | ( customAudioFile[customAudioIterator + 4] << 3) | (( customAudioFile[customAudioIterator + 5] >> 5) & 0x07));
    
      // Create ADTS object of frame
      aac::adts adtsPack(customAudioFile + customAudioIterator, frameSize);
      if (!adtsPack){
        WARN_MSG("Could not parse ADTS package. Invalid input file!");
        return;
      }
      currentFrameInfo = adtsPack;
      char *tempInitData = (char*)malloc(2);
      /* 
       * Create AudioSpecificConfig
       * DTSCAudioInit already includes the sequence header at pos 12
       * We need:
       *  objectType       = getAACProfile (5 bits) (probably 00001 AAC Main or 00010 AACLC)
       *  Sampling Rate    = 44100    = 0100
       *  Channels         = 2        = 0010
       *  + 000
       */
      tempInitData[0] = 0x02 + (currentFrameInfo.getAACProfile() << 3);
      tempInitData[1] = 0x10;
      const std::string initData = std::string(tempInitData, 2);
      
      if (tag.DTSCAudioInit("AAC", currentFrameInfo.getFrequency(), currentFrameInfo.getSampleCount(), currentFrameInfo.getChannelCount(), initData)){
        INFO_MSG("Loaded a %" PRIu64 " byte custom audio file as audio loop", customAudioSize);
        myConn.SendNow(RTMPStream::SendMedia(tag));
      }
    }

    sentHeader = true;
  }

  void OutRTMP::requestHandler(bool readable){
    // If needed, slow down the reading to a rate of maxbps on average
    static bool slowWarned = false;
    if (maxbps && (Util::bootSecs() - myConn.connTime()) &&
        myConn.dataDown() / (Util::bootSecs() - myConn.connTime()) > maxbps){
      if (!slowWarned){
        WARN_MSG("Slowing down connection from %s because rate of %" PRIu64 "kbps > %" PRIu32
                 "kbps",
                 getConnectedHost().c_str(),
                 (myConn.dataDown() / (Util::bootSecs() - myConn.connTime())) / 128, maxbps / 128);
        slowWarned = true;
      }
      Util::sleep(50);
    }
    if (softClose && lastSend + 1000 < Util::bootMS()) { onFinish(); }
    Output::requestHandler(readable);
  }

  void OutRTMP::onRequest() {
    parseChunk(myConn.Received());
    if (Util::bootSecs() > lastErrCheck + 4) {
      lastErrCheck = Util::bootSecs();
      if (amfErr < AMF::amfErrors) {
        WARN_MSG("Encountered %zu AMF parse errors in the last 5 seconds", AMF::amfErrors - amfErr);
        amfErr = AMF::amfErrors;
      }
      if (rtmpErr < RTMPStream::parseErr) {
        WARN_MSG("Encountered %zu RTMP parse errors in the last 5 seconds", RTMPStream::parseErr - rtmpErr);
        rtmpErr = RTMPStream::parseErr;
      }
    }
  }

  ///\brief Sends a RTMP command either in AMF or AMF3 mode.
  ///\param amfReply The data to be sent over RTMP.
  ///\param messageType The type of message.
  ///\param streamId The ID of the AMF stream.
  void OutRTMP::sendCommand(AMF::Object &amfReply, int messageType, int streamId){
    HIGH_MSG("Sending: %s", amfReply.Print().c_str());
    if (messageType == 17){
      myConn.SendNow(RTMPStream::SendChunk(3, messageType, streamId, (char)0 + amfReply.Pack()));
    }else{
      myConn.SendNow(RTMPStream::SendChunk(3, messageType, streamId, amfReply.Pack()));
    }
  }// sendCommand

  ///\brief Parses a single AMF command message, and sends a direct response through sendCommand().
  ///\param amfData The received request.
  ///\param messageType The type of message.
  ///\param streamId The ID of the AMF stream.
  /// \triggers
  /// The `"STREAM_PUSH"` trigger is stream-specific, and is ran right before an incoming push is
  /// accepted. If cancelled, the push is denied. Its payload is:
  /// ~~~~~~~~~~~~~~~
  /// streamname
  /// connected client host
  /// output handler name
  /// request URL (if any)
  /// ~~~~~~~~~~~~~~~
  /// The `"RTMP_PUSH_REWRITE"` trigger is global and ran right before an RTMP publish request is
  /// parsed. It cannot be cancelled, but an invalid URL can be returned; which is effectively
  /// equivalent to cancelling. This trigger is special: the response is used as RTMP URL override,
  /// and not handled as normal. If used, the handler for this trigger MUST return a valid RTMP URL
  /// to allow the push to go through. If used multiple times, the last defined handler overrides
  /// any and all previous handlers. Its payload is:
  /// ~~~~~~~~~~~~~~~
  /// current RTMP URL
  /// connected client host
  /// ~~~~~~~~~~~~~~~
  void OutRTMP::parseAMFCommand(AMF::Object &amfData, int messageType, int streamId){
    MEDIUM_MSG("Received command: %s", amfData.Print().c_str());
    HIGH_MSG("AMF0 command: %s", amfData.getContentP(0)->StrValue().c_str());
    if (amfData.getContentP(0)->StrValue() == "xsbwtest"){
      // send a _result reply
      AMF::Object amfReply;
      amfReply.addContent("_error"); // result success
      amfReply.addContent(amfData.getContent(1));     // same transaction ID
      amfReply.addContent(amfData.getContentP(0)->StrValue()); // null - command info
      amfReply.addContent("Hai XSplit user!"); // stream ID?
      sendCommand(amfReply, messageType, streamId);
      return;
    }
    if (amfData.getContentP(0)->StrValue() == "connect"){
      AMF::Object *cmd = amfData.getContentP(2);
      double objencoding = 0;
      if (cmd->getContentP("objectEncoding")) { objencoding = cmd->getContentP("objectEncoding")->NumValue(); }
      if (cmd->getContentP("flashVer")) { UA = cmd->getContentP("flashVer")->StrValue(); }
      enhancedCaps = 0;
      bool hasCaps = false;
      if (cmd->getContentP("capsEx")) {
        hasCaps = true;
        enhancedCaps = cmd->getContentP("capsEx")->NumValue();
      }
      bool hasCcList = false, hasAudioCc = false, hasVideoCc = false;
      if (cmd->getContentP("fourCcList")) {
        hasCcList = true;
        for (auto & it : *(cmd->getContentP("fourCcList"))) {
          const std::string & fourcc = it.StrValue();
          if (fourcc == "av01") { capa["codecs"][0u][0u].append("AV1"); }
          if (fourcc == "hvc1") { capa["codecs"][0u][0u].append("HEVC"); }
          if (fourcc == "vp08") { capa["codecs"][0u][0u].append("VP8"); }
          if (fourcc == "vp09") { capa["codecs"][0u][0u].append("VP9"); }
          if (fourcc == "Opus") { capa["codecs"][0u][1u].append("opus"); }
          if (fourcc == "ac-3") { capa["codecs"][0u][1u].append("AC3"); }
          if (fourcc == "ec-3") { capa["codecs"][0u][1u].append("EAC-3"); }
          if (fourcc == "fLaC") { capa["codecs"][0u][1u].append("FLAC"); }
          if (fourcc == "*") { capa["codecs"][0u] = capa["codecs"][1u]; }
        }
      }
      if (cmd->getContentP("audioFourCcInfoMap")) {
        hasAudioCc = true;
        for (auto & it : *(cmd->getContentP("audioFourCcInfoMap"))) {
          const std::string & fourcc = it.Indice();
          if (fourcc == "Opus") { capa["codecs"][0u][1u].append("opus"); }
          if (fourcc == "ac-3") { capa["codecs"][0u][1u].append("AC3"); }
          if (fourcc == "ec-3") { capa["codecs"][0u][1u].append("EAC-3"); }
          if (fourcc == "fLaC") { capa["codecs"][0u][1u].append("FLAC"); }
          if (fourcc == "*") { capa["codecs"][0u][1u] = capa["codecs"][1u][1u]; }
        }
      }
      if (cmd->getContentP("videoFourCcInfoMap")) {
        hasVideoCc = true;
        for (auto & it : *(cmd->getContentP("videoFourCcInfoMap"))) {
          const std::string & fourcc = it.Indice();
          if (fourcc == "av01") { capa["codecs"][0u][0u].append("AV1"); }
          if (fourcc == "hvc1") { capa["codecs"][0u][0u].append("HEVC"); }
          if (fourcc == "vp08") { capa["codecs"][0u][0u].append("VP8"); }
          if (fourcc == "vp09") { capa["codecs"][0u][0u].append("VP9"); }
          if (fourcc == "*") { capa["codecs"][0u][0u] = capa["codecs"][1u][0u]; }
        }
      }
      reqUrl = amfData.getContentP(2)->getContentP("tcUrl")->StrValue();
      app_name = HTTP::URL(reqUrl).path;

      // If this user agent matches, we can safely guess it's librtmp, and this is not dangerous
      if (UA == "FMLE/3.0 (compatible; FMSc/1.0)"){
        // set max chunk size early, to work around OBS v25 bug
        RTMPStream::chunk_snd_max = 65536;                                 // 64KiB
        myConn.SendNow(RTMPStream::SendCTL(1, RTMPStream::chunk_snd_max)); // send chunk size max (msg 1)
      }
      // send a _result reply
      AMF::Object amfReply;
      amfReply.addContent("_result"); // result success
      amfReply.addContent(amfData.getContent(1)); // same transaction ID
      AMF::Object *props = amfReply.addContent(AMF::AMF0_OBJECT); // server properties
      props->addContent("fmsVer", "FMS/3,5,5,2004");
      props->addContent("capabilities", 31.0);
      props->addContent("mode", 1.0);
      AMF::Object *info = amfReply.addContent(AMF::AMF0_OBJECT); // info
      info->addContent("level", "status");
      info->addContent("code", "NetConnection.Connect.Success");
      info->addContent("description", "Connection succeeded.");
      info->addContent("clientid", 1337.0);
      info->addContent("objectEncoding", objencoding);
      if (hasCaps) {
        // Report that we support Reconnect (1), ModEx (4) and TimestampNanoOffset (8)
        info->addContent("capsEx", 15.0);
      }
      if (hasCcList) {
        AMF::Object *ccList = info->addContent("fourCcList", AMF::AMF0_STRICT_ARRAY);
        ccList->addContent("av01");
        ccList->addContent("avc1");
        ccList->addContent("hvc1");
        ccList->addContent("vp08");
        ccList->addContent("vp09");
        ccList->addContent("ac-3");
        ccList->addContent("ec-3");
        ccList->addContent("Opus");
        ccList->addContent(".mp3");
        ccList->addContent("fLaC");
        ccList->addContent("mp4a");
      }
      if (hasAudioCc) {
        AMF::Object *ccList = info->addContent("audioFourCcInfoMap", AMF::AMF0_OBJECT);
        ccList->addContent("ac-3", 7.0);
        ccList->addContent("ec-3", 7.0);
        ccList->addContent("Opus", 7.0);
        ccList->addContent(".mp3", 7.0);
        ccList->addContent("fLaC", 7.0);
        ccList->addContent("mp4a", 7.0);
      }
      if (hasVideoCc) {
        AMF::Object *ccList = info->addContent("videoFourCcInfoMap", AMF::AMF0_OBJECT);
        ccList->addContent("av01", 7.0);
        ccList->addContent("avc1", 7.0);
        ccList->addContent("hvc1", 7.0);
        ccList->addContent("vp08", 7.0);
        ccList->addContent("vp09", 7.0);
      }
      // amfReply.getContentP(3)->addContent(AMF::Object("data", AMF::AMF0_ECMA_ARRAY));
      // amfReply.getContentP(3)->getContentP(4)->addContent(AMF::Object("version", "3,5,4,1004"));
      sendCommand(amfReply, messageType, streamId);
      // Send other stream-related packets
      RTMPStream::chunk_snd_max = 65536;                                 // 64KiB
      myConn.SendNow(RTMPStream::SendCTL(1, RTMPStream::chunk_snd_max)); // send chunk size max (msg 1)
      myConn.SendNow(RTMPStream::SendCTL(5, RTMPStream::snd_window_size)); // send window acknowledgement size (msg 5)
      myConn.SendNow(RTMPStream::SendCTL(6, RTMPStream::rec_window_size)); // send rec window acknowledgement size (msg 6)
      // myConn.SendNow(RTMPStream::SendUSR(0, 1)); //send UCM StreamBegin (0), stream 1
      // send onBWDone packet - no clue what it is, but real server sends it...
      // amfReply = AMF::Object("container", AMF::AMF0_DDV_CONTAINER);
      // amfReply.addContent(AMF::Object("", "onBWDone"));//result
      // amfReply.addContent(amfData.getContent(1));//same transaction ID
      // amfReply.addContent(AMF::Object("", (double)0, AMF::AMF0_NULL));//null
      // sendCommand(amfReply, messageType, streamId);
      return;
    }// connect
    if (amfData.getContentP(0)->StrValue() == "createStream") {
      // send a _result reply
      AMF::Object amfReply;
      amfReply.addContent("_result"); // result success
      amfReply.addContent(amfData.getContent(1)); // same transaction ID
      amfReply.addContent(AMF::AMF0_NULL); // null - command info
      amfReply.addContent(1.0); // stream ID - we use 1
      sendCommand(amfReply, messageType, streamId);
      // myConn.SendNow(RTMPStream::SendUSR(0, 1)); //send UCM StreamBegin (0), stream 1
      return;
    } // createStream
    if (amfData.getContentP(0)->StrValue() == "ping"){
      // send a _result reply
      AMF::Object amfReply;
      amfReply.addContent("_result"); // result success
      amfReply.addContent(amfData.getContent(1));                // same transaction ID
      amfReply.addContent(AMF::AMF0_NULL); // null - command info
      amfReply.addContent("Pong!"); // stream ID - we use 1
      sendCommand(amfReply, messageType, streamId);
      return;
    }// createStream
    if (amfData.getContentP(0)->StrValue() == "closeStream"){
      myConn.SendNow(RTMPStream::SendUSR(1, 1)); // send UCM StreamEOF (1), stream 1
      AMF::Object amfReply;
      amfReply.addContent("onStatus"); // status reply
      amfReply.addContent(0.0); // transaction ID
      amfReply.addContent(AMF::AMF0_NULL); // null - command info
      AMF::Object *info = amfReply.addContent(AMF::AMF0_OBJECT); // info
      info->addContent("level", "status");
      info->addContent("code", "NetStream.Play.Stop");
      info->addContent("description", "Stream stopped");
      info->addContent("details", "DDV");
      info->addContent("clientid", 1337.0);
      sendCommand(amfReply, 20, 1);
      stop();
      return;
    }
    if (amfData.getContentP(0)->StrValue() == "deleteStream"){
      Util::logExitReason(ER_CLEAN_INTENDED_STOP, "received deleteStream");
      stop();
      return;
    }
    if ((amfData.getContentP(0)->StrValue() == "FCUnpublish") ||
        (amfData.getContentP(0)->StrValue() == "releaseStream")){
      // ignored
      return;
    }
    if (amfData.getContentP(0)->StrValue() == "FCSubscribe") {
      // send a FCPublish reply
      AMF::Object amfReply;
      amfReply.addContent("onFCSubscribe"); // status reply
      amfReply.addContent(amfData.getContent(1)); // same transaction ID
      amfReply.addContent(AMF::AMF0_NULL); // null - command info
      AMF::Object *info = amfReply.addContent(AMF::AMF0_OBJECT); // info
      info->addContent("code", "NetStream.Play.Start");
      info->addContent("level", "status");
      info->addContent("description", "Please follow up with play or publish command, as we ignore this command.");
      sendCommand(amfReply, messageType, streamId);
      return;
    } // FCPublish
    if (amfData.getContentP(0)->StrValue() == "FCPublish") {
      // send a FCPublish reply
      AMF::Object amfReply;
      amfReply.addContent("onFCPublish"); // status reply
      amfReply.addContent(amfData.getContent(1)); // same transaction ID
      amfReply.addContent(AMF::AMF0_NULL); // null - command info
      AMF::Object *info = amfReply.addContent(AMF::AMF0_OBJECT); // info
      info->addContent("code", "NetStream.Publish.Start");
      info->addContent("description", "Please follow up with publish command, as we ignore this command.");
      sendCommand(amfReply, messageType, streamId);
      return;
    } // FCPublish
    if (amfData.getContentP(0)->StrValue() == "releaseStream") {
      // send a _result reply
      AMF::Object amfReply;
      amfReply.addContent("_result"); // result success
      amfReply.addContent(amfData.getContent(1)); // same transaction ID
      amfReply.addContent(AMF::AMF0_NULL); // null - command info
      amfReply.addContent(AMF::AMF0_UNDEFINED); // stream ID?
      sendCommand(amfReply, messageType, streamId);
      return;
    } // releaseStream
    if ((amfData.getContentP(0)->StrValue() == "getStreamLength") || (amfData.getContentP(0)->StrValue() == "getMovLen")) {
      // send a _result reply
      AMF::Object amfReply;
      amfReply.addContent("_result"); // result success
      amfReply.addContent(amfData.getContent(1)); // same transaction ID
      amfReply.addContent(AMF::AMF0_NULL); // null - command info
      amfReply.addContent(0.0); // zero length
      sendCommand(amfReply, messageType, streamId);
      return;
    } // getStreamLength
    if ((amfData.getContentP(0)->StrValue() == "publish")){
      if (config->getInteger("acceptable") == 1) { // Only allow outgoing ( = 1)? Abort!
        AMF::Object amfReply;
        amfReply.addContent("_error"); // result error
        amfReply.addContent(amfData.getContent(1)); // same transaction ID
        amfReply.addContent(AMF::AMF0_NULL); // null - command info
        AMF::Object *info = amfReply.addContent(AMF::AMF0_OBJECT); // info
        info->addContent("code", "NetStream.Publish.Rejected");
        info->addContent("description", "Publish rejected: this interface does not allow publishing");
        sendCommand(amfReply, messageType, streamId);
        INFO_MSG("Push from %s rejected - connector configured to only allow outgoing streams", getConnectedHost().c_str());
        onFinish();
        return;
      }
      if (amfData.getContentP(3)){
        streamName = Encodings::URL::decode(amfData.getContentP(3)->StrValue());
        reqUrl += "/" + streamName; // LTS

        // handle variables
        if (streamName.find('?') != std::string::npos){
          std::string tmpVars = streamName.substr(streamName.find('?') + 1);
          streamName = streamName.substr(0, streamName.find('?'));
          HTTP::parseVars(tmpVars, targetParams);
        }
        //Remove anything before the last slash
        if (streamName.find('/')){
          streamName = streamName.substr(0, streamName.find('/'));
        }

        if (checkStreamKey()) {
          if (!streamName.size()) {
            onFinish();
            return;
          }
        } else {
          if (Triggers::shouldTrigger("RTMP_PUSH_REWRITE")) {
            std::string payload = reqUrl + "\n" + getConnectedHost();
            std::string newUrl = reqUrl;
            Triggers::doTrigger("RTMP_PUSH_REWRITE", payload, "", false, newUrl);
            if (!newUrl.size()) {
              FAIL_MSG("Push from %s to URL %s rejected - RTMP_PUSH_REWRITE trigger blanked the URL",
                       getConnectedHost().c_str(), reqUrl.c_str());
              onFinish();
              return;
            }
            reqUrl = newUrl;
            size_t lSlash = newUrl.rfind('/');
            if (lSlash != std::string::npos) {
              streamName = newUrl.substr(lSlash + 1);
            } else {
              streamName = newUrl;
            }
            // handle variables
            if (streamName.find('?') != std::string::npos) {
              std::string tmpVars = streamName.substr(streamName.find('?') + 1);
              streamName = streamName.substr(0, streamName.find('?'));
              HTTP::parseVars(tmpVars, targetParams);
            }
          }

          size_t colonPos = streamName.find(':');
          if (colonPos != std::string::npos && colonPos < 6) {
            std::string oldName = streamName;
            if (std::string(".") + oldName.substr(0, colonPos) == oldName.substr(oldName.size() - colonPos - 1)) {
              streamName = oldName.substr(colonPos + 1);
            } else {
              streamName = oldName.substr(colonPos + 1) + std::string(".") + oldName.substr(0, colonPos);
            }
          }

          if (Triggers::shouldTrigger("PUSH_REWRITE")) {
            std::string payload = reqUrl + "\n" + getConnectedHost() + "\n" + streamName;
            std::string newStream = streamName;
            Triggers::doTrigger("PUSH_REWRITE", payload, "", false, newStream);
            if (!newStream.size()) {
              FAIL_MSG("Push from %s to URL %s rejected - PUSH_REWRITE trigger blanked the URL",
                       getConnectedHost().c_str(), reqUrl.c_str());
              Util::logExitReason(ER_TRIGGER, "Push from %s to URL %s rejected - PUSH_REWRITE trigger blanked the URL",
                                  getConnectedHost().c_str(), reqUrl.c_str());
              onFinish();
              return;
            } else {
              streamName = newStream;
              // handle variables
              if (streamName.find('?') != std::string::npos) {
                std::string tmpVars = streamName.substr(streamName.find('?') + 1);
                streamName = streamName.substr(0, streamName.find('?'));
                HTTP::parseVars(tmpVars, targetParams);
              }
            }
          }
          if (!allowPush(app_name)) {
            onFinish();
            return;
          }
        }

        // Clear the exit reason, in case it was set before now
        Util::mRExitReason = (char*)ER_UNKNOWN;
        Util::exitReason[0] = 0;
      }
      { // send a status reply
        AMF::Object amfReply;
        amfReply.addContent("onStatus"); // status reply
        amfReply.addContent(amfData.getContent(1)); // same transaction ID
        amfReply.addContent(AMF::AMF0_NULL); // null - command info
        AMF::Object *info = amfReply.addContent(AMF::AMF0_OBJECT); // info
        info->addContent("level", "status");
        info->addContent("code", "NetStream.Publish.Start");
        info->addContent("description", "Stream is now published!");
        info->addContent("clientid", 1337.0);
        sendCommand(amfReply, messageType, streamId);
      }
      /*
      //send a _result reply
      amfReply = AMF::Object("container", AMF::AMF0_DDV_CONTAINER);
      amfReply.addContent(AMF::Object("", "_result")); //result success
      amfReply.addContent(amfData.getContent(1)); //same transaction ID
      amfReply.addContent(AMF::Object("", (double)0, AMF::AMF0_NULL)); //null - command info
      amfReply.addContent(AMF::Object("", 1, AMF::AMF0_BOOL)); //publish success?
      sendCommand(amfReply, messageType, streamId);
      */
      myConn.SendNow(RTMPStream::SendUSR(0, 1)); // send UCM StreamBegin (0), stream 1
      return;
    }// getStreamLength
    if (amfData.getContentP(0)->StrValue() == "checkBandwidth") {
      // send a _result reply
      AMF::Object amfReply;
      amfReply.addContent("_result"); // result success
      amfReply.addContent(amfData.getContent(1)); // same transaction ID
      amfReply.addContent(AMF::AMF0_NULL); // null - command info
      amfReply.addContent(AMF::AMF0_NULL); // null - command info
      sendCommand(amfReply, messageType, streamId);
      return;
    } // checkBandwidth
    if (amfData.getContentP(0)->StrValue() == "onBWDone"){return;}
    if ((amfData.getContentP(0)->StrValue() == "play") ||
        (amfData.getContentP(0)->StrValue() == "play2")){
      // set reply number and stream name, actual reply is sent up in the ss.spool() handler
      double playTransaction = amfData.getContentP(1)->NumValue();
      int8_t playMessageType = messageType;
      int32_t playStreamId = streamId;
      streamName = Encodings::URL::decode(amfData.getContentP(3)->StrValue());
      reqUrl += "/" + streamName; // LTS

      // handle variables
      if (streamName.find('?') != std::string::npos){
        std::string tmpVars = streamName.substr(streamName.find('?') + 1);
        streamName = streamName.substr(0, streamName.find('?'));
        HTTP::parseVars(tmpVars, targetParams);
      }

      size_t colonPos = streamName.find(':');
      if (colonPos != std::string::npos && colonPos < 6){
        std::string oldName = streamName;
        if (std::string(".") + oldName.substr(0, colonPos) == oldName.substr(oldName.size() - colonPos - 1)){
          streamName = oldName.substr(colonPos + 1);
        }else{
          streamName = oldName.substr(colonPos + 1) + std::string(".") + oldName.substr(0, colonPos);
        }
      }

      if (config->getInteger("acceptable") == 2) { // Only allow incoming ( = 2)? Abort!
        AMF::Object amfReply;
        amfReply.addContent("_error"); // result success
        amfReply.addContent(amfData.getContent(1)); // same transaction ID
        amfReply.addContent(AMF::AMF0_NULL); // null - command info
        AMF::Object *info = amfReply.addContent(AMF::AMF0_OBJECT); // info
        info->addContent("code", "NetStream.Play.Rejected");
        info->addContent("description", "Play rejected: this interface does not allow playback");
        sendCommand(amfReply, messageType, streamId);
        INFO_MSG("Play of %s by %s rejected - connector configured to only allow incoming streams", streamName.c_str(),
                 getConnectedHost().c_str());
        onFinish();
        return;
      }

      initialize();
      //Abort if stream could not be opened
      if (!M) {
        INFO_MSG("Could not open stream, aborting");
        // send a _result reply
        AMF::Object amfReply;
        amfReply.addContent("_error"); // result success
        amfReply.addContent(amfData.getContent(1)); // same transaction ID
        amfReply.addContent(AMF::AMF0_NULL); // null - command info
        AMF::Object *info = amfReply.addContent(AMF::AMF0_OBJECT); // info
        info->addContent("code", "NetStream.Play.Rejected");
        info->addContent("description", "Play rejected: could not initialize stream");
        sendCommand(amfReply, messageType, streamId);
        onFinish();
        return;
      }

      { // send a status reply
        AMF::Object amfReply;
        amfReply.addContent("onStatus"); // status reply
        amfReply.addContent(playTransaction); // same transaction ID
        amfReply.addContent(AMF::AMF0_NULL); // null - command info
        AMF::Object *info = amfReply.addContent(AMF::AMF0_OBJECT); // info
        info->addContent("level", "status");
        info->addContent("code", "NetStream.Play.Reset");
        info->addContent("description", "Playing and resetting...");
        info->addContent("details", "DDV");
        info->addContent("clientid", 1337.0);
        sendCommand(amfReply, playMessageType, playStreamId);
      }
      // send streamisrecorded if stream, well, is recorded.
      if (M.getVod()){// isMember("length") && Strm.metadata["length"].asInt() > 0){
        myConn.SendNow(RTMPStream::SendUSR(4, 1)); // send UCM StreamIsRecorded (4), stream 1
      }
      // send streambegin
      myConn.SendNow(RTMPStream::SendUSR(0, 1)); // send UCM StreamBegin (0), stream 1

      initialSeek();
      rtmpOffset = targetParams.count("keepts") ? 0 : currentTime();

      { // and more reply
        AMF::Object amfReply;
        amfReply.addContent("onStatus"); // status reply
        amfReply.addContent(playTransaction); // same transaction ID
        amfReply.addContent(AMF::AMF0_NULL); // null - command info
        AMF::Object *info = amfReply.addContent(AMF::AMF0_OBJECT); // info
        info->addContent("level", "status");
        info->addContent("code", "NetStream.Play.Start");
        info->addContent("description", "Playing!");
        info->addContent("details", "DDV");
        info->addContent("clientid", 1337.0);
        info->addContent("timecodeOffset", (double)rtmpOffset);
        sendCommand(amfReply, playMessageType, playStreamId);
      }
      RTMPStream::chunk_snd_max = 65536;                                 // 64KiB
      myConn.SendNow(RTMPStream::SendCTL(1, RTMPStream::chunk_snd_max)); // send chunk size max (msg 1)
      // send dunno?
      myConn.SendNow(RTMPStream::SendUSR(32, 1)); // send UCM no clue?, stream 1

      parseData = true;
      return;
    }// play
    if ((amfData.getContentP(0)->StrValue() == "seek")){
      // set reply number and stream name, actual reply is sent up in the ss.spool() handler
      double playTransaction = amfData.getContentP(1)->NumValue();
      int8_t playMessageType = messageType;
      int32_t playStreamId = streamId;

      {
        AMF::Object amfReply;
        amfReply.addContent("onStatus"); // status reply
        amfReply.addContent(amfData.getContent(1)); // same transaction ID
        amfReply.addContent(AMF::AMF0_NULL); // null - command info
        AMF::Object *info = amfReply.addContent(AMF::AMF0_OBJECT); // info
        info->addContent("level", "status");
        info->addContent("code", "NetStream.Seek.Notify");
        info->addContent("description", "Seeking to the specified time");
        info->addContent("details", "DDV");
        info->addContent("clientid", 1337.0);
        sendCommand(amfReply, playMessageType, playStreamId);
      }
      seek(amfData.getContentP(3)->NumValue());

      { // send a status reply
        AMF::Object amfReply;
        amfReply.addContent("onStatus"); // status reply
        amfReply.addContent(playTransaction); // same transaction ID
        amfReply.addContent(AMF::AMF0_NULL); // null - command info
        AMF::Object *info = amfReply.addContent(AMF::AMF0_OBJECT); // info
        info->addContent("level", "status");
        info->addContent("code", "NetStream.Play.Reset");
        info->addContent("description", "Playing and resetting...");
        info->addContent("details", "DDV");
        info->addContent("clientid", 1337.0);
        sendCommand(amfReply, playMessageType, playStreamId);
      }
      // send streamisrecorded if stream, well, is recorded.
      if (M.getVod()){// isMember("length") && Strm.metadata["length"].asInt() > 0){
        myConn.SendNow(RTMPStream::SendUSR(4, 1)); // send UCM StreamIsRecorded (4), stream 1
      }
      // send streambegin
      myConn.SendNow(RTMPStream::SendUSR(0, 1)); // send UCM StreamBegin (0), stream 1
      { // and more reply
        AMF::Object amfReply;
        amfReply.addContent("onStatus"); // status reply
        amfReply.addContent(playTransaction); // same transaction ID
        amfReply.addContent(AMF::AMF0_NULL); // null - command info
        AMF::Object *info = amfReply.addContent(AMF::AMF0_OBJECT); // info
        info->addContent("level", "status");
        info->addContent("code", "NetStream.Play.Start");
        info->addContent("description", "Playing!");
        info->addContent("details", "DDV");
        info->addContent("clientid", 1337.0);
        if (M.getLive()) {
          rtmpOffset = currentTime();
          info->addContent("timecodeOffset", (double)rtmpOffset);
        }
        sendCommand(amfReply, playMessageType, playStreamId);
      }
      RTMPStream::chunk_snd_max = 65536;                                 // 64KiB
      myConn.SendNow(RTMPStream::SendCTL(1, RTMPStream::chunk_snd_max)); // send chunk size max (msg 1)
      // send dunno?
      myConn.SendNow(RTMPStream::SendUSR(32, 1)); // send UCM no clue?, stream 1

      return;
    }// seek
    if ((amfData.getContentP(0)->StrValue() == "pauseRaw") || (amfData.getContentP(0)->StrValue() == "pause")){
      int8_t playMessageType = messageType;
      int32_t playStreamId = streamId;
      if (amfData.getContentP(3)->NumValue()) {
        parseData = false;
        // send a status reply
        AMF::Object amfReply;
        amfReply.addContent("onStatus"); // status reply
        amfReply.addContent(amfData.getContent(1)); // same transaction ID
        amfReply.addContent(AMF::AMF0_NULL); // null - command info
        AMF::Object *info = amfReply.addContent(AMF::AMF0_OBJECT); // info
        info->addContent("level", "status");
        info->addContent("code", "NetStream.Pause.Notify");
        info->addContent("description", "Pausing playback");
        info->addContent("details", "DDV");
        info->addContent("clientid", 1337.0);
        sendCommand(amfReply, playMessageType, playStreamId);
      } else {
        parseData = true;
        // send a status reply
        AMF::Object amfReply;
        amfReply.addContent("onStatus"); // status reply
        amfReply.addContent(amfData.getContent(1)); // same transaction ID
        amfReply.addContent(AMF::AMF0_NULL); // null - command info
        AMF::Object *info = amfReply.addContent(AMF::AMF0_OBJECT); // info
        info->addContent("level", "status");
        info->addContent("code", "NetStream.Unpause.Notify");
        info->addContent("description", "Resuming playback");
        info->addContent("details", "DDV");
        info->addContent("clientid", 1337.0);
        sendCommand(amfReply, playMessageType, playStreamId);
      }
      return;
    }// seek
    if (amfData.getContentP(0)->StrValue() == "_error"){
      if (amfData.getContentP(3)->GetType() == AMF::AMF0_DDV_CONTAINER){
        WARN_MSG("Received generic error response (no useful content)");
        return;
      }
      if (amfData.getContentP(3)->GetType() == AMF::AMF0_OBJECT){
        std::string code, description;
        if (amfData.getContentP(3)->getContentP("code") &&
            amfData.getContentP(3)->getContentP("code")->StrValue().size()){
          code = amfData.getContentP(3)->getContentP("code")->StrValue();
        }
        if (amfData.getContentP(3)->getContentP("description") &&
            amfData.getContentP(3)->getContentP("description")->StrValue().size()){
          description = amfData.getContentP(3)->getContentP("description")->StrValue();
        }
        if (amfData.getContentP(3)->getContentP("details") &&
            amfData.getContentP(3)->getContentP("details")->StrValue().size()){
          if (description.size()){
            description += "," + amfData.getContentP(3)->getContentP("details")->StrValue();
          }else{
            description = amfData.getContentP(3)->getContentP("details")->StrValue();
          }
        }
        if (code.size() || description.size()){
          if (description.find("authmod=adobe") != std::string::npos){
            if (!pushUrl.user.size() && !pushUrl.pass.size()){
              Util::logExitReason(ER_FORMAT_SPECIFIC, "receiving side wants credentials, but none were provided in the target");
              myConn.close();
              return;
            }
            if (description.find("?reason=authfailed") != std::string::npos || authAttempts > 1){
              Util::logExitReason(ER_FORMAT_SPECIFIC, "credentials provided in the target were not accepted by the receiving side");
              myConn.close();
              return;
            }
            if (description.find("?reason=needauth") != std::string::npos){
              std::map<std::string, std::string> authVars;
              HTTP::parseVars(description.substr(description.find("?reason=needauth") + 1), authVars, "&", false);
              std::string authSalt = authVars.count("salt") ? authVars["salt"] : "";
              std::string authOpaque = authVars.count("opaque") ? authVars["opaque"] : "";
              std::string authChallenge = authVars.count("challenge") ? authVars["challenge"] : "";
              std::string authNonce = authVars.count("nonce") ? authVars["nonce"] : "";
              INFO_MSG("Adobe auth: sending credentials phase 2 (salt=%s, opaque=%s, challenge=%s, "
                       "nonce=%s)",
                       authSalt.c_str(), authOpaque.c_str(), authChallenge.c_str(), authNonce.c_str());
              authAttempts++;

              char md5buffer[16];
              std::string to_hash = pushUrl.user + authSalt + pushUrl.pass;
              Secure::md5bin(to_hash.data(), to_hash.size(), md5buffer);
              std::string hash_one = Encodings::Base64::encode(std::string(md5buffer, 16));
              if (authOpaque.size()){
                to_hash = hash_one + authOpaque + "00000000";
              }else if (authChallenge.size()){
                to_hash = hash_one + authChallenge + "00000000";
              }
              Secure::md5bin(to_hash.data(), to_hash.size(), md5buffer);
              std::string hash_two = Encodings::Base64::encode(std::string(md5buffer, 16));
              std::string authStr = "?authmod=adobe&user=" + Encodings::URL::encode(pushUrl.user, "/:=@[]+ ") +
                                    "&challenge=00000000&response=" + hash_two;
              if (authOpaque.size()){authStr += "&opaque=" + Encodings::URL::encode(authOpaque, "/:=@[]+ ");}
              startPushOut(authStr.c_str());
              return;
            }
            INFO_MSG("Adobe auth: sending credentials phase 1");
            authAttempts++;
            std::string authStr = "?authmod=adobe&user=" + Encodings::URL::encode(pushUrl.user, "/:=@[]");
            startPushOut(authStr.c_str());
            return;
          }
          WARN_MSG("Received error response: %s; %s",
                   amfData.getContentP(3)->getContentP("code")->StrValue().c_str(),
                   amfData.getContentP(3)->getContentP("description")->StrValue().c_str());
        }else{
          WARN_MSG("Received generic error response (no useful content)");
        }
        return;
      }
      if (amfData.getContentP(3)->GetType() == AMF::AMF0_STRING){
        WARN_MSG("Received error response: %s", amfData.getContentP(3)->StrValue().c_str());
        return;
      }
      WARN_MSG("Received error response: %s", amfData.Print().c_str());
      return;
    }
    // We received a result from one of our commands:
    if ((amfData.getContentP(0)->StrValue() == "_result") || (amfData.getContentP(0)->StrValue() == "onFCPublish") ||
        (amfData.getContentP(0)->StrValue() == "onStatus")){
      if (isRecording() && amfData.getContentP(0)->StrValue() == "_result" &&
          amfData.getContentP(1)->NumValue() == 1){
        if (amfData.getContentP(2)->GetType() == AMF::AMF0_OBJECT && amfData.getContentP(2)->getContentP("fmsVer")){
          UA = amfData.getContentP(2)->getContentP("fmsVer")->StrValue();
          INFO_MSG("Server version: %s", UA.c_str());
        }
        AMF::Object *connInfo = amfData.getContentP(3);
        if (connInfo) {
          if (connInfo->getContentP("capsEx")) { enhancedCaps = connInfo->getContentP("capsEx")->NumValue(); }
          bool moreCodecs = false;
          if (connInfo->getContentP("fourCcList")->GetType() == AMF::AMF0_STRICT_ARRAY) {
            moreCodecs = true;
            for (auto & it : *(connInfo->getContentP("fourCcList"))) {
              const std::string & fourcc = it.StrValue();
              if (fourcc == "av01") { capa["codecs"][0u][0u].append("AV1"); }
              if (fourcc == "hvc1") { capa["codecs"][0u][0u].append("HEVC"); }
              if (fourcc == "vp08") { capa["codecs"][0u][0u].append("VP8"); }
              if (fourcc == "vp09") { capa["codecs"][0u][0u].append("VP9"); }
              if (fourcc == "Opus") { capa["codecs"][0u][1u].append("opus"); }
              if (fourcc == "ac-3") { capa["codecs"][0u][1u].append("AC3"); }
              if (fourcc == "ec-3") { capa["codecs"][0u][1u].append("EAC-3"); }
              if (fourcc == "fLaC") { capa["codecs"][0u][1u].append("FLAC"); }
              if (fourcc == "*") { capa["codecs"][0u] = capa["codecs"][1u]; }
            }
          }
          if (connInfo->getContentP("audioFourCcInfoMap")->GetType() == AMF::AMF0_OBJECT) {
            moreCodecs = true;
            for (auto & it : *(connInfo->getContentP("audioFourCcInfoMap"))) {
              const std::string & fourcc = it.Indice();
              if (fourcc == "Opus") { capa["codecs"][0u][1u].append("opus"); }
              if (fourcc == "ac-3") { capa["codecs"][0u][1u].append("AC3"); }
              if (fourcc == "ec-3") { capa["codecs"][0u][1u].append("EAC-3"); }
              if (fourcc == "fLaC") { capa["codecs"][0u][1u].append("FLAC"); }
              if (fourcc == "*") { capa["codecs"][0u][1u] = capa["codecs"][1u][1u]; }
            }
          }
          if (connInfo->getContentP("videoFourCcInfoMap")->GetType() == AMF::AMF0_OBJECT) {
            moreCodecs = true;
            for (auto & it : *(connInfo->getContentP("videoFourCcInfoMap"))) {
              const std::string & fourcc = it.Indice();
              if (fourcc == "av01") { capa["codecs"][0u][0u].append("AV1"); }
              if (fourcc == "hvc1") { capa["codecs"][0u][0u].append("HEVC"); }
              if (fourcc == "vp08") { capa["codecs"][0u][0u].append("VP8"); }
              if (fourcc == "vp09") { capa["codecs"][0u][0u].append("VP9"); }
              if (fourcc == "*") { capa["codecs"][0u][0u] = capa["codecs"][1u][0u]; }
            }
          }
          if (moreCodecs) {
            INFO_MSG("Re-selecting tracks because compatible codec list was updated");
            selectDefaultTracks();
          }
        }
        if (connInfo && connInfo->getContentP("")) {
          AMF::Object amfReply;
          amfReply.addContent("releaseStream"); // command
          amfReply.addContent(2.0); // transaction ID
          amfReply.addContent(AMF::AMF0_NULL); // options
          amfReply.addContent(streamOut); // stream name
          sendCommand(amfReply, 20, 0);
        }
        {
          AMF::Object amfReply;
          amfReply.addContent("FCPublish"); // command
          amfReply.addContent(3.0); // transaction ID
          amfReply.addContent(AMF::AMF0_NULL); // options
          amfReply.addContent(streamOut); // stream name
          sendCommand(amfReply, 20, 0);
        }
        {
          AMF::Object amfReply;
          amfReply.addContent("createStream"); // command
          amfReply.addContent(4.0); // transaction ID
          amfReply.addContent(AMF::AMF0_NULL); // options
          sendCommand(amfReply, 20, 0);
        }
        {
          AMF::Object amfReply;
          amfReply.addContent("publish"); // command
          amfReply.addContent(5.0); // transaction ID
          amfReply.addContent(AMF::AMF0_NULL); // options
          amfReply.addContent(streamOut); // stream name
          amfReply.addContent("live"); // stream name
          sendCommand(amfReply, 20, 1);
        }

        HIGH_MSG("Publish starting");
        pushState = "publish";
        didPublish = true;
        return;
      }
      if (didPublish && isRecording() &&
          (amfData.getContentP(0)->StrValue() == "_result" || amfData.getContentP(0)->StrValue() == "onStatus") &&
          amfData.getContentP(1)->NumValue() == 5) { // result from transaction ID 5 (e.g. our publish call).

        if (!targetParams.count("realtime")) { realTime = 800; }
        parseData = true;
        didPublish = false;
        return;
      }

      // Also handle publish start without matching transaction ID
      if (didPublish && isRecording() && amfData.getContentP(0)->StrValue() == "onStatus" &&
          amfData.getContentP(3)->getContentP("code")->StrValue() == "NetStream.Publish.Start") {
        if (!targetParams.count("realtime")){realTime = 800;}
        parseData = true;
        didPublish = false;
        return;
      }

      // Handle reconnect request
      if (isRecording() && amfData.getContentP(0)->StrValue() == "onStatus" &&
          amfData.getContentP(3)->getContentP("code")->StrValue() == "NetConnection.Connect.ReconnectRequest") {
        std::string tcUrl = amfData.getContentP(3)->getContentP("tcUrl")->StrValue();
        std::string desc = amfData.getContentP(3)->getContentP("description")->StrValue();
        HTTP::URL newUrl(targetParams["tcUrl"]);
        if (tcUrl.size()) {
          newUrl = newUrl.link(tcUrl);
          targetParams["app"] = newUrl.path;
          config->getOption("target", true).append(newUrl.getUrl() + "/" + targetParams["stream"]);
        }
        INFO_MSG("Received reconnect request to %s: %s", config->getString("target").c_str(), desc.c_str());
        pushState = "ReconnectRequest";
        return;
      }

      // Handling generic errors remotely triggered errors.
      if (amfData.getContentP(0)->StrValue() == "onStatus" &&
          amfData.getContentP(3)->getContentP("level")->StrValue() == "error"){
        const std::string & errorCode = amfData.getContentP(3)->getContentP("code")->StrValue();
        const std::string & errorDesc = amfData.getContentP(3)->getContentP("description")->StrValue();
        Util::logExitReason(ER_FORMAT_SPECIFIC, "received %s during %s: %s", errorCode.c_str(), pushState.c_str(),
                            errorDesc.c_str());
        return;
      }

      // Other results are ignored. We don't really care.
      return;
    }

    WARN_MSG("AMF0 command not processed: %s", amfData.Print().c_str());
    { // send a _result reply
      AMF::Object amfReply;
      amfReply.addContent("_error"); // result success
      amfReply.addContent(amfData.getContent(1)); // same transaction ID
      amfReply.addContent(amfData.getContentP(0)->StrValue()); // null - command info
      amfReply.addContent("Command not implemented or recognized"); // stream ID?
      sendCommand(amfReply, messageType, streamId);
    }
  }// parseAMFCommand

  ///\brief Gets and parses one RTMP chunk at a time.
  ///\param inputBuffer A buffer filled with chunk data.
  void OutRTMP::parseChunk(Socket::Buffer &inputBuffer){
    // for DTSC conversion
    static std::stringstream prebuffer; // Temporary buffer before sending real data
    // for chunk parsing
    static RTMPStream::Chunk next;
    static FLV::Tag F;
    static AMF::Object amfdata;
    static AMF::Object amfelem;
    static AMF::Object3 amf3data;
    static AMF::Object3 amf3elem;
    static bool warned = false;

    while (next.Parse(inputBuffer)){
      if (!next.data.size()){
        if (!warned) {
          WARN_MSG("Ignored packet with invalid (null) data pointer - further warnings will be suppressed");
          warned = true;
        }
        continue;
      }

      // send ACK if we received a whole window
      if ((RTMPStream::rec_cnt - RTMPStream::rec_window_at > RTMPStream::rec_window_size / 4) || Util::bootSecs() > lastAck+15){
        lastAck = Util::bootSecs();
        RTMPStream::rec_window_at = RTMPStream::rec_cnt;
        myConn.SendNow(RTMPStream::SendCTL(3, RTMPStream::rec_cnt)); // send ack (msg 3)
      }

      switch (next.msg_type_id){
      case 0: // does not exist
        WARN_MSG("UNKN: Received a zero-type message. Possible data corruption? Aborting!");
        while (inputBuffer.size()){inputBuffer.get().clear();}
        stop();
        onFinish();
        break; // happens when connection breaks unexpectedly
      case 1:  // set chunk size
        RTMPStream::chunk_rec_max = Bit::btohl(next.data);
        MEDIUM_MSG("CTRL: Set chunk size: %zu", RTMPStream::chunk_rec_max);
        break;
      case 2: // abort message - we ignore this one
        MEDIUM_MSG("CTRL: Abort message");
        // 4 bytes of stream id to drop
        break;
      case 3: // ack
        VERYHIGH_MSG("CTRL: Acknowledgement");
        RTMPStream::snd_window_at = Bit::btohl(next.data);
        RTMPStream::snd_window_at = RTMPStream::snd_cnt;
        break;
      case 4:{
        // 2 bytes event type, rest = event data
        // types:
        // 0 = stream begin, 4 bytes ID
        // 1 = stream EOF, 4 bytes ID
        // 2 = stream dry, 4 bytes ID
        // 3 = setbufferlen, 4 bytes ID, 4 bytes length
        // 4 = streamisrecorded, 4 bytes ID
        // 6 = pingrequest, 4 bytes data
        // 7 = pingresponse, 4 bytes data
        // we don't need to process this
        int16_t ucmtype = Bit::btohs(next.data);
        switch (ucmtype){
          case 0: MEDIUM_MSG("CTRL: UCM StreamBegin %" PRIu32, Bit::btohl(next.data + 2)); break;
          case 1: MEDIUM_MSG("CTRL: UCM StreamEOF %" PRIu32, Bit::btohl(next.data + 2)); break;
          case 2: MEDIUM_MSG("CTRL: UCM StreamDry %" PRIu32, Bit::btohl(next.data + 2)); break;
          case 3:
            MEDIUM_MSG("CTRL: UCM SetBufferLength %" PRIu32 " %" PRIu32, Bit::btohl(next.data + 2), Bit::btohl(next.data + 6));
            break;
          case 4: MEDIUM_MSG("CTRL: UCM StreamIsRecorded %" PRIu32, Bit::btohl(next.data + 2)); break;
          case 6:
            MEDIUM_MSG("CTRL: UCM PingRequest %" PRIu32, Bit::btohl(next.data + 2));
            myConn.SendNow(RTMPStream::SendUSR(7, Bit::btohl(next.data + 2))); // send UCM PingResponse (7)
            break;
          case 7: MEDIUM_MSG("CTRL: UCM PingResponse %" PRIu32, Bit::btohl(next.data + 2)); break;
          default: MEDIUM_MSG("CTRL: UCM Unknown (%" PRId16 ")", ucmtype); break;
        }
      }break;
      case 5: // window size of other end
        MEDIUM_MSG("CTRL: Window size");
        RTMPStream::rec_window_size = Bit::btohl(next.data);
        RTMPStream::rec_window_at = RTMPStream::rec_cnt;
        myConn.SendNow(RTMPStream::SendCTL(3, RTMPStream::rec_cnt)); // send ack (msg 3)
        lastAck = Util::bootSecs();
        break;
      case 6:
        MEDIUM_MSG("CTRL: Set peer bandwidth");
        // 4 bytes window size, 1 byte limit type (ignored)
        RTMPStream::snd_window_size = Bit::btohl(next.data);
        myConn.SendNow(RTMPStream::SendCTL(5, RTMPStream::snd_window_size)); // send window acknowledgement size (msg 5)
        break;
      case 8:    // audio data
      case 9:    // video data
      case 18:{// meta data
        static std::map<size_t, AMF::Object> pushMeta;
        static std::map<size_t, uint64_t> lastTagTime;
        static std::map<size_t, int64_t> trackOffset;
        static std::map<size_t, size_t> reTrackToID;
        if (!isInitialized || !meta){
          MEDIUM_MSG("Received useless media data");
          onFinish();
          break;
        }
        F.ChunkLoader(next);
        if (!F.getDataLen()){break;}// ignore empty packets
        AMF::Object *amf_storage = 0;
        if (F.data[0] == 0x12 || pushMeta.count(next.cs_id) || !pushMeta.size()){
          amf_storage = &(pushMeta[next.cs_id]);
        }else{
          amf_storage = &(pushMeta.begin()->second);
        }

        size_t reTrack = (next.cs_id * 1000) + F.getTrackID();
        if (!reTrackToID.count(reTrack)){reTrackToID[reTrack] = INVALID_TRACK_ID;}
        F.toMeta(meta, *amf_storage, reTrackToID[reTrack], targetParams);
        if ((F.getDataLen() || (amf_storage && amf_storage->hasContent())) && !(F.needsInitData() && F.isInitData())){
          uint64_t tagTime = next.timestamp;
          if (!setRtmpOffset) {
            uint64_t timeOffset = 0;
            if (targetParams.count("timeoffset")) { timeOffset = JSON::Value(targetParams["timeoffset"]).asInt(); }
            if (!M.getBootMsOffset()) {
              meta.setBootMsOffset(Util::bootMS() - tagTime);
              rtmpOffset = timeOffset;
            } else {
              rtmpOffset = (Util::bootMS() - tagTime) - M.getBootMsOffset() + timeOffset;
            }
            setRtmpOffset = true;
          }
          tagTime += rtmpOffset + trackOffset[reTrack];
          uint64_t &ltt = lastTagTime[reTrack];
          if (tagTime < ltt){
            uint64_t diff = ltt - tagTime;
            // Round to 24-bit rollover if within 0xfff of it on either side.
            // Round to 32-bit rollover if within 0xfff of it on either side.
            // Make sure time increases by 1ms if neither applies.
            if (diff > 0xfff000ull && diff < 0x1000fffull){
              diff = 0x1000000ull;
              WARN_MSG("Timestamp for %s went from %" PRIu64 " to %" PRIu64 " (decreased by 24-bit rollover): compensating", trackType(next.msg_type_id), ltt, tagTime);
            }else if (diff > 0xfffff000ull && diff < 0x100000fffull){
              diff = 0x100000000ull;
              WARN_MSG("Timestamp for %s went from %" PRIu64 " to %" PRIu64 " (decreased by 32-bit rollover): compensating", trackType(next.msg_type_id), ltt, tagTime);
            }else{
              diff += 1;
              WARN_MSG("Timestamp for %s went from %" PRIu64 " to %" PRIu64 " (decreased by %" PRIu64 "): compensating", trackType(next.msg_type_id), ltt, tagTime, diff);
            }
            trackOffset[reTrack] += diff;
            tagTime += diff;
          }else if (tagTime > ltt + 600000){
            uint64_t diff = tagTime - ltt;
            // Round to 24-bit rollover if within 0xfff of it on either side.
            // Round to 32-bit rollover if within 0xfff of it on either side.
            // Make sure time increases by 1ms if neither applies.
            if (diff > 0xfff000ull && diff < 0x1000fffull){
              diff = 0x1000000ull;
              WARN_MSG("Timestamp for %s went from %" PRIu64 " to %" PRIu64 " (increased by 24-bit rollover): compensating", trackType(next.msg_type_id), ltt, tagTime);
            }else if (diff > 0xfffff000ull && diff < 0x100000fffull){
              diff = 0x100000000ull;
              WARN_MSG("Timestamp for %s went from %" PRIu64 " to %" PRIu64 " (increased by 32-bit rollover): compensating", trackType(next.msg_type_id), ltt, tagTime);
            }else{
              diff -= 1;
              if (ltt){
                WARN_MSG("Timestamp for %s went from %" PRIu64 " to %" PRIu64 " (increased by %" PRIu64 "): compensating", trackType(next.msg_type_id), ltt, tagTime, diff);
              }
            }
            if (ltt){
              trackOffset[reTrack] -= diff;
              tagTime -= diff;
            }
          }
          size_t idx = reTrackToID[reTrack];
          if (idx != INVALID_TRACK_ID && !userSelect.count(idx)){
            userSelect[idx].reload(streamName, idx, COMM_STATUS_ACTIVE | COMM_STATUS_SOURCE);
          }
          if (M.getCodec(idx) == "PCM" && M.getSize(idx) == 16){
            char *ptr = F.getData();
            uint32_t ptrSize = F.getDataLen();
            for (uint32_t i = 0; i < ptrSize; i += 2){
              char tmpchar = ptr[i];
              ptr[i] = ptr[i + 1];
              ptr[i + 1] = tmpchar;
            }
          }
          ltt = tagTime;
          if (ltt){
            for (std::map<size_t, uint64_t>::iterator it = lastTagTime.begin(); it != lastTagTime.end(); ++it){
              if (it->first == reTrack) { continue; }
              size_t iIdx = reTrackToID[it->first];
              if (it->second + 100 < ltt){
                meta.setNowms(iIdx, ltt-100);
                it->second = ltt-100;
              }
            }
          }
          if (F.data[0] == 0x12 && amf_storage){
            std::string mData = amf_storage->toJSON().toString();
            bufferLivePacket(tagTime, F.offset(), idx, mData.c_str(), mData.size(), 0, true);
          }else{
            bufferLivePacket(tagTime, F.offset(), idx, F.getData(), F.getDataLen(), 0, F.isKeyframe);
          }
          if (!meta){config->is_active = false;}
        }
        break;
      }
      case 15: MEDIUM_MSG("Received AMF3 data message"); break;
      case 16: MEDIUM_MSG("Received AMF3 shared object"); break;
      case 17:{
        MEDIUM_MSG("Received AMF3 command message");
        if (next.data[0] == 0) {
          MEDIUM_MSG("Received AMF3 command message");
          amfdata = AMF::parse(next.data + 1, next.data.size() - 1);
          parseAMFCommand(amfdata, 17, next.msg_stream_id);
        } // parsing AMF0-style
      }break;
      case 19: MEDIUM_MSG("Received AMF0 shared object"); break;
      case 20:{// AMF0 command message
        amfdata = AMF::parse(next.data, next.data.size());
        parseAMFCommand(amfdata, 20, next.msg_stream_id);
      }break;
      case 22: MEDIUM_MSG("Received aggregate message"); break;
      default:
        FAIL_MSG("Unknown chunk received! Probably protocol corruption, stopping parsing of "
                 "incoming data.");
        break;
      }
    }
  }

  // Called when we get disconnected and allows us to specify a
  // more detailed exit reason.
  void OutRTMP::determineExitReason() {
    if (pushState.size() && !myConn) {
      Util::logExitReason(ER_CLEAN_REMOTE_CLOSE, "connection closed during %s", pushState.c_str());
    }
  }

} // namespace Mist
