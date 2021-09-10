#include "output_webrtc.h"
#include <ifaddrs.h> // ifaddr, listing ip addresses.
#include <mist/procs.h>
#include <mist/timing.h>
#include <netdb.h> // ifaddr, listing ip addresses.

namespace Mist{

  OutWebRTC *classPointer = 0;

  /* ------------------------------------------------ */

  static uint32_t generateSSRC();
  static void webRTCInputOutputThreadFunc(void *arg);
  static int onDTLSHandshakeWantsToWriteCallback(const uint8_t *data, int *nbytes);
  static void onDTSCConverterHasPacketCallback(const DTSC::Packet &pkt);
  static void onDTSCConverterHasInitDataCallback(const uint64_t track, const std::string &initData);
  static void
  onRTPSorterHasPacketCallback(const uint64_t track,
                               const RTP::Packet &p); // when we receive RTP packets we store them in a sorter. Whenever there is a valid, sorted RTP packet that can be used this function is called.
  static void onRTPPacketizerHasDataCallback(void *socket, char *data, unsigned int len, unsigned int channel);
  static void onRTPPacketizerHasRTCPDataCallback(void *socket, const char *data, uint32_t nbytes);

  /* ------------------------------------------------ */

  WebRTCTrack::WebRTCTrack()
      : payloadType(0), SSRC(0), timestampMultiplier(0), ULPFECPayloadType(0), REDPayloadType(0),
        RTXPayloadType(0), prevReceivedSequenceNumber(0){}

  /* ------------------------------------------------ */

  OutWebRTC::OutWebRTC(Socket::Connection &myConn) : HTTPOutput(myConn){

    vidTrack = 0;
    audTrack = 0;
    webRTCInputOutputThread = NULL;
    udpPort = 0;
    SSRC = generateSSRC();
    rtcpTimeoutInMillis = 0;
    rtcpKeyFrameDelayInMillis = 2000;
    rtcpKeyFrameTimeoutInMillis = 0;
    videoBitrate = 6 * 1000 * 1000;
    RTP::MAX_SEND = 1200 - 28;
    didReceiveKeyFrame = false;

    if (cert.init("NL", "webrtc", "webrtc") != 0){
      onFail("Failed to create the certificate.", true);
      return;
    }
    if (dtlsHandshake.init(&cert.cert, &cert.key, onDTLSHandshakeWantsToWriteCallback) != 0){
      onFail("Failed to initialize the dtls-srtp handshake helper.", true);
      return;
    }

    sdpAnswer.setFingerprint(cert.getFingerprintSha256());
    classPointer = this;

    setBlocking(false);
  }

  OutWebRTC::~OutWebRTC(){

    if (webRTCInputOutputThread && webRTCInputOutputThread->joinable()){
      webRTCInputOutputThread->join();
      delete webRTCInputOutputThread;
      webRTCInputOutputThread = NULL;
    }

    if (srtpReader.shutdown() != 0){FAIL_MSG("Failed to cleanly shutdown the srtp reader.");}
    if (srtpWriter.shutdown() != 0){FAIL_MSG("Failed to cleanly shutdown the srtp writer.");}
    if (dtlsHandshake.shutdown() != 0){
      FAIL_MSG("Failed to cleanly shutdown the dtls handshake.");
    }
    if (cert.shutdown() != 0){FAIL_MSG("Failed to cleanly shutdown the certificate.");}
  }

  // Initialize the WebRTC output. This is where we define what
  // codes types are supported and accepted when parsing the SDP
  // offer or when generating the SDP. The `capa` member is
  // inherited from `Output`.
  void OutWebRTC::init(Util::Config *cfg){
    HTTPOutput::init(cfg);
    capa["name"] = "WebRTC";
    capa["desc"] = "Provides WebRTC output";
    capa["url_rel"] = "/webrtc/$";
    capa["url_match"] = "/webrtc/$";
    capa["codecs"][0u][0u].append("H264");
    capa["codecs"][0u][0u].append("VP8");
    capa["codecs"][0u][1u].append("opus");
    capa["codecs"][0u][1u].append("ALAW");
    capa["codecs"][0u][1u].append("ULAW");
    capa["methods"][0u]["handler"] = "webrtc";
    capa["methods"][0u]["type"] = "webrtc";
    capa["methods"][0u]["priority"] = 2;

    capa["optional"]["preferredvideocodec"]["name"] = "Preferred video codecs";
    capa["optional"]["preferredvideocodec"]["help"] =
        "Comma separated list of video codecs you want to support in preferred order. e.g. "
        "H264,VP8";
    capa["optional"]["preferredvideocodec"]["default"] = "H264,VP8";
    capa["optional"]["preferredvideocodec"]["type"] = "string";
    capa["optional"]["preferredvideocodec"]["option"] = "--webrtc-video-codecs";
    capa["optional"]["preferredvideocodec"]["short"] = "V";

    capa["optional"]["preferredaudiocodec"]["name"] = "Preferred audio codecs";
    capa["optional"]["preferredaudiocodec"]["help"] =
        "Comma separated list of audio codecs you want to support in preferred order. e.g. "
        "opus,ALAW,ULAW";
    capa["optional"]["preferredaudiocodec"]["default"] = "opus,ALAW,ULAW";
    capa["optional"]["preferredaudiocodec"]["type"] = "string";
    capa["optional"]["preferredaudiocodec"]["option"] = "--webrtc-audio-codecs";
    capa["optional"]["preferredaudiocodec"]["short"] = "A";

    capa["optional"]["bindhost"]["name"] = "UDP bind address";
    capa["optional"]["bindhost"]["help"] = "Interface address or hostname to bind SRTP UDP socket "
                                           "to. Defaults to originating interface address.";
    capa["optional"]["bindhost"]["default"] = "";
    capa["optional"]["bindhost"]["type"] = "string";
    capa["optional"]["bindhost"]["option"] = "--bindhost";
    capa["optional"]["bindhost"]["short"] = "B";

    config->addOptionsFromCapabilities(capa);
  }

  void OutWebRTC::preWebsocketConnect(){
    HTTP::URL tmpUrl("http://" + H.GetHeader("Host"));
    externalAddr = tmpUrl.host;
  }

