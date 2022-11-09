#include <mist/socket_srt.h>
#include "output_tssrt.h"
#include <mist/defines.h>
#include <mist/http_parser.h>
#include <mist/url.h>
#include <mist/encode.h>
#include <mist/stream.h>
#include <mist/triggers.h>

bool allowStreamNameOverride = true;

namespace Mist{
  OutTSSRT::OutTSSRT(Socket::Connection &conn, Socket::SRTConnection & _srtSock) : TSOutput(conn), srtConn(_srtSock){
    // NOTE: conn is useless for SRT, as it uses a different socket type.
    sendRepeatingHeaders = 500; // PAT/PMT every 500ms (DVB spec)
    streamName = config->getString("streamname");
    Util::setStreamName(streamName);
    pushOut = false;
    assembler.setLive();
    // Push output configuration
    if (config->getString("target").size()){
      target = HTTP::URL(config->getString("target"));
      if (target.protocol != "srt"){
        FAIL_MSG("Target %s must begin with srt://, aborting", target.getUrl().c_str());
        onFail("Invalid srt target: doesn't start with srt://", true);
        return;
      }
      if (!target.getPort()){
        FAIL_MSG("Target %s must contain a port, aborting", target.getUrl().c_str());
        onFail("Invalid srt target: missing port", true);
        return;
      }
      pushOut = true;
      HTTP::parseVars(target.args, targetParams);
      size_t connectCnt = 0;
      do{
        srtConn.connect(target.host, target.getPort(), "output", targetParams);
        if (!srtConn){
          Util::sleep(1000);
        }else{
          INFO_MSG("Connect success on attempt %zu", connectCnt+1);
          break;
        }
        ++connectCnt;
      }while (!srtConn && connectCnt < 5);
      if (!srtConn){
        FAIL_MSG("Failed to connect to '%s'!", config->getString("target").c_str());
      }
      wantRequest = false;
      parseData = true;
      initialize();
    }else{
      // Pull output configuration, In this case we have an srt connection in the second constructor parameter.
      // Handle override / append of streamname options
      std::string sName = srtConn.getStreamName();
      if (allowStreamNameOverride){
        if (sName != ""){
          streamName = sName;
          Util::sanitizeName(streamName);
          Util::setStreamName(streamName);
        }
      }

      int64_t accTypes = config->getInteger("acceptable");
      if (accTypes == 0){//Allow both directions
        srtConn.setBlocking(false);
        //Try to read the socket 10 times. If any reads succeed, assume they are pushing in
        size_t retries = 60;
        while (!accTypes && srtConn && retries){
          size_t recvSize = srtConn.Recv();
          if (recvSize){
            accTypes = 2;
            INFO_MSG("Connection put into ingest mode");
            assembler.assemble(tsIn, srtConn.recvbuf, recvSize, true);
          }else{
            Util::sleep(50);
          }
          --retries;
        }
        //If not, assume they are receiving.
        if (!accTypes){
          accTypes = 1;
          INFO_MSG("Connection put into egress mode");
        }
      }
      if (accTypes == 1){// Only allow outgoing
        srtConn.setBlocking(true);
        srtConn.direction = "output";
        parseData = true;
        wantRequest = false;
        initialize();
      }else if (accTypes == 2){//Only allow incoming
        srtConn.setBlocking(false);
        srtConn.direction = "input";
        if (Triggers::shouldTrigger("PUSH_REWRITE")){
          HTTP::URL reqUrl;
          reqUrl.protocol = "srt";
          reqUrl.port = config->getString("port");
          reqUrl.host = config->getString("interface");
          reqUrl.args = "streamid="+Encodings::URL::encode(sName);
          std::string payload = reqUrl.getUrl() + "\n" + getConnectedHost() + "\n" + streamName;
          std::string newStream = streamName;
          Triggers::doTrigger("PUSH_REWRITE", payload, "", false, newStream);
          if (!newStream.size()){
            FAIL_MSG("Push from %s to URL %s rejected - PUSH_REWRITE trigger blanked the URL",
                     getConnectedHost().c_str(), reqUrl.getUrl().c_str());
            Util::logExitReason(ER_TRIGGER,
                "Push from %s to URL %s rejected - PUSH_REWRITE trigger blanked the URL",
                getConnectedHost().c_str(), reqUrl.getUrl().c_str());
            onFinish();
            return;
          }else{
            streamName = newStream;
            Util::sanitizeName(streamName);
          }
        }
        myConn.setHost(srtConn.remotehost);
        if (!allowPush("")){
          onFinish();
          return;
        }
        if (config->getString("datatrack") == "json"){
          tsIn.setRawDataParser(TS::JSON);
        }
        parseData = false;
        wantRequest = true;
      }

    }
    lastTimeStamp = 0;
    timeStampOffset = 0;
  }

