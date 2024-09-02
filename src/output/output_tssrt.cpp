#include <mist/socket_srt.h>
#include "output_tssrt.h"
#include <mist/defines.h>
#include <mist/http_parser.h>
#include <mist/url.h>
#include <mist/encode.h>
#include <mist/stream.h>
#include <mist/triggers.h>
#include <mist/auth.h>

bool allowStreamNameOverride = true;

namespace Mist{
  OutTSSRT::OutTSSRT(Socket::Connection &conn, Socket::SRTConnection * _srtSock) : TSOutput(conn){
    srtConn = _srtSock;
    // NOTE: conn is useless for SRT, as it uses a different socket type.
    sendRepeatingHeaders = 500; // PAT/PMT every 500ms (DVB spec)
    streamName = config->getString("streamname");
    Util::setStreamName(streamName);
    pushOut = false;
    bootMSOffsetCalculated = false;
    assembler.setLive();
    udpInit = 0;
    // Push output configuration
    if (config->getString("target").size()){
      Socket::SRT::libraryInit();
      target = HTTP::URL(config->getString("target"));
      HTTP::parseVars(target.args, targetParams);
      std::string addData;
      if (targetParams.count("streamid")){addData = targetParams["streamid"];}
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
      size_t connectCnt = 0;
      do{
        if (!srtConn){srtConn = new Socket::SRTConnection();}
        if (srtConn){srtConn->connect(target.host, target.getPort(), "output", targetParams);}
        if (!*srtConn){
          Util::sleep(1000);
        }else{
          INFO_MSG("SRT socket %s on attempt %zu", srtConn->getStateStr(), connectCnt+1);
          break;
        }
        ++connectCnt;
      }while ((!srtConn || !*srtConn) && connectCnt < 5);
      if (!srtConn){
        FAIL_MSG("Failed to connect to '%s'!", config->getString("target").c_str());
      }
      wantRequest = false;
      parseData = true;
    }else{
      // Pull output configuration, In this case we have an srt connection in the second constructor parameter.
      // Handle override / append of streamname options
      std::string sName = srtConn->getStreamName();
      if (allowStreamNameOverride){
        if (sName != ""){
          streamName = sName;
          Util::sanitizeName(streamName);
          Util::setStreamName(streamName);
        }
      }
      myConn.setHost(srtConn->remotehost);

      int64_t accTypes = config->getInteger("acceptable");
      if (accTypes == 0){//Allow both directions
        srtConn->setBlocking(false);
        //Try to read the socket 10 times. If any reads succeed, assume they are pushing in
        size_t retries = 60;
        while (!accTypes && *srtConn && retries){
          size_t recvSize = srtConn->Recv();
          if (recvSize){
            accTypes = 2;
            INFO_MSG("Connection put into ingest mode");
            assembler.assemble(tsIn, srtConn->recvbuf, recvSize, true);
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
        srtConn->setBlocking(true);
        srtConn->direction = "output";
        parseData = true;
        wantRequest = false;
        initialize();
      }else if (accTypes == 2){//Only allow incoming
        srtConn->setBlocking(false);
        srtConn->direction = "input";
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
        if (!streamName.size()){
          Util::logExitReason(ER_FORMAT_SPECIFIC, "Push from %s rejected - there is no stream name set", getConnectedHost().c_str());
          onFinish();
          return;
        }
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
    lastWorked = Util::bootSecs();
    lastTimeStamp = 0;
    timeStampOffset = 0;
  }

  bool OutTSSRT::onFinish(){
    config->is_active = false;
    return false;
  }

  OutTSSRT::~OutTSSRT(){
    if(srtConn){
      srtConn->close();
      delete srtConn;
      srtConn = 0;
    }
    Socket::SRT::libraryCleanup();
  }

  // Override initialSeek to go to last possible position for live streams
  void OutTSSRT::initialSeek(bool dryRun){
    if (!meta){return;}
    meta.removeLimiter();

    uint64_t seekPos = 0;

    std::set<size_t> validTracks = M.getValidTracks();
    if (M.getLive() && validTracks.size()){
      if (userSelect.size()){
        for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end(); it++){
          if (M.trackValid(it->first) && (M.getNowms(it->first) < seekPos || !seekPos)){
            seekPos = meta.getNowms(it->first);
          }
        }
      }else{
        for (std::set<size_t>::iterator it = validTracks.begin(); it != validTracks.end(); it++){
          if (meta.getNowms(*it) < seekPos || !seekPos){seekPos = meta.getNowms(*it);}
        }
      }
    }
    seek(seekPos);
  }

  static void addIntOpt(JSON::Value & pp, const std::string & param, const std::string & name, const std::string & help, size_t def = 0,const std::string unit = ""){
    pp[param]["name"] = name;
    pp[param]["help"] = help;
    pp[param]["type"] = "int";
    pp[param]["default"] = (uint64_t)def;
    if (unit != "") {
      pp[param]["unit"] = unit;
    }
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
    pp[param]["select"][0u][0u] = "";
    pp[param]["select"][0u][1u] = def?"Default (true)":"Default (false)";
    pp[param]["select"][1u][0u] = 0;
    pp[param]["select"][1u][1u] = "False";
    pp[param]["select"][2u][0u] = 1;
    pp[param]["select"][2u][1u] = "True";
    pp[param]["type"] = "select";

  }

  void OutTSSRT::init(Util::Config *cfg){
    Output::init(cfg);
    capa["name"] = "TSSRT";
    capa["friendly"] = "TS over SRT";
    capa["desc"] = "Real time streaming of TS data over SRT";
    capa["deps"] = "";

    capa["incoming_push_url"] = "srt://$host:$port?streamid=$stream";
    capa["url_rel"] = "?streamid=$";
   
    capa["methods"][0u]["handler"] = "srt";
    capa["methods"][0u]["type"] = "srt";
    capa["methods"][0u]["hrn"] = "SRT";
    capa["methods"][0u]["priority"] = 10;

    capa["optional"]["streamname"]["name"] = "Stream";
    capa["optional"]["streamname"]["help"] = "What streamname to serve if no streamid is given by the other end of the connection";
    capa["optional"]["streamname"]["type"] = "str";
    capa["optional"]["streamname"]["option"] = "--stream";
    capa["optional"]["streamname"]["short"] = "s";
    capa["optional"]["streamname"]["default"] = "";

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

    capa["codecs"][0u][0u].append("+HEVC");
    capa["codecs"][0u][0u].append("+H264");
    capa["codecs"][0u][0u].append("+MPEG2");
    capa["codecs"][0u][1u].append("+AAC");
    capa["codecs"][0u][1u].append("+MP3");
    capa["codecs"][0u][1u].append("+AC3");
    capa["codecs"][0u][1u].append("+MP2");
    capa["codecs"][0u][1u].append("+opus");
    capa["codecs"][0u][2u].append("+JSON");
    capa["codecs"][1u][0u].append("rawts");
    cfg->addConnectorOptions(8889, capa);
    capa["optional"]["port"]["name"] = "UDP port";
    capa["optional"]["port"]["help"] = "UDP port to listen on";
    config = cfg;
    capa["push_urls"].append("srt://*");

    config->addStandardPushCapabilities(capa);
    JSON::Value & pp = capa["push_parameters"];

    pp["srtopts_main"]["name"] = "Commonly used SRT options";
    pp["srtopts_main"]["help"] = "Control the SRT connection";
    pp["srtopts_main"]["type"] = "group";
    pp["srtopts_main"]["sort"] = "aaa";
    pp["srtopts_main"]["expand"] = true;

    addIntOpt(pp["srtopts_main"]["options"], "latency", "Latency", "Socket latency, in milliseconds.", 120,"ms");
    addStrOpt(pp["srtopts_main"]["options"], "streamid", "Stream ID", "Stream ID to transmit to the other side. MistServer uses this field for the stream name, but the field is entirely free-form and may contain anything.");
    addStrOpt(pp["srtopts_main"]["options"], "passphrase", "Encryption passphrase", "Enables encryption with the given passphrase.");


    pp["srtopts"]["name"] = "More SRT options";
    pp["srtopts"]["help"] = "Control the SRT connection";
    pp["srtopts"]["type"] = "group";
    pp["srtopts"]["sort"] = "ab";

    pp["srtopts"]["options"]["mode"]["name"] = "Mode";
    pp["srtopts"]["options"]["mode"]["help"] = "The connection mode. Can be listener, caller, or rendezvous. By default is listener if the host is missing from the URL, and is caller otherwise.";
    pp["srtopts"]["options"]["mode"]["type"] = "select";
    pp["srtopts"]["options"]["mode"]["select"][0u][0u] = "";
    pp["srtopts"]["options"]["mode"]["select"][0u][1u] = "Default (listener)";
    pp["srtopts"]["options"]["mode"]["select"][1u][0u] = "listener";
    pp["srtopts"]["options"]["mode"]["select"][1u][1u] = "Listener";
    pp["srtopts"]["options"]["mode"]["select"][2u][0u] = "caller";
    pp["srtopts"]["options"]["mode"]["select"][2u][1u] = "Caller";
    pp["srtopts"]["options"]["mode"]["select"][3u][0u] = "rendezvous";
    pp["srtopts"]["options"]["mode"]["select"][3u][1u] = "Rendezvous";

    pp["srtopts"]["options"]["transtype"]["name"] = "Transmission type";
    pp["srtopts"]["options"]["transtype"]["help"] = "This should be set to live (the default) unless you know what you're doing.";
    pp["srtopts"]["options"]["transtype"]["select"][0u][0u] = "";
    pp["srtopts"]["options"]["transtype"]["select"][0u][1u] = "Default (live)";
    pp["srtopts"]["options"]["transtype"]["select"][1u][0u] = "live";
    pp["srtopts"]["options"]["transtype"]["select"][1u][1u] = "Live";
    pp["srtopts"]["options"]["transtype"]["select"][2u][0u] = "file";
    pp["srtopts"]["options"]["transtype"]["select"][2u][1u] = "File";
    pp["srtopts"]["options"]["transtype"]["type"] = "select";
    
    
    //addStrOpt(pp, "adapter", "", "");
    //addIntOpt(pp, "timeout", "", "");
    //addIntOpt(pp, "port", "", "");
    
    addBoolOpt(pp["srtopts"]["options"], "tsbpd", "Timestamp-based Packet Delivery mode", "In this mode the packet's time is assigned at the sending time (or allowed to be predefined), transmitted in the packet's header, and then restored on the receiver side so that the time intervals between consecutive packets are preserved when delivering to the application.", true);

    addBoolOpt(pp["srtopts"]["options"], "linger", "Linger closed sockets", "Whether to keep closed sockets around for 180 seconds of linger time or not.", true);
    addIntOpt(pp["srtopts"]["options"], "maxbw", "Maximum send bandwidth", "Maximum send bandwidth in bytes per second, -1 for infinite, 0 for relative to input bandwidth.", -1,"bytes/s");


    pp["srtopts"]["options"]["pbkeylen"]["name"] = "Encryption key length";
    pp["srtopts"]["options"]["pbkeylen"]["help"] = "The encryption key length. Default: auto.";
    pp["srtopts"]["options"]["pbkeylen"]["select"][0u][0u] = "";
    pp["srtopts"]["options"]["pbkeylen"]["select"][0u][1u] = "Default (auto)";
    pp["srtopts"]["options"]["pbkeylen"]["select"][1u][0u] = "0";
    pp["srtopts"]["options"]["pbkeylen"]["select"][1u][1u] = "Auto";
    pp["srtopts"]["options"]["pbkeylen"]["select"][2u][0u] = "16";
    pp["srtopts"]["options"]["pbkeylen"]["select"][2u][1u] = "AES-128";
    pp["srtopts"]["options"]["pbkeylen"]["select"][3u][0u] = "24";
    pp["srtopts"]["options"]["pbkeylen"]["select"][3u][1u] = "AES-192";
    pp["srtopts"]["options"]["pbkeylen"]["select"][4u][0u] = "32";
    pp["srtopts"]["options"]["pbkeylen"]["select"][4u][1u] = "AES-256";
    pp["srtopts"]["options"]["pbkeylen"]["default"] = "0"; 
    pp["srtopts"]["options"]["pbkeylen"]["type"] = "select";



    addIntOpt(pp["srtopts"]["options"], "mss", "Maximum Segment Size", "Maximum size for packets including all headers, in bytes. The default of 1500 is generally the maximum value you can use in most networks.", 1500,"bytes");
    addIntOpt(pp["srtopts"]["options"], "fc", "Flight Flag Size", "Maximum packets that may be 'in flight' without being acknowledged.", 25600,"packets");
    addIntOpt(pp["srtopts"]["options"], "sndbuf", "Send Buffer Size", "Size of the send buffer, in bytes",0,"bytes");
    addIntOpt(pp["srtopts"]["options"], "rcvbuf", "Receive Buffer Size", "Size of the receive buffer, in bytes",0,"bytes");
    addIntOpt(pp["srtopts"]["options"], "ipttl", "TTL", "Time To Live for IPv4 connections or unicast hops for IPv6 connections. Defaults to system default.",0,"hops");
    addIntOpt(pp["srtopts"]["options"], "iptos", "Type of Service", "TOS for IPv4 connections or Traffic Class for IPv6 connections. Defaults to system default.");
    addIntOpt(pp["srtopts"]["options"], "inputbw", "Input bandwidth", "Estimated bandwidth of data to be sent. Default of 0 means automatic.",0,"bytes/s");
    addIntOpt(pp["srtopts"]["options"], "oheadbw", "Recovery Bandwidth Overhead", "Percentage of bandwidth to use for recovery.",25,"%");
    //addIntOpt(pp, "rcvlatency", "Receive Latency", "Latency in receive mode, in milliseconds", 120);
    //addIntOpt(pp, "peerlatency", "", "");
    addBoolOpt(pp["srtopts"]["options"], "tlpktdrop", "Too-late Packet Drop", "Skips packets that cannot (sending) or have not (receiving) been delivered in time", true);
    addIntOpt(pp["srtopts"]["options"], "snddropdelay", "Send Drop Delay", "Extra delay before Too-late packet drop on sender side is triggered, in milliseconds.",0,"ms");
    addBoolOpt(pp["srtopts"]["options"], "nakreport", "Repeat loss reports", "When enabled, repeats loss reports every time the retransmission timeout has expired.", true);
    addIntOpt(pp["srtopts"]["options"], "conntimeo", "Connect timeout", "Milliseconds to wait before timing out a connection attempt for caller and rendezvous modes.", 3000,"ms");
    addIntOpt(pp["srtopts"]["options"], "lossmaxttl", "Reorder Tolerance", "Maximum amount of packets that may be out of order, or 0 to disable this mechanism.",0,"packets");
    addIntOpt(pp["srtopts"]["options"], "minversion", "Minimum SRT version", "Minimum SRT version to require the other side of the connection to support.");

    addStrOpt(pp["srtopts"]["options"], "congestion", "Congestion controller", "May be set to 'live' or 'file'", "live");
    pp["srtopts"]["options"]["congestion"]["select"][0u][0u] = "";
    pp["srtopts"]["options"]["congestion"]["select"][0u][1u] = "Default (live)";
    pp["srtopts"]["options"]["congestion"]["select"][1u][0u] = "live";
    pp["srtopts"]["options"]["congestion"]["select"][1u][1u] = "Live";
    pp["srtopts"]["options"]["congestion"]["select"][2u][0u] = "file";
    pp["srtopts"]["options"]["congestion"]["select"][2u][1u] = "File";
    pp["srtopts"]["options"]["congestion"]["type"] = "select";

    addBoolOpt(pp["srtopts"]["options"], "messageapi", "Message API", "When true, uses the default Message API. When false, uses the Stream API", true);
    //addIntOpt(pp, "kmrefreshrate", "", "");
    //addIntOpt(pp, "kmreannounce", "", "");
    addBoolOpt(pp["srtopts"]["options"], "enforcedencryption", "Enforced Encryption", "If enabled, enforces that both sides either set no passphrase, or set the same passphrase. When disabled, falls back to no passphrase if the passphrases do not match.", true);
    addIntOpt(pp["srtopts"]["options"], "peeridletimeo", "Peer Idle Timeout", "Time to wait, in milliseconds, before the connection is considered broken if the peer does not respond.", 5000,"ms");
    addStrOpt(pp["srtopts"]["options"], "packetfilter", "Packet Filter", "Sets the SRT packet filter string, see SRT library documentation for details.");

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

    capa["optional"]["passphrase"]["name"] = "Passphrase";
    capa["optional"]["passphrase"]["help"] = "If set, requires a SRT passphrase to connect";
    capa["optional"]["passphrase"]["type"] = "string";
    capa["optional"]["passphrase"]["option"] = "--passphrase";
    capa["optional"]["passphrase"]["short"] = "P";
    capa["optional"]["passphrase"]["default"] = "";

    opt.null();
    opt["long"] = "passphrase";
    opt["short"] = "P";
    opt["arg"] = "string";
    opt["default"] = "";
    opt["help"] = "If set, requires a SRT passphrase to connect";
    config->addOption("passphrase", opt);

    capa["optional"]["sockopts"]["name"] = "SRT socket options";
    capa["optional"]["sockopts"]["help"] = "Any additional SRT socket options to apply";
    capa["optional"]["sockopts"]["type"] = "string";
    capa["optional"]["sockopts"]["option"] = "--sockopts";
    capa["optional"]["sockopts"]["short"] = "O";
    capa["optional"]["sockopts"]["default"] = "";

    opt.null();
    opt["long"] = "sockopts";
    opt["short"] = "O";
    opt["arg"] = "string";
    opt["default"] = "";
    opt["help"] = "Any additional SRT socket options to apply";
    config->addOption("sockopts", opt);


  }

  // Buffers TS packets and sends after 7 are buffered.
  void OutTSSRT::sendTS(const char *tsData, size_t len){
    packetBuffer.append(tsData, len);
    if (packetBuffer.size() >= 1316){//7 whole TS packets
      if (!*srtConn){
        if (config->getString("target").size()){
          if (lastWorked + 5 < Util::bootSecs()){
            Util::logExitReason(ER_CLEAN_REMOTE_CLOSE, "SRT connection closed, no reconnect success after 5s");
            config->is_active = false;
            parseData = false;
            return;
          }
          INFO_MSG("Reconnecting...");
          if (srtConn){
            srtConn->close();
            delete srtConn;
          }
          if (udpInit){
            srtConn = new Socket::SRTConnection(*udpInit, "rendezvous", targetParams);
          }else{
            srtConn = new Socket::SRTConnection();
          }
          srtConn->connect(target.host, target.getPort(), "output", targetParams);
          if (!*srtConn){Util::sleep(500);}
        }else{
          Util::logExitReason(ER_CLEAN_REMOTE_CLOSE, "SRT connection closed (mid-send)");
          config->is_active = false;
          parseData = false;
          return;
        }
      }
      if (*srtConn){
        srtConn->SendNow(packetBuffer, packetBuffer.size());
        if (!*srtConn){
          if (!config->getString("target").size()){
            Util::logExitReason(ER_CLEAN_REMOTE_CLOSE, "SRT connection closed (post-send)");
            config->is_active = false;
            parseData = false;
          }
        }else{
          lastWorked = Util::bootSecs();
        }
      }
      packetBuffer.assign(0,0);
    }
  }

  void OutTSSRT::requestHandler(){
    size_t recvSize = srtConn->Recv();
    if (!recvSize){
      if (!*srtConn){
        Util::logExitReason(ER_CLEAN_REMOTE_CLOSE, "SRT connection %s (in request handler)", srtConn->getStateStr());
        config->is_active = false;
        srtConn->close();
        wantRequest = false;
      }else{
        Util::sleep(50);
      }
      return;
    }
    lastRecv = Util::bootSecs();
    if (!assembler.assemble(tsIn, srtConn->recvbuf, recvSize, true)){return;}
    while (tsIn.hasPacket()){
      tsIn.getEarliestPacket(thisPacket);
      if (!thisPacket){
        Util::logExitReason(ER_FORMAT_SPECIFIC, "Could not get TS packet");
        config->is_active = false;
        srtConn->close();
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
        userSelect[thisIdx].reload(streamName, thisIdx, COMM_STATUS_ACTIVE | COMM_STATUS_SOURCE | COMM_STATUS_DONOTTRACK);
      }

      uint64_t pktTimeWithOffset = thisPacket.getTime() + timeStampOffset;
      if (lastTimeStamp || timeStampOffset){
        uint64_t targetTime = Util::bootMS() - M.getBootMsOffset();
        if (targetTime + 5000 < pktTimeWithOffset || targetTime > pktTimeWithOffset + 5000){
          INFO_MSG("Timestamp jump " PRETTY_PRINT_MSTIME " -> " PRETTY_PRINT_MSTIME ", compensating.",
                  PRETTY_ARG_MSTIME(targetTime), PRETTY_ARG_MSTIME(pktTimeWithOffset));
          timeStampOffset += (targetTime - pktTimeWithOffset);
          pktTimeWithOffset = thisPacket.getTime() + timeStampOffset;
        }
      }
      if (!bootMSOffsetCalculated){
        meta.setBootMsOffset(Util::bootMS() - pktTimeWithOffset);
        bootMSOffsetCalculated = true;
      }
      lastTimeStamp = pktTimeWithOffset;
      thisPacket.setTime(pktTimeWithOffset);
      thisTime = pktTimeWithOffset;
      bufferLivePacket(thisPacket);
    }
  }

  bool OutTSSRT::dropPushTrack(uint32_t trackId, const std::string & dropReason){
    Util::logExitReason(ER_SHM_LOST, "track dropped by buffer");
    config->is_active = false;
    if (srtConn){srtConn->close();}
    return Output::dropPushTrack(trackId, dropReason);
  }

  void OutTSSRT::connStats(uint64_t now, Comms::Connections &statComm){
    if (!srtConn || !*srtConn){return;}
    statComm.setUp(srtConn->dataUp());
    statComm.setDown(srtConn->dataDown());
    statComm.setTime(now - srtConn->connTime());
    statComm.setPacketCount(srtConn->packetCount());
    statComm.setPacketLostCount(srtConn->packetLostCount());
    statComm.setPacketRetransmitCount(srtConn->packetRetransmitCount());
  }


  bool OutTSSRT::listenMode(){
    std::string tgt = config->getString("target");
    return (!tgt.size() || (tgt.size() >= 6 && tgt.substr(0, 6) == "srt://" && Socket::interpretSRTMode(HTTP::URL(tgt)) == "listener"));
  }

  void initSRTConnection(Socket::UDPConnection & s, std::map<std::string, std::string> & arguments, bool listener = true){
    Socket::SRT::libraryInit();
    Socket::SRTConnection * tmpSock = new Socket::SRTConnection(s, listener?"output":"rendezvous", arguments);
    if (!*tmpSock){
      delete tmpSock;
      return;
    }
    if (!listener){
      std::string host;
      uint32_t port;
      s.GetDestination(host, port);
      tmpSock->connect(host, port, "output", arguments);
      INFO_MSG("UDP to SRT socket conversion: %s", tmpSock->getStateStr());
    }
    Socket::Connection S(1, 0);
    mistOut tmp(S, tmpSock);
    tmp.run();
  }

  void OutTSSRT::listener(Util::Config &conf, int (*callback)(Socket::Connection &S)){
    // Check SRT options/arguments first
    std::string target = conf.getString("target");
    std::map<std::string, std::string> arguments;
    if (target.size()){
      //Force acceptable option to 1 (outgoing only), since this is a push output and we can't accept incoming connections
      conf.getOption("acceptable", true).append((uint64_t)1);
      //Disable overriding streamname with streamid parameter on other side
      allowStreamNameOverride = false;
      HTTP::URL tgt(target);
      HTTP::parseVars(tgt.args, arguments);
      conf.getOption("interface", true).append(tgt.host);
      conf.getOption("port", true).append((uint64_t)tgt.getPort());
      conf.getOption("target", true).append("");
    }else{
      HTTP::parseVars(conf.getString("sockopts"), arguments);
      std::string opt = conf.getString("passphrase");
      if (opt.size()){arguments["passphrase"] = opt;}
    }

    uint16_t localPort;
    // Either re-use socket 0 or bind a new socket
    Socket::UDPConnection udpSrv;
    if (Socket::checkTrueSocket(0)){
      udpSrv.assimilate(0);
      localPort = udpSrv.getBoundPort();
    }else{
      localPort = udpSrv.bind(conf.getInteger("port"), conf.getString("interface"));
    }
    // Ensure socket zero is now us
    if (udpSrv.getSock()){
      int oldSock = udpSrv.getSock();
      if (!dup2(oldSock, 0)){
        udpSrv.assimilate(0);
      }
    }
    if (!udpSrv){
      Util::logExitReason(ER_READ_START_FAILURE, "Failure to open listening socket");
      conf.is_active = false;
      return;
    }
    Util::Config::setServerFD(0);
    udpSrv.allocateDestination();

    Util::Procs::socketList.insert(udpSrv.getSock());
    int maxFD = udpSrv.getSock();
    while (conf.is_active && udpSrv){

      fd_set rfds;
      FD_ZERO(&rfds);
      FD_SET(maxFD, &rfds);

      struct timeval T;
      T.tv_sec = 2;
      T.tv_usec = 0;
      int r = select(maxFD + 1, &rfds, NULL, NULL, &T);
      if (r){
        while(udpSrv.Receive()){
          // Ignore if it's not an SRT handshake packet
          if (udpSrv.data.size() >= 4 && udpSrv.data[0] == 0x80 && !udpSrv.data[1] && !udpSrv.data[2] && !udpSrv.data[3]){
            bool rendezvous = false;
            if (udpSrv.data.size() >= 40){
              rendezvous = (!udpSrv.data[36] && !udpSrv.data[37] && !udpSrv.data[38] && !udpSrv.data[39]);
            }
            std::string remoteIP, localIP;
            uint32_t remotePort, localPort;
            udpSrv.GetDestination(remoteIP, remotePort);
            udpSrv.GetLocalDestination(localIP, localPort);
            INFO_MSG("SRT handshake from %s:%" PRIu32 "! Spawning child process to handle it...", remoteIP.c_str(), remotePort);
            if (!fork()){
              Socket::UDPConnection s(udpSrv);
              udpSrv.close();
              if (!s.connect()){return;}
              return initSRTConnection(s, arguments, !rendezvous);
            }
          }
        }
      }
    }
  }

}// namespace Mist

