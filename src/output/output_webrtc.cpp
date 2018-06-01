#include <ifaddrs.h>    // ifaddr, listing ip addresses.
#include <netdb.h>      // ifaddr, listing ip addresses.
#include <mist/timing.h>
#include "output_webrtc.h"

namespace Mist{

  OutWebRTC *classPointer = 0;

  /* ------------------------------------------------ */

  static uint32_t generateSSRC();
  static void webRTCInputOutputThreadFunc(void* arg);
  static int  onDTLSHandshakeWantsToWriteCallback(const uint8_t* data, int* nbytes);
  static void onDTSCConverterHasPacketCallback(const DTSC::Packet& pkt);
  static void onDTSCConverterHasInitDataCallback(const uint64_t track, const std::string &initData);
  static void onRTPSorterHasPacketCallback(const uint64_t track, const RTP::Packet &p); // when we receive RTP packets we store them in a sorter. Whenever there is a valid, sorted RTP packet that can be used this function is called. 
  static void onRTPPacketizerHasDataCallback(void* socket, char* data, unsigned int len, unsigned int channel);
  static void onRTPPacketizerHasRTCPDataCallback(void* socket, const char* data, uint32_t nbytes);
  static std::vector<std::string> getLocalIP4Addresses(); 

  /* ------------------------------------------------ */

  WebRTCTrack::WebRTCTrack()
    :payloadType(0)
    ,SSRC(0)
    ,timestampMultiplier(0)
    ,ULPFECPayloadType(0)
    ,REDPayloadType(0)
    ,RTXPayloadType(0)
    ,prevReceivedSequenceNumber(0)
  {
  }

  /* ------------------------------------------------ */

  OutWebRTC::OutWebRTC(Socket::Connection &myConn) : HTTPOutput(myConn){

    webRTCInputOutputThread = NULL;
    udpPort = 0;
    SSRC = generateSSRC();
    rtcpTimeoutInMillis = 0;
    rtcpKeyFrameDelayInMillis = 2000;
    rtcpKeyFrameTimeoutInMillis = 0;
    rtpOutBuffer = NULL;
    videoBitrate = 6 * 1000 * 1000;
    RTP::MAX_SEND = 1200 - 28;
    didReceiveKeyFrame = false;
    
    if (cert.init("NL", "webrtc", "webrtc") != 0) {
      FAIL_MSG("Failed to create the certificate.");
      exit(EXIT_FAILURE);
      // \todo how do we handle this further? disconnect?
    }
    if (dtlsHandshake.init(&cert.cert, &cert.key, onDTLSHandshakeWantsToWriteCallback) != 0) {
      FAIL_MSG("Failed to initialize the dtls-srtp handshake helper.");
      exit(EXIT_FAILURE);
      // \todo how do we handle this? 
    }

    rtpOutBuffer = (char*)malloc(2048);
    if (!rtpOutBuffer) {
      // \todo Jaron how do you handle these cases?
      FAIL_MSG("Failed to allocate our RTP output buffer.");
      exit(EXIT_FAILURE);
    }
    
    sdpAnswer.setFingerprint(cert.getFingerprintSha256());
    classPointer = this;
    
    setBlocking(false);
  }

  OutWebRTC::~OutWebRTC() {

    if (webRTCInputOutputThread && webRTCInputOutputThread->joinable()) {
      webRTCInputOutputThread->join();
      delete webRTCInputOutputThread;
      webRTCInputOutputThread = NULL;
    }

    if (rtpOutBuffer) {
      free(rtpOutBuffer);
      rtpOutBuffer = NULL;
    }

    if (srtpReader.shutdown() != 0) {
      FAIL_MSG("Failed to cleanly shutdown the srtp reader.");
    }
    if (srtpWriter.shutdown() != 0) {
      FAIL_MSG("Failed to cleanly shutdown the srtp writer.");
    }
    if (dtlsHandshake.shutdown() != 0) {
      FAIL_MSG("Failed to cleanly shutdown the dtls handshake.");
    }
    if (cert.shutdown() != 0) {
      FAIL_MSG("Failed to cleanly shutdown the certificate.");
    }
  }

  // Initialize the WebRTC output. This is where we define what
  // codes types are supported and accepted when parsing the SDP
  // offer or when generating the SDP. The `capa` member is
  // inherited from `Output`.
  void OutWebRTC::init(Util::Config *cfg) {
    HTTPOutput::init(cfg);
    capa["name"] = "WebRTC";
    capa["desc"] = "Provides WebRTC output";
    capa["url_rel"] = "/webrtc/$";
    capa["url_match"] = "/webrtc/$"; 
    capa["codecs"][0u][0u].append("H264");
    capa["codecs"][0u][0u].append("VP8");
    capa["codecs"][0u][1u].append("opus");
    capa["methods"][0u]["handler"] = "webrtc";
    capa["methods"][0u]["type"] = "webrtc";
    capa["methods"][0u]["priority"] = 2ll;

    capa["optional"]["preferredvideocodec"]["name"]    = "Preferred video codecs";
    capa["optional"]["preferredvideocodec"]["help"]    = "Comma separated list of video codecs you want to support in preferred order. e.g. H264,VP8";
    capa["optional"]["preferredvideocodec"]["default"] = "H264,VP8";
    capa["optional"]["preferredvideocodec"]["type"]    = "string";
    capa["optional"]["preferredvideocodec"]["option"]  = "--webrtc-video-codecs";
    capa["optional"]["preferredvideocodec"]["short"]   = "V";

    capa["optional"]["preferredaudiocodec"]["name"]    = "Preferred audio codecs";
    capa["optional"]["preferredaudiocodec"]["help"]    = "Comma separated list of audio codecs you want to support in preferred order. e.g. OPUS";
    capa["optional"]["preferredaudiocodec"]["default"] = "OPUS";
    capa["optional"]["preferredaudiocodec"]["type"]    = "string";
    capa["optional"]["preferredaudiocodec"]["option"]  = "--webrtc-audio-codecs";
    capa["optional"]["preferredaudiocodec"]["short"]   = "A";

    config->addOptionsFromCapabilities(capa);
  }

  // This function is executed when we receive a signaling data.
  // The signaling data contains commands that are used to start
  // an input or output stream. 
  void OutWebRTC::onWebsocketFrame() {

    if (!webSock) {
      FAIL_MSG("We assume `webSock` is valid at this point.");
      return;
    }

    if (webSock->frameType == 0x8) {
      HIGH_MSG("Not handling websocket data; close frame.");
      return;
    }

    JSON::Value msg = JSON::fromString(webSock->data, webSock->data.size());
    handleSignalingCommand(*webSock, msg);
  }
  
  // This function is our first handler for commands that we
  // receive via the WebRTC signaling channel (our websocket
  // connection). We validate the command and check what
  // command-specific function we need to call.
  void OutWebRTC::handleSignalingCommand(HTTP::Websocket& ws, const JSON::Value &command) {

    JSON::Value commandResult;
    if (false == validateSignalingCommand(ws, command, commandResult)) {
      return;
    }

    if (command["type"] == "offer_sdp") {
      if (!handleSignalingCommandRemoteOffer(ws, command)) {
        sendSignalingError(ws, "on_answer_sdp", "Failed to handle the offer SDP.");
      }
    }
    else if (command["type"] == "video_bitrate") {
      if (!handleSignalingCommandVideoBitrate(ws, command)) {
        sendSignalingError(ws, "on_video_bitrate", "Failed to handle the video bitrate change request.");
      }
    }
    else if (command["type"] == "seek") {
      if (!handleSignalingCommandSeek(ws, command)) {
        sendSignalingError(ws, "on_seek", "Failed to handle the seek request.");
      }
    }
    else if (command["type"] == "stop") {
      INFO_MSG("Received stop() command.");
      myConn.close();
      return;
    }
    else if (command["type"] == "keyframe_interval") {
      if (!handleSignalingCommandKeyFrameInterval(ws, command)) {
        sendSignalingError(ws, "on_keyframe_interval", "Failed to set the keyframe interval.");
      }
    }
    else {
      FAIL_MSG("Unhandled signal command %s.", command["type"].asString().c_str());
    }
  }