  bool OutTSSRT::onFinish(){
    myConn.close();
    srtConn.close();
    return false;
  }

  OutTSSRT::~OutTSSRT(){}

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


  void OutTSSRT::init(Util::Config *cfg){
    Output::init(cfg);
    capa["name"] = "TSSRT";
    capa["friendly"] = "TS over SRT";
    capa["desc"] = "Real time streaming of TS data over SRT";
    capa["deps"] = "";

    capa["optional"]["streamname"]["name"] = "Stream";
    capa["optional"]["streamname"]["help"] = "What streamname to serve if no streamid is given by the other end of the connection";
    capa["optional"]["streamname"]["type"] = "str";
    capa["optional"]["streamname"]["option"] = "--stream";
    capa["optional"]["streamname"]["short"] = "s";
    capa["optional"]["streamname"]["default"] = "";

    capa["optional"]["filelimit"]["name"] = "Open file descriptor limit";
    capa["optional"]["filelimit"]["help"] = "Increase open file descriptor to this value if current system value is lower. A higher value may be needed for handling many concurrent SRT connections.";

    capa["optional"]["filelimit"]["type"] = "int";
    capa["optional"]["filelimit"]["option"] = "--filelimit";
    capa["optional"]["filelimit"]["short"] = "l";
    capa["optional"]["filelimit"]["default"] = "1024";

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

    capa["codecs"][0u][0u].append("HEVC");
    capa["codecs"][0u][0u].append("H264");
    capa["codecs"][0u][0u].append("MPEG2");
    capa["codecs"][0u][1u].append("AAC");
    capa["codecs"][0u][1u].append("MP3");
    capa["codecs"][0u][1u].append("AC3");
    capa["codecs"][0u][1u].append("MP2");
    capa["codecs"][0u][1u].append("opus");
    capa["codecs"][0u][2u].append("JSON");
    capa["codecs"][1u][0u].append("rawts");
    cfg->addConnectorOptions(8889, capa);
    config = cfg;
    capa["push_urls"].append("srt://*");

    config->addStandardPushCapabilities(capa);
    JSON::Value & pp = capa["push_parameters"];

    pp["mode"]["name"] = "Mode";
    pp["mode"]["help"] = "The connection mode. Can be listener, caller, or rendezvous. By default is listener if the host is missing from the URL, and is caller otherwise.";
    pp["mode"]["type"] = "select";
    pp["mode"]["select"][0u][0u] = "default";
    pp["mode"]["select"][0u][1u] = "Default";
    pp["mode"]["select"][1u][0u] = "listener";
    pp["mode"]["select"][1u][1u] = "Listener";
    pp["mode"]["select"][2u][0u] = "caller";
    pp["mode"]["select"][2u][1u] = "Caller";
    pp["mode"]["select"][3u][0u] = "rendezvous";
    pp["mode"]["select"][3u][1u] = "Rendezvous";
    pp["mode"]["type"] = "select";

    pp["transtype"]["name"] = "Transmission type";
    pp["transtype"]["help"] = "This should be set to live (the default) unless you know what you're doing.";
    pp["transtype"]["type"] = "select";
    pp["transtype"]["select"][0u][0u] = "";
    pp["transtype"]["select"][0u][1u] = "Live";
    pp["transtype"]["select"][1u][0u] = "file";
    pp["transtype"]["select"][1u][1u] = "File";
    pp["transtype"]["type"] = "select";
    
    //addStrOpt(pp, "adapter", "", "");
    //addIntOpt(pp, "timeout", "", "");
    //addIntOpt(pp, "port", "", "");
    addBoolOpt(pp, "tsbpd", "Timestamp-based Packet Delivery mode", "In this mode the packet's time is assigned at the sending time (or allowed to be predefined), transmitted in the packet's header, and then restored on the receiver side so that the time intervals between consecutive packets are preserved when delivering to the application.", true);
    addBoolOpt(pp, "linger", "Linger closed sockets", "Whether to keep closed sockets around for 180 seconds of linger time or not.", true);
    addIntOpt(pp, "maxbw", "Maximum send bandwidth", "Maximum send bandwidth in bytes per second, -1 for infinite, 0 for relative to input bandwidth.", -1);
    addIntOpt(pp, "pbkeylen", "Encryption key length", "May be 0 (auto), 16 (AES-128), 24 (AES-192) or 32 (AES-256).", 0);
    addStrOpt(pp, "passphrase", "Encryption passphrase", "Enables encryption with the given passphrase.");
    addIntOpt(pp, "mss", "Maximum Segment Size", "Maximum size for packets including all headers, in bytes. The default of 1500 is generally the maximum value you can use in most networks.", 1500);
    addIntOpt(pp, "fc", "Flight Flag Size", "Maximum packets that may be 'in flight' without being acknowledged.", 25600);
    addIntOpt(pp, "sndbuf", "Send Buffer Size", "Size of the send buffer, in bytes");
    addIntOpt(pp, "rcvbuf", "Receive Buffer Size", "Size of the receive buffer, in bytes");
    addIntOpt(pp, "ipttl", "TTL", "Time To Live for IPv4 connections or unicast hops for IPv6 connections. Defaults to system default.");
    addIntOpt(pp, "iptos", "Type of Service", "TOS for IPv4 connections or Traffic Class for IPv6 connections. Defaults to system default.");
    addIntOpt(pp, "inputbw", "Input bandwidth", "Estimated bandwidth of data to be sent. Default of 0 means automatic.");
    addIntOpt(pp, "oheadbw", "Recovery Bandwidth Overhead", "Percentage of bandwidth to use for recovery.", 25);
    addIntOpt(pp, "latency", "Latency", "Socket latency, in milliseconds.", 120);
    //addIntOpt(pp, "rcvlatency", "Receive Latency", "Latency in receive mode, in milliseconds", 120);
    //addIntOpt(pp, "peerlatency", "", "");
    addBoolOpt(pp, "tlpktdrop", "Too-late Packet Drop", "Skips packets that cannot (sending) or have not (receiving) been delivered in time", true);
    addIntOpt(pp, "snddropdelay", "Send Drop Delay", "Extra delay before Too-late packet drop on sender side is triggered, in milliseconds.");
    addBoolOpt(pp, "nakreport", "Repeat loss reports", "When enabled, repeats loss reports every time the retransmission timeout has expired.", true);
    addIntOpt(pp, "conntimeo", "Connect timeout", "Milliseconds to wait before timing out a connection attempt for caller and rendezvous modes.", 3000);
    addIntOpt(pp, "lossmaxttl", "Reorder Tolerance", "Maximum amount of packets that may be out of order, or 0 to disable this mechanism.");
    addIntOpt(pp, "minversion", "Minimum SRT version", "Minimum SRT version to require the other side of the connection to support.");
    addStrOpt(pp, "streamid", "Stream ID", "Stream ID to transmit to the other side. MistServer uses this field for the stream name, but the field is entirely free-form and may contain anything.");
    addStrOpt(pp, "congestion", "Congestion controller", "May be set to 'live' or 'file'", "live");
    addBoolOpt(pp, "messageapi", "Message API", "When true, uses the default Message API. When false, uses the Stream API", true);
    //addIntOpt(pp, "kmrefreshrate", "", "");
    //addIntOpt(pp, "kmreannounce", "", "");
    addBoolOpt(pp, "enforcedencryption", "Enforced Encryption", "If enabled, enforces that both sides either set no passphrase, or set the same passphrase. When disabled, falls back to no passphrase if the passphrases do not match.", true);
    addIntOpt(pp, "peeridletimeo", "Peer Idle Timeout", "Time to wait, in milliseconds, before the connection is considered broken if the peer does not respond.", 5000);
    addStrOpt(pp, "packetfilter", "Packet Filter", "Sets the SRT packet filter string, see SRT library documentation for details.");

    JSON::Value opt;
    opt["arg"] = "string";
    opt["default"] = "";
    opt["arg_num"] = 1;
    opt["help"] = "Target srt:// URL to push out towards.";
    cfg->addOption("target", opt);

    capa["optional"]["datatrack"]["name"] = "MPEG Data track parser";
    capa["optional"]["datatrack"]["help"] = "Which parser to use for data tracks";
    capa["optional"]["datatrack"]["type"] = "select";
    capa["optional"]["datatrack"]["option"] = "--datatrack";
    capa["optional"]["datatrack"]["short"] = "D";
    capa["optional"]["datatrack"]["default"] = "";
    capa["optional"]["datatrack"]["select"][0u][0u] = "";
    capa["optional"]["datatrack"]["select"][0u][1u] = "None / disabled";
    capa["optional"]["datatrack"]["select"][1u][0u] = "json";
    capa["optional"]["datatrack"]["select"][1u][1u] = "2b size-prepended JSON";

    opt.null();
    opt["long"] = "datatrack";
    opt["short"] = "D";
    opt["arg"] = "string";
    opt["default"] = "";
    opt["help"] = "Which parser to use for data tracks";
    config->addOption("datatrack", opt);
  }

