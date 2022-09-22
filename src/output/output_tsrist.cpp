#include "output_tsrist.h"
#include <mist/defines.h>
#include <mist/http_parser.h>
#include <mist/url.h>
#include <mist/encode.h>
#include <mist/stream.h>
#include <mist/triggers.h>

uint64_t pktSent = 0;
uint64_t pktRetransmitted = 0;
uint64_t upBytes = 0;

namespace Mist{


  struct rist_logging_settings log_settings;
  int rist_log_callback(void *, enum rist_log_level llvl, const char *msg){
    switch (llvl){
    case RIST_LOG_WARN: WARN_MSG("RIST: %s", msg); break;
    case RIST_LOG_ERROR: ERROR_MSG("RIST: %s", msg); break;
    case RIST_LOG_DEBUG:
    case RIST_LOG_SIMULATE: DONTEVEN_MSG("RIST: %s", msg); break;
    default: INFO_MSG("RIST: %s", msg);
    }
    return 0;
  }

  static int cb_auth_connect(void *arg, const char *connecting_ip, uint16_t connecting_port,
                             const char *local_ip, uint16_t local_port, struct rist_peer *peer){
    return 0;
  }

  static int cb_auth_disconnect(void *arg, struct rist_peer *peer){
    return 0;
  }

  int cb_stats(void *arg, const struct rist_stats *stats_container){
    JSON::Value stats = JSON::fromString(stats_container->stats_json, stats_container->json_size);
    JSON::Value & sObj = stats["sender-stats"]["peer"]["stats"];
    pktSent += sObj["sent"].asInt();
    pktRetransmitted += sObj["retransmitted"].asInt();
    rist_stats_free(stats_container);
    return 0;
  }

  static void addIntOpt(JSON::Value & pp, const std::string & param, const std::string & name, const std::string & help, size_t def = 0){
    pp[param]["name"] = name;
    pp[param]["help"] = help;
    pp[param]["type"] = "int";
    pp[param]["default"] = (uint64_t)def;
  }

  static void addStrOpt(JSON::Value & pp, const std::string & param, const std::string & name, const std::string & help, const std::string & def = ""){
    pp[param]["name"] = name;
    pp[param]["help"] = help;
    pp[param]["type"] = "str";
    pp[param]["default"] = def;
  }

  static void addBoolOpt(JSON::Value & pp, const std::string & param, const std::string & name, const std::string & help, bool def = false){
    pp[param]["name"] = name;
    pp[param]["help"] = help;
    pp[param]["type"] = "select";
    pp[param]["select"][0u][0u] = 0;
    pp[param]["select"][0u][1u] = "False";
    pp[param]["select"][1u][0u] = 1;
    pp[param]["select"][1u][1u] = "True";
    pp[param]["type"] = "select";
    pp[param]["default"] = def?1:0;

  }