  /// This function will check if the received command contains
  /// the required fields. All commands need the `type`
  /// field. When the `type` requires a `data` element we check
  /// that too. When this function returns `true` you can assume
  /// that it can be processed. In case of an error we return
  /// false and send an error back to the other peer.
  bool OutWebRTC::validateSignalingCommand(HTTP::Websocket& ws, const JSON::Value &command, JSON::Value& errorResult) {

    if (!command.isMember("type")) {
      sendSignalingError(ws, "error", "Received an command but not type property was given.");
      return false;
    }

    /* seek command */
    if (command["type"] == "seek") {
      if (!command.isMember("seek_time")) {
        sendSignalingError(ws, "on_seek", "Received a seek request but no `seek_time` property.");
        return false;
      }
    }

    /* offer command */
    if (command["type"] == "offer_sdp") {
      if (!command.isMember("offer_sdp")) {
        sendSignalingError(ws, "on_offer_sdp", "A `offer_sdp` command needs the offer SDP in the `offer_sdp` field.");
        return false;
      }
      if (command["offer_sdp"].asString() == "") {
        sendSignalingError(ws, "on_offer_sdp", "The given `offer_sdp` field is empty.");
        return false;
      }
    }

    /* video bitrate */
    if (command["type"] == "video_bitrate") {
      if (!command.isMember("video_bitrate")) {
        sendSignalingError(ws, "on_video_bitrate", "No video_bitrate attribute found.");
        return false;
      }
    }

    /* keyframe interval */
    if (command["type"] == "keyframe_interval") {
      if (!command.isMember("keyframe_interval_millis")) {
        sendSignalingError(ws, "on_keyframe_interval", "No keyframe_interval_millis attribute found.");
        return false;
      }
    }

    /* when we arrive here everything is fine and validated. */
    return true;
  }

  void OutWebRTC::sendSignalingError(HTTP::Websocket& ws,
                                     const std::string& commandType,
                                     const std::string& errorMessage)
  {
    JSON::Value commandResult;
    commandResult["type"] = commandType;
    commandResult["result"] = false;
    commandResult["message"] = errorMessage;
    ws.sendFrame(commandResult.toString());
  }
  
  bool OutWebRTC::handleSignalingCommandRemoteOffer(HTTP::Websocket &ws, const JSON::Value &command) {

    // get video and supported video formats from offer. 
    SDP::Session sdpParser;
    std::string sdpOffer = command["offer_sdp"].asString();
    if (!sdpParser.parseSDP(sdpOffer)) {
      FAIL_MSG("Failed to parse the remote offer sdp.");
      return false;
    }

    // when the SDP offer contains a `a=recvonly` we expect that
    // the other peer wants to receive media from us.
    if (sdpParser.hasReceiveOnlyMedia()) {
      return handleSignalingCommandRemoteOfferForOutput(ws, sdpParser, sdpOffer);
    }
    else {
      return handleSignalingCommandRemoteOfferForInput(ws, sdpParser, sdpOffer);
    }

    return false;
  }

  // This function is called for a peer that wants to receive
  // data from us. First we update our capabilities by checking
  // what codecs the peer and we support. After updating the
  // codecs we `initialize()` and use `selectDefaultTracks()` to
  // pick the right tracks based on our supported (and updated)
  // capabilities.
  bool OutWebRTC::handleSignalingCommandRemoteOfferForOutput(HTTP::Websocket &ws, SDP::Session &sdpSession, const std::string& sdpOffer) {

    updateCapabilitiesWithSDPOffer(sdpSession);
    initialize();
    selectDefaultTracks();

    // \todo I'm not sure if this is the nicest location to bind the socket but it's necessary when we create our answer SDP
    if (0 == udpPort) {
      bindUDPSocketOnLocalCandidateAddress(0);
    }
    
    // get codecs from selected stream which are used to create our SDP answer.
    int32_t dtscVideoTrackID = -1;
    int32_t dtscAudioTrackID = -1;
    std::string videoCodec;
    std::string audioCodec;
    std::map<uint32_t, DTSC::Track>::iterator it = myMeta.tracks.begin();
    while (it != myMeta.tracks.end()) {
      DTSC::Track& Trk = it->second;
      if (Trk.type == "video") {
        videoCodec = Trk.codec;
        dtscVideoTrackID = Trk.trackID;
      }
      else if (Trk.type == "audio") {
        audioCodec = Trk.codec;
        dtscAudioTrackID = Trk.trackID;
      }
      ++it;
    }
    
    // parse offer SDP and setup the answer SDP using the selected codecs.
    if (!sdpAnswer.parseOffer(sdpOffer)) {
      FAIL_MSG("Failed to parse the received offer SDP");
      FAIL_MSG("%s", sdpOffer.c_str());
      return false;
    }
    sdpAnswer.setDirection("sendonly");

    // setup video WebRTC Track.
    if (!videoCodec.empty()) {
      if (sdpAnswer.enableVideo(videoCodec)) {
        WebRTCTrack videoTrack;
        if (!createWebRTCTrackFromAnswer(sdpAnswer.answerVideoMedia, sdpAnswer.answerVideoFormat, videoTrack)) {
          FAIL_MSG("Failed to create the WebRTCTrack for the selected video.");
          return false;
        }
        videoTrack.rtpPacketizer = RTP::Packet(videoTrack.payloadType, rand(), 0, videoTrack.SSRC, 0);
        videoTrack.timestampMultiplier = 90;
        webrtcTracks[dtscVideoTrackID] = videoTrack;
      }
    }

    // setup audio WebRTC Track
    if (!audioCodec.empty()) {

      // @todo maybe, create a isAudioSupported() function (?)
      if (sdpAnswer.enableAudio(audioCodec)) {
        WebRTCTrack audioTrack;
        if (!createWebRTCTrackFromAnswer(sdpAnswer.answerAudioMedia, sdpAnswer.answerAudioFormat, audioTrack)) {
          FAIL_MSG("Failed to create the WebRTCTrack for the selected audio.");
          return false;
        }
        audioTrack.rtpPacketizer = RTP::Packet(audioTrack.payloadType, rand(), 0, audioTrack.SSRC, 0);
        audioTrack.timestampMultiplier = 48;
        webrtcTracks[dtscAudioTrackID] = audioTrack;
      }
    }

    // this is necessary so that we can get the remote IP when creating STUN replies.
    udp.SetDestination("0.0.0.0", 4444);
    
    // create result message.
    JSON::Value commandResult;
    commandResult["type"] = "on_answer_sdp";
    commandResult["result"] = true;
    commandResult["answer_sdp"] = sdpAnswer.toString();
    ws.sendFrame(commandResult.toString());

    // we set parseData to `true` to start the data flow. Is also
    // used to break out of our loop in `onHTTP()`.
    parseData = true;

    return true;
  }

  // When the receive a command with a `type` attribute set to
  // `video_bitrate` we will extract the bitrate and use it for
  // our REMB messages that are sent as soon as an connection has
  // been established. REMB messages are used from server to
  // client to define the preferred bitrate. 
  bool OutWebRTC::handleSignalingCommandVideoBitrate(HTTP::Websocket &ws, const JSON::Value &command) {

    videoBitrate = command["video_bitrate"].asInt();
    if (videoBitrate == 0) {
      FAIL_MSG("We received an invalid video_bitrate; resetting to default.");
      videoBitrate = 6 * 1000 * 1000;
      return false;
    }

    JSON::Value commandResult;
    commandResult["type"] = "on_video_bitrate";
    commandResult["result"] = true;
    commandResult["video_bitrate"] = videoBitrate;
    ws.sendFrame(commandResult.toString());

    return true;
  }

