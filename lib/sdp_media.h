#pragma once
#include "dtsc.h"
#include <map>
#include <set>
#include <string>
#include <vector>

#define SDP_PAYLOAD_TYPE_NONE 9999 /// Define that is used to indicate a payload type is not set.
#define SDP_LOSS_PREVENTION_NONE 0
#define SDP_LOSS_PREVENTION_NACK                                                                   \
  (1 << 1) /// Use simple NACK based loss prevention. (e.g. send a NACK to pusher of video stream when a packet is lost)
#define SDP_LOSS_PREVENTION_ULPFEC                                                                 \
  (1 << 2) /// Use FEC (See rtp.cpp, PacketRED). When used we try to add the correct `a=rtpmap` for RED and ULPFEC to the SDP when supported by the offer.

namespace SDP{

  /// A MediaFormat stores the infomation that is specific for an
  /// encoding.  With RTSP there is often just one media format
  /// per media line. Though with WebRTC, where an SDP is used to
  /// determine a common capability, one media line can contain
  /// different formats. These formats are indicated by with the
  /// <fmt> attribute of the media line. For each <fmt> there may
  /// be one or more custom properties. For each property, like
  /// the `encodingName` (e.g. VP8, VP9, H264, etc). we create a
  /// new `SDP::MediaFormat` object and store it in the `formats`
  /// member of `SDP::Media`.
  ///
  /// When you want to retrieve some specific data and there is a
  /// getter function defined for it, you SHOULD use this
  /// function as these functions add some extra logic based on
  /// the set members.
  class MediaFormat{
  public:
    MediaFormat();
    std::string getFormatParameterForName(const std::string &name) const; ///< Get a parameter which was part of the `a=fmtp:` line.
    uint32_t getAudioSampleRate() const; ///< Returns the audio sample rate. When `audioSampleRate` has been set this will be
                                         ///< returned, otherwise we use the `payloadType` to determine the samplerate or
                                         ///< return 0 when we fail to determine to samplerate.
    uint32_t
    getAudioNumChannels() const; ///< Returns the number of audio channels. When `audioNumChannels` has been set this
                                 ///< will be returned, otherwise we use the `payloadType` when it's set to determine
                                 ///< the samplerate or we return 0 when we can't determine the number of channels.
    uint32_t getAudioBitSize() const; ///< Returns the audio bitsize. When `audioBitSize` has been set this will be
                                      ///< returned, othwerise we use the `encodingName` to determine the right
                                      ///< `audioBitSize` or 0 when we can't determine the `audioBitSize`
    uint32_t getVideoRate() const; ///< Returns the video time base. When `videoRate` has been set this will
                                   ///< be returned, otherwise we use the `encodingName` to determine the
                                   ///< right value or  0 when we can't determine the video rate.
    uint32_t getVideoOrAudioRate() const; ///< Returns whichever rate has been set.
    uint64_t getPayloadType() const;      ///< Returns the `payloadType` member.
    int32_t getPacketizationModeForH264(); ///< When this represents a h264 format this will return the
                                           ///< packetization mode when it was provided in the SDP
    std::string getProfileLevelIdForH264(); ///< When this represents a H264 format, this will return the
                                            ///< profile-level-id from the format parameters.

    operator bool() const;

  public:
    uint64_t payloadType; ///< The payload type as set in the media line (the <fmt> is -the-
                          ///< payloadType).
    uint64_t associatedPayloadType; ///< From `a=fmtp:<pt> apt=<apt>`; maps this format to another payload type.
    int32_t audioSampleRate;  ///< Samplerate of the audio type.
    int32_t audioNumChannels; ///< Number of audio channels extracted from the `a=fmtp` or set in
                              ///< `setDefaultsForPayloadType()`.
    int32_t audioBitSize;     ///< Number of bits used in case this is an audio type 8, 16, set in
                              ///< `setDefaultsForCodec()` and `setDefaultsForPayloadType()`.
    int32_t videoRate;        ///< Video framerate, e.g. 9000
    std::string encodingName; ///< Stores the UPPERCASED encoding name from the `a=rtpmap:<payload
                              ///< type> <encoding name>
    std::string iceUFrag;     ///< From `a=ice-ufrag:<ufrag>, used with WebRTC / STUN.
    std::string icePwd;       ///< From `a=ice-pwd:<pwd>`, used with WebRTC / STUN
    std::string rtpmap;       ///< The `a=<rtpmap:...> value; value between brackets.
    std::map<std::string, std::string> formatParameters; ///< Stores the var-val pairs from `a=fmtp:<fmt>` entry e.g. =
                                                         ///< `packetization-mode=1;profile-level-id=4d0029;sprop-parameter-sets=Z00AKeKQCADDYC3AQEBpB4kRUA==,aO48gA==`
                                                         ///< */
    std::set<std::string> rtcpFormats; ///< Stores the `fb-val` from the line with `a=rtcp-fb:<fmt> <fb-val>`.
  };