  OutTSRIST::OutTSRIST(Socket::Connection &conn) : TSOutput(conn){
    connTime = Util::bootSecs();
    lastTimeStamp = 0;
    timeStampOffset = 0;
    sendRepeatingHeaders = 500; // PAT/PMT every 500ms (DVB spec)
    streamName = config->getString("streamname");
    Util::setStreamName(streamName);
    pushOut = false;
    // Push output configuration
    if (config->getString("target").size()){
      std::string ristURL = config->getString("target").c_str();
      //If there are two ?, grab everything after the last first
      if (ristURL.rfind('?') != std::string::npos && ristURL.find('?') != ristURL.rfind('?')){
        std::string extraParams = ristURL.substr(ristURL.rfind('?')+1);
        std::map<std::string, std::string> arguments;
        HTTP::parseVars(extraParams, arguments);
        for (std::map<std::string, std::string>::iterator it = arguments.begin(); it != arguments.end(); ++it){
          targetParams[it->first] = it->second;
        }
        ristURL.erase(ristURL.rfind('?'));
      }
      target = HTTP::URL(ristURL);
      if (target.protocol != "rist"){
        FAIL_MSG("Target %s must begin with rist://, aborting", target.getUrl().c_str());
        onFail("Invalid RIST target: doesn't start with rist://", true);
        return;
      }
      if (!target.getPort()){
        FAIL_MSG("Target %s must contain a port, aborting", target.getUrl().c_str());
        onFail("Invalid RIST target: missing port", true);
        return;
      }
      if (target.getPort() % 2 != 0){
        FAIL_MSG("Target %s must contain an even port number, aborting", target.getUrl().c_str());
        onFail("Invalid RIST target: port number may not be uneven", true);
        return;
      }
      pushOut = true;

      rist_profile profile = RIST_PROFILE_MAIN;
      if (targetParams.count("profile")){
        profile = (rist_profile)JSON::Value(targetParams["profile"]).asInt();
      }

      if (rist_sender_create(&sender_ctx, profile, 0, &log_settings) != 0){
        onFail("Failed to create sender context");
        return;
      }
      INFO_MSG("Letting libRIST parse URL: %s", ristURL.c_str());
      struct rist_peer_config *peer_config_link = 0;
      if (rist_parse_address2(ristURL.c_str(), &peer_config_link)){
        onFail("Failed to parse target URL: %s", config->getString("target").c_str());
        return;
      }
      strcpy(peer_config_link->cname, streamName.c_str());
      INFO_MSG("Set up RIST target address for %s", target.getUrl().c_str());
      if (rist_peer_create(sender_ctx, &peer, peer_config_link) == -1){
        onFail("Could not create peer");
        return;
      }
      if (rist_stats_callback_set(sender_ctx, 1000, cb_stats, 0) == -1){
        onFail("Error setting up stats callback");
        return;
      }
      if (rist_start(sender_ctx) == -1){
        onFail("Failed to start RIST connection");
        return;
      }
      myConn.setHost(target.host);
      wantRequest = false;
      parseData = true;
      initialize();
      return;
    }
      
    // Handle override / append of streamname options
    //std::string sName = srtConn.getStreamName();
    //if (sName != ""){
    //  streamName = sName;
    //  Util::sanitizeName(streamName);
    //  Util::setStreamName(streamName);
    //}

      //srtConn.setBlocking(false);
      //srtConn.direction = "input";
      if (Triggers::shouldTrigger("PUSH_REWRITE")){
        HTTP::URL reqUrl;
        reqUrl.protocol = "rist";
        reqUrl.port = config->getString("port");
        reqUrl.host = config->getString("interface");
        //reqUrl.args = "streamid="+Encodings::URL::encode(sName);
        std::string payload = reqUrl.getUrl() + "\n" + getConnectedHost() + "\n" + streamName;
        std::string newStream = "";
        Triggers::doTrigger("PUSH_REWRITE", payload, "", false, newStream);
        if (!newStream.size()){
          FAIL_MSG("Push from %s to URL %s rejected - PUSH_REWRITE trigger blanked the URL",
                   getConnectedHost().c_str(), reqUrl.getUrl().c_str());
          Util::logExitReason(
              "Push from %s to URL %s rejected - PUSH_REWRITE trigger blanked the URL",
              getConnectedHost().c_str(), reqUrl.getUrl().c_str());
          onFinish();
          return;
        }else{
          streamName = newStream;
          Util::sanitizeName(streamName);
        }
      }
      if (!allowPush("")){
        onFinish();
        return;
      }
      parseData = false;
      wantRequest = true;

  }

  OutTSRIST::~OutTSRIST(){}
  
  std::string OutTSRIST::getConnectedHost(){
    if (!pushOut) { return Output::getConnectedHost(); }
    return target.host;
  }
  std::string OutTSRIST::getConnectedBinHost(){
    if (!pushOut) { return Output::getConnectedBinHost(); }
    std::string binHost = Socket::getBinForms(target.host);
    if (binHost.size() > 16){ binHost = binHost.substr(0, 16); }
    if (binHost.size() < 16){
      binHost = std::string("\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\001", 16);
    }
    return binHost;
  }

