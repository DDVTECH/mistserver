#include <mist/socket_srt.h>
#include "output_tssrt.h"
#include <mist/defines.h>
#include <mist/http_parser.h>
#include <mist/url.h>
#include <mist/encode.h>
#include <mist/stream.h>
#include <mist/triggers.h>

namespace Mist{
  OutTSSRT::OutTSSRT(Socket::Connection &conn, Socket::SRTConnection & _srtSock) : TSOutput(conn), srtConn(_srtSock){
    // NOTE: conn is useless for SRT, as it uses a different socket type.
    sendRepeatingHeaders = 500; // PAT/PMT every 500ms (DVB spec)
    streamName = config->getString("streamname");
    Util::setStreamName(streamName);
    pushOut = false;
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
      std::map<std::string, std::string> arguments;
      HTTP::parseVars(target.args, arguments);
      for (std::map<std::string, std::string>::iterator it = arguments.begin(); it != arguments.end(); ++it){
        targetParams[it->first] = it->second;
      }
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
      wantRequest = false;
      parseData = true;
      initialize();
    }else{
      // Pull output configuration, In this case we have an srt connection in the second constructor parameter.
      // Handle override / append of streamname options
      std::string sName = srtConn.getStreamName();
      if (sName != ""){
        streamName = sName;
        Util::sanitizeName(streamName);
        Util::setStreamName(streamName);
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
          std::string payload = reqUrl.getUrl() + "\n" + getConnectedHost();
          std::string newUrl = "";
          Triggers::doTrigger("PUSH_REWRITE", payload, "", false, newUrl);
          if (!newUrl.size()){
            FAIL_MSG("Push from %s to URL %s rejected - PUSH_REWRITE trigger blanked the URL",
                     getConnectedHost().c_str(), reqUrl.getUrl().c_str());
            Util::logExitReason(
                "Push from %s to URL %s rejected - PUSH_REWRITE trigger blanked the URL",
                getConnectedHost().c_str(), reqUrl.getUrl().c_str());
            onFinish();
            return;
          }
          reqUrl = HTTP::URL(newUrl);
          if (reqUrl.args.size()){
            std::map<std::string, std::string> args;
            HTTP::parseVars(reqUrl.args, args);
            if (args.count("streamid")){
              streamName = args["streamid"];
              Util::sanitizeName(streamName);
              Util::setStreamName(streamName);
            }
          }
        }
        myConn.setHost(srtConn.remotehost);
        if (!allowPush("")){
          onFinish();
          srtConn.close();
          return;
        }
        parseData = false;
        wantRequest = true;
      }

    }
    lastTimeStamp = 0;
    timeStampOffset = 0;
  }

  OutTSSRT::~OutTSSRT(){}

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
    cfg->addConnectorOptions(8889, capa);
    config = cfg;
    capa["push_urls"].append("srt://*");

    JSON::Value opt;
    opt["arg"] = "string";
    opt["default"] = "";
    opt["arg_num"] = 1;
    opt["help"] = "Target srt:// URL to push out towards.";
    cfg->addOption("target", opt);
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
          Util::logExitReason("SRT connection closed");
          myConn.close();
          parseData = false;
          return;
        }
      }
      if (srtConn){
        srtConn.SendNow(packetBuffer, packetBuffer.size());
        if (!srtConn){
          if (!config->getString("target").size()){
            Util::logExitReason("SRT connection closed");
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

  void OutTSSRT::connStats(uint64_t now, Comms::Statistics &statComm){
    if (!srtConn){return;}
    statComm.setUp(srtConn.dataUp());
    statComm.setDown(srtConn.dataDown());
    statComm.setTime(now - srtConn.connTime());
    statComm.setPacketCount(srtConn.packetCount());
    statComm.setPacketLostCount(srtConn.packetLostCount());
    statComm.setPacketRetransmitCount(srtConn.packetRetransmitCount());
  }

}// namespace Mist