  // This function is executed when we receive a signaling data.
  // The signaling data contains commands that are used to start
  // an input or output stream.
  void OutWebRTC::onWebsocketFrame(){
    if (webSock->frameType != 1){
      HIGH_MSG("Ignoring non-text websocket frame");
      return;
    }

    JSON::Value command = JSON::fromString(webSock->data, webSock->data.size());
    JSON::Value commandResult;

    // Check if there's a command type
    if (!command.isMember("type")){
      sendSignalingError("error", "Received a command but no type property was given.");
      return;
    }

    if (command["type"] == "offer_sdp"){
      if (!command.isMember("offer_sdp")){
        sendSignalingError("on_offer_sdp",
                           "An `offer_sdp` command needs the offer SDP in the `offer_sdp` field.");
        return;
      }
      if (command["offer_sdp"].asString() == ""){
        sendSignalingError("on_offer_sdp", "The given `offer_sdp` field is empty.");
        return;
      }

      // get video and supported video formats from offer.
      SDP::Session sdpParser;
      const std::string &offerStr = command["offer_sdp"].asStringRef();
      if (!sdpParser.parseSDP(offerStr) || !sdpAnswer.parseOffer(offerStr)){
        sendSignalingError("offer_sdp", "Failed to parse the offered SDP");
        return;
      }

      bool ret = false;
      if (sdpParser.hasReceiveOnlyMedia()){
        ret = handleSignalingCommandRemoteOfferForOutput(sdpParser);
      }else{
        ret = handleSignalingCommandRemoteOfferForInput(sdpParser);
      }
      // create result message.
      JSON::Value commandResult;
      commandResult["type"] = "on_answer_sdp";
      commandResult["result"] = ret;
      if (ret){commandResult["answer_sdp"] = sdpAnswer.toString();}
      webSock->sendFrame(commandResult.toString());
      return;
    }

    if (command["type"] == "video_bitrate"){
      if (!command.isMember("video_bitrate")){
        sendSignalingError("on_video_bitrate", "No video_bitrate attribute found.");
        return;
      }
      videoBitrate = command["video_bitrate"].asInt();
      if (videoBitrate == 0){
        FAIL_MSG("We received an invalid video_bitrate; resetting to default.");
        videoBitrate = 6 * 1000 * 1000;
        sendSignalingError("on_video_bitrate",
                           "Failed to handle the video bitrate change request.");
        return;
      }
      JSON::Value commandResult;
      commandResult["type"] = "on_video_bitrate";
      commandResult["result"] = true;
      commandResult["video_bitrate"] = videoBitrate;
      webSock->sendFrame(commandResult.toString());
      return;
    }

    if (command["type"] == "tracks"){
      if (command.isMember("audio")){
        if (!command["audio"].isNull()){
          targetParams["audio"] = command["audio"].asString();
          if (audTrack && command["audio"].asInt()){
            uint64_t tId = command["audio"].asInt();
            if (myMeta.tracks.count(tId) && myMeta.tracks[tId].codec != myMeta.tracks[audTrack].codec){
              targetParams["audio"] = "none";
              sendSignalingError("tracks", "Cannot select track because it is encoded as " +
                                               myMeta.tracks[tId].codec + " but the already negotiated track is " +
                                               myMeta.tracks[audTrack].codec +
                                               ". Please re-negotiate to play this track.");
            }
          }
        }else{
          targetParams.erase("audio");
        }
      }
      if (command.isMember("video")){
        if (!command["video"].isNull()){
          targetParams["video"] = command["video"].asString();
          if (vidTrack && command["video"].asInt()){
            uint64_t tId = command["video"].asInt();
            if (myMeta.tracks.count(tId) && myMeta.tracks[tId].codec != myMeta.tracks[vidTrack].codec){
              targetParams["video"] = "none";
              sendSignalingError("tracks", "Cannot select track because it is encoded as " +
                                               myMeta.tracks[tId].codec + " but the already negotiated track is " +
                                               myMeta.tracks[vidTrack].codec +
                                               ". Please re-negotiate to play this track.");
            }
          }
        }else{
          targetParams.erase("video");
        }
      }
      selectDefaultTracks();
      for (std::set<unsigned long>::iterator it = selectedTracks.begin(); it != selectedTracks.end(); it++){
        DTSC::Track &dtscTrack = myMeta.tracks[*it];
        WebRTCTrack *trackPointer = 0;
        if (dtscTrack.type == "video" && webrtcTracks.count(vidTrack)){
          trackPointer = &webrtcTracks[vidTrack];
        }
        if (dtscTrack.type == "audio" && webrtcTracks.count(audTrack)){
          trackPointer = &webrtcTracks[audTrack];
        }
        if (webrtcTracks.count(*it)){trackPointer = &webrtcTracks[*it];}
        if (!trackPointer){continue;}
        WebRTCTrack &rtcTrack = *trackPointer;
        sendSPSPPS(dtscTrack, rtcTrack);
      }
      onIdle();
      return;
    }

    if (command["type"] == "seek"){
      if (!command.isMember("seek_time")){
        sendSignalingError("on_seek", "Received a seek request but no `seek_time` property.");
        return;
      }
      uint64_t seek_time = command["seek_time"].asInt();
      if (!parseData){
        parseData = true;
        selectDefaultTracks();
      }
      seek(seek_time, true);
      JSON::Value commandResult;
      commandResult["type"] = "on_seek";
      commandResult["result"] = true;
      webSock->sendFrame(commandResult.toString());
      onIdle();
      return;
    }

    if (command["type"] == "pause"){
      parseData = !parseData;
      JSON::Value commandResult;
      commandResult["type"] = "on_time";
      commandResult["paused"] = !parseData;
      commandResult["current"] = currentTime();
      commandResult["begin"] = startTime();
      commandResult["end"] = endTime();
      for (std::set<unsigned long>::iterator it = selectedTracks.begin(); it != selectedTracks.end(); it++){
        commandResult["tracks"].append(*it);
      }
      webSock->sendFrame(commandResult.toString());
      return;
    }

    if (command["type"] == "stop"){
      INFO_MSG("Received stop() command.");
      myConn.close();
      return;
    }

    if (command["type"] == "keyframe_interval"){
      if (!command.isMember("keyframe_interval_millis")){
        sendSignalingError("on_keyframe_interval", "No keyframe_interval_millis attribute found.");
        return;
      }

      rtcpKeyFrameDelayInMillis = command["keyframe_interval_millis"].asInt();
      if (rtcpKeyFrameDelayInMillis < 500){
        WARN_MSG("Requested a keyframe delay < 500ms; 500ms is the minimum you can set.");
        rtcpKeyFrameDelayInMillis = 500;
      }

      rtcpKeyFrameTimeoutInMillis = Util::getMS() + rtcpKeyFrameDelayInMillis;
      JSON::Value commandResult;
      commandResult["type"] = "on_keyframe_interval";
      commandResult["result"] = rtcpKeyFrameDelayInMillis;
      webSock->sendFrame(commandResult.toString());
      return;
    }

    // Unhandled
    sendSignalingError(command["type"].asString(), "Unhandled command type: " + command["type"].asString());
  }

  void OutWebRTC::sendSignalingError(const std::string &commandType, const std::string &errorMessage){
    JSON::Value commandResult;
    commandResult["type"] = "on_error";
    commandResult["command"] = commandType;
    commandResult["message"] = errorMessage;
    webSock->sendFrame(commandResult.toString());
  }