  void OutTSRIST::init(Util::Config *cfg){
    Output::init(cfg);
    capa["name"] = "TSRIST";
    capa["friendly"] = "TS over RIST";
    capa["desc"] = "Real time streaming of TS data over RIST";
    capa["deps"] = "This output protocol can currently only be used by the pushing system.";
    capa["PUSHONLY"] = true;

    capa["optional"]["streamname"]["name"] = "Stream";
    capa["optional"]["streamname"]["help"] = "What streamname to serve if no streamid is given by the other end of the connection";
    capa["optional"]["streamname"]["type"] = "str";
    capa["optional"]["streamname"]["option"] = "--stream";
    capa["optional"]["streamname"]["short"] = "s";
    capa["optional"]["streamname"]["default"] = "";

    capa["codecs"][0u][0u].append("+HEVC");
    capa["codecs"][0u][0u].append("+H264");
    capa["codecs"][0u][0u].append("+MPEG2");
    capa["codecs"][0u][1u].append("+AAC");
    capa["codecs"][0u][1u].append("+MP3");
    capa["codecs"][0u][1u].append("+AC3");
    capa["codecs"][0u][1u].append("+MP2");
    capa["codecs"][0u][1u].append("+opus");
    capa["codecs"][1u][0u].append("rawts");

    capa["optional"]["profile"]["name"] = "RIST profile";
    capa["optional"]["profile"]["help"] = "RIST profile to use";
    capa["optional"]["profile"]["option"] = "--profile";
    capa["optional"]["profile"]["short"] = "P";
    capa["optional"]["profile"]["default"] = 1;
    capa["optional"]["profile"]["type"] = "select";
    capa["optional"]["profile"]["select"][0u][0u] = 0;
    capa["optional"]["profile"]["select"][0u][1u] = "Simple";
    capa["optional"]["profile"]["select"][1u][0u] = 1;
    capa["optional"]["profile"]["select"][1u][1u] = "Main";

    cfg->addBasicConnectorOptions(capa);
    config = cfg;
    config->addStandardPushCapabilities(capa);
    capa["push_urls"].append("rist://*");
                                   //
    JSON::Value & pp = capa["push_parameters"];

    pp["profile"]["name"] = "RIST profile";
    pp["profile"]["help"] = "RIST profile to use";
    pp["profile"]["type"] = "select";
    pp["profile"]["default"] = 1;
    pp["profile"]["select"][0u][0u] = 0;
    pp["profile"]["select"][0u][1u] = "Simple";
    pp["profile"]["select"][1u][0u] = 1;
    pp["profile"]["select"][1u][1u] = "Main";
    pp["profile"]["type"] = "select";

    addIntOpt(pp, "buffer", "Buffer size", "Sets the buffer size in milliseconds. The buffer size will work best at four to seven times the ping time.");
    addIntOpt(pp, "bandwidth", "Bandwidth", "Sets the maximum bandwidth in Kbps. It is necessary to configure the bandwidth to be higher than the max bandwidth of your stream(s). This is in order to allow room for messaging headroom, plus the re-requested packets. When tuning a connection for the first time, analyze your stream statistics locally at first, then start at 10% higher for a constant bitrate, 100% higher for variable bitrate. Especially for VBR, provide generous \"headroom\" in your bandwidth. You can always reduce it when configuring and tuning the connection.");
    addIntOpt(pp, "return-bandwidth", "Return bandwidth", "Sets the maximum bandwidth in Kbps for just the receiver-to-sender direction. This is an option which may sometimes help avoid congestion insofar as it may limit re-request messages in poor network conditions.");
    addIntOpt(pp, "reorder-buffer", "Reorder buffer", "Sets the size for a secondary buffer in which after all re=requested packets have been received, the out-of-order packets are released in the correct order. in most cases there should be no need to adjust this setting, but it may be helpful in conjuction with very long distance/large buffer/poor network conditions.");
    addStrOpt(pp, "cname", "Cacnonical name", "Provides a canonical name for the media. If multi-plexing more than one stream through a tunnel, this provides a convenient way to identify a particular stream within the log. You should make it standard practice to assign a cononical name whenever multi-plexing.");
    addIntOpt(pp, "rtt-min", "Minimum Round Trip Time", "Sets the minimum rtt setting in milliseconds. This can help reduce congestion by reducing the number of repeated re-requests in poor network conditions. More importantly, for very long-distance or connections that traverse under-sea cables, it may be important to adjust this setting.");
    addIntOpt(pp, "rtt-max", "Maximum Round Trip Time", "Sets the maximum rtt setting in milliseconds. See rtt-min for a more complete description. in most cases, minimum and maximum should be set to be equal to one another.");
    addIntOpt(pp, "aes-type", "AES Type", "Specifies the specific encrytion. Specify \"128\" for AES-128 or \"256\" for AES-256. Remember that you must also specify the pass phrase, and that encryption is not supported for the simple protocol at all.");
    addIntOpt(pp, "session-timeout", "Session timeout", "Terminates the RIST connection after inactivity/lack of keepalive response for the limit (in milliseconds) which you set with this parameter.");
    addStrOpt(pp, "secret", "Passphrase", "Sets the specified passphrase for Main or Advanced profile encryption. Note that simple protocol does not support encryption, and that you must in addition to the passphrase specify the \"AES Type\" parameter. The rotating keys shall be placed inside the rtcp messages, using your passphrase as the pre-shared key. Be sure that the passphrase for sender and receiver match!");
    addIntOpt(pp, "virt-dst-port", "Virtual destination port", "The port within the GRE tunnel. This has nothing to do with the media port(s). If the GRE is device /dev/tun11, having an address of 1.1.1.2, and the virtual destination port is 10000, and your media is using port 8193/4, the operating system will use 1.1.1.2:10000 as the destination from the sender's point of view, or the inbound on the receiver's point of view. libRIST will make use of that device/IP/port. As far as your media source and media player are concerned, the media is on ports 8193/4 on their respective interfaces. The media knows nothing of the tunnel.");
    addIntOpt(pp, "keepalive-interval", "Keepalive interval", "Time in milliseconds between pings. As is standard practice for GRE tunnels, the keep alive helps ensure the tunnel remains connected and open should no media be traversing it at a given time.");
    addIntOpt(pp, "key-rotation", "Key rotation interval", "Sets the key rotation period in milliseconds when aes and a passphrases are specified.");
    addIntOpt(pp, "congestion-control", "Congestion control", "The three options for this parameter are 0=disabled, 1=normal and 2=aggressive. In general, don't set the parameter to \"aggressive\" unless you've definitely established that congestion is a problem.");
    addIntOpt(pp, "min-retries", "Minimum retries", "Sets a minimum number of re-requests for a lost packet. Note that setting this too high can lead to congestion. Regardless of this setting, the size of the buffer and the roundtrip time will render too high a minimum value here irrelevant.");
    addIntOpt(pp, "max-retries", "Maximum retries", "Sets a maximum number of re-requests for a lost packet.");
    addIntOpt(pp, "weight", "Path weight", "Sets the relative share for load balanced connections. The best way to describe this will be to provide an example. The default is five, so in a setup where two paths are given weights of 5 and 10 respectively, the former would receive 1/3 of packets sent, and the latter would receive 2/3.");
    addIntOpt(pp, "stream-id", "Stream ID", "Sets the encapsulated udp destination port, this must be even. ");
    addBoolOpt(pp, "compression", "Compression", "Utilizes liblz4 to compress all traffic in the GRE tunnel");

    addStrOpt(pp, "", "", "");

    JSON::Value opt;
    opt["arg"] = "string";
    opt["default"] = "";
    opt["arg_num"] = 1;
    opt["help"] = "Target rist:// URL to push out towards.";
    cfg->addOption("target", opt);
  }

