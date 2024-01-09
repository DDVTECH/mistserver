#include "output_sdp.h"
#include "lib/defines.h"
#include "mist/defines.h"
#include "src/output/output.h"
#include <fstream>
#include <sys/socket.h>

namespace Mist{
  Socket::Connection *mainConn = 0;

  OutSDP::OutSDP(Socket::Connection &conn) : HTTPOutput(conn){
    mainConn = &conn;
    exitOnNoRTCP = false;
  }

  /// Function used to send RTP packets over UDP
  ///\param socket A UDP Connection pointer, sent as a void*, to keep portability.
  ///\param data The RTP Packet that needs to be sent
  ///\param len The size of data
  ///\param channel Not used here, but is kept for compatibility with sendTCP
  void sendUDP(void *socket, const char *data, size_t len, uint8_t){
    ((Socket::UDPConnection *)socket)->SendNow(data, len);
    if (mainConn){mainConn->addUp(len);}
  }

  /// \brief Initializes the SDP state
  /// \param  port: Each track will have a data and RTCP port.
  ///                    These are consecutive and start at startPort
  /// \param  targetIP: (Multicast supported) IP address we are pushing the stream towards
  void OutSDP::initTracks(uint32_t & port, std::string targetIP){
    for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end(); ++it){
      SDP::Track newTrk;
      newTrk.setPackCodec(&M, it->first);
      if (!newTrk.prepareSockets(targetIP, port)){
        FAIL_MSG("Could not bind required UDP sockets. Aborting push...");
        parseData = false;
        wantRequest = true;
        return;
      }
      // If a starting port was set, increment by 2 for the next track
      if (port){
        port+=2;
      }
      sdpState.tracks.insert(std::pair<size_t,SDP::Track>(it->first, newTrk));
    }
  }
  
  void OutSDP::sendHeader(){
    if (isRecording()){
      std::string targetIP;
      if (targetParams["targetIP"].size()){
        targetIP = targetParams["targetIP"];
      } else {
        targetIP = "234.234.234.234";
      }
      // If not set, default to random ports we can bind
      uint32_t port = 0;
      if (targetParams["startPort"].size()){
        port = atoll(targetParams["startPort"].c_str());
      }
      // Add meta tracks to sdpState
      initTracks(port, targetIP);
      myConn.SendNow(generateSDP(targetIP, streamName));
      INFO_MSG("Pushing %zu tracks towards target address '%s'.", sdpState.tracks.size(), targetIP.c_str());
    }
    sentHeader = true;
  }

  /// \brief Generates an SDP file which clients can use to connect to the stream
  ///        initTracks should be called before this function is called, to make sure we have the right ports
  /// \param targetAddress: the (multicast supported) target, where we will push the stream towards
  /// \param streamName: the name of the stream we are serving
  std::string OutSDP::generateSDP(std::string targetAddress, std::string streamName){
    prevRTCP = Util::bootSecs();
    std::stringstream sdpAsString;
    sdpAsString.precision(3);
    // Add session description & time description
    sdpAsString << std::fixed <<
      "v=0\r\n"
      "o=- " << Util::getMS() << " 1 IN IP4 " << targetAddress << "\r\n"
      "s=" << streamName << "\r\n"
      "i=" << streamName << "\r\n"
      "c=IN IP4 " << targetAddress << "\r\n"
      "t=0 0\r\n"
      "a=tool:" APPIDENT "\r\n"
      "a=recvonly\r\n"
      "a=type:broadcast\r\n";
    // Add Media description
    for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end(); ++it){
      DONTEVEN_MSG("Track '%zu' should be pushing data towards UDP port '%" PRIu32 "'", it->first, sdpState.tracks[it->first].portA);
      sdpAsString << SDP::mediaDescription(&M, it->first, sdpState.tracks.at(it->first).portA);
    }
    return sdpAsString.str();
  }

  void OutSDP::init(Util::Config *cfg){
    HTTPOutput::init(cfg);
    capa["name"] = "SDP";
    capa["friendly"] = "RTP streams using SDP";
    capa["desc"] = "Make streams available as a RTP stream, using the SDP";
    capa["url_rel"] = "/$.sdp";
    capa["url_match"] = "/$.sdp";
    capa["codecs"][0u][0u].append("+H264");
    capa["codecs"][0u][1u].append("+HEVC");
    capa["codecs"][0u][2u].append("+MPEG2");
    capa["codecs"][0u][3u].append("+VP8");
    capa["codecs"][0u][4u].append("+VP9");
    capa["codecs"][0u][5u].append("+AAC");
    capa["codecs"][0u][6u].append("+MP3");
    capa["codecs"][0u][7u].append("+AC3");
    capa["codecs"][0u][8u].append("+ALAW");
    capa["codecs"][0u][9u].append("+ULAW");
    capa["codecs"][0u][10u].append("+PCM");
    capa["codecs"][0u][11u].append("+opus");
    capa["codecs"][0u][12u].append("+MP2");

    capa["methods"][0u]["handler"] = "http";
    capa["methods"][0u]["type"] = "html5/application/sdp";
    capa["methods"][0u]["url_rel"] = "/$.sdp";
    capa["methods"][0u]["priority"] = 11;

    config->addStandardPushCapabilities(capa);
    capa["push_urls"].append("/*.sdp");

    JSON::Value opt;
    opt["arg"] = "string";
    opt["default"] = "";
    opt["arg_num"] = 1;
    opt["help"] = "Target filename to store SDP file as, or - for stdout.";
    cfg->addOption("target", opt);

  }

  OutSDP::~OutSDP(){
    // Remove written SDP file, if it was set as the output target
    if (isRecording()){
      HTTP::URL target(config->getString("target"));
      if(target.getExt() == "sdp"){
        INFO_MSG("Removing .SDP file located at '%s'", target.getFilePath().c_str());
        std::remove(target.getFilePath().c_str());
      }
    }
  }

  std::string OutSDP::getConnectedHost(){
    if (!sdpState.tracks.size()) { return Output::getConnectedHost(); }
    std::string hostname;
    uint32_t port;
    sdpState.tracks[0].data.GetDestination(hostname, port);
    return hostname;
  }
  std::string OutSDP::getConnectedBinHost(){
    if (!sdpState.tracks.size()) { return Output::getConnectedBinHost(); }
    return sdpState.tracks[0].data.getBinDestination();
  }

  void OutSDP::sendNext(){
    char *dataPointer = 0;
    size_t dataLen = 0;
    thisPacket.getString("data", dataPointer, dataLen);
    uint64_t timestamp = thisPacket.getTime();

    void *socket = 0;
    void (*callBack)(void *, const char *, size_t, uint8_t) = 0;

    // Get data socket and send RTCP
    if (sdpState.tracks[thisIdx].channel == -1){
      socket = &sdpState.tracks[thisIdx].data;
      callBack = sendUDP;
      if (Util::bootSecs() != sdpState.tracks[thisIdx].rtcpSent){
        sdpState.tracks[thisIdx].pack.setTimestamp(timestamp * SDP::getMultiplier(&M, thisIdx));
        sdpState.tracks[thisIdx].rtcpSent = Util::bootSecs();
        sdpState.tracks[thisIdx].pack.sendRTCP_SR(&sdpState.tracks[thisIdx].rtcp, sdpState.tracks[thisIdx].channel, sendUDP);
      }
    }else{
      FAIL_MSG("RTP SDP output does not support TCP. No data will be sent to the target address");
      return;
    }
    uint64_t offset = thisPacket.getInt("offset");
    sdpState.tracks[thisIdx].pack.setTimestamp((timestamp + offset) * SDP::getMultiplier(&M, thisIdx));
    sdpState.tracks[thisIdx].pack.sendData(socket, callBack, dataPointer, dataLen,
                                           sdpState.tracks[thisIdx].channel, meta.getCodec(thisIdx));
    
    // Update last RTCP received variable
    if (exitOnNoRTCP){
      checkForRTCP(thisIdx);
      // Exit if no RTCP packets received for 30 seconds
      if (Util::bootSecs() - prevRTCP > 30){
        parseData = false;
        wantRequest = true;
        WARN_MSG("Shutting down because we have not received RTCP receiver reports for 30 seconds.");
      }
    }
  }

  /// Reads RTCP packets sent back by viewers
  void OutSDP::checkForRTCP(uint64_t thisIdx){
    Socket::UDPConnection &socket = sdpState.tracks[thisIdx].rtcp;
    while (socket.Receive()){
      prevRTCP = Util::bootSecs();
      INFO_MSG("received RTCP packet '%u'", prevRTCP);
      if (myConn){
        myConn.addDown(sdpState.tracks[thisIdx].rtcp.data.size());
      };
      RTP::Packet pack(sdpState.tracks[thisIdx].rtcp.data, sdpState.tracks[thisIdx].rtcp.data.size());
    }
  }

  void OutSDP::onHTTP(){
    std::string targetIP;
    uint32_t port;

    // Init latest RTCP packet time on current time
    prevRTCP = Util::bootSecs();
    if (myConn.getHost().size()){
      targetIP = myConn.getHost();
    } else {
      targetIP = "234.234.234.234";
    }
    // If not set, default to random ports we can bind
    if (targetParams["startPort"].size()){
      port = atoll(targetParams["startPort"].c_str());
    } else {
      port = 0;
    }
    // When we start an RTP stream via a HTTP request, close it when we no longer receive RTCP packets
    exitOnNoRTCP = true;

    // Add meta tracks to sdpState
    initTracks(port, targetIP);  
    INFO_MSG("Pushing %zu tracks towards target address '%s')", sdpState.tracks.size(), targetIP.c_str());

    // Send SDP file back
    H.setCORSHeaders();
    H.SetBody(generateSDP(targetIP, streamName));
    H.SendResponse("200", "OK", myConn);
    H.CleanPreserveHeaders();
    // No more requests needed. Keep alive until we are no longer receiving RTCP packets
    parseData = true;
    wantRequest = false;
  }

  /// Disconnects the user
  bool OutSDP::onFinish(){
    INFO_MSG("Shutting down...");
    if (myConn){myConn.close();}
    return false;
  }
}// namespace Mist
