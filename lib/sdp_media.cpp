#include "defines.h"
#include "sdp_media.h"
#include <algorithm>
#include <cstdarg>

namespace SDP{

  std::string codecRTP2Mist(const std::string &codec){
    if (codec == "H265"){
      return "HEVC";
    }else if (codec == "H264"){
      return "H264";
    }else if (codec == "VP8"){
      return "VP8";
    }else if (codec == "VP9"){
      return "VP9";
    }else if (codec == "AC3"){
      return "AC3";
    }else if (codec == "PCMA"){
      return "ALAW";
    }else if (codec == "PCMU"){
      return "ULAW";
    }else if (codec == "L8"){
      return "PCM";
    }else if (codec == "L16"){
      return "PCM";
    }else if (codec == "L20"){
      return "PCM";
    }else if (codec == "L24"){
      return "PCM";
    }else if (codec == "MPA"){
      // can also be MP2, the data should be inspected.
      return "MP3";
    }else if (codec == "MPEG4-GENERIC"){
      return "AAC";
    }else if (codec == "OPUS"){
      return "opus";
    }else if (codec == "ULPFEC"){
      return "";
    }else if (codec == "RED"){
      return "";
    }
    ERROR_MSG("%s support not implemented", codec.c_str());
    return "";
  }

  std::string codecMist2RTP(const std::string &codec){
    if (codec == "HEVC"){
      return "H265";
    }else if (codec == "H264"){
      return "H264";
    }else if (codec == "VP8"){
      return "VP8";
    }else if (codec == "VP9"){
      return "VP9";
    }else if (codec == "AC3"){
      return "AC3";
    }else if (codec == "ALAW"){
      return "PCMA";
    }else if (codec == "ULAW"){
      return "PCMU";
    }else if (codec == "PCM"){
      return "L24";
    }else if (codec == "MP2"){
      return "MPA";
    }else if (codec == "MP3"){
      return "MPA";
    }else if (codec == "AAC"){
      return "MPEG4-GENERIC";
    }else if (codec == "opus"){
      return "OPUS";
    }else if (codec == "ULPFEC"){
      return "";
    }else if (codec == "RED"){
      return "";
    }
    ERROR_MSG("%s support not implemented", codec.c_str());
    BACKTRACE;
    return "";
  }

  static std::vector<std::string>
  sdp_split(const std::string &str, const std::string &delim,
            bool keepEmpty); // Split a string on the given delimeter and return a vector with the parts.
  static bool sdp_extract_payload_type(const std::string &str,
                                       uint64_t &result); // Extract the payload number from a SDP line that
                                                          // starts like: `a=something:[payloadtype]`.
  static bool sdp_get_name_value_from_varval(const std::string &str, std::string &var,
                                             std::string &value); // Extracts the `name` and `value` from a string like `<name>=<value>`.
                                                                  // The `name` will always be converted to lowercase!.
  static void
  sdp_get_all_name_values_from_string(const std::string &str, std::map<std::string, std::string> &result); // Extracts all the name/value pairs from a string like:
                                                                                                           // `<name>=<value>;<name>=<value>`. The `name` part will
                                                                                                           // always be converted to lowercase.
  static bool sdp_get_attribute_value(const std::string &str,
                                      std::string &result); // Extract an "attribute" value, which is formatted
                                                            // like: `a=something:<attribute-value>`
  static std::string string_to_upper(const std::string &str);

  MediaFormat::MediaFormat(){
    payloadType = SDP_PAYLOAD_TYPE_NONE;
    associatedPayloadType = SDP_PAYLOAD_TYPE_NONE;
    audioSampleRate = 0;
    audioNumChannels = 0;
    audioBitSize = 0;
    videoRate = 0;
  }

  /// \TODO what criteria do you (Jaron) want to use?
  MediaFormat::operator bool() const{
    if (payloadType == SDP_PAYLOAD_TYPE_NONE){return false;}
    if (encodingName.empty()){return false;}
    return true;
  }

  uint32_t MediaFormat::getAudioSampleRate() const{
    if (0 != audioSampleRate){return audioSampleRate;}
    if (payloadType != SDP_PAYLOAD_TYPE_NONE){
      switch (payloadType){
      case 0:{
        return 8000;
      }
      case 8:{
        return 8000;
      }
      case 10:{
        return 44100;
      }
      case 11:{
        return 44100;
      }
      }
    }
    return 0;
  }

  uint32_t MediaFormat::getAudioNumChannels() const{
    if (0 != audioNumChannels){return audioNumChannels;}
    if (payloadType != SDP_PAYLOAD_TYPE_NONE){
      switch (payloadType){
      case 0:{
        return 1;
      }
      case 8:{
        return 1;
      }
      case 10:{
        return 2;
      }
      case 11:{
        return 1;
      }
      }
    }
    return 0;
  }

  uint32_t MediaFormat::getAudioBitSize() const{

    if (0 != audioBitSize){return audioBitSize;}

    if (payloadType != SDP_PAYLOAD_TYPE_NONE){
      switch (payloadType){
      case 10:{
        return 16;
      }
      case 11:{
        return 16;
      }
      }
    }

    if (encodingName == "L8"){return 8;}
    if (encodingName == "L16"){return 16;}
    if (encodingName == "L20"){return 20;}
    if (encodingName == "L24"){return 24;}

    return 0;
  }