  bool OutWebRTC::handleSignalingCommandSeek(HTTP::Websocket& ws, const JSON::Value &command) {

    uint64_t seek_time = command["seek_time"].asInt();
    seek(seek_time);

    JSON::Value commandResult;
    commandResult["type"] = "on_seek";
    commandResult["result"] = true;
    ws.sendFrame(commandResult.toString());

    return true;
  }

  bool OutWebRTC::handleSignalingCommandKeyFrameInterval(HTTP::Websocket &ws, const JSON::Value &command) {
    
    rtcpKeyFrameDelayInMillis = command["keyframe_interval_millis"].asInt();
    if (rtcpKeyFrameDelayInMillis < 500) {
      WARN_MSG("Requested a keyframe delay < 500ms; 500ms is the minimum you can set.");
      rtcpKeyFrameDelayInMillis = 500;
    }
    
    rtcpKeyFrameTimeoutInMillis = Util::getMS() + rtcpKeyFrameDelayInMillis;

    JSON::Value commandResult;
    commandResult["type"] = "on_keyframe_interval";
    commandResult["result"] = true;
    ws.sendFrame(commandResult.toString());

    return true;
  }

  // Creates a WebRTCTrack for the given `SDP::Media` and
  // `SDP::MediaFormat`. The `SDP::MediaFormat` must contain the
  // `icePwd` and `iceUFrag` which are used when we're handling
  // STUN messages (these can be used to verify the integrity).
  //
  // When the `SDP::Media` has it's SSRC set (not 0) we copy it.
  // This is the case when we're receiving data from another peer
  // and the `WebRTCTrack` is used to handle input data. When the
  // SSRC is 0 we generate a new one and we assume that the other
  // peer expect us to send data.
  bool OutWebRTC::createWebRTCTrackFromAnswer(const SDP::Media& mediaAnswer,
                                              const SDP::MediaFormat& formatAnswer,
                                              WebRTCTrack& result)
  {
    if (formatAnswer.payloadType == SDP_PAYLOAD_TYPE_NONE) {
      FAIL_MSG("Cannot create a WebRTCTrack, the given SDP::MediaFormat has no `payloadType` set.");
      return false;
    }

    if (formatAnswer.icePwd.empty()) {
      FAIL_MSG("Cannot create a WebRTCTrack, the given SDP::MediaFormat has no `icePwd` set.");
      return false;
    }

    if (formatAnswer.iceUFrag.empty()) {
      FAIL_MSG("Cannot create a WebRTCTrack, the given SDP::MediaFormat has no `iceUFrag` set.");
      return false;
    }

    result.payloadType = formatAnswer.getPayloadType();
    result.localIcePwd = formatAnswer.icePwd;
    result.localIceUFrag = formatAnswer.iceUFrag;

    if (mediaAnswer.SSRC != 0) {
      result.SSRC = mediaAnswer.SSRC;
    }
    else {
      result.SSRC = rand();
    }

    return true;
  }

  // This function checks what capabilities the offer has and
  // updates the `capa[codecs]` member with the codecs that we
  // and the offer supports. Note that the names in the preferred
  // codec array below should use the codec names as MistServer
  // defines them. SDP internally uses fullcaps.
  //
  // Jaron advised me to use this approach: when we receive an
  // offer we update the capabilities with the matching codecs
  // and once we've updated those we call `selectDefaultTracks()`
  // which sets up the tracks in `myMeta`. 
  void OutWebRTC::updateCapabilitiesWithSDPOffer(SDP::Session &sdpSession) {
    
    capa["codecs"].null();

    const char* videoCodecPreference[] = { "H264", "VP8", NULL } ;
    const char** videoCodec = videoCodecPreference;
    SDP::Media* videoMediaOffer = sdpSession.getMediaForType("video");
    if (videoMediaOffer) {
      while (*videoCodec) {
        if (sdpSession.getMediaFormatByEncodingName("video", *videoCodec)) {
          capa["codecs"][0u][0u].append(*videoCodec);
        }
        videoCodec++;
      }
    }

    const char* audioCodecPreference[] = { "opus", NULL } ;
    const char** audioCodec = audioCodecPreference;
    SDP::Media* audioMediaOffer = sdpSession.getMediaForType("audio");
    if (audioMediaOffer) {
      while (*audioCodec) {
        if (sdpSession.getMediaFormatByEncodingName("audio", *audioCodec)) {
          capa["codecs"][0u][1u].append(*audioCodec);
        }
        audioCodec++;
      }
    }
  }

  // This function is called to handle an offer from a peer that wants to push data towards us.
  bool OutWebRTC::handleSignalingCommandRemoteOfferForInput(HTTP::Websocket &webSock, SDP::Session &sdpSession, const std::string& sdpOffer) {

    if (webRTCInputOutputThread != NULL) {
      FAIL_MSG("It seems that we're already have a webrtc i/o thread running.");
      return false;
    }

    if (0 == udpPort) {
      bindUDPSocketOnLocalCandidateAddress(0);
    }

    if (!sdpAnswer.parseOffer(sdpOffer)) {
      FAIL_MSG("Failed to parse the received offer SDP");
      FAIL_MSG("%s", sdpOffer.c_str());
      return false;
    }

    std::string prefVideoCodec = "VP8,H264";
    if (config && config->hasOption("preferredvideocodec")) {
      prefVideoCodec = config->getString("preferredvideocodec");
      if (prefVideoCodec.empty()) {
        WARN_MSG("No preferred video codec value set; resetting to default.");
        prefVideoCodec = "VP8,H264";
      }
    }

    std::string prefAudioCodec = "OPUS";
    if (config && config->hasOption("preferredaudiocodec")) {
      prefAudioCodec = config->getString("preferredaudiocodec");
      if (prefAudioCodec.empty()) {
        WARN_MSG("No preferred audio codec value set; resetting to default.");
        prefAudioCodec = "OPUS";
      }
    }

    // video 
    if (sdpAnswer.enableVideo(prefVideoCodec)) {

      WebRTCTrack videoTrack;
      videoTrack.payloadType = sdpAnswer.answerVideoFormat.getPayloadType();
      videoTrack.localIcePwd = sdpAnswer.answerVideoFormat.icePwd;
      videoTrack.localIceUFrag = sdpAnswer.answerVideoFormat.iceUFrag;
      videoTrack.SSRC = sdpAnswer.answerVideoMedia.SSRC;

      SDP::MediaFormat* fmtRED  = sdpSession.getMediaFormatByEncodingName("video", "RED");
      SDP::MediaFormat* fmtULPFEC = sdpSession.getMediaFormatByEncodingName("video", "ULPFEC");
      if (fmtRED && fmtULPFEC) {
        videoTrack.ULPFECPayloadType = fmtULPFEC->payloadType;
        videoTrack.REDPayloadType = fmtRED->payloadType;
        payloadTypeToWebRTCTrack[fmtRED->payloadType] = videoTrack.payloadType;
      }
      sdpAnswer.videoLossPrevention = SDP_LOSS_PREVENTION_NACK;
      videoTrack.sorter.tmpVideoLossPrevention = sdpAnswer.videoLossPrevention;

      DTSC::Track dtscVideo;
      if (!sdpAnswer.setupVideoDTSCTrack(dtscVideo)) {
        FAIL_MSG("Failed to setup video DTSC track.");
        return false;
      }

      videoTrack.rtpToDTSC.setProperties(dtscVideo);
      videoTrack.rtpToDTSC.setCallbacks(onDTSCConverterHasPacketCallback, onDTSCConverterHasInitDataCallback);
      videoTrack.sorter.setCallback(videoTrack.payloadType, onRTPSorterHasPacketCallback);

      webrtcTracks[videoTrack.payloadType] = videoTrack;
      myMeta.tracks[dtscVideo.trackID] = dtscVideo;
    }

    // audio setup
    if (sdpAnswer.enableAudio(prefAudioCodec)) {

      WebRTCTrack audioTrack;
      audioTrack.payloadType = sdpAnswer.answerAudioFormat.getPayloadType();
      audioTrack.localIcePwd = sdpAnswer.answerAudioFormat.icePwd;
      audioTrack.localIceUFrag = sdpAnswer.answerAudioFormat.iceUFrag;
      audioTrack.SSRC = sdpAnswer.answerAudioMedia.SSRC;

      DTSC::Track dtscAudio;
      if (!sdpAnswer.setupAudioDTSCTrack(dtscAudio)) {
        FAIL_MSG("Failed to setup audio DTSC track.");
      }

      audioTrack.rtpToDTSC.setProperties(dtscAudio);
      audioTrack.rtpToDTSC.setCallbacks(onDTSCConverterHasPacketCallback, onDTSCConverterHasInitDataCallback);
      audioTrack.sorter.setCallback(audioTrack.payloadType, onRTPSorterHasPacketCallback);

      webrtcTracks[audioTrack.payloadType] = audioTrack;
      myMeta.tracks[dtscAudio.trackID] = dtscAudio;
    }

    sdpAnswer.setDirection("recvonly");

    // allow peer to push video/audio
    if (!allowPush("")) {
      FAIL_MSG("Failed to allow push for stream %s.", streamName.c_str());
      /* \todo when I try to send a error message back to the browser it fails; probably because socket gets closed (?). */
      return false;
    }

    // start our receive thread (handles STUN, DTLS, RTP input)
    webRTCInputOutputThread = new tthread::thread(webRTCInputOutputThreadFunc, NULL);
    rtcpTimeoutInMillis = Util::getMS() + 2000;
    rtcpKeyFrameTimeoutInMillis = Util::getMS() + 2000;
    
    // create result command for websock client.
    JSON::Value commandResult;
    commandResult["type"] = "on_answer_sdp";
    commandResult["result"] = true;
    commandResult["answer_sdp"] = sdpAnswer.toString();
    webSock.sendFrame(commandResult.toString());

    return true;
  }