  // Buffers TS packets and sends after 7 are buffered.
  void OutTSRIST::sendTS(const char *tsData, size_t len){
    packetBuffer.append(tsData, len);
    if (packetBuffer.size() >= 1316){//7 whole TS packets
      struct rist_data_block data_blk;
      data_blk.virt_src_port = 0;
      data_blk.virt_dst_port = 1968;
      data_blk.flags = 0;
      data_blk.flow_id = 0;
      data_blk.payload =  packetBuffer;
      data_blk.payload_len = packetBuffer.size();
      data_blk.peer = 0;
      data_blk.ref = 0;
      data_blk.seq = 0;
      data_blk.ts_ntp = 0;
      rist_sender_data_write(sender_ctx, &data_blk);
      upBytes += packetBuffer.size();
      packetBuffer.assign(0,0);
    }
  }

  void OutTSRIST::requestHandler(){
    //size_t recvSize = conn.Recv();
    size_t recvSize = 0;
    if (!recvSize){return;}
    lastRecv = Util::bootSecs();
    //if (!assembler.assemble(tsIn, srtConn.recvbuf, recvSize, true)){return;}
    while (tsIn.hasPacket()){
      tsIn.getEarliestPacket(thisPacket);
      if (!thisPacket){
        INFO_MSG("Could not get TS packet");
        myConn.close();
        wantRequest = false;
        return;
      }

      tsIn.initializeMetadata(meta);
      size_t thisIdx = M.trackIDToIndex(thisPacket.getTrackId(), getpid());
      if (thisIdx == INVALID_TRACK_ID){return;}
      if (!userSelect.count(thisIdx)){
        userSelect[thisIdx].reload(streamName, thisIdx, COMM_STATUS_SOURCE | COMM_STATUS_DONOTTRACK);
      }

      uint64_t adjustTime = thisPacket.getTime() + timeStampOffset;
      if (lastTimeStamp || timeStampOffset){
        if (lastTimeStamp + 5000 < adjustTime || lastTimeStamp > adjustTime + 5000){
          INFO_MSG("Timestamp jump " PRETTY_PRINT_MSTIME " -> " PRETTY_PRINT_MSTIME ", compensating.",
                   PRETTY_ARG_MSTIME(lastTimeStamp), PRETTY_ARG_MSTIME(adjustTime));
          timeStampOffset += (lastTimeStamp - adjustTime);
          adjustTime = thisPacket.getTime() + timeStampOffset;
        }
      }
      lastTimeStamp = adjustTime;
      thisPacket.setTime(adjustTime);
      bufferLivePacket(thisPacket);
    }
  }

