#include "output_ts.h"
#include <mist/defines.h>
#include <mist/http_parser.h>
#include <mist/url.h>
#include <mist/triggers.h>
#include <mist/stream.h>

namespace Mist{
  OutTS::OutTS(Socket::Connection &conn) : TSOutput(conn){
    sendRepeatingHeaders = 500; // PAT/PMT every 500ms (DVB spec)
    streamName = config->getString("streamname");
    pushOut = false;
    sendFEC = false;
    wrapRTP = false;
    dropPercentage = 0;
    std::string tracks = config->getString("tracks");
    if (config->getString("target").size()){
      HTTP::URL target(config->getString("target"));
      if (target.protocol != "tsudp" && target.protocol != "tsrtp" && target.protocol != "tstcp"){
        FAIL_MSG("Target %s must begin with tsudp:// or tsrtp:// or tstcp://, aborting", target.getUrl().c_str());
        onFail("Invalid TS target: doesn't start with tsudp:// or tsrtp:// or tstcp://", true);
        return;
      }
      if (!target.getPort()){
        FAIL_MSG("Target %s must contain a port, aborting", target.getUrl().c_str());
        onFail("Invalid TS target: missing port", true);
        return;
      }
      // Wrap TS packets inside an RTP packet
      wrapRTP = false;
      if (target.protocol == "tsrtp"){
        // MP2T payload, no CSRC list and init to sequence number 1, random SSRC and random timestamp
        tsOut = RTP::Packet(33, 1, rand(), rand());
        wrapRTP = true;
      }
      if (wrapRTP && targetParams.count("fec")){
        if (targetParams.at("fec") == "prompeg"){
          uint8_t rows = 8;
          uint8_t columns = 4;
          if (targetParams.count("rows")){
            rows = atoi(targetParams.at("rows").c_str());
          }
          if (targetParams.count("columns")){
            columns = atoi(targetParams.at("columns").c_str());
          }
          if (tsOut.configureFEC(rows, columns)){
            // Send Pro-MPEG FEC columns over port number + 2
            fecColumnSock.SetDestination(target.host, target.getPort() + 2);
            // Send Pro-MPEG FEC rows over port number + 4
            fecRowSock.SetDestination(target.host, target.getPort() + 4);
            sendFEC = true;
          } else {
            WARN_MSG("Failed to configure FEC. Running without forward error correction");
          }
        }else{
          WARN_MSG("Unsupported FEC of name '%s'. Running without forward error correction", targetParams.at("fec").c_str());
        }
      }
      if (targetParams.count("drop")){
        dropPercentage = atoi(targetParams.at("drop").c_str());
      }
      pushOut = true;
      if (target.protocol == "tstcp"){
        pushOut = false;
        myConn.open(target.host, target.getPort(), true);
        if (!myConn){
          disconnect();
          streamName = "";
          userSelect.clear();
          config->is_active = false;
          return;
        }
      }
      udpSize = 7;
      if (targetParams.count("tracks")){tracks = targetParams["tracks"];}
      if (targetParams.count("pkts")){udpSize = atoi(targetParams["pkts"].c_str());}
      packetBuffer.reserve(188 * udpSize);
      if (target.path.size()){
        if (!pushSock.bind(0, target.path)){
          disconnect();
          streamName = "";
          userSelect.clear();
          config->is_active = false;
          return;
        }
      }
      pushSock.SetDestination(target.host, target.getPort());
      myConn.setHost(target.host);
      pushing = false;
    }else{
      //No push target? Check if this is a push input or pull output by waiting for data for 5s
      setBlocking(false);
      size_t ctr = 0;
      while (++ctr <= 50 && !pushing){
        Util::wait(100);
        pushing = conn.spool() || conn.Received().size();
      }
      setBlocking(true);
    }

    //set the correct mode depending on pushing yes/no
    wantRequest = pushing;
    parseData = !pushing;
    if (pushing){
      if (Triggers::shouldTrigger("PUSH_REWRITE")){
        std::string payload = "ts-tcp://" + myConn.getBoundAddress() + ":" + config->getOption("port").asString() + "\n" + getConnectedHost() + "\n" + streamName;
        std::string newStream = streamName;
        Triggers::doTrigger("PUSH_REWRITE", payload, "", false, newStream);
        if (!newStream.size()){
          FAIL_MSG("Push from %s to URL %s rejected - PUSH_REWRITE trigger blanked the URL",
                   getConnectedHost().c_str(), reqUrl.c_str());
          config->is_active = false;
          return;
        }else{
          streamName = newStream;
          Util::sanitizeName(streamName);
          Util::setStreamName(streamName);
        }
      }
      if (!allowPush("")){
        FAIL_MSG("Pushing not allowed");
        config->is_active = false;
        return;
      }
    }
    initialize();

    size_t currTrack = 0;
    bool hasTrack = false;
    // loop over tracks, add any found track IDs to selectedTracks
    if (!pushing && tracks != ""){
      userSelect.clear();
      if (tracks == "passthrough"){
        std::set<size_t> validTracks = getSupportedTracks();
        for (std::set<size_t>::iterator it = validTracks.begin(); it != validTracks.end(); ++it){
          userSelect[*it].reload(streamName, *it);
        }
      }else{
        for (unsigned int i = 0; i < tracks.size(); ++i){
          if (tracks[i] >= '0' && tracks[i] <= '9'){
            currTrack = currTrack * 10 + (tracks[i] - '0');
            hasTrack = true;
          }else{
            if (hasTrack){userSelect[currTrack].reload(streamName, currTrack);}
            currTrack = 0;
            hasTrack = false;
          }
        }
        if (hasTrack){userSelect[currTrack].reload(streamName, currTrack);}
      }
    }
  }