  // Get the IP address on which we should bind our UDP socket that is
  // used to receive the STUN, DTLS, SRTP, etc.. 
  std::string OutWebRTC::getLocalCandidateAddress() {
    
    std::vector<std::string> localIP4Addresses = getLocalIP4Addresses();
    if (localIP4Addresses.size() == 0) {
      FAIL_MSG("No IP4 addresses found.");
      return "";
    }

    return localIP4Addresses[0];
  }

  bool OutWebRTC::bindUDPSocketOnLocalCandidateAddress(uint16_t port) {

    if (udpPort != 0) {
      FAIL_MSG("Already bound the UDP socket.");
      return false;
    }
    
    std::string local_ip = getLocalCandidateAddress();
    if (local_ip.empty()) {
      FAIL_MSG("Failed to get the local candidate address. W/o ICE will probably fail or will have issues during startup. See https://gist.github.com/roxlu/6c5ab696840256dac71b6247bab59ce9");
    }
    
    udpPort = udp.bind(port, local_ip);
    sdpAnswer.setCandidate(local_ip, udpPort);

    return true;
  }

  /* ------------------------------------------------ */

  // This function is called from the `webRTCInputOutputThreadFunc()`
  // function. The `webRTCInputOutputThreadFunc()` is basically empty
  // and all work for the thread is done here.
  void OutWebRTC::handleWebRTCInputOutputFromThread() {
    udp.SetDestination("0.0.0.0", 4444);
    while (keepGoing()) {
      if (!handleWebRTCInputOutput()) {
        Util::sleep(100);
      }
    }
  }

  // Checks if there is data on our UDP socket. The data can be
  // STUN, DTLS, SRTP or SRTCP. When we're receiving media from
  // the browser (e.g. from webcam) this function is called from
  // a separate thread. When we're pushing media to the browser
  // this is called from the main thread.
  bool OutWebRTC::handleWebRTCInputOutput() {

    if (!udp.Receive()) {
      DONTEVEN_MSG("Waiting for data...");
      return false;
    }

    myConn.addDown(udp.data_len);
      
    uint8_t fb = (uint8_t)udp.data[0];
    
    if (fb > 127 && fb < 192) {
      handleReceivedRTPOrRTCPPacket();
    }
    else if (fb > 19 && fb < 64) {
      handleReceivedDTLSPacket();
    }
    else if (fb < 2) {
      handleReceivedSTUNPacket();
    }
    else {
      FAIL_MSG("Unhandled WebRTC data. Type: %02X", fb);
    }

    return true;
  }
  
  void OutWebRTC::handleReceivedSTUNPacket() {

    size_t nparsed = 0;
    StunMessage stun_msg;
    if (stunReader.parse((uint8_t*)udp.data, udp.data_len, nparsed, stun_msg) != 0) {
      FAIL_MSG("Failed to parse a stun message.");
      return;
    }
    
    if(stun_msg.type != STUN_MSG_TYPE_BINDING_REQUEST) {
      INFO_MSG("We only handle STUN binding requests as we're an ice-lite implementation.");
      return;
    }
    
    // get the username for whom we got a binding request.
    StunAttribute* usernameAttrib = stun_msg.getAttributeByType(STUN_ATTR_TYPE_USERNAME);
    if (!usernameAttrib) {
      ERROR_MSG("No username attribute found in the STUN binding request. Cannot create success binding response.");
      return;
    }
    if (usernameAttrib->username.value == 0) {
      ERROR_MSG("The username attribute is empty.");
      return;
    }
    std::string username(usernameAttrib->username.value, usernameAttrib->length);
    std::size_t usernameColonPos = username.find(":");
    if (usernameColonPos == std::string::npos) {
      ERROR_MSG("The username in the STUN attribute has an invalid format: %s.", username.c_str());
      return;
    }
    std::string usernameLocal = username.substr(0, usernameColonPos);

    // get the password for the username that is used to create our message-integrity.
    std::string passwordLocal;
    std::map<uint64_t, WebRTCTrack>::iterator rtcTrackIt = webrtcTracks.begin();
    while (rtcTrackIt != webrtcTracks.end()) {
      WebRTCTrack& tr = rtcTrackIt->second;
      if (tr.localIceUFrag == usernameLocal) {
        passwordLocal = tr.localIcePwd;
      }
      ++rtcTrackIt;
    }
    if (passwordLocal.empty()) {
      ERROR_MSG("No local ICE password found for username %s. Did you create a WebRTCTrack?", usernameLocal.c_str());
      return;
    }
    
    std::string remoteIP = "";
    uint32_t remotePort = 0;
    udp.GetDestination(remoteIP, remotePort);

    // create the binding success response
    stun_msg.removeAttributes();
    stun_msg.setType(STUN_MSG_TYPE_BINDING_RESPONSE_SUCCESS);
    
    StunWriter stun_writer;
    stun_writer.begin(stun_msg);
    stun_writer.writeXorMappedAddress(STUN_IP4, remotePort, remoteIP);
    stun_writer.writeMessageIntegrity(passwordLocal);
    stun_writer.writeFingerprint();
    stun_writer.end();

    udp.SendNow((const char*)stun_writer.getBufferPtr(), stun_writer.getBufferSize());
    myConn.addUp(stun_writer.getBufferSize());
  }