  void OutTSRIST::connStats(uint64_t now, Comms::Connections &statComm){
    if (!myConn){return;}
    statComm.setUp(upBytes);
    statComm.setDown(0);
    statComm.setTime(now - connTime);
    statComm.setPacketCount(pktSent);
    statComm.setPacketLostCount(0);
    statComm.setPacketRetransmitCount(pktRetransmitted);
  }

}// namespace Mist



static uint64_t sockCount = 0;
struct rist_ctx *rec_ctx;

static void connection_status_callback(void *arg, struct rist_peer *peer,
                                       enum rist_connection_status peer_connection_status){
  (void)arg;
  if (peer_connection_status == RIST_CONNECTION_ESTABLISHED || peer_connection_status == RIST_CLIENT_CONNECTED){
    sockCount++;
  }else{
    sockCount--;
  }
  if (!sockCount && Util::Config::is_restarting){
    Util::Config::is_active = false;
    INFO_MSG("Last active connection closed; triggering rolling restart now!");
  }
  INFO_MSG(
      "Connection Status changed for Peer %p, new status is %d, peer connected count is %" PRIu64,
      peer, peer_connection_status, sockCount);
}

void (*oldSignal)(int, siginfo_t *,void *) = 0;
void signal_handler(int signum, siginfo_t *sigInfo, void *ignore){
  ///\TODO Update for RIST
  //server_socket.close();
  if (oldSignal){
    oldSignal(signum, sigInfo, ignore);
  }
}

void handleUSR1(int signum, siginfo_t *sigInfo, void *ignore){
  if (!sockCount){
    INFO_MSG("USR1 received - triggering rolling restart (no connections active)");
    Util::Config::is_restarting = true;
    Util::logExitReason("signal USR1, no connections");
    ///\TODO Update for RIST
    //server_socket.close();
    Util::Config::is_active = false;
  }else{
    INFO_MSG("USR1 received - triggering rolling restart when connection count reaches zero");
    Util::Config::is_restarting = true;
    Util::logExitReason("signal USR1, after disconnect wait");
  }
}