  OutTS::~OutTS(){}

  void OutTS::init(Util::Config *cfg){
    Output::init(cfg);
    capa["name"] = "TS";
    capa["friendly"] = "TS over TCP";
    capa["desc"] = "Real time streaming in MPEG2/TS format over TCP, UDP or RTP";
    capa["deps"] = "";
    capa["required"]["streamname"]["name"] = "Stream";
    capa["required"]["streamname"]["help"] = "What streamname to serve. For multiple streams, add "
                                             "this protocol multiple times using different ports.";
    capa["required"]["streamname"]["type"] = "str";
    capa["required"]["streamname"]["option"] = "--stream";
    capa["required"]["streamname"]["short"] = "s";
    capa["optional"]["tracks"]["name"] = "Tracks";
    capa["optional"]["tracks"]["help"] =
        "The track IDs of the stream that this connector will transmit separated by spaces";
    capa["optional"]["tracks"]["type"] = "str";
    capa["optional"]["tracks"]["option"] = "--tracks";
    capa["optional"]["tracks"]["short"] = "t";
    capa["optional"]["tracks"]["default"] = "";
    capa["codecs"][0u][0u].append("+HEVC");
    capa["codecs"][0u][0u].append("+H264");
    capa["codecs"][0u][0u].append("+MPEG2");
    capa["codecs"][0u][1u].append("+AAC");
    capa["codecs"][0u][1u].append("+MP3");
    capa["codecs"][0u][1u].append("+AC3");
    capa["codecs"][0u][1u].append("+EAC3");
    capa["codecs"][0u][1u].append("+MP2");
    capa["codecs"][0u][1u].append("+opus");
    capa["codecs"][1u][0u].append("rawts");
    cfg->addConnectorOptions(8888, capa);
    config = cfg;
    capa["push_urls"].append("tsudp://*");
    capa["push_urls"].append("tsrtp://*");
    capa["push_urls"].append("tstcp://*");

    JSON::Value opt;
    opt["arg"] = "string";
    opt["default"] = "";
    opt["arg_num"] = 1;
    opt["help"] = "Target tsudp:// or tsrtp:// or tstcp:// URL to push out towards.";
    cfg->addOption("target", opt);
  }

  void OutTS::initialSeek(){
    // Adds passthrough support to the regular initialSeek function
    if (targetParams.count("passthrough")){selectAllTracks();}
    Output::initialSeek();
  }