  void OutWebRTC::handleReceivedDTLSPacket() {

    if (dtlsHandshake.hasKeyingMaterial()) {
      DONTEVEN_MSG("Not feeding data into the handshake .. already done.");
      return;
    }
      
    if (dtlsHandshake.parse((const uint8_t*)udp.data, udp.data_len) != 0) {
      FAIL_MSG("Failed to parse a DTLS packet.");
      return;
    }

    if (!dtlsHandshake.hasKeyingMaterial()) {
      return;
    }

    if (srtpReader.init(dtlsHandshake.cipher, dtlsHandshake.remote_key, dtlsHandshake.remote_salt) != 0) {
      FAIL_MSG("Failed to initialize the SRTP reader.");
      return;
    }

    if (srtpWriter.init(dtlsHandshake.cipher, dtlsHandshake.local_key, dtlsHandshake.local_salt) != 0) {
      FAIL_MSG("Failed to initialize the SRTP writer.");
      return;
    }
  }

  void OutWebRTC::handleReceivedRTPOrRTCPPacket() {

    uint8_t pt = udp.data[1] & 0x7F;
    
    if ((pt < 64) || (pt >= 96)) {

      int len = (int)udp.data_len;
      if (srtpReader.unprotectRtp((uint8_t*)udp.data, &len) != 0) {
        FAIL_MSG("Failed to unprotect a RTP packet.");
        return;
      }

      RTP::Packet rtp_pkt((const char*)udp.data, (unsigned int)len);
      uint8_t payloadType = rtp_pkt.getPayloadType();
      uint64_t rtcTrackID = payloadType;
      
      // Do we need to map the payload type to a WebRTC Track? (e.g. RED)
      if (payloadTypeToWebRTCTrack.count(payloadType) != 0) {
        rtcTrackID = payloadTypeToWebRTCTrack[payloadType];
      }

      if (webrtcTracks.count(rtcTrackID) == 0) {
        FAIL_MSG("Received an RTP packet for a track that we didn't prepare for. PayloadType is %llu", rtp_pkt.getPayloadType());
        // \todo @jaron should we close the socket here?
        return;
      }

      // Here follows a very rudimentary algo for requesting lost
      // packets; I guess after some experimentation a better
      // algorithm should be used; this is used to trigger NACKs.
      WebRTCTrack& rtcTrack = webrtcTracks[rtcTrackID];
      uint16_t expectedSeqNum = rtcTrack.prevReceivedSequenceNumber + 1;
      uint16_t currSeqNum = rtp_pkt.getSequence();
      if (rtcTrack.prevReceivedSequenceNumber != 0
          && (rtcTrack.prevReceivedSequenceNumber + 1) != currSeqNum)
        {
          while (rtcTrack.prevReceivedSequenceNumber < currSeqNum) {
            FAIL_MSG("=> nack seqnum: %u", rtcTrack.prevReceivedSequenceNumber);
            sendRTCPFeedbackNACK(rtcTrack, rtcTrack.prevReceivedSequenceNumber);
            rtcTrack.prevReceivedSequenceNumber++;
          }
        }
      
      rtcTrack.prevReceivedSequenceNumber = rtp_pkt.getSequence();
      
      if (payloadType == rtcTrack.REDPayloadType) {
        rtcTrack.sorter.addREDPacket(udp.data, len, rtcTrack.payloadType, rtcTrack.REDPayloadType, rtcTrack.ULPFECPayloadType);
      }
      else {
        rtcTrack.sorter.addPacket(rtp_pkt);
      }
    }
    else if ((pt >= 64) && (pt < 96)) {

#if 0
      // \todo it seems that we don't need handle RTCP messages.
      int len = udp.data_len;
      if (srtpReader.unprotectRtcp((uint8_t*)udp.data, &len) != 0) {
        FAIL_MSG("Failed to unprotect RTCP.");
        return;
      }
#endif
      
    }
    else {
      FAIL_MSG("Unknown payload type: %u", pt);
    }
  }

  /* ------------------------------------------------ */

  int OutWebRTC::onDTLSHandshakeWantsToWrite(const uint8_t* data, int* nbytes) {
    // \todo udp.SendNow() does not return an error code so we assume everything is sent nicely.
    udp.SendNow((const char*)data, (size_t)*nbytes);
    myConn.addUp(*nbytes);
    return 0;
  }
  
  void OutWebRTC::onDTSCConverterHasPacket(const DTSC::Packet& pkt) {

    // extract meta data (init data, width/height, etc);
    uint64_t trackID = pkt.getTrackId();
    DTSC::Track& DTSCTrack = myMeta.tracks[trackID];
    if (DTSCTrack.codec == "H264") {
      if (DTSCTrack.init.empty()) {
        FAIL_MSG("No init data found for trackID %llu (note: we use payloadType as trackID)", trackID);
        return;
      }
    }
    else if (DTSCTrack.codec == "VP8") {
      if (pkt.getFlag("keyframe")) {
        extractFrameSizeFromVP8KeyFrame(pkt);
      }
    }

    // create rtcp packet (set bitrate and request keyframe).
    if (DTSCTrack.codec == "H264" || DTSCTrack.codec == "VP8") {
      uint64_t now = Util::getMS();
      
      if (now >= rtcpTimeoutInMillis) {
        WebRTCTrack& rtcTrack = webrtcTracks[trackID];
        sendRTCPFeedbackREMB(rtcTrack);
        sendRTCPFeedbackRR(rtcTrack);
        rtcpTimeoutInMillis = now + 1000;  /* @todo was 5000, lowered for FEC */
      }
      
      if (now >= rtcpKeyFrameTimeoutInMillis) {
        WebRTCTrack& rtcTrack = webrtcTracks[trackID];
        sendRTCPFeedbackPLI(rtcTrack);
        rtcpKeyFrameTimeoutInMillis = now + rtcpKeyFrameDelayInMillis;
      }
    }

    bufferLivePacket(pkt);
  }

  void OutWebRTC::onDTSCConverterHasInitData(const uint64_t trackID, const std::string &initData) {
    
    if (webrtcTracks.count(trackID) == 0) {
      ERROR_MSG("Recieved init data for a track that we don't manager. TrackID/PayloadType: %llu", trackID);
      return;
    }
    
    MP4::AVCC avccbox;
    avccbox.setPayload(initData);
    if (avccbox.getSPSLen() == 0 || avccbox.getPPSLen() == 0) {
      WARN_MSG("Received init data, but partially. SPS nbytes: %u, PPS nbytes: %u.", avccbox.getSPSLen(), avccbox.getPPSLen());
      return;
    }

    h264::sequenceParameterSet sps(avccbox.getSPS(), avccbox.getSPSLen());
    h264::SPSMeta hMeta = sps.getCharacteristics();
    DTSC::Track& Trk = myMeta.tracks[trackID];
    Trk.width = hMeta.width;
    Trk.height = hMeta.height;
    Trk.fpks = hMeta.fps * 1000;
   
    avccbox.multiplyPPS(57);//Inject all possible PPS packets into init
    myMeta.tracks[trackID].init = std::string(avccbox.payload(), avccbox.payloadSize());
  }

  void OutWebRTC::onRTPSorterHasPacket(const uint64_t trackID, const RTP::Packet &pkt) {

    if (webrtcTracks.count(trackID) == 0) {
      ERROR_MSG("Received a sorted RTP packet for track %llu but we don't manage this track.", trackID);
      return;
    }

    webrtcTracks[trackID].rtpToDTSC.addRTP(pkt);
  }

  // \todo when rtpOutputBuffer is allocated on the stack (I
  // created a member with 2048 as its size and when I called
  // `memcpy()` to copy the `data` it ran into a
  // segfault. valgrind pointed me to EBML (I was testing VP8);
  // it feels like somewhere the stack gets overwritten. Shortly
  // discussed this with Jaron and he told me this could be
  // indeed the case. For now I'm allocating the buffer on the
  // heap. This function will be called when we're sending data
  // to the browser (other peer). 
  void OutWebRTC::onRTPPacketizerHasRTPPacket(char* data, uint32_t nbytes) {

    memcpy(rtpOutBuffer, data, nbytes);

    int protectedSize = nbytes;
    if (srtpWriter.protectRtp((uint8_t*)rtpOutBuffer, &protectedSize) != 0) {
      ERROR_MSG("Failed to protect the RTCP message.");
      return;
    }
    
    udp.SendNow((const char*)rtpOutBuffer, (size_t)protectedSize);
    myConn.addUp(protectedSize);

    /* << TODO: remove if this doesn't work; testing output pacing >> */
    if (didReceiveKeyFrame) {
      //Util::sleep(4);
    }
  }