  // Buffers TS packets and sends after 7 are buffered.
  void OutTSSRT::sendTS(const char *tsData, size_t len){
    packetBuffer.append(tsData, len);
    if (packetBuffer.size() >= 1316){//7 whole TS packets
      if (!srtConn){
        if (config->getString("target").size()){
          INFO_MSG("Reconnecting...");
          srtConn.connect(target.host, target.getPort(), "output", targetParams);
          if (!srtConn){Util::sleep(500);}
        }else{
          Util::logExitReason(ER_CLEAN_REMOTE_CLOSE, "SRT connection closed");
          myConn.close();
          parseData = false;
          return;
        }
      }
      if (srtConn){
        srtConn.SendNow(packetBuffer, packetBuffer.size());
        if (!srtConn){
          if (!config->getString("target").size()){
            Util::logExitReason(ER_CLEAN_REMOTE_CLOSE, "SRT connection closed");
            myConn.close();
            parseData = false;
          }
        }
      }
      packetBuffer.assign(0,0);
    }
  }

  void OutTSSRT::requestHandler(){
    size_t recvSize = srtConn.Recv();
    if (!recvSize){
      if (!srtConn){
        myConn.close();
        srtConn.close();
        wantRequest = false;
      }else{
        Util::sleep(50);
      }
      return;
    }
    lastRecv = Util::bootSecs();
    if (!assembler.assemble(tsIn, srtConn.recvbuf, recvSize, true)){return;}
    while (tsIn.hasPacket()){
      tsIn.getEarliestPacket(thisPacket);
      if (!thisPacket){
        INFO_MSG("Could not get TS packet");
        myConn.close();
        srtConn.close();
        wantRequest = false;
        return;
      }

      // Reconnect to meta if needed, restart push if needed
      meta.reloadReplacedPagesIfNeeded();
      if (!meta && !allowPush("")){
        onFinish();
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
      if (!lastTimeStamp){meta.setBootMsOffset(Util::bootMS() - adjustTime);}
      lastTimeStamp = adjustTime;
      thisPacket.setTime(adjustTime);
      bufferLivePacket(thisPacket);
    }
  }

  bool OutTSSRT::dropPushTrack(uint32_t trackId, const std::string & dropReason){
    Util::logExitReason(ER_SHM_LOST, "track dropped by buffer");
    myConn.close();
    srtConn.close();
    return Output::dropPushTrack(trackId, dropReason);
  }

  void OutTSSRT::connStats(uint64_t now, Comms::Connections &statComm){
    if (!srtConn){return;}
    statComm.setUp(srtConn.dataUp());
    statComm.setDown(srtConn.dataDown());
    statComm.setTime(now - srtConn.connTime());
    statComm.setPacketCount(srtConn.packetCount());
    statComm.setPacketLostCount(srtConn.packetLostCount());
    statComm.setPacketRetransmitCount(srtConn.packetRetransmitCount());
  }

}// namespace Mist



Socket::SRTServer server_socket;
static uint64_t sockCount = 0;

void (*oldSignal)(int, siginfo_t *,void *) = 0;
void signal_handler(int signum, siginfo_t *sigInfo, void *ignore){
  server_socket.close();
  if (oldSignal){
    oldSignal(signum, sigInfo, ignore);
  }
}

void handleUSR1(int signum, siginfo_t *sigInfo, void *ignore){
  if (!sockCount){
    INFO_MSG("USR1 received - triggering rolling restart (no connections active)");
    Util::Config::is_restarting = true;
    Util::logExitReason(ER_CLEAN_SIGNAL, "signal USR1, no connections");
    server_socket.close();
    Util::Config::is_active = false;
  }else{
    INFO_MSG("USR1 received - triggering rolling restart when connection count reaches zero");
    Util::Config::is_restarting = true;
    Util::logExitReason(ER_CLEAN_SIGNAL, "signal USR1, after disconnect wait");
  }
}

// Callback for SRT-serving threads
static void callThreadCallbackSRT(void *srtPtr){
  sockCount++;
  Socket::SRTConnection & srtSock = *(Socket::SRTConnection*)srtPtr;
  int fds[2];
  pipe(fds);
  Socket::Connection Sconn(fds[0], fds[1]);
  HIGH_MSG("Started thread for socket %i", srtSock.getSocket());
  mistOut tmp(Sconn,srtSock);
  tmp.run();
  HIGH_MSG("Closing thread for socket %i", srtSock.getSocket());
  Sconn.close();
  srtSock.close();
  delete &srtSock;
  sockCount--;
  if (!sockCount && Util::Config::is_restarting){
    server_socket.close();
    Util::Config::is_active = false;
    INFO_MSG("Last active connection closed; triggering rolling restart now!");
  }
}

int main(int argc, char *argv[]){
  DTSC::trackValidMask = TRACK_VALID_EXT_HUMAN;
  Util::redirectLogsIfNeeded();
  Util::Config conf(argv[0]);
  Util::Config::binaryType = Util::OUTPUT;
  mistOut::init(&conf);
  if (conf.parseArgs(argc, argv)){
    if (conf.getBool("json")){
      mistOut::capa["version"] = PACKAGE_VERSION;
      std::cout << mistOut::capa.toString() << std::endl;
      return -1;
    }
    conf.activate();

    int filelimit = conf.getInteger("filelimit");
    Util::sysSetNrOpenFiles(filelimit);

    std::string target = conf.getString("target");
    if (!mistOut::listenMode() && (!target.size() || Socket::interpretSRTMode(HTTP::URL(target)) != "listener")){
      Socket::Connection S(fileno(stdout), fileno(stdin));
      Socket::SRTConnection tmpSock;
      mistOut tmp(S, tmpSock);
      return tmp.run();
    }
    {
      struct sigaction new_action;
      new_action.sa_sigaction = handleUSR1;
      sigemptyset(&new_action.sa_mask);
      new_action.sa_flags = 0;
      sigaction(SIGUSR1, &new_action, NULL);
    }
    if (target.size()){
      //Force acceptable option to 1 (outgoing only), since this is a push output and we can't accept incoming connections
      conf.getOption("acceptable", true).append((uint64_t)1);
      //Disable overriding streamname with streamid parameter on other side
      allowStreamNameOverride = false;
      HTTP::URL tgt(target);
      std::map<std::string, std::string> arguments;
      HTTP::parseVars(tgt.args, arguments);
      server_socket = Socket::SRTServer(tgt.getPort(), tgt.host, arguments, false, "output");
      conf.getOption("target", true).append("");
    }else{
      std::map<std::string, std::string> arguments;
      server_socket = Socket::SRTServer(conf.getInteger("port"), conf.getString("interface"), arguments, false, "output");
    }
    if (!server_socket.connected()){
      DEVEL_MSG("Failure to open socket");
      return 1;
    }
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
    Comms::defaultCommFlags = COMM_STATUS_NOKILL;
    Util::Procs::socketList.insert(server_socket.getSocket());
    while (conf.is_active && server_socket.connected()){
      Socket::SRTConnection S = server_socket.accept(false, "output");
      if (S.connected()){// check if the new connection is valid
        // spawn a new thread for this connection
        tthread::thread T(callThreadCallbackSRT, (void *)new Socket::SRTConnection(S));
        // detach it, no need to keep track of it anymore
        T.detach();
      }else{
        Util::sleep(10); // sleep 10ms
      }
    }
    Util::Procs::socketList.erase(server_socket.getSocket());
    server_socket.close();
    if (conf.is_restarting){
      INFO_MSG("Reloading input...");
      execvp(argv[0], argv);
      FAIL_MSG("Error reloading: %s", strerror(errno));
    }
  }
  INFO_MSG("Exit reason: %s", Util::exitReason);
  return 0;
}