  // This function is called for a peer that wants to receive
  // data from us. First we update our capabilities by checking
  // what codecs the peer and we support. After updating the
  // codecs we `initialize()` and use `selectDefaultTracks()` to
  // pick the right tracks based on our supported (and updated)
  // capabilities.
  bool OutWebRTC::handleSignalingCommandRemoteOfferForOutput(SDP::Session &sdpSession){

    updateCapabilitiesWithSDPOffer(sdpSession);
    initialize();
    selectDefaultTracks();

    if (0 == udpPort){bindUDPSocketOnLocalCandidateAddress(0);}

    // get codecs from selected stream which are used to create our SDP answer.
    std::string videoCodec;
    std::string audioCodec;
    capa["codecs"][0u][0u].null();
    capa["codecs"][0u][1u].null();
    std::set<unsigned long>::iterator it = selectedTracks.begin();
    while (it != selectedTracks.end()){
      DTSC::Track &Trk = myMeta.tracks[*it];
      if (Trk.type == "video"){
        videoCodec = Trk.codec;
        vidTrack = Trk.trackID;
        capa["codecs"][0u][0u].append(videoCodec);
      }else if (Trk.type == "audio"){
        audioCodec = Trk.codec;
        audTrack = Trk.trackID;
        capa["codecs"][0u][1u].append(audioCodec);
      }
      ++it;
    }

    sdpAnswer.setDirection("sendonly");

    // setup video WebRTC Track.
    if (!videoCodec.empty()){
      if (sdpAnswer.enableVideo(videoCodec)){
        WebRTCTrack videoTrack;
        if (!createWebRTCTrackFromAnswer(sdpAnswer.answerVideoMedia, sdpAnswer.answerVideoFormat, videoTrack)){
          FAIL_MSG("Failed to create the WebRTCTrack for the selected video.");
          return false;
        }
        videoTrack.rtpPacketizer = RTP::Packet(videoTrack.payloadType, rand(), 0, videoTrack.SSRC, 0);
        videoTrack.timestampMultiplier = 90;
        webrtcTracks[vidTrack] = videoTrack;
        // Enabled NACKs
        sdpAnswer.videoLossPrevention = SDP_LOSS_PREVENTION_NACK;
        videoTrack.sorter.tmpVideoLossPrevention = sdpAnswer.videoLossPrevention;
      }
    }

    // setup audio WebRTC Track
    if (!audioCodec.empty()){
      if (sdpAnswer.enableAudio(audioCodec)){
        WebRTCTrack audioTrack;
        if (!createWebRTCTrackFromAnswer(sdpAnswer.answerAudioMedia, sdpAnswer.answerAudioFormat, audioTrack)){
          FAIL_MSG("Failed to create the WebRTCTrack for the selected audio.");
          return false;
        }
        audioTrack.rtpPacketizer = RTP::Packet(audioTrack.payloadType, rand(), 0, audioTrack.SSRC, 0);
        audioTrack.timestampMultiplier = 48;
        webrtcTracks[audTrack] = audioTrack;
      }
    }

    // this is necessary so that we can get the remote IP when creating STUN replies.
    udp.SetDestination("0.0.0.0", 4444);

    // we set parseData to `true` to start the data flow. Is also
    // used to break out of our loop in `onHTTP()`.
    parseData = true;
    idleInterval = 1000;

    return true;
  }

  void OutWebRTC::onIdle(){
    if (parseData){
      JSON::Value commandResult;
      commandResult["type"] = "on_time";
      commandResult["current"] = currentTime();
      commandResult["begin"] = startTime();
      commandResult["end"] = endTime();
      for (std::set<unsigned long>::iterator it = selectedTracks.begin(); it != selectedTracks.end(); it++){
        commandResult["tracks"].append(*it);
      }
      webSock->sendFrame(commandResult.toString());
    }
  }