  void OutWebRTC::onRTPPacketizerHasRTCPPacket(char* data, uint32_t nbytes) {

    if (nbytes > 2048) {
      FAIL_MSG("The received RTCP packet is too big to handle.");
      return;
    }
    if (!rtpOutBuffer) {
      FAIL_MSG("rtpOutBuffer not yet allocated.");
      return;
    }
    if (!data) {
      FAIL_MSG("Invalid RTCP packet given.");
      return;
    }
    
    FAIL_MSG("# Copy data into rtpOutBuffer (%u bytes)", nbytes);
    
    memcpy((void*)rtpOutBuffer, data, nbytes);
    int rtcpPacketSize = nbytes;
    FAIL_MSG("# Protect rtcp");
    if (srtpWriter.protectRtcp((uint8_t*)rtpOutBuffer, &rtcpPacketSize) != 0) {
      ERROR_MSG("Failed to protect the RTCP message.");
      return;
    }

    WARN_MSG("# has RTCP packet, %d bytes.", rtcpPacketSize);

    udp.SendNow((const char*)rtpOutBuffer, rtcpPacketSize);

    /* @todo add myConn.addUp(). */
  }  

  // This function was implemented (it's virtual) to handle
  // pushing of media to the browser. This function blocks until
  // the DTLS handshake has been finished. This prevents
  // `sendNext()` from being called which is correct because we
  // don't want to send packets when we can't protect them with
  // DTLS.
  void OutWebRTC::sendHeader() {

    // first make sure that we complete the DTLS handshake. 
    while (!dtlsHandshake.hasKeyingMaterial()){
      if (!handleWebRTCInputOutput()) {
        Util::sleep(10);
      }
    }

    sentHeader = true;
  }

  void OutWebRTC::sendNext() {

    // once the DTLS handshake has been done, we still have to
    // deal with STUN consent messages and RTCP.
    handleWebRTCInputOutput();
    
    char* dataPointer = 0;
    size_t dataLen = 0;
    thisPacket.getString("data", dataPointer, dataLen);

    // make sure the webrtcTracks were setup correctly for output.
    uint32_t tid = thisPacket.getTrackId();
    if (webrtcTracks.count(tid) == 0) {
      FAIL_MSG("No WebRTCTrack found for track id %llu.", tid);
      return;
    }

    WebRTCTrack& rtcTrack = webrtcTracks[tid];
    if (rtcTrack.timestampMultiplier == 0) {
      FAIL_MSG("The WebRTCTrack::timestampMultiplier is 0; invalid.");
      return;
    }

    uint64_t timestamp = thisPacket.getTime();
    uint64_t offset = thisPacket.getInt("offset");
    rtcTrack.rtpPacketizer.setTimestamp((timestamp + offset) * rtcTrack.timestampMultiplier);

    DTSC::Track& dtscTrack = myMeta.tracks[tid];

    /* ----------------------- BEGIN NEEDS CLEANUP -------------------------------------- */
    
    bool isKeyFrame = thisPacket.getFlag("keyframe");
    didReceiveKeyFrame = isKeyFrame;
    if (isKeyFrame && dtscTrack.codec == "H264") {
      uint8_t nalType = dataPointer[5] & 0x1F;
      if (nalType == 5) {
        sendSPSPPS(dtscTrack, rtcTrack);
      }
    }
    
#if 0
    /*
      @todo
      - wel versturen als isKeyframe true is en op byte positie 5, nal type 5
      4 bytes size, 1 byte nal type.
      - jaron heeft een keyframe check functie 
     */
    bool isKeyFrame = thisPacket.getFlag("keyframe");
    
    if (!dtscTrack.init.empty()) {
      static bool didSentInit = false;
      if (!didSentInit || isKeyFrame) {

        MP4::AVCC avcc;
        avcc.setPayload(dtscTrack.init);
        std::vector<char> buf;
        for (uint32_t i = 0; i < avcc.getSPSCount(); ++i) {
          uint32_t len = avcc.getSPSLen(i);
          buf.clear();
          buf.assign(4, 0);
          *(uint32_t*)&buf[0] = htonl(len);
          std::copy(avcc.getSPS(i), avcc.getSPS(i) + avcc.getSPSLen(i), std::back_inserter(buf));
          rtcTrack.rtpPacketizer.sendData(&udp,
                                          onRTPPacketizerHasDataCallback,
                                          &buf[0],
                                          buf.size(),
                                          rtcTrack.payloadType,
                                          dtscTrack.codec);
        }

        for (uint32_t i = 0; i < avcc.getPPSCount(); ++i) {
          uint32_t len = avcc.getPPSLen(i);
          buf.clear();
          buf.assign(4, 0);
          *(uint32_t*)&buf[0] = htonl(len);
          std::copy(avcc.getPPS(i), avcc.getPPS(i) + avcc.getPPSLen(i), std::back_inserter(buf));
          rtcTrack.rtpPacketizer.sendData(&udp,
                                          onRTPPacketizerHasDataCallback,
                                          &buf[0],
                                          buf.size(),
                                          rtcTrack.payloadType,
                                          dtscTrack.codec);

        }
        
        didSentInit = true;
      }
    }
#endif    
    /* test: repeat sending of SPS. */
#if 0    
    static uint64_t repeater = Util::getMS() + 2000;
    if (Util::getMS() >= repeater) {
      FAIL_MSG("=> send next, sending SPS again.");
      repeater = Util::getMS() + 2000;
      MP4::AVCC avcc;
      avcc.setPayload(dtscTrack.init);
      std::vector<char> buf;
      for (uint32_t i = 0; i < avcc.getSPSCount(); ++i) {
        uint32_t len = avcc.getSPSLen(i);
        buf.clear();
        buf.assign(4, 0);
        *(uint32_t*)&buf[0] = htonl(len);
        std::copy(avcc.getSPS(i), avcc.getSPS(i) + avcc.getSPSLen(i), std::back_inserter(buf));
        rtcTrack.rtpPacketizer.sendData(&udp,
                                        onRTPPacketizerHasDataCallback,
                                        &buf[0],
                                        buf.size(),
                                        rtcTrack.payloadType,
                                        dtscTrack.codec);
      }
    }
#endif
    /* ----------------------- END NEEDS CLEANUP -------------------------------------- */
    
    rtcTrack.rtpPacketizer.sendData(&udp,
                                    onRTPPacketizerHasDataCallback,
                                    dataPointer,
                                    dataLen,
                                    rtcTrack.payloadType,
                                    dtscTrack.codec);
  }