  void OutTS::sendTS(const char *tsData, size_t len){
    if (pushOut){
      static size_t curFilled = 0;
      if (curFilled == udpSize){
        // in MPEG-TS over RTP mode, wrap TS packets in a RTP header
        if (wrapRTP){
          // Send RTP packet itself
          if (rand() % 100 >= dropPercentage){
            tsOut.sendTS(&pushSock, packetBuffer.c_str(), packetBuffer.size());
            myConn.addUp(tsOut.getHsize() + tsOut.getPayloadSize());
          } else {
            INFO_MSG("Dropping RTP packet in order to simulate packet loss");
            tsOut.sendNoPacket(packetBuffer.size());
          }
          if (sendFEC){
            // Send FEC packet if available
            uint64_t bytesSent = 0;
            tsOut.parseFEC(&fecColumnSock, &fecRowSock, bytesSent, packetBuffer.c_str(), packetBuffer.size());
            myConn.addUp(bytesSent);
          }
        }else{
          pushSock.SendNow(packetBuffer);
          myConn.addUp(packetBuffer.size());
        }
        packetBuffer.clear();
        packetBuffer.reserve(udpSize * len);
        curFilled = 0;
      }
      packetBuffer.append(tsData, len);
      curFilled++;
    }else{
      myConn.SendNow(tsData, len);
      if (!myConn){
        Util::logExitReason(ER_CLEAN_REMOTE_CLOSE, "connection closed by peer");
        config->is_active = false;
      }
    }
  }

  std::string OutTS::getConnectedHost(){
    if (!pushOut) { return Output::getConnectedHost(); }
    std::string hostname;
    uint32_t port;
    pushSock.GetDestination(hostname, port);
    return hostname;
  }
  std::string OutTS::getConnectedBinHost(){
    if (!pushOut) { return Output::getConnectedBinHost(); }
    return pushSock.getBinDestination();
  }

  bool OutTS::listenMode(){return !(config->getString("target").size());}

  void OutTS::onRequest(){
    while (myConn.Received().available(188)){
      std::string spy = myConn.Received().copy(5);
      // check for sync byte, skip to next one if needed
      if (spy[0] != 0x47){
        if (spy[4] == 0x47){
          INSANE_MSG("Stripping time from 192-byte packet");
          myConn.Received().remove(4);
        }else{
          myConn.Received().remove(1);
          MEDIUM_MSG("Lost sync - resyncing...");
          continue;
        }
      }
      if (parseData){
        parseData = false;
        if (Triggers::shouldTrigger("PUSH_REWRITE")){
          std::string payload = "ts-tcp://" + myConn.getBoundAddress() + ":" + config->getOption("port").asString() + "\n" + getConnectedHost() + "\n" + streamName;
          std::string newStream = "";
          Triggers::doTrigger("PUSH_REWRITE", payload, "", false, newStream);
          if (!newStream.size()){
            FAIL_MSG("Push from %s to URL %s rejected - PUSH_REWRITE trigger blanked the URL",
                     getConnectedHost().c_str(), reqUrl.c_str());
            onFinish();
            return;
          }else{
            streamName = newStream;
            Util::sanitizeName(streamName);
            Util::setStreamName(streamName);
          }
        }
        if (!allowPush("")){
          onFinish();
          return;
        }
      }
      // we now know we probably have a packet ready at the next 188 bytes
      // remove from buffer and insert into TS input
      spy = myConn.Received().remove(188);
      tsIn.parse((char *)spy.data(), 0);
      while (tsIn.hasPacketOnEachTrack()){
        tsIn.getEarliestPacket(thisPacket);
        if (!thisPacket){
          FAIL_MSG("Could not getNext TS packet!");
          return;
        }
        size_t idx = M.trackIDToIndex(thisPacket.getTrackId(), getpid());
        if (M.trackIDToIndex(idx == INVALID_TRACK_ID) || !M.getCodec(idx).size()){
          tsIn.initializeMetadata(meta, thisPacket.getTrackId());
        }
        bufferLivePacket(thisPacket);
      }
    }
  }

  std::string OutTS::getStatsName(){
    if (!parseData){
      return "INPUT:" + capa["name"].asStringRef();
    }else{
      return Output::getStatsName();
    }
  }

  bool OutTS::isReadyForPlay(){return true;}

}// namespace Mist