int main(int argc, char *argv[]){
  DTSC::trackValidMask = TRACK_VALID_EXT_HUMAN;
  Util::redirectLogsIfNeeded();
  Util::Config conf(argv[0]);
  mistOut::init(&conf);
  if (conf.parseArgs(argc, argv)){
    if (conf.getBool("json")){
      mistOut::capa["version"] = PACKAGE_VERSION;
      std::cout << mistOut::capa.toString() << std::endl;
      return -1;
    }

    //Setup logger
    Mist::log_settings.log_cb = &Mist::rist_log_callback;
    Mist::log_settings.log_cb_arg = 0;
    Mist::log_settings.log_socket = -1;
    Mist::log_settings.log_stream = 0;
    if (Util::printDebugLevel >= 10){
      Mist::log_settings.log_level = RIST_LOG_SIMULATE;
    }else if(Util::printDebugLevel >= 4){
      Mist::log_settings.log_level = RIST_LOG_INFO;
    }else{
      Mist::log_settings.log_level = RIST_LOG_WARN;
    }

    conf.activate();

    //int filelimit = conf.getInteger("filelimit");
    //Util::sysSetNrOpenFiles(filelimit);

    if (!mistOut::listenMode()){
      Socket::Connection S(fileno(stdout), fileno(stdin));
      ///\TODO Update for RIST
      //Socket::SRTConnection tmpSock;
      mistOut tmp(S);
      return tmp.run();
    }
    {
      struct sigaction new_action;
      new_action.sa_sigaction = handleUSR1;
      sigemptyset(&new_action.sa_mask);
      new_action.sa_flags = 0;
      sigaction(SIGUSR1, &new_action, NULL);
    }
    if (conf.getInteger("port") && conf.getString("interface").size()){

      if (rist_receiver_create(&rec_ctx, (rist_profile)conf.getInteger("profile"), &Mist::log_settings) != 0){
        FAIL_MSG("Failed to create receiver context");
        return 1;
      }


      if (rist_auth_handler_set(rec_ctx, Mist::cb_auth_connect, Mist::cb_auth_disconnect, rec_ctx) != 0){
        FAIL_MSG("Failed to set up RIST auth handler");
        return 1;
      }

      if (rist_connection_status_callback_set(rec_ctx, connection_status_callback, NULL) == -1){
        FAIL_MSG("Failed to set up RIST connection status handler");
        return 1;
      }

//	if (rist_stats_callback_set(ctx, statsinterval, cb_stats, NULL) == -1){
//		rist_log(&logging_settings, RIST_LOG_ERROR, "Could not enable stats callback\n");
//		exit(1);
//	}

//      struct rist_peer_config *peer_config_link = 0;
//      if (rist_parse_address2(config->getString("target").c_str(), &peer_config_link)){
//        onFail("Failed to parse target URL: %s", config->getString("target").c_str());
//        return;
//}
//      strcpy(peer_config_link->cname, streamName.c_str());
//      INFO_MSG("Set up RIST target address for %s", target.getUrl().c_str());
//      if (rist_peer_create(sender_ctx, &peer, peer_config_link) == -1){
//        onFail("Could not create peer");
//        return;
//}
//      if (rist_stats_callback_set(sender_ctx, 1000, cb_stats, 0) == -1){
//        onFail("Error setting up stats callback");
//        return;
//}
//      if (rist_start(sender_ctx) == -1){
//        onFail("Failed to start RIST connection");
//        return;
//}
      
//      server_socket = Socket::SRTServer(conf.getInteger("port"), conf.getString("interface"), false, "output");
    }
 //   if (!server_socket.connected()){
 //     DEVEL_MSG("Failure to open socket");
 //     return 1;
 //}
    struct sigaction new_action;
    struct sigaction cur_action;
    new_action.sa_sigaction = signal_handler;
    sigemptyset(&new_action.sa_mask);
    new_action.sa_flags = SA_SIGINFO;
    sigaction(SIGINT, &new_action, &cur_action);
    if (cur_action.sa_sigaction && cur_action.sa_sigaction != oldSignal){
      if (oldSignal){WARN_MSG("Multiple signal handlers! I can't deal with this.");}
      oldSignal = cur_action.sa_sigaction;
    }
    sigaction(SIGHUP, &new_action, &cur_action);
    if (cur_action.sa_sigaction && cur_action.sa_sigaction != oldSignal){
      if (oldSignal){WARN_MSG("Multiple signal handlers! I can't deal with this.");}
      oldSignal = cur_action.sa_sigaction;
    }
    sigaction(SIGTERM, &new_action, &cur_action);
    if (cur_action.sa_sigaction && cur_action.sa_sigaction != oldSignal){
      if (oldSignal){WARN_MSG("Multiple signal handlers! I can't deal with this.");}
      oldSignal = cur_action.sa_sigaction;
    }
//    Util::Procs::socketList.insert(server_socket.getSocket());
//    while (conf.is_active && server_socket.connected()){
//      Socket::SRTConnection S = server_socket.accept(false, "output");
//      if (S.connected()){// check if the new connection is valid
//        // spawn a new thread for this connection
//        tthread::thread T(callThreadCallbackSRT, (void *)new Socket::SRTConnection(S));
//        // detach it, no need to keep track of it anymore
//        T.detach();
//}else{
//        Util::sleep(10); // sleep 10ms
//}
//}
//    Util::Procs::socketList.erase(server_socket.getSocket());
//    server_socket.close();
    if (conf.is_restarting){
      INFO_MSG("Reloading input...");
      execvp(argv[0], argv);
      FAIL_MSG("Error reloading: %s", strerror(errno));
    }
  }
  INFO_MSG("Exit reason: %s", Util::exitReason);
  return 0;
}