  // When the RTP::toDTSC converter collected a complete VP8
  // frame, it wil call our callback `onDTSCConverterHasPacket()`
  // with a valid packet that can be fed into
  // MistServer. Whenever we receive a keyframe we update the
  // width and height of the corresponding track.
  void OutWebRTC::extractFrameSizeFromVP8KeyFrame(const DTSC::Packet &pkt) {

    char *vp8PayloadBuffer = 0;
    size_t vp8PayloadLen = 0;
    pkt.getString("data", vp8PayloadBuffer, vp8PayloadLen);

    if (!vp8PayloadBuffer || vp8PayloadLen < 9) {
      FAIL_MSG("Cannot extract vp8 frame size. Failed to get data.");
      return;
    }
    
    if (vp8PayloadBuffer[3] != 0x9d
        || vp8PayloadBuffer[4] != 0x01
        || vp8PayloadBuffer[5] != 0x2a)
      {
        FAIL_MSG("Invalid signature. It seems that either the VP8 frames is incorrect or our parsing is wrong.");
        return;
      }

    uint32_t width  = ((vp8PayloadBuffer[7] << 8) + vp8PayloadBuffer[6]) & 0x3FFF;
    uint32_t height = ((vp8PayloadBuffer[9] << 8) + vp8PayloadBuffer[8]) & 0x3FFF;
    
    DONTEVEN_MSG("Recieved VP8 keyframe with resolution: %u x %u", width, height);
    
    if (width == 0) {
      FAIL_MSG("VP8 frame width is 0; parse error?");
      return;
    }
    
    if (height == 0) {
      FAIL_MSG("VP8 frame height is 0; parse error?");
      return;
    }

    uint64_t trackID = pkt.getTrackId();
    if (myMeta.tracks.count(trackID) == 0) {
      FAIL_MSG("No track found with ID %llu.", trackID);
      return;
    }
      
    DTSC::Track& Trk = myMeta.tracks[trackID];
    Trk.width = width;
    Trk.height = height;
  }

  void OutWebRTC::sendRTCPFeedbackREMB(const WebRTCTrack& rtcTrack) {

    if (videoBitrate == 0) {
      FAIL_MSG("videoBitrate is 0, which is invalid. Resetting to our default value.");
      videoBitrate = 6 * 1000 * 1000;
    }
    
    // create the `BR Exp` and `BR Mantissa parts.
    uint32_t br_exponent = 0;
    uint32_t br_mantissa = videoBitrate;
    while (br_mantissa > 0x3FFFF) {
      br_mantissa >>= 1;
      ++br_exponent;
    }

    std::vector<uint8_t> buffer;
    buffer.push_back(0x80 | 0x0F);                                                 // V =2 (0x80) | FMT=15 (0x0F)
    buffer.push_back(0xCE);                                                        // payload type = 206
    buffer.push_back(0x00);                                                        // tmp length
    buffer.push_back(0x00);                                                        // tmp length
    buffer.push_back((SSRC >> 24) & 0xFF);                                         // ssrc of sender
    buffer.push_back((SSRC >> 16) & 0xFF);                                         // ssrc of sender 
    buffer.push_back((SSRC >> 8)  & 0xFF);                                         // ssrc of sender
    buffer.push_back((SSRC)       & 0xFF);                                         // ssrc of sender
    buffer.push_back(0x00);                                                        // ssrc of media source (always 0)
    buffer.push_back(0x00);                                                        // ssrc of media source (always 0)
    buffer.push_back(0x00);                                                        // ssrc of media source (always 0)
    buffer.push_back(0x00);                                                        // ssrc of media source (always 0)
    buffer.push_back('R');                                                         // `R`, `E`, `M`, `B`
    buffer.push_back('E');                                                         // `R`, `E`, `M`, `B`
    buffer.push_back('M');                                                         // `R`, `E`, `M`, `B`
    buffer.push_back('B');                                                         // `R`, `E`, `M`, `B`
    buffer.push_back(0x01);                                                        // num ssrc
    buffer.push_back((uint8_t) (br_exponent << 2) + ((br_mantissa >> 16) & 0x03)); // br-exp and br-mantissa
    buffer.push_back((uint8_t) (br_mantissa >> 8));                                // br-exp and br-mantissa
    buffer.push_back((uint8_t) br_mantissa);                                       // br-exp and br-mantissa
    buffer.push_back((rtcTrack.SSRC >> 24) & 0xFF);                                // ssrc to which this remb packet applies to.
    buffer.push_back((rtcTrack.SSRC >> 16) & 0xFF);                                // ssrc to which this remb packet applies to.
    buffer.push_back((rtcTrack.SSRC >> 8)  & 0xFF);                                // ssrc to which this remb packet applies to.
    buffer.push_back((rtcTrack.SSRC)       & 0xFF);                                // ssrc to which this remb packet applies to.

    // rewrite size
    int buffer_size_in_bytes = (int)buffer.size();
    int buffer_size_in_words_minus1 = ((int)buffer.size() / 4) - 1;
    buffer[2] = (buffer_size_in_words_minus1 >> 8) & 0xFF;
    buffer[3] = buffer_size_in_words_minus1 & 0xFF;
    
    // protect.
    size_t trailer_space = SRTP_MAX_TRAILER_LEN + 4;
    for (size_t i = 0; i < trailer_space; ++i) {
      buffer.push_back(0x00);
    }
    
    if (srtpWriter.protectRtcp(&buffer[0], &buffer_size_in_bytes) != 0) {
      ERROR_MSG("Failed to protect the RTCP message.");
      return;
    }

    udp.SendNow((const char*)&buffer[0], buffer_size_in_bytes);
    myConn.addUp(buffer_size_in_bytes);
  }

  void OutWebRTC::sendRTCPFeedbackPLI(const WebRTCTrack& rtcTrack) {
    
    std::vector<uint8_t> buffer;
    buffer.push_back(0x80 | 0x01);                  // V=2 (0x80) | FMT=1 (0x01)
    buffer.push_back(0xCE);                         // payload type = 206
    buffer.push_back(0x00);                         // payload size in words minus 1 (2) 
    buffer.push_back(0x02);                         // payload size in words minus 1 (2) 
    buffer.push_back((SSRC >> 24) & 0xFF);          // ssrc of sender
    buffer.push_back((SSRC >> 16) & 0xFF);          // ssrc of sender
    buffer.push_back((SSRC >> 8)  & 0xFF);          // ssrc of sender
    buffer.push_back((SSRC)       & 0xFF);          // ssrc of sender
    buffer.push_back((rtcTrack.SSRC >> 24) & 0xFF); // ssrc of receiver 
    buffer.push_back((rtcTrack.SSRC >> 16) & 0xFF); // ssrc of receiver 
    buffer.push_back((rtcTrack.SSRC >> 8)  & 0xFF); // ssrc of receiver 
    buffer.push_back((rtcTrack.SSRC)       & 0xFF); // ssrc of receiver 
    
    // space for protection
    size_t trailer_space = SRTP_MAX_TRAILER_LEN + 4;
    for (size_t i = 0; i < trailer_space; ++i) {
      buffer.push_back(0x00);
    }

    // protect.
    int buffer_size_in_bytes = (int)buffer.size();
    if (srtpWriter.protectRtcp(&buffer[0], &buffer_size_in_bytes) != 0) {
      ERROR_MSG("Failed to protect the RTCP message.");
      return;
    }

    udp.SendNow((const char*)&buffer[0], buffer_size_in_bytes);
    myConn.addUp(buffer_size_in_bytes);
  }