  class Media{
  public:
    Media();
    bool parseMediaLine(const std::string &sdpLine); ///< Parses `m=` line. Creates a `MediaFormat`
                                                     ///< entry for each of the found <fmt> values.
    bool parseRtpMapLine(const std::string &sdpLine); ///< Parses `a=rtpmap:` line which contains the some codec
                                                      ///< specific info. When this line contains the samplerate and
                                                      ///< number of audio channels they will be extracted.
    bool parseRtspControlLine(const std::string &sdpLine);      ///< Parses `a=control:`
    bool parseFrameRateLine(const std::string &sdpLine);        ///< Parses `a=framerate:`
    bool parseFormatParametersLine(const std::string &sdpLine); ///< Parses `a=fmtp:<payload-type>`.
    bool parseRtcpFeedbackLine(const std::string &sdpLine); ///< Parses `a=rtcp-fb:<payload-type>`. See RFC4584
    bool parseFingerprintLine(const std::string &sdpLine); ///< Parses `a=fingerprint:<hash-func> <value>`. See
                                                           ///< https://tools.ietf.org/html/rfc8122#section-5, used with WebRTC
    bool parseSSRCLine(const std::string &sdpLine); ///< Parses `a=ssrc:<ssrc>`.
    MediaFormat *getFormatForSdpLine(const std::string &sdpLine); ///< Returns the track to which this SDP line applies. This means that the
                                                                  ///< SDP line should be formatteed like: `a=something:[payloadtype]`.
    MediaFormat *
    getFormatForPayloadType(uint64_t &payloadType); ///< Finds the `MediaFormat` in `formats`. Returns NULL when no
                                                    ///< format was found for the given payload type. .
    MediaFormat *getFormatForEncodingName(const std::string &encName); ///< Finds the `MediaFormats in `formats`. Returns NULL when no format was
                                                                       ///< found for the given encoding name. E.g. `VP8`, `VP9`, `H264`
    std::vector<SDP::MediaFormat *> getFormatsForEncodingName(const std::string &encName);
    std::string getIcePwdForFormat(const MediaFormat &fmt); ///< The `a=ice-pwd` can be session global or media specific. This function will
                                                            ///< check if the `SDP::MediaFormat` has a ice-pwd that we should use.
    uint32_t getSSRC() const; ///< Returns the first SSRC `a=ssrc:<value>` value found for the media.
    operator bool() const;
    MediaFormat *getRetransMissionFormatForPayloadType(
        uint64_t pt); ///< When available, it resurns the RTX format that is directly associated with
                      ///< the media (not encapsulated with a RED header). RTX can be combined with
                      ///< FEC in which case it's supposed to be stored in RED packets.   The `encName` should be something like H264,VP8; e.g. the format for which you want to get the RTX format.

  public:
    std::string type;    ///< The `media` field of the media line: `m=<media> <port> <proto> <fmt>`,
                         ///< like "video" or "audio"
    std::string proto;   ///< The `proto` field of the media line: `m=<media> <port> <proto> <fmt>`,
                         ///< like "video" or "audio"
    std::string control; ///< From `a=control:` The RTSP control url.
    std::string direction;   ///< From `a=sendonly`, `a=recvonly` and `a=sendrecv`
    std::string iceUFrag;    ///< From `a=ice-ufrag:<ufrag>, used with WebRTC / STUN.
    std::string icePwd;      ///< From `a=ice-pwd:<pwd>`, used with WebRTC / STUN
    std::string setupMethod; ///< From `a=setup:<passive, active, actpass>, used with WebRTC / STUN
    std::string fingerprintHash; ///< From `a=fingerprint:<hash> <value>`, e.g. sha-256, used with
                                 ///< WebRTC / STUN
    std::string fingerprintValue; ///< From `a=fingerprint:<hash> <value>`, the actual fingerprint, used
                                  ///< with WebRTC / STUN, see https://tools.ietf.org/html/rfc8122#section-5
    std::string mediaID; ///< From `a=mid:<value>`. When generating an WebRTC answer this value must
                         ///< be the same as in the offer.
    std::string candidateIP; ///< Used when we generate a WebRTC answer.
    uint16_t candidatePort;  ///< Used when we generate a WebRTC answer.
    uint32_t SSRC;        ///< From `a=ssrc:<SSRC> <something>`; the first SSRC that we encountered.
    double framerate;     ///< From `a=framerate`.
    bool supportsRTCPMux; ///< From `a=rtcp-mux`, indicates if it can mux RTP and RTCP on one
                          ///< transport channel.
    bool supportsRTCPReducedSize; ///< From `a=rtcp-rsize`, reduced size RTCP packets.
    std::set<std::string> extmap;
    std::string payloadTypes; ///< From `m=` line, all the payload types as string, separated by space.
    std::map<uint64_t, MediaFormat> formats; ///< Formats indexed by payload type. Payload type is the number in the <fmt>
                                             ///< field(s) from the `m=` line.
  };