  uint32_t MediaFormat::getVideoRate() const{

    if (0 != videoRate){return videoRate;}

    if (encodingName == "H264"){
      return 90000;
    }else if (encodingName == "H265"){
      return 90000;
    }else if (encodingName == "VP8"){
      return 90000;
    }else if (encodingName == "VP9"){
      return 90000;
    }

    return 0;
  }

  /// \todo Maybe we should create one member `rate` which is used by audio and video (?)
  uint32_t MediaFormat::getVideoOrAudioRate() const{
    uint32_t r = getAudioSampleRate();
    if (0 == r){r = getVideoRate();}
    return r;
  }

  std::string MediaFormat::getFormatParameterForName(const std::string &name) const{
    std::string name_lower = name;
    std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(), ::tolower);
    std::map<std::string, std::string>::const_iterator it = formatParameters.find(name_lower);
    if (it == formatParameters.end()){return "";}
    return it->second;
  }

  uint64_t MediaFormat::getPayloadType() const{return payloadType;}

  int32_t MediaFormat::getPacketizationModeForH264(){

    if (encodingName != "H264"){
      ERROR_MSG("Encoding is not H264.");
      return -1;
    }

    std::string val = getFormatParameterForName("packetization-mode");
    if (val.empty()){
      WARN_MSG(
          "No packetization-mode found for this format. We default to packetization-mode = 0.");
      return 0;
    }

    std::stringstream ss;
    ss << val;
    int32_t pm = 0;
    ss >> pm;

    return pm;
  }

  std::string MediaFormat::getProfileLevelIdForH264(){

    if (encodingName != "H264"){
      ERROR_MSG("Encoding is not H264, cannot get profile-level-id.");
      return "";
    }

    return getFormatParameterForName("profile-level-id");
  }

  Media::Media(){
    framerate = 0.0;
    supportsRTCPMux = false;
    supportsRTCPReducedSize = false;
    candidatePort = 0;
    SSRC = 0;
  }

  /// \TODO what other checks do you want to perform?
  Media::operator bool() const{
    if (formats.size() == 0){return false;}
    if (type.empty()){return false;}
    return true;
  }

  /// Parses a SDP media line like `m=video 9 UDP/TLS/RTP/SAVPF
  /// 96 97 98 99 100 101 102` For each payloadtype it will
  /// create a MediaFormat entry and initializes it with some
  /// default settings.
  bool Media::parseMediaLine(const std::string &line){

    // split and verify
    std::vector<std::string> words = sdp_split(line, " ", false);
    if (!words.size()){
      ERROR_MSG("Invalid media line.");
      return false;
    }

    // check if we're dealing with audio or video.
    if (words[0] == "m=audio"){
      type = "audio";
    }else if (words[0] == "m=video"){
      type = "video";
    }else{
      ERROR_MSG("Unhandled media type: `%s`.", words[0].c_str());
      return false;
    }

    // proto: UDP/TLS/RTP/SAVP
    proto = words[2];

    // create static and dynamic tracks.
    for (size_t i = 3; i < words.size(); ++i){
      SDP::MediaFormat format;
      format.payloadType = JSON::Value(words[i]).asInt();
      formats[format.payloadType] = format;
      if (!payloadTypes.empty()){payloadTypes += " ";}
      payloadTypes += words[i];
    }

    return true;
  }

  bool Media::parseRtpMapLine(const std::string &line){

    MediaFormat *format = getFormatForSdpLine(line);
    if (NULL == format){
      ERROR_MSG(
          "Cannot parse the a=rtpmap line because we did not find the track for the payload type.");
      return false;
    }

    // convert <encoding-name> to fullcaps
    std::string mediaType = line.substr(line.find(' ', 8) + 1);
    std::string encodingName = mediaType.substr(0, mediaType.find('/'));
    for (unsigned int i = 0; i < encodingName.size(); ++i){
      if (encodingName[i] <= 122 && encodingName[i] >= 97){encodingName[i] -= 32;}
    }
    format->encodingName = encodingName;
    format->rtpmap = line.substr(line.find("=") + 1);

    // extract audio info
    if (type == "audio"){
      std::string extraInfo = mediaType.substr(mediaType.find('/') + 1);
      size_t lastSlash = extraInfo.find('/');
      if (lastSlash != std::string::npos){
        format->audioSampleRate = atoll(extraInfo.substr(0, lastSlash).c_str());
        format->audioNumChannels = atoll(extraInfo.substr(lastSlash + 1).c_str());
      }else{
        format->audioSampleRate = atoll(extraInfo.c_str());
        format->audioNumChannels = 1;
      }
    }

    return true;
  }

  bool Media::parseRtspControlLine(const std::string &line){

    if (line.substr(0, 10) != "a=control:"){
      ERROR_MSG(
          "Cannot parse the given rtsp control url line because it's incorrectly formatted: `%s`.",
          line.c_str());
      return false;
    }

    control = line.substr(10);
    if (control.empty()){
      ERROR_MSG("Failed to parse the rtsp control line.");
      return false;
    }

    return true;
  }

  bool Media::parseFrameRateLine(const std::string &line){

    if (line.substr(0, 12) != "a=framerate:"){
      ERROR_MSG("Cannot parse the `a=framerate:` line because it's incorrectly formatted: `%s`.", line.c_str());
      return false;
    }

    framerate = atof(line.c_str() + 12) * 1000;
    return true;
  }

  /// Parses a line like:
  /// `a=fmtp:97
  /// streamtype=5;profile-level-id=2;mode=AAC-hbr;config=1408;sizelength=13;indexlength=3;indexdeltalength=3;bitrate=32000`
  /// `a=fmtp:96
  /// packetization-mode=1;profile-level-id=4d0029;sprop-parameter-sets=Z00AKeKQCADDYC3AQEBpB4kRUA==,aO48gA==`
  /// `a=fmtp:97 apt=96`
  bool Media::parseFormatParametersLine(const std::string &line){

    MediaFormat *format = getFormatForSdpLine(line);
    if (!format){
      ERROR_MSG("No format found for the given `a=fmtp:` line. The payload type (<fmt>) should be "
                "part of the media line.");
      return false;
    }

    // start parsing the parameters after the first <space> character.
    size_t start = line.find(" ");
    if (start == std::string::npos){
      ERROR_MSG(
          "Invalid formatted a=fmtp line. No space between format and data. `a=fmtp:<fmt> <data>`");
      return false;
    }
    start = start + 1;
    sdp_get_all_name_values_from_string(line.substr(start), format->formatParameters);

    // When this format is associated with another format
    // which is the case for RTX, we store the associated
    // payload type too. `apt` means "Associated Payload Type".
    if (format->formatParameters.count("apt") != 0){
      std::stringstream ss(format->formatParameters["apt"]);
      ss >> format->associatedPayloadType;
    }
    return true;
  }

  bool Media::parseRtcpFeedbackLine(const std::string &line){

    // does this feedback mechanism apply to all or only a specific format?
    MediaFormat *format = NULL;
    size_t num_formats = 0;
    if (line.substr(0, 11) == "a=rtcp-fb:*"){
      num_formats = formats.size();
      format = &formats[0];
    }else{
      num_formats = 1;
      format = getFormatForSdpLine(line);
    }

    // make sure we found something valid.
    if (!format){
      ERROR_MSG("No format found for the given `a=rtcp-fb` line. The payload type (<fmt>) should "
                "be part of the media line.");
      return false;
    }
    if (num_formats == 0){
      ERROR_MSG("num_formats is 0. Seems like no format has been added. Invalid media line in SDP "
                "maybe?");
      return false;
    }
    std::string fb = line.substr(11);
    if (fb.empty()){
      ERROR_MSG("The given `a=rtcp-fb` line doesn't contain a rtcp-fb-val.");
      return false;
    }

    // add the feedback mechanism to the found format(s)
    for (size_t i = 0; i < num_formats; ++i){format[i].rtcpFormats.insert(fb);}

    return true;
  }
  /// Extracts the fingerpint hash and value, from a line like:
  /// a=fingerprint:sha-256
  /// C7:98:6F:A9:55:75:C0:73:F2:EB:CF:14:B8:6E:58:FE:A5:F1:B0:C7:41:B7:BA:D3:4A:CF:7E:5C:69:8B:FA:F4
  bool Media::parseFingerprintLine(const std::string &sdpLine){

    // extract the <hash> type.
    size_t start = sdpLine.find(":");
    if (start == std::string::npos){
      ERROR_MSG("Invalid `a=fingerprint:<hash> <value>` line, no `:` found.");
      return false;
    }
    size_t end = sdpLine.find(" ", start);
    if (end == std::string::npos){
      ERROR_MSG("Invalid `a=fingerprint:<hash> <value>` line, no <space> found after `:`.");
      return false;
    }
    if (end <= start){
      ERROR_MSG("Invalid `a=fingerpint:<hash> <value>` line. Space before the `:` found.");
      return false;
    }
    fingerprintHash = sdpLine.substr(start, end - start);
    fingerprintValue = sdpLine.substr(end);
    return true;
  }

  bool Media::parseSSRCLine(const std::string &sdpLine){

    if (0 != SSRC){
      // We set our SSRC to the first one that we found.
      return true;
    }

    size_t firstSpace = sdpLine.find(" ");
    if (firstSpace == std::string::npos){
      ERROR_MSG("Failed to parse the `a=ssrc:<ssrc>` line.");
      return false;
    }
    if (firstSpace < 7){
      ERROR_MSG("We found an invalid space position. Cannot get SSRC.");
      return false;
    }

    std::string ssrcStr = sdpLine.substr(7, firstSpace - 7);
    std::stringstream ss;
    ss << ssrcStr;
    ss >> SSRC;

    return true;
  }

  MediaFormat *Media::getFormatForSdpLine(const std::string &sdpLine){
    uint64_t payloadType = 0;
    if (!sdp_extract_payload_type(sdpLine, payloadType)){
      ERROR_MSG("Cannot get track for the given SDP line: %s", sdpLine.c_str());
      return NULL;
    }
    return getFormatForPayloadType(payloadType);
  }

  MediaFormat *Media::getFormatForPayloadType(uint64_t &payloadType){
    std::map<uint64_t, MediaFormat>::iterator it = formats.find(payloadType);
    if (it == formats.end()){
      ERROR_MSG("No format found for payload type: %" PRIu64 ".", payloadType);
      return NULL;
    }
    return &it->second;
  }

  // This will check if there is a `SDP::MediaFormat` with a
  // codec that matches the given codec name. Note that we will
  // convert the given `encName` into fullcaps as SDP stores all
  // codecs in caps.
  MediaFormat *Media::getFormatForEncodingName(const std::string &encName){

    std::string encNameCaps = codecMist2RTP(encName);
    std::map<uint64_t, MediaFormat>::iterator it = formats.begin();
    while (it != formats.end()){
      MediaFormat &mf = it->second;
      if (mf.encodingName == encNameCaps){return &mf;}
      ++it;
    }

    return NULL;
  }

  std::vector<SDP::MediaFormat *> Media::getFormatsForEncodingName(const std::string &encName){

    std::string encNameCaps = string_to_upper(encName);
    std::vector<MediaFormat *> result;
    std::map<uint64_t, MediaFormat>::iterator it = formats.begin();
    while (it != formats.end()){
      MediaFormat &mf = it->second;
      if (mf.encodingName == encNameCaps){result.push_back(&mf);}
      ++it;
    }

    return result;
  }

  MediaFormat *Media::getRetransMissionFormatForPayloadType(uint64_t pt){

    std::vector<SDP::MediaFormat *> rtxFormats = getFormatsForEncodingName("RTX");
    if (rtxFormats.size() == 0){return NULL;}

    for (size_t i = 0; i < rtxFormats.size(); ++i){
      if (rtxFormats[i]->associatedPayloadType == pt){return rtxFormats[i];}
    }

    return NULL;
  }

  std::string Media::getIcePwdForFormat(const MediaFormat &fmt){
    if (!fmt.icePwd.empty()){return fmt.icePwd;}
    return icePwd;
  }

  uint32_t Media::getSSRC() const{return SSRC;}

  // Get the `SDP::Media*` for a given type, e.g. audio or video.
  Media *Session::getMediaForType(const std::string &type){
    size_t n = medias.size();
    for (size_t i = 0; i < n; ++i){
      if (medias[i].type == type){return &medias[i];}
    }
    return NULL;
  }

  /// Get the `SDP::MediaFormat` which represents the format and
  /// e.g. encoding, rtp attributes for a specific codec (H264, OPUS, etc.)
  ///
  /// @param mediaType      `video` or `audio`
  /// @param encodingName   Encoding name in fullcaps, e.g. `H264`, `OPUS`, etc.
  MediaFormat *Session::getMediaFormatByEncodingName(const std::string &mediaType, const std::string &encodingName){
    SDP::Media *media = getMediaForType(mediaType);
    if (!media){
      WARN_MSG("No SDP::Media found for media type %s.", mediaType.c_str());
      return NULL;
    }

    SDP::MediaFormat *mediaFormat = media->getFormatForEncodingName(encodingName);
    if (!mediaFormat){
      WARN_MSG("No SDP::MediaFormat found for encoding name %s.", encodingName.c_str());
      return NULL;
    }
    return mediaFormat;
  }

  bool Session::hasReceiveOnlyMedia(){
    size_t numMedias = medias.size();
    for (size_t i = 0; i < numMedias; ++i){
      if (medias[i].direction == "recvonly"){return true;}
    }
    return false;
  }

  bool Session::hasSendOnlyMedia(){
    size_t numMedias = medias.size();
    for (size_t i = 0; i < numMedias; ++i){
      if (medias[i].direction == "sendonly"){return true;}
    }
    return false;
  }

  bool Session::parseSDP(const std::string &sdp){

    if (sdp.empty()){
      FAIL_MSG("Requested to parse an empty SDP.");
      return false;
    }

    SDP::Media *currMedia = NULL;
    std::stringstream ss(sdp);
    std::string line;
    bool mozilla = false;

    while (std::getline(ss, line, '\n')){

      // validate line
      if (!line.empty() && *line.rbegin() == '\r'){line.erase(line.size() - 1, 1);}
      if (line.empty()){
        continue;
      }
      if (line.find("mozilla") != std::string::npos){mozilla = true;}

      // Parse session (or) media data.
      else if (line.substr(0, 2) == "m="){
        SDP::Media media;
        if (!media.parseMediaLine(line)){
          ERROR_MSG("Failed to parse the m= line.");
          return false;
        }
        medias.push_back(media);
        currMedia = &medias.back();
        // set properties which can be global and may be overwritten per stream
        currMedia->iceUFrag = iceUFrag;
        currMedia->icePwd = icePwd;
      }

      // the lines below assume that we found a media line already.
      if (!currMedia){continue;}

      // parse properties we need later.
      if (line.substr(0, 8) == "a=rtpmap"){
        currMedia->parseRtpMapLine(line);
      }else if (line.substr(0, 10) == "a=control:"){
        currMedia->parseRtspControlLine(line);
      }else if (line.substr(0, 12) == "a=framerate:"){
        currMedia->parseFrameRateLine(line);
      }else if (line.substr(0, 7) == "a=fmtp:"){
        currMedia->parseFormatParametersLine(line);
      }else if (line.substr(0, 11) == "a=rtcp-fb:"){
        currMedia->parseRtcpFeedbackLine(line);
      }else if (line.substr(0, 10) == "a=sendonly"){
        currMedia->direction = "sendonly";
      }else if (line.substr(0, 10) == "a=sendrecv"){
        currMedia->direction = "sendrecv";
      }else if (line.substr(0, 10) == "a=recvonly"){
        currMedia->direction = "recvonly";
      }else if (line.substr(0, 11) == "a=ice-ufrag"){
        sdp_get_attribute_value(line, currMedia->iceUFrag);
      }else if (line.substr(0, 9) == "a=ice-pwd"){
        sdp_get_attribute_value(line, currMedia->icePwd);
      }else if (line.substr(0, 10) == "a=rtcp-mux"){
        currMedia->supportsRTCPMux = true;
      }else if (line.substr(0, 10) == "a=rtcp-rsize"){
        currMedia->supportsRTCPReducedSize = true;
      }else if (line.substr(0, 7) == "a=setup"){
        sdp_get_attribute_value(line, currMedia->setupMethod);
      }else if (line.substr(0, 13) == "a=fingerprint"){
        currMedia->parseFingerprintLine(line);
      }else if (line.substr(0, 6) == "a=mid:"){
        sdp_get_attribute_value(line, currMedia->mediaID);
      }else if (line.substr(0, 7) == "a=ssrc:"){
        currMedia->parseSSRCLine(line);
      }else if (mozilla && line.substr(0, 9) == "a=extmap:"){
        currMedia->extmap.insert(line);
      }
    }// while

    return true;
  }

  static std::vector<std::string> sdp_split(const std::string &str, const std::string &delim, bool keepEmpty){
    std::vector<std::string> strings;
    std::ostringstream word;
    for (size_t n = 0; n < str.size(); ++n){
      if (std::string::npos == delim.find(str[n])){
        word << str[n];
      }else{
        if (false == word.str().empty() || true == keepEmpty){strings.push_back(word.str());}
        word.str("");
      }
    }
    if (false == word.str().empty() || true == keepEmpty){strings.push_back(word.str());}
    return strings;
  }

  static bool sdp_extract_payload_type(const std::string &str, uint64_t &result){
    // extract payload type.
    size_t start_pos = str.find_first_of(':');
    size_t end_pos = str.find_first_of(' ', start_pos);
    if (start_pos == std::string::npos || end_pos == std::string::npos || (start_pos + 1) >= end_pos){
      FAIL_MSG("Invalid `a=rtpmap` line. Has not payload type.");
      return false;
    }
    // make sure payload type was part of the media line and is supported.
    result = JSON::Value(str.substr(start_pos + 1, end_pos - (start_pos + 1))).asInt();
    return true;
  }

  // Extract the `name` and `value` from a string like
  // `<name>=<value>`.  This function will return on success,
  // when it extract the `name` and `value` and returns false
  // when the given input string doesn't contains a
  // `<name>=<value>` pair. This function is for example used
  // when parsing the `a=fmtp:<fmt>` line.
  //
  // Note that we will always convert the `var` to lowercase.
  static bool sdp_get_name_value_from_varval(const std::string &str, std::string &var, std::string &value){

    if (str.empty()){
      ERROR_MSG("Cannot get `name` and `value` from string because the given string is empty. "
                "String is: `%s`",
                str.c_str());
      return false;
    }

    size_t pos = str.find("=");
    if (pos == std::string::npos){
      WARN_MSG("Cannot get `name` and `value` from string becuase it doesn't contain a `=` sign. "
               "String is: `%s`. Returning the string as is.",
               str.c_str());
      value = str;
      return true;
    }

    var = str.substr(0, pos);
    value = str.substr(pos + 1, str.length() - pos);
    std::transform(var.begin(), var.end(), var.begin(), ::tolower);
    return true;
  }

  // This function will extract name=value pairs from a string
  // which are separated by a ";" delmiter. In the future we
  // might want to pass this delimiter as an parameter.
  // Currently this is used to parse a `a=fmtp` line.
  static void sdp_get_all_name_values_from_string(const std::string &str,
                                                  std::map<std::string, std::string> &result){

    std::string varval;
    std::string name;
    std::string value;
    size_t start = 0;
    size_t end = str.find(";");
    while (end != std::string::npos){
      varval = str.substr(start, end - start);
      if (sdp_get_name_value_from_varval(varval, name, value)){result[name] = value;}
      start = end + 1;
      end = str.find(";", start);
    }

    // the last element needs to read separately
    varval = str.substr(start, end);
    if (sdp_get_name_value_from_varval(varval, name, value)){result[name] = value;}
  }

  // Extract an "attribute" value, which is formatted like:
  // `a=something:<attribute-value>`
  static bool sdp_get_attribute_value(const std::string &str, std::string &result){

    if (str.empty()){
      ERROR_MSG("Cannot get attribute value because the given string is empty.");
      return false;
    }

    size_t start = str.find(":");
    if (start == std::string::npos){
      ERROR_MSG("Cannot get attribute value because we did not find the : character in %s.", str.c_str());
      return false;
    }

    result = str.substr(start + 1, result.length() - start);
    return true;
  }

  Answer::Answer()
      : isAudioEnabled(false), isVideoEnabled(false), candidatePort(0),
        videoLossPrevention(SDP_LOSS_PREVENTION_NONE){}

  bool Answer::parseOffer(const std::string &sdp){

    if (!sdpOffer.parseSDP(sdp)){
      FAIL_MSG("Cannot parse given offer SDP.");
      return false;
    }

    return true;
  }

  bool Answer::hasVideo(){
    SDP::Media *m = sdpOffer.getMediaForType("video");
    return (m != NULL) ? true : false;
  }

  bool Answer::hasAudio(){
    SDP::Media *m = sdpOffer.getMediaForType("audio");
    return (m != NULL) ? true : false;
  }

  bool Answer::enableVideo(const std::string &codecName){
    if (!enableMedia("video", codecName, answerVideoMedia, answerVideoFormat)){
      DONTEVEN_MSG("Failed to enable video.");
      return false;
    }
    isVideoEnabled = true;
    return true;
  }

  bool Answer::enableAudio(const std::string &codecName){
    if (!enableMedia("audio", codecName, answerAudioMedia, answerAudioFormat)){
      DONTEVEN_MSG("Not enabling audio.");
      return false;
    }
    isAudioEnabled = true;
    return true;
  }

  void Answer::setCandidate(const std::string &ip, uint16_t port){
    if (ip.empty()){WARN_MSG("Given candidate IP is empty. It's fine if you want to unset it.");}
    candidateIP = ip;
    candidatePort = port;
  }

  void Answer::setFingerprint(const std::string &fingerprintSha){
    if (fingerprintSha.empty()){
      WARN_MSG(
          "Given fingerprint is empty; fine when you want to unset it; otherwise check your code.");
    }
    fingerprint = fingerprintSha;
  }

  void Answer::setDirection(const std::string &dir){
    if (dir.empty()){WARN_MSG("Given direction string is empty; fine if you want to unset.");}
    direction = dir;
  }

  bool Answer::setupVideoDTSCTrack(DTSC::Meta &M, size_t tid){
    if (!isVideoEnabled){
      FAIL_MSG("Video is disabled; cannot setup DTSC::Track.");
      return false;
    }

    M.setCodec(tid, codecRTP2Mist(answerVideoFormat.encodingName));
    if (M.getCodec(tid).empty()){
      FAIL_MSG("Failed to convert the format codec into one that " APPNAME " understands. %s.",
               answerVideoFormat.encodingName.c_str());
      return false;
    }
    M.setType(tid, "video");
    M.setRate(tid, answerVideoFormat.getVideoRate());
    M.setID(tid, answerVideoFormat.payloadType);
    INFO_MSG("Setup video track %zu for payload type %" PRIu64, tid, answerVideoFormat.payloadType);
    return true;
  }

  bool Answer::setupAudioDTSCTrack(DTSC::Meta &M, size_t tid){
    if (!isAudioEnabled){
      FAIL_MSG("Audio is disabled; cannot setup DTSC::Track.");
      return false;
    }

    M.setCodec(tid, codecRTP2Mist(answerAudioFormat.encodingName));
    if (M.getCodec(tid).empty()){
      FAIL_MSG("Failed to convert the format codec into one that " APPNAME " understands. %s.",
               answerAudioFormat.encodingName.c_str());
      return false;
    }

    M.setType(tid, "audio");
    M.setRate(tid, answerAudioFormat.getAudioSampleRate());
    M.setChannels(tid, answerAudioFormat.getAudioNumChannels());
    M.setSize(tid, answerAudioFormat.getAudioBitSize());
    M.setID(tid, answerAudioFormat.payloadType);
    INFO_MSG("Setup audio track %zu for payload time %" PRIu64, tid, answerAudioFormat.payloadType);
    return true;
  }

  std::string Answer::toString(){

    if (direction.empty()){
      FAIL_MSG("Cannot create SDP answer; direction not set. call setDirection().");
      return "";
    }
    if (candidateIP.empty()){
      FAIL_MSG("Cannot create SDP answer; candidate not set. call setCandidate().");
      return "";
    }
    if (fingerprint.empty()){
      FAIL_MSG("Cannot create SDP answer; fingerprint not set. call setFingerpint().");
      return "";
    }

    std::string result;
    output.clear();

    // session
    addLine("v=0");
    addLine("o=- %s 0 IN IP4 0.0.0.0", generateSessionId().c_str());
    addLine("s=-");
    addLine("t=0 0");
    addLine("a=ice-lite");

    // session: bundle (audio and video use same candidate)
    if (isVideoEnabled && isAudioEnabled){
      if (answerVideoMedia.mediaID.empty()){
        FAIL_MSG("video media has no media id; necessary for BUNDLE.");
        return "";
      }
      if (answerAudioMedia.mediaID.empty()){
        FAIL_MSG("audio media has no media id; necessary for BUNDLE.");
        return "";
      }
      std::string bundled;
      for (size_t i = 0; i < sdpOffer.medias.size(); ++i){
        if (sdpOffer.medias[i].type == "audio" || sdpOffer.medias[i].type == "video"){
          if (!bundled.empty()){bundled += " ";}
          bundled += sdpOffer.medias[i].mediaID;
        }
      }
      addLine("a=group:BUNDLE %s", bundled.c_str());
    }

    // add medias (order is important)
    for (size_t i = 0; i < sdpOffer.medias.size(); ++i){

      SDP::Media &mediaOffer = sdpOffer.medias[i];
      std::string type = mediaOffer.type;
      SDP::Media *media = NULL;
      SDP::MediaFormat *fmtMedia = NULL;
      SDP::MediaFormat *fmtRED = NULL;
      SDP::MediaFormat *fmtULPFEC = NULL;

      bool isEnabled = false;
      std::vector<uint8_t> supportedPayloadTypes;
      if (type != "audio" && type != "video"){continue;}

      // port = 9 (default), port = 0 (disable this media)
      if (type == "audio"){
        isEnabled = isAudioEnabled;
        media = &answerAudioMedia;
        fmtMedia = &answerAudioFormat;
      }else if (type == "video"){
        isEnabled = isVideoEnabled;
        media = &answerVideoMedia;
        fmtMedia = &answerVideoFormat;
        fmtRED = media->getFormatForEncodingName("RED");
        fmtULPFEC = media->getFormatForEncodingName("ULPFEC");
      }

      if (!media){
        WARN_MSG("No media found.");
        continue;
      }
      if (!fmtMedia){
        WARN_MSG("No format found.");
        continue;
      }
      // we collect all supported payload types (e.g. RED and
      // ULPFEC have their own payload type). We then serialize
      // them payload types into a string that is used with the
      // `m=` line to indicate we have support for these.
      supportedPayloadTypes.push_back((uint8_t)fmtMedia->payloadType);
      if ((videoLossPrevention & SDP_LOSS_PREVENTION_ULPFEC) && fmtRED && fmtULPFEC){
        supportedPayloadTypes.push_back(fmtRED->payloadType);
        supportedPayloadTypes.push_back(fmtULPFEC->payloadType);
      }

      std::stringstream ss;
      size_t nels = supportedPayloadTypes.size();
      for (size_t k = 0; k < nels; ++k){
        ss << (int)supportedPayloadTypes[k];
        if ((k + 1) < nels){ss << " ";}
      }
      std::string payloadTypes = ss.str();

      if (isEnabled){
        addLine("m=%s 9 UDP/TLS/RTP/SAVPF %s", type.c_str(), payloadTypes.c_str());
      }else{
        addLine("m=%s %u UDP/TLS/RTP/SAVPF %s", type.c_str(), 0, mediaOffer.payloadTypes.c_str());
      }

      addLine("c=IN IP4 0.0.0.0");
      if (!isEnabled){
        // We have to add the direction otherwise we'll receive an error
        // like "Answer tried to set recv when offer did not set send"
        // from Firefox.
        addLine("a=%s", direction.c_str());
        continue;
      }

      addLine("a=rtcp:9");
      addLine("a=%s", direction.c_str());
      addLine("a=setup:passive");
      addLine("a=fingerprint:sha-256 %s", fingerprint.c_str());
      addLine("a=ice-ufrag:%s", fmtMedia->iceUFrag.c_str());
      addLine("a=ice-pwd:%s", fmtMedia->icePwd.c_str());
      addLine("a=rtcp-mux");
      addLine("a=rtcp-rsize");
      addLine("a=%s", fmtMedia->rtpmap.c_str());

      // BEGIN FEC/RTX: testing with just FEC or RTX
      if ((videoLossPrevention & SDP_LOSS_PREVENTION_ULPFEC) && fmtRED && fmtULPFEC){
        addLine("a=rtpmap:%u ulpfec/90000", fmtULPFEC->payloadType);
        addLine("a=rtpmap:%u red/90000", fmtRED->payloadType);
      }
      if (videoLossPrevention & SDP_LOSS_PREVENTION_NACK){
        addLine("a=rtcp-fb:%u nack", fmtMedia->payloadType);
      }
      // END FEC/RTX
      if (type == "video"){addLine("a=rtcp-fb:%u goog-remb", fmtMedia->payloadType);}

      if (!media->mediaID.empty()){addLine("a=mid:%s", media->mediaID.c_str());}

      if (fmtMedia->encodingName == "H264"){
        std::string usedProfile = fmtMedia->getFormatParameterForName("profile-level-id");
        if (usedProfile != "42e01f"){
          WARN_MSG("The selected profile-level-id was not 42e01f. We rewrite it into this because "
                   "that's what we support atm.");
          usedProfile = "42e01f";
        }

        addLine("a=fmtp:%u profile-level-id=42e01f;level-asymmetry-allowed=1;packetization-mode=1",
                fmtMedia->payloadType);
      }else if (fmtMedia->encodingName == "OPUS"){
        addLine("a=fmtp:%u minptime=10;useinbandfec=1", fmtMedia->payloadType);
      }
      //hacky way of adding sdes:mid extension line
      if (isVideoEnabled && isAudioEnabled){
        for (std::set<std::string>::iterator it = media->extmap.begin(); it != media->extmap.end(); ++it){
          if (it->find("sdes:mid") != std::string::npos){
            addLine(*it);
          }
        }
      }
      addLine("a=candidate:1 1 udp 2130706431 %s %u typ host", candidateIP.c_str(), candidatePort);
      addLine("a=end-of-candidates");
    }

    // combine all the generated lines.
    size_t nlines = output.size();
    for (size_t i = 0; i < nlines; ++i){result += output[i] + "\r\n";}

    return result;
  }

  // The parameter here is NOT a reference, because va_start specifies that its parameter is not
  // allowed to be one.
  void Answer::addLine(const std::string fmt, ...){

    char buffer[1024] ={};
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt.c_str(), args);
    va_end(args);

    output.push_back(buffer);
  }

  // This function will check if the offer you passed into
  // `parseOffer()` contains a media line for the given
  // `type`. When found we also check if it contains a codec for
  // the given `codecName`. When both are found copy it to the
  // given `outMedia` and `outFormat` (which are our answer
  // objects. We also generate the values for ice-pwd and
  // ice-ufrag which are used during STUN.
  //
  // @param codecName (string)   Can be a comma separated
  //                             string with codecs that you
  //                             support; we select the first
  //                             one that we find.
  bool Answer::enableMedia(const std::string &type, const std::string &codecList,
                           SDP::Media &outMedia, SDP::MediaFormat &outFormat){
    Media *media = sdpOffer.getMediaForType(type);
    if (!media){
      INFO_MSG("Cannot enable %s codec; offer doesn't have %s media.", codecList.c_str(), type.c_str());
      return false;
    }

    std::vector<std::string> codecs = splitString(codecList, ',');
    if (codecs.size() == 0){
      FAIL_MSG("Failed to split the given codecList.");
      return false;
    }

    // ok, this is a bit ugly sorry for that... but when H264 was
    // requested we have to check if the packetization mode is
    // what we support. Firefox does packetization-mode 0 and 1
    // and provides both formats in their SDP. It may happen that
    // an SDP contains multiple format-specs for H264
    SDP::MediaFormat *format = NULL;
    SDP::MediaFormat *backupFormat = NULL;
    for (size_t i = 0; i < codecs.size(); ++i){
      std::string codec = codecMist2RTP(codecs[i]);
      std::vector<SDP::MediaFormat *> formats = media->getFormatsForEncodingName(codec);
      for (size_t j = 0; j < formats.size(); ++j){
        if (codec == "H264"){
          if (formats[j]->getPacketizationModeForH264() != 1){
            MEDIUM_MSG("Skipping this H264 format because it uses a packetization mode we don't support.");
            continue;
          }
          if (formats[j]->getProfileLevelIdForH264() != "42e01f"){
            MEDIUM_MSG("Skipping this H264 format because it uses an unsupported profile-level-id.");
            //Store the best match so far, though - just in case we never find a proper match
            if (!backupFormat){
              backupFormat = formats[j];
            }else{
              std::string ideal = "42e01f";
              std::string oProf = backupFormat->getProfileLevelIdForH264();
              std::string nProf = formats[j]->getProfileLevelIdForH264();
              size_t oScore = 0, nScore = 0;
              for (size_t k = 0; k < 6 && k < oProf.size(); ++k){
                if (oProf[k] == ideal[k]){++oScore;}else{break;}
              }
              for (size_t k = 0; k < 6 && k < nProf.size(); ++k){
                if (nProf[k] == ideal[k]){++nScore;}else{break;}
              }
              if (nScore > oScore){backupFormat = formats[j];}
            }
            continue;
          }
        }
        format = formats[j];
        break;
      }
      if (format){break;}
    }

    if (!format){
      format = backupFormat;
      INFO_MSG("Picking non-perfect match for codec string");
    }
    if (!format){
      FAIL_MSG("Cannot enable %s; codec not found %s.", type.c_str(), codecList.c_str());
      return false;
    }

    INFO_MSG("Enabling media for codec: %s", format->encodingName.c_str());

    outMedia = *media;
    outFormat = *format;
    outFormat.rtcpFormats.clear();
    outFormat.icePwd = generateIcePwd();
    outFormat.iceUFrag = generateIceUFrag();

    return true;
  }

  std::string Answer::generateSessionId(){

    srand(time(NULL));
    uint64_t id = Util::getMicros();
    id += rand();

    std::stringstream ss;
    ss << id;

    return ss.str();
  }

  std::string Answer::generateIceUFrag(){return Util::generateRandomString(4);}

  std::string Answer::generateIcePwd(){return Util::generateRandomString(22);}

  std::vector<std::string> Answer::splitString(const std::string &str, char delim){

    std::stringstream ss(str);
    std::string segment;
    std::vector<std::string> result;

    while (std::getline(ss, segment, delim)){result.push_back(segment);}

    return result;
  }

  static std::string string_to_upper(const std::string &str){
    std::string result = str;
    char *p = (char *)result.c_str();
    while (*p != 0){
      if (*p >= 'a' && *p <= 'z'){*p = *p & ~0x20;}
      p++;
    }
    return result;
  }

}// namespace SDP