  // Notify sender that we lost a packet. See
  // https://tools.ietf.org/html/rfc4585#section-6.1 which
  // describes the use of the `BLP` field; when more successive
  // sequence numbers are lost it makes sense to implement this
  // too.
  void OutWebRTC::sendRTCPFeedbackNACK(const WebRTCTrack &rtcTrack, uint16_t lostSequenceNumber) {
    
    std::vector<uint8_t> buffer;
    buffer.push_back(0x80 | 0x01);                      // V=2 (0x80) | FMT=1 (0x01)
    buffer.push_back(0xCD);                             // payload type = 205, RTPFB, https://tools.ietf.org/html/rfc4585#section-6.1
    buffer.push_back(0x00);                             // payload size in words minus 1 (3) 
    buffer.push_back(0x03);                             // payload size in words minus 1 (3) 
    buffer.push_back((SSRC >> 24) & 0xFF);              // ssrc of sender
    buffer.push_back((SSRC >> 16) & 0xFF);              // ssrc of sender
    buffer.push_back((SSRC >> 8)  & 0xFF);              // ssrc of sender
    buffer.push_back((SSRC)       & 0xFF);              // ssrc of sender
    buffer.push_back((rtcTrack.SSRC >> 24) & 0xFF);     // ssrc of receiver 
    buffer.push_back((rtcTrack.SSRC >> 16) & 0xFF);     // ssrc of receiver 
    buffer.push_back((rtcTrack.SSRC >> 8)  & 0xFF);     // ssrc of receiver 
    buffer.push_back((rtcTrack.SSRC)       & 0xFF);     // ssrc of receiver
    buffer.push_back((lostSequenceNumber >> 8) & 0xFF); // PID: missing sequence number 
    buffer.push_back((lostSequenceNumber)      & 0xFF); // PID: missing sequence number 
    buffer.push_back(0x00);                             // BLP: Bitmask of following losses. (not implemented atm).
    buffer.push_back(0x00);                             // BLP: Bitmask of following losses. (not implemented atm).
    
    // space for protection
    size_t trailer_space = SRTP_MAX_TRAILER_LEN + 4;
    for (size_t i = 0; i < trailer_space; ++i) {
      buffer.push_back(0x00);
    }

    // protect.
    int buffer_size_in_bytes = (int)buffer.size();
    if (srtpWriter.protectRtcp(&buffer[0], &buffer_size_in_bytes) != 0) {
      ERROR_MSG("Failed to protect the RTCP message.");
      return;
    }

    udp.SendNow((const char*)&buffer[0], buffer_size_in_bytes);
    myConn.addUp(buffer_size_in_bytes);
  }

  void OutWebRTC::sendRTCPFeedbackRR(WebRTCTrack &rtcTrack) {

    ((RTP::FECPacket*)&(rtcTrack.rtpPacketizer))->sendRTCP_RR(rtcTrack.sorter,
                                       SSRC,
                                       rtcTrack.SSRC,
                                       (void*)&udp,
                                       onRTPPacketizerHasRTCPDataCallback);
             
  }
  
  void OutWebRTC::sendSPSPPS(DTSC::Track& dtscTrack, WebRTCTrack& rtcTrack) {

    if (dtscTrack.init.empty()) {
      WARN_MSG("No init data found in the DTSC::Track. Not sending SPS and PPS");
      return;
    }

    std::vector<char> buf;
    MP4::AVCC avcc;
    avcc.setPayload(dtscTrack.init);

    /* SPS */
    for (uint32_t i = 0; i < avcc.getSPSCount(); ++i) {

      uint32_t len = avcc.getSPSLen(i);
      if (len == 0) {
        WARN_MSG("Empty SPS stored?");
        continue;
      }
      
      buf.clear();
      buf.assign(4, 0);
      *(uint32_t*)&buf[0] = htonl(len);
      std::copy(avcc.getSPS(i), avcc.getSPS(i) + avcc.getSPSLen(i), std::back_inserter(buf));

      rtcTrack.rtpPacketizer.sendData(&udp,
                                      onRTPPacketizerHasDataCallback,
                                      &buf[0],
                                      buf.size(),
                                      rtcTrack.payloadType,
                                      dtscTrack.codec);
    }

    /* PPS */
    for (uint32_t i = 0; i < avcc.getPPSCount(); ++i) {
      
      uint32_t len = avcc.getPPSLen(i);
      if (len == 0) {
        WARN_MSG("Empty PPS stored?");
        continue;
      }
      
      buf.clear();
      buf.assign(4, 0);
      *(uint32_t*)&buf[0] = htonl(len);
      std::copy(avcc.getPPS(i),  avcc.getPPS(i) + avcc.getPPSLen(i), std::back_inserter(buf));
      
      rtcTrack.rtpPacketizer.sendData(&udp,
                                      onRTPPacketizerHasDataCallback,
                                      &buf[0],
                                      buf.size(),
                                      rtcTrack.payloadType,
                                      dtscTrack.codec);

    }
  }

  /* ------------------------------------------------ */
  
  // This is our thread function that is started right before we
  // call `allowPush()` and send our answer SDP back to the
  // client.
  static void webRTCInputOutputThreadFunc(void* arg) {
    if (!classPointer) {
      FAIL_MSG("classPointer hasn't been set. Exiting thread.");
      return;
    }
    classPointer->handleWebRTCInputOutputFromThread();
  }

  static int onDTLSHandshakeWantsToWriteCallback(const uint8_t* data, int* nbytes) {
    if (!classPointer) {
      FAIL_MSG("Requested to send DTLS handshake data but the `classPointer` hasn't been set.");
      return -1;
    }
    return classPointer->onDTLSHandshakeWantsToWrite(data, nbytes);
  }

  static void onRTPSorterHasPacketCallback(const uint64_t track, const RTP::Packet &p) {
    if (!classPointer) {
      FAIL_MSG("We received a sorted RTP packet but our `classPointer` is invalid.");
      return;
    }
    classPointer->onRTPSorterHasPacket(track, p);
  }

  static void onDTSCConverterHasInitDataCallback(const uint64_t track, const std::string &initData) {
    if (!classPointer) {
      FAIL_MSG("Received a init data, but our `classPointer` is invalid.");
      return;
    }
    classPointer->onDTSCConverterHasInitData(track, initData);
  }
  
  static void onDTSCConverterHasPacketCallback(const DTSC::Packet& pkt) {
    if (!classPointer) {
      FAIL_MSG("Received a DTSC packet that was created from RTP data, but our `classPointer` is invalid.");
      return;
    }
    classPointer->onDTSCConverterHasPacket(pkt);
  }

  static void onRTPPacketizerHasDataCallback(void* socket, char* data, unsigned int len, unsigned int channel) {
    if (!classPointer) {
      FAIL_MSG("Received a RTP packet but our `classPointer` is invalid.");
      return;
    }
    classPointer->onRTPPacketizerHasRTPPacket(data, len);
  }

  static void onRTPPacketizerHasRTCPDataCallback(void* socket, const char* data, uint32_t len) {
    if (!classPointer) {
      FAIL_MSG("Received a RTCP packet, but out `classPointer` is invalid.");
      return;
    }
    classPointer->onRTPPacketizerHasRTCPPacket((char*)data, len);
  }
  
  /* ------------------------------------------------ */

  static uint32_t generateSSRC() {

    uint32_t ssrc = 0;
    
    do {
        ssrc = rand();
        ssrc = ssrc << 16;
        ssrc += rand();
    } while (ssrc == 0 || ssrc == 0xffffffff);

    return ssrc;
  }

  // This function is used to return a vector of the IP4
  // addresses of the interfaces on this machine. This is used
  // when we create the candidate address that is shared in our
  // SDP answer. The other WebRTC-peer will use this address to
  // deliver data.
  static std::vector<std::string> getLocalIP4Addresses() {

    std::vector<std::string> result;
    ifaddrs* ifaddr = NULL;
    ifaddrs* ifa = NULL;
    sockaddr_in* addr = NULL;
    char host[128] = { 0 };
    int s = 0;
    int i = 0;

    if (getifaddrs(&ifaddr) == -1) {
      FAIL_MSG("Failed to get the local interface addresses.");
      return result;
    }

    for (ifa = ifaddr, i = 0; ifa != NULL; ifa = ifa->ifa_next, i++) {
      if (ifa->ifa_addr == NULL) {
        continue;
      }
      if (ifa->ifa_addr->sa_family != AF_INET) {
        continue;
      }
      addr = (sockaddr_in*)ifa->ifa_addr;
      if (addr->sin_addr.s_addr == htonl(INADDR_LOOPBACK)) {
        continue;
      }
      s = getnameinfo(ifa->ifa_addr, sizeof(sockaddr_in), host, sizeof(host), NULL, 0, NI_NUMERICHOST);
      if (0 != s) {
        FAIL_MSG("FAiled to get name info for an interface.");
        continue;
      }
      result.push_back(host);
    }

    if (ifaddr != NULL) {
      freeifaddrs(ifaddr);
      ifaddr = NULL;
    }

    return result;
  }

  /* ------------------------------------------------ */

} // mist namespace