  bool OutWebRTC::onFinish(){
    if (parseData){
      JSON::Value commandResult;
      commandResult["type"] = "on_stop";
      commandResult["current"] = currentTime();
      commandResult["begin"] = startTime();
      commandResult["end"] = endTime();
      webSock->sendFrame(commandResult.toString());
      parseData = false;
    }
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
  bool OutWebRTC::createWebRTCTrackFromAnswer(const SDP::Media &mediaAnswer,
                                              const SDP::MediaFormat &formatAnswer, WebRTCTrack &result){
    if (formatAnswer.payloadType == SDP_PAYLOAD_TYPE_NONE){
      FAIL_MSG("Cannot create a WebRTCTrack, the given SDP::MediaFormat has no `payloadType` set.");
      return false;
    }

    if (formatAnswer.icePwd.empty()){
      FAIL_MSG("Cannot create a WebRTCTrack, the given SDP::MediaFormat has no `icePwd` set.");
      return false;
    }

    if (formatAnswer.iceUFrag.empty()){
      FAIL_MSG("Cannot create a WebRTCTrack, the given SDP::MediaFormat has no `iceUFrag` set.");
      return false;
    }

    result.payloadType = formatAnswer.getPayloadType();
    result.localIcePwd = formatAnswer.icePwd;
    result.localIceUFrag = formatAnswer.iceUFrag;

    if (mediaAnswer.SSRC != 0){
      result.SSRC = mediaAnswer.SSRC;
    }else{
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
  void OutWebRTC::updateCapabilitiesWithSDPOffer(SDP::Session &sdpSession){

    capa["codecs"].null();

    const char *videoCodecPreference[] ={"H264", "VP8", NULL};
    const char **videoCodec = videoCodecPreference;
    SDP::Media *videoMediaOffer = sdpSession.getMediaForType("video");
    if (videoMediaOffer){
      while (*videoCodec){
        if (sdpSession.getMediaFormatByEncodingName("video", *videoCodec)){
          capa["codecs"][0u][0u].append(*videoCodec);
        }
        videoCodec++;
      }
    }

    const char *audioCodecPreference[] ={"opus", "ALAW", "ULAW", NULL};
    const char **audioCodec = audioCodecPreference;
    SDP::Media *audioMediaOffer = sdpSession.getMediaForType("audio");
    if (audioMediaOffer){
      while (*audioCodec){
        if (sdpSession.getMediaFormatByEncodingName("audio", *audioCodec)){
          capa["codecs"][0u][1u].append(*audioCodec);
        }
        audioCodec++;
      }
    }
  }

  // This function is called to handle an offer from a peer that wants to push data towards us.
  bool OutWebRTC::handleSignalingCommandRemoteOfferForInput(SDP::Session &sdpSession){

    if (webRTCInputOutputThread != NULL){
      FAIL_MSG("It seems that we're already have a webrtc i/o thread running.");
      return false;
    }

    if (0 == udpPort){bindUDPSocketOnLocalCandidateAddress(0);}

    std::string prefVideoCodec = "VP8,H264";
    if (config && config->hasOption("preferredvideocodec")){
      prefVideoCodec = config->getString("preferredvideocodec");
      if (prefVideoCodec.empty()){
        WARN_MSG("No preferred video codec value set; resetting to default.");
        prefVideoCodec = "VP8,H264";
      }
    }

    std::string prefAudioCodec = "opus,ALAW,ULAW";
    if (config && config->hasOption("preferredaudiocodec")){
      prefAudioCodec = config->getString("preferredaudiocodec");
      if (prefAudioCodec.empty()){
        WARN_MSG("No preferred audio codec value set; resetting to default.");
        prefAudioCodec = "opus,ALAW,ULAW";
      }
    }

    // video
    if (sdpAnswer.enableVideo(prefVideoCodec)){

      WebRTCTrack videoTrack;
      videoTrack.payloadType = sdpAnswer.answerVideoFormat.getPayloadType();
      videoTrack.localIcePwd = sdpAnswer.answerVideoFormat.icePwd;
      videoTrack.localIceUFrag = sdpAnswer.answerVideoFormat.iceUFrag;
      videoTrack.SSRC = sdpAnswer.answerVideoMedia.SSRC;

      SDP::MediaFormat *fmtRED = sdpSession.getMediaFormatByEncodingName("video", "RED");
      SDP::MediaFormat *fmtULPFEC = sdpSession.getMediaFormatByEncodingName("video", "ULPFEC");
      if (fmtRED && fmtULPFEC){
        videoTrack.ULPFECPayloadType = fmtULPFEC->payloadType;
        videoTrack.REDPayloadType = fmtRED->payloadType;
        payloadTypeToWebRTCTrack[fmtRED->payloadType] = videoTrack.payloadType;
      }
      sdpAnswer.videoLossPrevention = SDP_LOSS_PREVENTION_NACK;
      videoTrack.sorter.tmpVideoLossPrevention = sdpAnswer.videoLossPrevention;

      DTSC::Track dtscVideo;
      if (!sdpAnswer.setupVideoDTSCTrack(dtscVideo)){
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
    if (sdpAnswer.enableAudio(prefAudioCodec)){

      WebRTCTrack audioTrack;
      audioTrack.payloadType = sdpAnswer.answerAudioFormat.getPayloadType();
      audioTrack.localIcePwd = sdpAnswer.answerAudioFormat.icePwd;
      audioTrack.localIceUFrag = sdpAnswer.answerAudioFormat.iceUFrag;
      audioTrack.SSRC = sdpAnswer.answerAudioMedia.SSRC;

      DTSC::Track dtscAudio;
      if (!sdpAnswer.setupAudioDTSCTrack(dtscAudio)){
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
    if (!allowPush("")){
      FAIL_MSG("Failed to allow push for stream %s.", streamName.c_str());
      /* \todo when I try to send a error message back to the browser it fails; probably because socket gets closed (?). */
      return false;
    }

    // start our receive thread (handles STUN, DTLS, RTP input)
    webRTCInputOutputThread = new tthread::thread(webRTCInputOutputThreadFunc, NULL);
    rtcpTimeoutInMillis = Util::getMS() + 2000;
    rtcpKeyFrameTimeoutInMillis = Util::getMS() + 2000;

    return true;
  }

  bool OutWebRTC::bindUDPSocketOnLocalCandidateAddress(uint16_t port){

    if (udpPort != 0){
      FAIL_MSG("Already bound the UDP socket.");
      return false;
    }

    udpPort =
        udp.bind(port, (config && config->hasOption("bindhost") && config->getString("bindhost").size())
                           ? config->getString("bindhost")
                           : myConn.getBoundAddress());
    Util::Procs::socketList.insert(udp.getSock());
    sdpAnswer.setCandidate(externalAddr, udpPort);

    return true;
  }

  /* ------------------------------------------------ */

  // This function is called from the `webRTCInputOutputThreadFunc()`
  // function. The `webRTCInputOutputThreadFunc()` is basically empty
  // and all work for the thread is done here.
  void OutWebRTC::handleWebRTCInputOutputFromThread(){
    udp.SetDestination("0.0.0.0", 4444);
    while (keepGoing()){
      if (!handleWebRTCInputOutput()){Util::sleep(20);}
    }
  }

  // Checks if there is data on our UDP socket. The data can be
  // STUN, DTLS, SRTP or SRTCP. When we're receiving media from
  // the browser (e.g. from webcam) this function is called from
  // a separate thread. When we're pushing media to the browser
  // this is called from the main thread.
  bool OutWebRTC::handleWebRTCInputOutput(){

    bool hadPack = false;
    while (udp.Receive()){
      hadPack = true;

      myConn.addDown(udp.data_len);

      uint8_t fb = (uint8_t)udp.data[0];

      if (fb > 127 && fb < 192){
        handleReceivedRTPOrRTCPPacket();
      }else if (fb > 19 && fb < 64){
        handleReceivedDTLSPacket();
      }else if (fb < 2){
        handleReceivedSTUNPacket();
      }else{
        FAIL_MSG("Unhandled WebRTC data. Type: %02X", fb);
      }
    }
    return hadPack;
  }

  void OutWebRTC::handleReceivedSTUNPacket(){

    size_t nparsed = 0;
    StunMessage stun_msg;
    if (stunReader.parse((uint8_t *)udp.data, udp.data_len, nparsed, stun_msg) != 0){
      FAIL_MSG("Failed to parse a stun message.");
      return;
    }

    if (stun_msg.type != STUN_MSG_TYPE_BINDING_REQUEST){
      INFO_MSG("We only handle STUN binding requests as we're an ice-lite implementation.");
      return;
    }

    // get the username for whom we got a binding request.
    StunAttribute *usernameAttrib = stun_msg.getAttributeByType(STUN_ATTR_TYPE_USERNAME);
    if (!usernameAttrib){
      ERROR_MSG("No username attribute found in the STUN binding request. Cannot create success "
                "binding response.");
      return;
    }
    if (usernameAttrib->username.value == 0){
      ERROR_MSG("The username attribute is empty.");
      return;
    }
    std::string username(usernameAttrib->username.value, usernameAttrib->length);
    std::size_t usernameColonPos = username.find(":");
    if (usernameColonPos == std::string::npos){
      ERROR_MSG("The username in the STUN attribute has an invalid format: %s.", username.c_str());
      return;
    }
    std::string usernameLocal = username.substr(0, usernameColonPos);

    // get the password for the username that is used to create our message-integrity.
    std::string passwordLocal;
    std::map<uint64_t, WebRTCTrack>::iterator rtcTrackIt = webrtcTracks.begin();
    while (rtcTrackIt != webrtcTracks.end()){
      WebRTCTrack &tr = rtcTrackIt->second;
      if (tr.localIceUFrag == usernameLocal){passwordLocal = tr.localIcePwd;}
      ++rtcTrackIt;
    }
    if (passwordLocal.empty()){
      ERROR_MSG("No local ICE password found for username %s. Did you create a WebRTCTrack?",
                usernameLocal.c_str());
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

    udp.SendNow((const char *)stun_writer.getBufferPtr(), stun_writer.getBufferSize());
    myConn.addUp(stun_writer.getBufferSize());
  }

  void OutWebRTC::handleReceivedDTLSPacket(){

    if (dtlsHandshake.hasKeyingMaterial()){
      DONTEVEN_MSG("Not feeding data into the handshake .. already done.");
      return;
    }

    if (dtlsHandshake.parse((const uint8_t *)udp.data, udp.data_len) != 0){
      FAIL_MSG("Failed to parse a DTLS packet.");
      return;
    }

    if (!dtlsHandshake.hasKeyingMaterial()){return;}

    if (srtpReader.init(dtlsHandshake.cipher, dtlsHandshake.remote_key, dtlsHandshake.remote_salt) != 0){
      FAIL_MSG("Failed to initialize the SRTP reader.");
      return;
    }

    if (srtpWriter.init(dtlsHandshake.cipher, dtlsHandshake.local_key, dtlsHandshake.local_salt) != 0){
      FAIL_MSG("Failed to initialize the SRTP writer.");
      return;
    }
  }

  void OutWebRTC::ackNACK(uint32_t pSSRC, uint16_t seq){
    if (!outBuffers.count(pSSRC)){
      WARN_MSG("Could not answer NACK for %lu: we don't know this track", pSSRC);
      return;
    }
    nackBuffer &nb = outBuffers[pSSRC];
    if (!nb.isBuffered(seq)){
      WARN_MSG("Could not answer NACK for %lu #%u: packet not buffered", pSSRC, seq);
      return;
    }
    udp.SendNow(nb.getData(seq), nb.getSize(seq));
    myConn.addUp(nb.getSize(seq));
    INFO_MSG("Answered NACK for %lu #%u", pSSRC, seq);
  }

  void OutWebRTC::handleReceivedRTPOrRTCPPacket(){

    uint8_t pt = udp.data[1] & 0x7F;

    if ((pt < 64) || (pt >= 96)){

      RTP::Packet rtp_pkt((const char *)udp.data, (unsigned int)udp.data_len);
      uint8_t payloadType = rtp_pkt.getPayloadType();
      uint64_t rtcTrackID = payloadType;
      uint16_t currSeqNum = rtp_pkt.getSequence();

      // Do we need to map the payload type to a WebRTC Track? (e.g. RED)
      if (payloadTypeToWebRTCTrack.count(payloadType) != 0){
        rtcTrackID = payloadTypeToWebRTCTrack[payloadType];
      }

      if (webrtcTracks.count(rtcTrackID) == 0){
        FAIL_MSG(
            "Received an RTP packet for a track that we didn't prepare for. PayloadType is %llu", payloadType);
        return;
      }

      // Find the WebRTCTrack corresponding to the packet we received
      WebRTCTrack &rtcTrack = webrtcTracks[rtcTrackID];

      // Do not parse packets we don't care about
      if (!rtcTrack.sorter.wantSeq(currSeqNum)){return;}

      // Decrypt the SRTP to RTP
      int len = (int)udp.data_len;
      if (srtpReader.unprotectRtp((uint8_t *)udp.data, &len) != 0){
        FAIL_MSG("Failed to unprotect a RTP packet.");
        return;
      }

      // Here follows a very rudimentary algo for requesting lost
      // packets; I guess after some experimentation a better
      // algorithm should be used; this is used to trigger NACKs.
      uint16_t expectedSeqNum = rtcTrack.prevReceivedSequenceNumber + 1;
      if (rtcTrack.prevReceivedSequenceNumber != 0 && (rtcTrack.prevReceivedSequenceNumber + 1) != currSeqNum){
        while (rtcTrack.prevReceivedSequenceNumber < currSeqNum){
          sendRTCPFeedbackNACK(rtcTrack, rtcTrack.prevReceivedSequenceNumber);
          rtcTrack.prevReceivedSequenceNumber++;
        }
      }

      rtcTrack.prevReceivedSequenceNumber = currSeqNum;

      if (payloadType == rtcTrack.REDPayloadType){
        rtcTrack.sorter.addREDPacket(udp.data, len, rtcTrack.payloadType, rtcTrack.REDPayloadType,
                                     rtcTrack.ULPFECPayloadType);
      }else{
        rtcTrack.sorter.addPacket(RTP::Packet(udp.data, len));
      }
    }else if ((pt >= 64) && (pt < 96)){

      if (pt == 77 || pt == 78 || pt == 65){
        int len = udp.data_len;
        if (srtpReader.unprotectRtcp((uint8_t *)udp.data, &len) != 0){
          FAIL_MSG("Failed to unprotect RTCP.");
          return;
        }
        uint8_t fmt = udp.data[0] & 0x1F;
        if (pt == 77 || pt == 65){
          if (fmt == 1){
            uint32_t pSSRC = Bit::btohl(udp.data + 8);
            uint16_t seq = Bit::btohs(udp.data + 12);
            uint16_t bitmask = Bit::btohs(udp.data + 14);
            ackNACK(pSSRC, seq);
            if (bitmask & 1){ackNACK(pSSRC, seq + 1);}
            if (bitmask & 2){ackNACK(pSSRC, seq + 2);}
            if (bitmask & 4){ackNACK(pSSRC, seq + 3);}
            if (bitmask & 8){ackNACK(pSSRC, seq + 4);}
            if (bitmask & 16){ackNACK(pSSRC, seq + 5);}
            if (bitmask & 32){ackNACK(pSSRC, seq + 6);}
            if (bitmask & 64){ackNACK(pSSRC, seq + 7);}
            if (bitmask & 128){ackNACK(pSSRC, seq + 8);}
            if (bitmask & 256){ackNACK(pSSRC, seq + 9);}
            if (bitmask & 512){ackNACK(pSSRC, seq + 10);}
            if (bitmask & 1024){ackNACK(pSSRC, seq + 11);}
            if (bitmask & 2048){ackNACK(pSSRC, seq + 12);}
            if (bitmask & 4096){ackNACK(pSSRC, seq + 13);}
            if (bitmask & 8192){ackNACK(pSSRC, seq + 14);}
            if (bitmask & 16384){ackNACK(pSSRC, seq + 15);}
            if (bitmask & 32768){ackNACK(pSSRC, seq + 16);}
          }else{
            INFO_MSG("Received unimplemented RTP feedback message (%d)", fmt);
          }
        }
        if (pt == 78){
          if (fmt == 1){
            DONTEVEN_MSG("Received picture loss indication");
          }else{
            INFO_MSG("Received unimplemented payload-specific feedback message (%d)", fmt);
          }
        }
      }

    }else{
      FAIL_MSG("Unknown payload type: %u", pt);
    }
  }

  /* ------------------------------------------------ */

  int OutWebRTC::onDTLSHandshakeWantsToWrite(const uint8_t *data, int *nbytes){
    udp.SendNow((const char *)data, (size_t)*nbytes);
    myConn.addUp(*nbytes);
    return 0;
  }

  void OutWebRTC::onDTSCConverterHasPacket(const DTSC::Packet &pkt){

    // extract meta data (init data, width/height, etc);
    uint64_t trackID = pkt.getTrackId();
    DTSC::Track &DTSCTrack = myMeta.tracks[trackID];
    if (DTSCTrack.codec == "H264"){
      if (DTSCTrack.init.empty()){
        FAIL_MSG("No init data found for trackID %llu (note: we use payloadType as trackID)", trackID);
        return;
      }
    }else if (DTSCTrack.codec == "VP8"){
      if (pkt.getFlag("keyframe")){extractFrameSizeFromVP8KeyFrame(pkt);}
    }

    // create rtcp packet (set bitrate and request keyframe).
    if (DTSCTrack.codec == "H264" || DTSCTrack.codec == "VP8"){
      uint64_t now = Util::getMS();

      if (now >= rtcpTimeoutInMillis){
        WebRTCTrack &rtcTrack = webrtcTracks[trackID];
        sendRTCPFeedbackREMB(rtcTrack);
        sendRTCPFeedbackRR(rtcTrack);
        rtcpTimeoutInMillis = now + 1000; /* was 5000, lowered for FEC */
      }

      if (now >= rtcpKeyFrameTimeoutInMillis){
        WebRTCTrack &rtcTrack = webrtcTracks[trackID];
        sendRTCPFeedbackPLI(rtcTrack);
        rtcpKeyFrameTimeoutInMillis = now + rtcpKeyFrameDelayInMillis;
      }
    }

    bufferLivePacket(pkt);
  }

  void OutWebRTC::onDTSCConverterHasInitData(const uint64_t trackID, const std::string &initData){

    if (webrtcTracks.count(trackID) == 0){
      ERROR_MSG("Recieved init data for a track that we don't manager. TrackID/PayloadType: %llu", trackID);
      return;
    }

    MP4::AVCC avccbox;
    avccbox.setPayload(initData);
    if (avccbox.getSPSLen() == 0 || avccbox.getPPSLen() == 0){
      WARN_MSG("Received init data, but partially. SPS nbytes: %u, PPS nbytes: %u.",
               avccbox.getSPSLen(), avccbox.getPPSLen());
      return;
    }

    h264::sequenceParameterSet sps(avccbox.getSPS(), avccbox.getSPSLen());
    h264::SPSMeta hMeta = sps.getCharacteristics();
    DTSC::Track &Trk = myMeta.tracks[trackID];
    Trk.width = hMeta.width;
    Trk.height = hMeta.height;
    Trk.fpks = hMeta.fps * 1000;

    avccbox.multiplyPPS(57); // Inject all possible PPS packets into init
    myMeta.tracks[trackID].init = std::string(avccbox.payload(), avccbox.payloadSize());
  }

  void OutWebRTC::onRTPSorterHasPacket(const uint64_t trackID, const RTP::Packet &pkt){

    if (webrtcTracks.count(trackID) == 0){
      ERROR_MSG("Received a sorted RTP packet for track %llu but we don't manage this track.", trackID);
      return;
    }

    webrtcTracks[trackID].rtpToDTSC.addRTP(pkt);
  }

  // This function will be called when we're sending data
  // to the browser (other peer).
  void OutWebRTC::onRTPPacketizerHasRTPPacket(char *data, uint32_t nbytes){

    rtpOutBuffer.allocate(nbytes + 256);
    rtpOutBuffer.assign(data, nbytes);

    int protectedSize = nbytes;
    if (srtpWriter.protectRtp((uint8_t *)(void *)rtpOutBuffer, &protectedSize) != 0){
      ERROR_MSG("Failed to protect the RTCP message.");
      return;
    }

    udp.SendNow(rtpOutBuffer, (size_t)protectedSize);

    RTP::Packet tmpPkt(rtpOutBuffer, protectedSize);
    uint32_t pSSRC = tmpPkt.getSSRC();
    uint16_t seq = tmpPkt.getSequence();
    outBuffers[pSSRC].assign(seq, rtpOutBuffer, protectedSize);

    myConn.addUp(protectedSize);
  }

  void OutWebRTC::onRTPPacketizerHasRTCPPacket(char *data, uint32_t nbytes){

    if (nbytes > 2048){
      FAIL_MSG("The received RTCP packet is too big to handle.");
      return;
    }
    if (!data){
      FAIL_MSG("Invalid RTCP packet given.");
      return;
    }

    rtpOutBuffer.allocate(nbytes + 256);
    rtpOutBuffer.assign(data, nbytes);
    int rtcpPacketSize = nbytes;
    if (srtpWriter.protectRtcp((uint8_t *)(void *)rtpOutBuffer, &rtcpPacketSize) != 0){
      ERROR_MSG("Failed to protect the RTCP message.");
      return;
    }

    udp.SendNow(rtpOutBuffer, rtcpPacketSize);
    myConn.addUp(rtcpPacketSize);
  }

  // This function was implemented (it's virtual) to handle
  // pushing of media to the browser. This function blocks until
  // the DTLS handshake has been finished. This prevents
  // `sendNext()` from being called which is correct because we
  // don't want to send packets when we can't protect them with
  // DTLS.
  void OutWebRTC::sendHeader(){

    // first make sure that we complete the DTLS handshake.
    while (!dtlsHandshake.hasKeyingMaterial()){
      if (!handleWebRTCInputOutput()){Util::sleep(10);}
    }

    sentHeader = true;
  }

  void OutWebRTC::sendNext(){

    // once the DTLS handshake has been done, we still have to
    // deal with STUN consent messages and RTCP.
    handleWebRTCInputOutput();

    char *dataPointer = 0;
    size_t dataLen = 0;
    thisPacket.getString("data", dataPointer, dataLen);

    // make sure the webrtcTracks were setup correctly for output.
    uint32_t tid = thisPacket.getTrackId();
    DTSC::Track &dtscTrack = myMeta.tracks[tid];

    WebRTCTrack *trackPointer = 0;

    // If we see this is audio or video, use the webrtc track we negotiated
    if (dtscTrack.type == "video" && webrtcTracks.count(vidTrack)){
      trackPointer = &webrtcTracks[vidTrack];
    }
    if (dtscTrack.type == "audio" && webrtcTracks.count(audTrack)){
      trackPointer = &webrtcTracks[audTrack];
    }

    // If we negotiated this track, always use it directly.
    if (webrtcTracks.count(tid)){trackPointer = &webrtcTracks[tid];}

    // Abort if no negotiation happened
    if (!trackPointer){
      WARN_MSG("Don't know how to send data for track %" PRIu32, tid);
      return;
    }

    WebRTCTrack &rtcTrack = *trackPointer;

    if (rtcTrack.timestampMultiplier == 0){
      FAIL_MSG("The WebRTCTrack::timestampMultiplier is 0; invalid.");
      return;
    }

    uint64_t timestamp = thisPacket.getTime();
    rtcTrack.rtpPacketizer.setTimestamp(timestamp * rtcTrack.timestampMultiplier);

    bool isKeyFrame = thisPacket.getFlag("keyframe");
    didReceiveKeyFrame = isKeyFrame;
    if (isKeyFrame && dtscTrack.codec == "H264"){sendSPSPPS(dtscTrack, rtcTrack);}

    rtcTrack.rtpPacketizer.sendData(&udp, onRTPPacketizerHasDataCallback, dataPointer, dataLen,
                                    rtcTrack.payloadType, dtscTrack.codec);
  }

  // When the RTP::toDTSC converter collected a complete VP8
  // frame, it wil call our callback `onDTSCConverterHasPacket()`
  // with a valid packet that can be fed into
  // MistServer. Whenever we receive a keyframe we update the
  // width and height of the corresponding track.
  void OutWebRTC::extractFrameSizeFromVP8KeyFrame(const DTSC::Packet &pkt){

    char *vp8PayloadBuffer = 0;
    size_t vp8PayloadLen = 0;
    pkt.getString("data", vp8PayloadBuffer, vp8PayloadLen);

    if (!vp8PayloadBuffer || vp8PayloadLen < 9){
      FAIL_MSG("Cannot extract vp8 frame size. Failed to get data.");
      return;
    }

    if (vp8PayloadBuffer[3] != 0x9d || vp8PayloadBuffer[4] != 0x01 || vp8PayloadBuffer[5] != 0x2a){
      FAIL_MSG("Invalid signature. It seems that either the VP8 frames is incorrect or our parsing "
               "is wrong.");
      return;
    }

    uint32_t width = ((vp8PayloadBuffer[7] << 8) + vp8PayloadBuffer[6]) & 0x3FFF;
    uint32_t height = ((vp8PayloadBuffer[9] << 8) + vp8PayloadBuffer[8]) & 0x3FFF;

    DONTEVEN_MSG("Recieved VP8 keyframe with resolution: %u x %u", width, height);

    if (width == 0){
      FAIL_MSG("VP8 frame width is 0; parse error?");
      return;
    }

    if (height == 0){
      FAIL_MSG("VP8 frame height is 0; parse error?");
      return;
    }

    uint64_t trackID = pkt.getTrackId();
    if (myMeta.tracks.count(trackID) == 0){
      FAIL_MSG("No track found with ID %llu.", trackID);
      return;
    }

    DTSC::Track &Trk = myMeta.tracks[trackID];
    Trk.width = width;
    Trk.height = height;
  }

  void OutWebRTC::sendRTCPFeedbackREMB(const WebRTCTrack &rtcTrack){

    if (videoBitrate == 0){
      FAIL_MSG("videoBitrate is 0, which is invalid. Resetting to our default value.");
      videoBitrate = 6 * 1000 * 1000;
    }

    // create the `BR Exp` and `BR Mantissa parts.
    uint32_t br_exponent = 0;
    uint32_t br_mantissa = videoBitrate;
    while (br_mantissa > 0x3FFFF){
      br_mantissa >>= 1;
      ++br_exponent;
    }

    std::vector<uint8_t> buffer;
    buffer.push_back(0x80 | 0x0F);         // V =2 (0x80) | FMT=15 (0x0F)
    buffer.push_back(0xCE);                // payload type = 206
    buffer.push_back(0x00);                // tmp length
    buffer.push_back(0x00);                // tmp length
    buffer.push_back((SSRC >> 24) & 0xFF); // ssrc of sender
    buffer.push_back((SSRC >> 16) & 0xFF); // ssrc of sender
    buffer.push_back((SSRC >> 8) & 0xFF);  // ssrc of sender
    buffer.push_back((SSRC)&0xFF);         // ssrc of sender
    buffer.push_back(0x00);                // ssrc of media source (always 0)
    buffer.push_back(0x00);                // ssrc of media source (always 0)
    buffer.push_back(0x00);                // ssrc of media source (always 0)
    buffer.push_back(0x00);                // ssrc of media source (always 0)
    buffer.push_back('R');                 // `R`, `E`, `M`, `B`
    buffer.push_back('E');                 // `R`, `E`, `M`, `B`
    buffer.push_back('M');                 // `R`, `E`, `M`, `B`
    buffer.push_back('B');                 // `R`, `E`, `M`, `B`
    buffer.push_back(0x01);                // num ssrc
    buffer.push_back((uint8_t)(br_exponent << 2) + ((br_mantissa >> 16) & 0x03)); // br-exp and br-mantissa
    buffer.push_back((uint8_t)(br_mantissa >> 8));  // br-exp and br-mantissa
    buffer.push_back((uint8_t)br_mantissa);         // br-exp and br-mantissa
    buffer.push_back((rtcTrack.SSRC >> 24) & 0xFF); // ssrc to which this remb packet applies to.
    buffer.push_back((rtcTrack.SSRC >> 16) & 0xFF); // ssrc to which this remb packet applies to.
    buffer.push_back((rtcTrack.SSRC >> 8) & 0xFF);  // ssrc to which this remb packet applies to.
    buffer.push_back((rtcTrack.SSRC) & 0xFF);       // ssrc to which this remb packet applies to.

    // rewrite size
    int buffer_size_in_bytes = (int)buffer.size();
    int buffer_size_in_words_minus1 = ((int)buffer.size() / 4) - 1;
    buffer[2] = (buffer_size_in_words_minus1 >> 8) & 0xFF;
    buffer[3] = buffer_size_in_words_minus1 & 0xFF;

    // protect.
    size_t trailer_space = SRTP_MAX_TRAILER_LEN + 4;
    for (size_t i = 0; i < trailer_space; ++i){buffer.push_back(0x00);}

    if (srtpWriter.protectRtcp(&buffer[0], &buffer_size_in_bytes) != 0){
      ERROR_MSG("Failed to protect the RTCP message.");
      return;
    }

    udp.SendNow((const char *)&buffer[0], buffer_size_in_bytes);
    myConn.addUp(buffer_size_in_bytes);
  }

  void OutWebRTC::sendRTCPFeedbackPLI(const WebRTCTrack &rtcTrack){

    std::vector<uint8_t> buffer;
    buffer.push_back(0x80 | 0x01);                  // V=2 (0x80) | FMT=1 (0x01)
    buffer.push_back(0xCE);                         // payload type = 206
    buffer.push_back(0x00);                         // payload size in words minus 1 (2)
    buffer.push_back(0x02);                         // payload size in words minus 1 (2)
    buffer.push_back((SSRC >> 24) & 0xFF);          // ssrc of sender
    buffer.push_back((SSRC >> 16) & 0xFF);          // ssrc of sender
    buffer.push_back((SSRC >> 8) & 0xFF);           // ssrc of sender
    buffer.push_back((SSRC)&0xFF);                  // ssrc of sender
    buffer.push_back((rtcTrack.SSRC >> 24) & 0xFF); // ssrc of receiver
    buffer.push_back((rtcTrack.SSRC >> 16) & 0xFF); // ssrc of receiver
    buffer.push_back((rtcTrack.SSRC >> 8) & 0xFF);  // ssrc of receiver
    buffer.push_back((rtcTrack.SSRC) & 0xFF);       // ssrc of receiver

    // space for protection
    size_t trailer_space = SRTP_MAX_TRAILER_LEN + 4;
    for (size_t i = 0; i < trailer_space; ++i){buffer.push_back(0x00);}

    // protect.
    int buffer_size_in_bytes = (int)buffer.size();
    if (srtpWriter.protectRtcp(&buffer[0], &buffer_size_in_bytes) != 0){
      ERROR_MSG("Failed to protect the RTCP message.");
      return;
    }

    udp.SendNow((const char *)&buffer[0], buffer_size_in_bytes);
    myConn.addUp(buffer_size_in_bytes);
  }

  // Notify sender that we lost a packet. See
  // https://tools.ietf.org/html/rfc4585#section-6.1 which
  // describes the use of the `BLP` field; when more successive
  // sequence numbers are lost it makes sense to implement this
  // too.
  void OutWebRTC::sendRTCPFeedbackNACK(const WebRTCTrack &rtcTrack, uint16_t lostSequenceNumber){
    HIGH_MSG("Requesting missing sequence number %u", lostSequenceNumber);

    std::vector<uint8_t> buffer;
    buffer.push_back(0x80 | 0x01); // V=2 (0x80) | FMT=1 (0x01)
    buffer.push_back(0xCD); // payload type = 205, RTPFB, https://tools.ietf.org/html/rfc4585#section-6.1
    buffer.push_back(0x00);                             // payload size in words minus 1 (3)
    buffer.push_back(0x03);                             // payload size in words minus 1 (3)
    buffer.push_back((SSRC >> 24) & 0xFF);              // ssrc of sender
    buffer.push_back((SSRC >> 16) & 0xFF);              // ssrc of sender
    buffer.push_back((SSRC >> 8) & 0xFF);               // ssrc of sender
    buffer.push_back((SSRC)&0xFF);                      // ssrc of sender
    buffer.push_back((rtcTrack.SSRC >> 24) & 0xFF);     // ssrc of receiver
    buffer.push_back((rtcTrack.SSRC >> 16) & 0xFF);     // ssrc of receiver
    buffer.push_back((rtcTrack.SSRC >> 8) & 0xFF);      // ssrc of receiver
    buffer.push_back((rtcTrack.SSRC) & 0xFF);           // ssrc of receiver
    buffer.push_back((lostSequenceNumber >> 8) & 0xFF); // PID: missing sequence number
    buffer.push_back((lostSequenceNumber)&0xFF);        // PID: missing sequence number
    buffer.push_back(0x00); // BLP: Bitmask of following losses. (not implemented atm).
    buffer.push_back(0x00); // BLP: Bitmask of following losses. (not implemented atm).

    // space for protection
    size_t trailer_space = SRTP_MAX_TRAILER_LEN + 4;
    for (size_t i = 0; i < trailer_space; ++i){buffer.push_back(0x00);}

    // protect.
    int buffer_size_in_bytes = (int)buffer.size();
    if (srtpWriter.protectRtcp(&buffer[0], &buffer_size_in_bytes) != 0){
      ERROR_MSG("Failed to protect the RTCP message.");
      return;
    }

    udp.SendNow((const char *)&buffer[0], buffer_size_in_bytes);
    myConn.addUp(buffer_size_in_bytes);
  }

  void OutWebRTC::sendRTCPFeedbackRR(WebRTCTrack &rtcTrack){

    ((RTP::FECPacket *)&(rtcTrack.rtpPacketizer))->sendRTCP_RR(rtcTrack.sorter, SSRC, rtcTrack.SSRC, (void *)&udp, onRTPPacketizerHasRTCPDataCallback);
  }

  void OutWebRTC::sendSPSPPS(DTSC::Track &dtscTrack, WebRTCTrack &rtcTrack){

    if (dtscTrack.init.empty()){
      WARN_MSG("No init data found in the DTSC::Track. Not sending SPS and PPS");
      return;
    }

    std::vector<char> buf;
    MP4::AVCC avcc;
    avcc.setPayload(dtscTrack.init);

    /* SPS */
    for (uint32_t i = 0; i < avcc.getSPSCount(); ++i){

      uint32_t len = avcc.getSPSLen(i);
      if (len == 0){
        WARN_MSG("Empty SPS stored?");
        continue;
      }

      buf.clear();
      buf.assign(4, 0);
      *(uint32_t *)&buf[0] = htonl(len);
      std::copy(avcc.getSPS(i), avcc.getSPS(i) + avcc.getSPSLen(i), std::back_inserter(buf));

      rtcTrack.rtpPacketizer.sendData(&udp, onRTPPacketizerHasDataCallback, &buf[0], buf.size(),
                                      rtcTrack.payloadType, dtscTrack.codec);
    }

    /* PPS */
    for (uint32_t i = 0; i < avcc.getPPSCount(); ++i){

      uint32_t len = avcc.getPPSLen(i);
      if (len == 0){
        WARN_MSG("Empty PPS stored?");
        continue;
      }

      buf.clear();
      buf.assign(4, 0);
      *(uint32_t *)&buf[0] = htonl(len);
      std::copy(avcc.getPPS(i), avcc.getPPS(i) + avcc.getPPSLen(i), std::back_inserter(buf));

      rtcTrack.rtpPacketizer.sendData(&udp, onRTPPacketizerHasDataCallback, &buf[0], buf.size(),
                                      rtcTrack.payloadType, dtscTrack.codec);
    }
  }

  /* ------------------------------------------------ */

  // This is our thread function that is started right before we
  // call `allowPush()` and send our answer SDP back to the
  // client.
  static void webRTCInputOutputThreadFunc(void *arg){
    if (!classPointer){
      FAIL_MSG("classPointer hasn't been set. Exiting thread.");
      return;
    }
    classPointer->handleWebRTCInputOutputFromThread();
  }

  static int onDTLSHandshakeWantsToWriteCallback(const uint8_t *data, int *nbytes){
    if (!classPointer){
      FAIL_MSG("Requested to send DTLS handshake data but the `classPointer` hasn't been set.");
      return -1;
    }
    return classPointer->onDTLSHandshakeWantsToWrite(data, nbytes);
  }

  static void onRTPSorterHasPacketCallback(const uint64_t track, const RTP::Packet &p){
    if (!classPointer){
      FAIL_MSG("We received a sorted RTP packet but our `classPointer` is invalid.");
      return;
    }
    classPointer->onRTPSorterHasPacket(track, p);
  }

  static void onDTSCConverterHasInitDataCallback(const uint64_t track, const std::string &initData){
    if (!classPointer){
      FAIL_MSG("Received a init data, but our `classPointer` is invalid.");
      return;
    }
    classPointer->onDTSCConverterHasInitData(track, initData);
  }

  static void onDTSCConverterHasPacketCallback(const DTSC::Packet &pkt){
    if (!classPointer){
      FAIL_MSG("Received a DTSC packet that was created from RTP data, but our `classPointer` is "
               "invalid.");
      return;
    }
    classPointer->onDTSCConverterHasPacket(pkt);
  }

  static void onRTPPacketizerHasDataCallback(void *socket, char *data, unsigned int len, unsigned int channel){
    if (!classPointer){
      FAIL_MSG("Received a RTP packet but our `classPointer` is invalid.");
      return;
    }
    classPointer->onRTPPacketizerHasRTPPacket(data, len);
  }

  static void onRTPPacketizerHasRTCPDataCallback(void *socket, const char *data, uint32_t len){
    if (!classPointer){
      FAIL_MSG("Received a RTCP packet, but out `classPointer` is invalid.");
      return;
    }
    classPointer->onRTPPacketizerHasRTCPPacket((char *)data, len);
  }

  static uint32_t generateSSRC(){

    uint32_t ssrc = 0;

    do{
      ssrc = rand();
      ssrc = ssrc << 16;
      ssrc += rand();
    }while (ssrc == 0 || ssrc == 0xffffffff);

    return ssrc;
  }

}// namespace Mist