  class Session{
  public:
    bool parseSDP(const std::string &sdp);
    Media *getMediaForType(const std::string &type); ///< Get a `SDP::Media*` for the given type, e.g. `video` or
                                                     ///< `audio`. Returns NULL when the type was not found.
    MediaFormat *getMediaFormatByEncodingName(const std::string &mediaType, const std::string &encodingName);
    bool hasReceiveOnlyMedia(); ///< Returns true when one of the media sections has a `a=recvonly`
                                ///< attribute. This is used to determine if the other peer only
                                ///< wants to receive or also sent data. */
    bool hasSendOnlyMedia();    ///< Returns true when one of the media sections has a `a=sendonly`
                                ///< attribute. This is used to determine if the other peer only
                                ///< wants to receive or also sent data. */

  public:
    std::vector<SDP::Media> medias; ///< For each `m=` line we create a `SDP::Media` instance. The
                                    ///< stream specific infomration is stored in a `MediaFormat`
    std::string icePwd; ///< From `a=ice-pwd`, this property can be session-wide or media specific.
                        ///< Used with WebRTC and STUN when calculating the message-integrity.
    std::string iceUFrag; ///< From `a=ice-ufag`, this property can be session-wide or media specific. Used
                          ///< with WebRTC and STUN when calculating the message-integrity.
  };

  class Answer{
  public:
    Answer();
    bool parseOffer(const std::string &sdp);
    bool hasVideo(); ///< Check if the offer has video.
    bool hasAudio(); ///< Check if the offer has audio.
    bool enableVideo(const std::string &codecName);
    bool enableAudio(const std::string &codecName);
    void setCandidate(const std::string &ip, uint16_t port);
    void setFingerprint(const std::string &fingerprintSha); ///< Set the SHA265 that represents the
                                                            ///< certificate that is used with DTLS.
    void setDirection(const std::string &dir);
    bool setupVideoDTSCTrack(DTSC::Meta &M, size_t tid);
    bool setupAudioDTSCTrack(DTSC::Meta &M, size_t tid);
    std::string toString();

  private:
    bool enableMedia(const std::string &type, const std::string &codecName, SDP::Media &outMedia,
                     SDP::MediaFormat &outFormat);
    void addLine(const std::string fmt, ...);
    std::string generateSessionId();
    std::string generateIceUFrag(); ///< Generates the `ice-ufrag` value.
    std::string generateIcePwd();   ///< Generates the `ice-pwd` value.
    std::vector<std::string> splitString(const std::string &str, char delim);

  public:
    SDP::Session sdpOffer;
    SDP::Media answerVideoMedia;
    SDP::Media answerAudioMedia;
    SDP::MediaFormat answerVideoFormat;
    SDP::MediaFormat answerAudioFormat;
    bool isAudioEnabled;
    bool isVideoEnabled;
    std::string candidateIP; ///< We use rtcp-mux and BUNDLE; so only one candidate necessary.
    uint16_t candidatePort;  ///< We use rtcp-mux and BUNDLE; so only one candidate necessary.
    std::string fingerprint;
    std::string direction;           ///< The direction used when generating the answer SDP string.
    std::vector<std::string> output; ///< The lines that are used when adding lines (see `addLine()`
                                     ///< for the answer sdp.).
    uint8_t videoLossPrevention; ///< See the SDP_LOSS_PREVENTION_* values at the top of this header.
  };

}// namespace SDP
