#include "output_rtmp.h"
#include <mist/http_parser.h>
#include <mist/defines.h>
#include <mist/stream.h>
#include <cstring>
#include <cstdlib>

namespace Mist {
  OutRTMP::OutRTMP(Socket::Connection & conn) : Output(conn) {
    setBlocking(false);
    while (!conn.Received().available(1537) && conn.connected()) {
      conn.spool();
      Util::sleep(5);
    }
    if (!conn){
      return;
    }
    RTMPStream::handshake_in.append(conn.Received().remove(1537));
    RTMPStream::rec_cnt += 1537;

    if (RTMPStream::doHandshake()) {
      conn.SendNow(RTMPStream::handshake_out);
      while (!conn.Received().available(1536) && conn.connected()) {
        conn.spool();
        Util::sleep(5);
      }
      conn.Received().remove(1536);
      RTMPStream::rec_cnt += 1536;
      DEBUG_MSG(DLVL_HIGH, "Handshake success!");
    } else {
      DEBUG_MSG(DLVL_DEVEL, "Handshake fail!");
    }
    counter = 0;
    sending = false;
    streamReset = false;
    maxSkipAhead = 1500;
    minSkipAhead = 500;
  }

  OutRTMP::~OutRTMP() {}

  void OutRTMP::init(Util::Config * cfg) {
    capa["name"] = "RTMP";
    capa["desc"] = "Enables the RTMP protocol which is used by Adobe Flash Player.";
    capa["deps"] = "";
    capa["url_rel"] = "/play/$";
    capa["codecs"][0u][0u].append("H264");
    capa["codecs"][0u][0u].append("H263");
    capa["codecs"][0u][0u].append("VP6");
    capa["codecs"][0u][1u].append("AAC");
    capa["codecs"][0u][1u].append("MP3");
    capa["methods"][0u]["handler"] = "rtmp";
    capa["methods"][0u]["type"] = "flash/10";
    capa["methods"][0u]["priority"] = 6ll;
    cfg->addConnectorOptions(1935, capa);
    config = cfg;
  }
  
  void OutRTMP::sendNext() {
    char rtmpheader[] = {0, //byte 0 = cs_id | ch_type
                         0, 0, 0, //bytes 1-3 = timestamp
                         0, 0, 0, //bytes 4-6 = length
                         0x12, //byte 7 = msg_type_id
                         1, 0, 0, 0, //bytes 8-11 = msg_stream_id = 1
                         0, 0, 0, 0}; //bytes 12-15 = extended timestamp
    char dataheader[] = {0, 0, 0, 0, 0};
    unsigned int dheader_len = 1;
    char * tmpData = 0;//pointer to raw media data
    unsigned int data_len = 0;//length of processed media data
    currentPacket.getString("data", tmpData, data_len);
    DTSC::Track & track = myMeta.tracks[currentPacket.getTrackId()];
    
    //set msg_type_id
    if (track.type == "video"){
      rtmpheader[7] = 0x09;
      if (track.codec == "H264"){
        dheader_len += 4;
        dataheader[0] = 7;
        if (currentPacket.getFlag("nalu")){
          dataheader[1] = 1;
        }else{
          dataheader[1] = 2;
        }
        if (currentPacket.getInt("offset") > 0){
          long long offset = currentPacket.getInt("offset");
          dataheader[2] = (offset >> 16) & 0xFF;
          dataheader[3] = (offset >> 8) & 0xFF;
          dataheader[4] = offset & 0xFF;
        }
      }
      if (track.codec == "H263"){
        dataheader[0] = 2;
      }
      if (currentPacket.getFlag("keyframe")){
        dataheader[0] |= 0x10;
      }
      if (currentPacket.getFlag("interframe")){
        dataheader[0] |= 0x20;
      }
      if (currentPacket.getFlag("disposableframe")){
        dataheader[0] |= 0x30;
      }
    }
    
    if (track.type == "audio"){
      rtmpheader[7] = 0x08;
      if (track.codec == "AAC"){
        dataheader[0] += 0xA0;
        dheader_len += 1;
        dataheader[1] = 1; //raw AAC data, not sequence header
      }
      if (track.codec == "MP3"){
        dataheader[0] += 0x20;
      }
      if (track.rate >= 44100){
        dataheader[0] |= 0x0C;
      }else if (track.rate >= 22050){
        dataheader[0] |= 0x08;
      }else if (track.rate >= 11025){
        dataheader[0] |= 0x04;
      }
      if (track.size == 16){
        dataheader[0] |= 0x02;
      }
      if (track.channels > 1){
        dataheader[0] |= 0x01;
      }
    }
    data_len += dheader_len;
    
    unsigned int timestamp = currentPacket.getTime();
    
    bool allow_short = RTMPStream::lastsend.count(4);
    RTMPStream::Chunk & prev = RTMPStream::lastsend[4];
    unsigned char chtype = 0x00;
    unsigned int header_len = 12;
    bool time_is_diff = false;
    if (allow_short && (prev.cs_id == 4)){
      if (prev.msg_stream_id == 1){
        chtype = 0x40;
        header_len = 8; //do not send msg_stream_id
        if (data_len == prev.len && rtmpheader[7] == prev.msg_type_id){
          chtype = 0x80;
          header_len = 4; //do not send len and msg_type_id
          if (timestamp == prev.timestamp){
            chtype = 0xC0;
            header_len = 1; //do not send timestamp
          }
        }
        //override - we always sent type 0x00 if the timestamp has decreased since last chunk in this channel
        if (timestamp < prev.timestamp){
          chtype = 0x00;
          header_len = 12;
        }else{
          //store the timestamp diff instead of the whole timestamp
          timestamp -= prev.timestamp;
          time_is_diff = true;
        }
      }
    }
    
    //update previous chunk variables
    prev.cs_id = 4;
    prev.msg_stream_id = 1;
    prev.len = data_len;
    prev.msg_type_id = rtmpheader[7];
    if (time_is_diff){
      prev.timestamp += timestamp;
    }else{
      prev.timestamp = timestamp;
    }

    //cs_id and ch_type
    rtmpheader[0] = chtype | 4;
    //data length, 3 bytes
    rtmpheader[4] = (data_len >> 16) & 0xff;
    rtmpheader[5] = (data_len >> 8) & 0xff;
    rtmpheader[6] = data_len & 0xff;
    //timestamp, 3 bytes
    if (timestamp >= 0x00ffffff){
      //send extended timestamp
      rtmpheader[1] = 0xff;
      rtmpheader[2] = 0xff;
      rtmpheader[3] = 0xff;
      rtmpheader[header_len++] = timestamp & 0xff;
      rtmpheader[header_len++] = (timestamp >> 8) & 0xff;
      rtmpheader[header_len++] = (timestamp >> 16) & 0xff;
      rtmpheader[header_len++] = (timestamp >> 24) & 0xff;
    }else{
      //regular timestamp
      rtmpheader[1] = (timestamp >> 16) & 0xff;
      rtmpheader[2] = (timestamp >> 8) & 0xff;
      rtmpheader[3] = timestamp & 0xff;
    }
    
    //send the header
    myConn.setBlocking(true);
    myConn.SendNow(rtmpheader, header_len);
    //set the header's first byte to the "continue" type chunk, for later use
    rtmpheader[0] = 0xC4;

    //sent actual data - never send more than chunk_snd_max at a time
    //interleave blocks of max chunk_snd_max bytes with 0xC4 bytes to indicate continue
    unsigned int len_sent = 0;
    unsigned int steps = 0;
    while (len_sent < data_len){
      unsigned int to_send = std::min(data_len - len_sent, RTMPStream::chunk_snd_max);
      if (!len_sent){
        myConn.SendNow(dataheader, dheader_len);
        to_send -= dheader_len;
        len_sent += dheader_len;
      }
      myConn.SendNow(tmpData+len_sent-dheader_len, to_send);
      len_sent += to_send;
      if (len_sent < data_len){
        myConn.SendNow(rtmpheader, 1);
        ++steps;
      }
    }
    myConn.setBlocking(false);
    //update the sent data counter
    RTMPStream::snd_cnt += header_len + data_len + steps;
  }

  void OutRTMP::sendHeader() {
    FLV::Tag tag;
    tag.DTSCMetaInit(myMeta, selectedTracks);
    if (tag.len) {
      myConn.SendNow(RTMPStream::SendMedia(tag));
    }

    for (std::set<long unsigned int>::iterator it = selectedTracks.begin(); it != selectedTracks.end(); it++) {
      if (myMeta.tracks[*it].type == "video") {
        tag.DTSCVideoInit(myMeta.tracks[*it]);
        if (tag.len) {
          myConn.SendNow(RTMPStream::SendMedia(tag));
        }
      }
      if (myMeta.tracks[*it].type == "audio") {
        tag.DTSCAudioInit(myMeta.tracks[*it]);
        if (tag.len) {
          myConn.SendNow(RTMPStream::SendMedia(tag));
        }
      }
    }
    sentHeader = true;
  }

  void OutRTMP::onRequest() {
    parseChunk(myConn.Received());
  }

  ///\brief Sends a RTMP command either in AMF or AMF3 mode.
  ///\param amfReply The data to be sent over RTMP.
  ///\param messageType The type of message.
  ///\param streamId The ID of the AMF stream.
  void OutRTMP::sendCommand(AMF::Object & amfReply, int messageType, int streamId) {
#if DEBUG >= 8
    std::cerr << amfReply.Print() << std::endl;
#endif
    if (messageType == 17) {
      myConn.SendNow(RTMPStream::SendChunk(3, messageType, streamId, (char)0 + amfReply.Pack()));
    } else {
      myConn.SendNow(RTMPStream::SendChunk(3, messageType, streamId, amfReply.Pack()));
    }
  } //sendCommand

  ///\brief Parses a single AMF command message, and sends a direct response through sendCommand().
  ///\param amfData The received request.
  ///\param messageType The type of message.
  ///\param streamId The ID of the AMF stream.
  void OutRTMP::parseAMFCommand(AMF::Object & amfData, int messageType, int streamId) {
#if DEBUG >= 5
    fprintf(stderr, "Received command: %s\n", amfData.Print().c_str());
#endif
#if DEBUG >= 8
    fprintf(stderr, "AMF0 command: %s\n", amfData.getContentP(0)->StrValue().c_str());
#endif
    if (amfData.getContentP(0)->StrValue() == "connect") {
      double objencoding = 0;
      if (amfData.getContentP(2)->getContentP("objectEncoding")) {
        objencoding = amfData.getContentP(2)->getContentP("objectEncoding")->NumValue();
      }
#if DEBUG >= 6
      int tmpint;
      if (amfData.getContentP(2)->getContentP("videoCodecs")) {
        tmpint = (int)amfData.getContentP(2)->getContentP("videoCodecs")->NumValue();
        if (tmpint & 0x04) {
          fprintf(stderr, "Sorensen video support detected\n");
        }
        if (tmpint & 0x80) {
          fprintf(stderr, "H264 video support detected\n");
        }
      }
      if (amfData.getContentP(2)->getContentP("audioCodecs")) {
        tmpint = (int)amfData.getContentP(2)->getContentP("audioCodecs")->NumValue();
        if (tmpint & 0x04) {
          fprintf(stderr, "MP3 audio support detected\n");
        }
        if (tmpint & 0x400) {
          fprintf(stderr, "AAC audio support detected\n");
        }
      }
#endif
      app_name = amfData.getContentP(2)->getContentP("tcUrl")->StrValue();
      app_name = app_name.substr(app_name.find('/', 7) + 1);
      RTMPStream::chunk_snd_max = 4096;
      myConn.Send(RTMPStream::SendCTL(1, RTMPStream::chunk_snd_max)); //send chunk size max (msg 1)
      myConn.Send(RTMPStream::SendCTL(5, RTMPStream::snd_window_size)); //send window acknowledgement size (msg 5)
      myConn.Send(RTMPStream::SendCTL(6, RTMPStream::rec_window_size)); //send rec window acknowledgement size (msg 6)
      myConn.Send(RTMPStream::SendUSR(0, 1)); //send UCM StreamBegin (0), stream 1
      //send a _result reply
      AMF::Object amfReply("container", AMF::AMF0_DDV_CONTAINER);
      amfReply.addContent(AMF::Object("", "_result")); //result success
      amfReply.addContent(amfData.getContent(1)); //same transaction ID
      amfReply.addContent(AMF::Object("")); //server properties
      amfReply.getContentP(2)->addContent(AMF::Object("fmsVer", "FMS/3,5,5,2004"));
      amfReply.getContentP(2)->addContent(AMF::Object("capabilities", (double)31));
      amfReply.getContentP(2)->addContent(AMF::Object("mode", (double)1));
      amfReply.addContent(AMF::Object("")); //info
      amfReply.getContentP(3)->addContent(AMF::Object("level", "status"));
      amfReply.getContentP(3)->addContent(AMF::Object("code", "NetConnection.Connect.Success"));
      amfReply.getContentP(3)->addContent(AMF::Object("description", "Connection succeeded."));
      amfReply.getContentP(3)->addContent(AMF::Object("clientid", 1337));
      amfReply.getContentP(3)->addContent(AMF::Object("objectEncoding", objencoding));
      //amfReply.getContentP(3)->addContent(AMF::Object("data", AMF::AMF0_ECMA_ARRAY));
      //amfReply.getContentP(3)->getContentP(4)->addContent(AMF::Object("version", "3,5,4,1004"));
      sendCommand(amfReply, messageType, streamId);
      //send onBWDone packet - no clue what it is, but real server sends it...
      //amfReply = AMF::Object("container", AMF::AMF0_DDV_CONTAINER);
      //amfReply.addContent(AMF::Object("", "onBWDone"));//result
      //amfReply.addContent(amfData.getContent(1));//same transaction ID
      //amfReply.addContent(AMF::Object("", (double)0, AMF::AMF0_NULL));//null
      //sendCommand(amfReply, messageType, streamId);
      return;
    } //connect
    if (amfData.getContentP(0)->StrValue() == "createStream") {
      //send a _result reply
      AMF::Object amfReply("container", AMF::AMF0_DDV_CONTAINER);
      amfReply.addContent(AMF::Object("", "_result")); //result success
      amfReply.addContent(amfData.getContent(1)); //same transaction ID
      amfReply.addContent(AMF::Object("", (double)0, AMF::AMF0_NULL)); //null - command info
      amfReply.addContent(AMF::Object("", (double)1)); //stream ID - we use 1
      sendCommand(amfReply, messageType, streamId);
      myConn.Send(RTMPStream::SendUSR(0, 1)); //send UCM StreamBegin (0), stream 1
      return;
    } //createStream
    if ((amfData.getContentP(0)->StrValue() == "closeStream") || (amfData.getContentP(0)->StrValue() == "deleteStream")) {
      stop();
      return;
    }
    if ((amfData.getContentP(0)->StrValue() == "FCUnpublish") || (amfData.getContentP(0)->StrValue() == "releaseStream")) {
      // ignored
      return;
    }
    if ((amfData.getContentP(0)->StrValue() == "FCPublish")) {
      //send a FCPublic reply
      AMF::Object amfReply("container", AMF::AMF0_DDV_CONTAINER);
      amfReply.addContent(AMF::Object("", "onFCPublish")); //status reply
      amfReply.addContent(AMF::Object("", 0, AMF::AMF0_NUMBER)); //same transaction ID
      amfReply.addContent(AMF::Object("", (double)0, AMF::AMF0_NULL)); //null - command info
      amfReply.addContent(AMF::Object("")); //info
      amfReply.getContentP(3)->addContent(AMF::Object("code", "NetStream.Publish.Start"));
      amfReply.getContentP(3)->addContent(AMF::Object("description", "Please followup with publish command..."));
      sendCommand(amfReply, messageType, streamId);
      return;
    } //FCPublish
    if (amfData.getContentP(0)->StrValue() == "releaseStream") {
      //send a _result reply
      AMF::Object amfReply("container", AMF::AMF0_DDV_CONTAINER);
      amfReply.addContent(AMF::Object("", "_result")); //result success
      amfReply.addContent(amfData.getContent(1)); //same transaction ID
      amfReply.addContent(AMF::Object("", (double)0, AMF::AMF0_NULL)); //null - command info
      amfReply.addContent(AMF::Object("", AMF::AMF0_UNDEFINED)); //stream ID?
      sendCommand(amfReply, messageType, streamId);
      return;
    }//releaseStream
    if ((amfData.getContentP(0)->StrValue() == "getStreamLength") || (amfData.getContentP(0)->StrValue() == "getMovLen")) {
      //send a _result reply
      AMF::Object amfReply("container", AMF::AMF0_DDV_CONTAINER);
      amfReply.addContent(AMF::Object("", "_result")); //result success
      amfReply.addContent(amfData.getContent(1)); //same transaction ID
      amfReply.addContent(AMF::Object("", (double)0, AMF::AMF0_NULL)); //null - command info
      amfReply.addContent(AMF::Object("", (double)0)); //zero length
      sendCommand(amfReply, messageType, streamId);
      return;
    } //getStreamLength
    if ((amfData.getContentP(0)->StrValue() == "publish")) {
      if (amfData.getContentP(3)) {
        streamName = amfData.getContentP(3)->StrValue();
        Util::Stream::sanitizeName(streamName);
        //pull the server configuration
        JSON::Value servConf = JSON::fromFile(Util::getTmpFolder() + "streamlist");    
        if (servConf.isMember("streams") && servConf["streams"].isMember(streamName)){
          JSON::Value & streamConfig = servConf["streams"][streamName];
          if (!streamConfig.isMember("source") || streamConfig["source"].asStringRef().substr(0, 7) != "push://"){
            DEBUG_MSG(DLVL_FAIL, "Push rejected - stream not a push-able stream. (%s != push://*)", streamConfig["source"].asStringRef().c_str());
            myConn.close();
            return;
          }
          std::string source = streamConfig["source"].asStringRef().substr(7);
          std::string IP = source.substr(0, source.find('@'));
          /*LTS-START*/
          std::string password;
          if (source.find('@') != std::string::npos){
            password = source.substr(source.find('@')+1);
            if (password != ""){
              if (password == app_name){
                DEBUG_MSG(DLVL_DEVEL, "Password accepted - ignoring IP settings.");
                IP = "";
              }else{
                DEBUG_MSG(DLVL_DEVEL, "Password rejected - checking IP.");
                if (IP == ""){
                  IP = "deny-all.invalid";
                }
              }
            }
          }
          /*LTS-END*/
          if (IP != ""){
            if (!myConn.isAddress(IP)){
              DEBUG_MSG(DLVL_FAIL, "Push rejected - source host not whitelisted");
              myConn.close();
              return;
            }
          }
        }else{
          DEBUG_MSG(DLVL_FAIL, "Push rejected - stream not configured.");
          myConn.close();
          return;
        }
        initialize();
      }
      //send a _result reply
      AMF::Object amfReply("container", AMF::AMF0_DDV_CONTAINER);
      amfReply.addContent(AMF::Object("", "_result")); //result success
      amfReply.addContent(amfData.getContent(1)); //same transaction ID
      amfReply.addContent(AMF::Object("", (double)0, AMF::AMF0_NULL)); //null - command info
      amfReply.addContent(AMF::Object("", 1, AMF::AMF0_BOOL)); //publish success?
      sendCommand(amfReply, messageType, streamId);
      myConn.Send(RTMPStream::SendUSR(0, 1)); //send UCM StreamBegin (0), stream 1
      //send a status reply
      amfReply = AMF::Object("container", AMF::AMF0_DDV_CONTAINER);
      amfReply.addContent(AMF::Object("", "onStatus")); //status reply
      amfReply.addContent(AMF::Object("", 0, AMF::AMF0_NUMBER)); //same transaction ID
      amfReply.addContent(AMF::Object("", (double)0, AMF::AMF0_NULL)); //null - command info
      amfReply.addContent(AMF::Object("")); //info
      amfReply.getContentP(3)->addContent(AMF::Object("level", "status"));
      amfReply.getContentP(3)->addContent(AMF::Object("code", "NetStream.Publish.Start"));
      amfReply.getContentP(3)->addContent(AMF::Object("description", "Stream is now published!"));
      amfReply.getContentP(3)->addContent(AMF::Object("clientid", (double)1337));
      sendCommand(amfReply, messageType, streamId);
      return;
    } //getStreamLength
    if (amfData.getContentP(0)->StrValue() == "checkBandwidth") {
      //send a _result reply
      AMF::Object amfReply("container", AMF::AMF0_DDV_CONTAINER);
      amfReply.addContent(AMF::Object("", "_result")); //result success
      amfReply.addContent(amfData.getContent(1)); //same transaction ID
      amfReply.addContent(AMF::Object("", (double)0, AMF::AMF0_NULL)); //null - command info
      amfReply.addContent(AMF::Object("", (double)0, AMF::AMF0_NULL)); //null - command info
      sendCommand(amfReply, messageType, streamId);
      return;
    } //checkBandwidth
    if ((amfData.getContentP(0)->StrValue() == "play") || (amfData.getContentP(0)->StrValue() == "play2")) {
      //set reply number and stream name, actual reply is sent up in the ss.spool() handler
      int playTransaction = amfData.getContentP(1)->NumValue();
      int playMessageType = messageType;
      int playStreamId = streamId;
      streamName = amfData.getContentP(3)->StrValue();
      initialize();
      
      //send a status reply
      AMF::Object amfreply("container", AMF::AMF0_DDV_CONTAINER);
      amfreply.addContent(AMF::Object("", "onStatus")); //status reply
      amfreply.addContent(AMF::Object("", (double)playTransaction)); //same transaction ID
      amfreply.addContent(AMF::Object("", (double)0, AMF::AMF0_NULL)); //null - command info
      amfreply.addContent(AMF::Object("")); //info
      amfreply.getContentP(3)->addContent(AMF::Object("level", "status"));
      amfreply.getContentP(3)->addContent(AMF::Object("code", "NetStream.Play.Reset"));
      amfreply.getContentP(3)->addContent(AMF::Object("description", "Playing and resetting..."));
      amfreply.getContentP(3)->addContent(AMF::Object("details", "DDV"));
      amfreply.getContentP(3)->addContent(AMF::Object("clientid", (double)1337));
      sendCommand(amfreply, playMessageType, playStreamId);
      //send streamisrecorded if stream, well, is recorded.
      if (myMeta.vod) { //isMember("length") && Strm.metadata["length"].asInt() > 0){
        myConn.Send(RTMPStream::SendUSR(4, 1)); //send UCM StreamIsRecorded (4), stream 1
      }
      //send streambegin
      myConn.Send(RTMPStream::SendUSR(0, 1)); //send UCM StreamBegin (0), stream 1
      //and more reply
      amfreply = AMF::Object("container", AMF::AMF0_DDV_CONTAINER);
      amfreply.addContent(AMF::Object("", "onStatus")); //status reply
      amfreply.addContent(AMF::Object("", (double)playTransaction)); //same transaction ID
      amfreply.addContent(AMF::Object("", (double)0, AMF::AMF0_NULL)); //null - command info
      amfreply.addContent(AMF::Object("")); //info
      amfreply.getContentP(3)->addContent(AMF::Object("level", "status"));
      amfreply.getContentP(3)->addContent(AMF::Object("code", "NetStream.Play.Start"));
      amfreply.getContentP(3)->addContent(AMF::Object("description", "Playing!"));
      amfreply.getContentP(3)->addContent(AMF::Object("details", "DDV"));
      amfreply.getContentP(3)->addContent(AMF::Object("clientid", (double)1337));
      sendCommand(amfreply, playMessageType, playStreamId);
      RTMPStream::chunk_snd_max = 102400; //100KiB
      myConn.Send(RTMPStream::SendCTL(1, RTMPStream::chunk_snd_max)); //send chunk size max (msg 1)
      //send dunno?
      myConn.Send(RTMPStream::SendUSR(32, 1)); //send UCM no clue?, stream 1

      parseData = true;
      return;
    } //play
    if ((amfData.getContentP(0)->StrValue() == "seek")) {
      //set reply number and stream name, actual reply is sent up in the ss.spool() handler
      int playTransaction = amfData.getContentP(1)->NumValue();
      int playMessageType = messageType;
      int playStreamId = streamId;

      AMF::Object amfReply("container", AMF::AMF0_DDV_CONTAINER);
      amfReply.addContent(AMF::Object("", "onStatus")); //status reply
      amfReply.addContent(amfData.getContent(1)); //same transaction ID
      amfReply.addContent(AMF::Object("", (double)0, AMF::AMF0_NULL)); //null - command info
      amfReply.addContent(AMF::Object("")); //info
      amfReply.getContentP(3)->addContent(AMF::Object("level", "status"));
      amfReply.getContentP(3)->addContent(AMF::Object("code", "NetStream.Seek.Notify"));
      amfReply.getContentP(3)->addContent(AMF::Object("description", "Seeking to the specified time"));
      amfReply.getContentP(3)->addContent(AMF::Object("details", "DDV"));
      amfReply.getContentP(3)->addContent(AMF::Object("clientid", (double)1337));
      sendCommand(amfReply, playMessageType, playStreamId);
      seek((long long int)amfData.getContentP(3)->NumValue());

      //send a status reply
      AMF::Object amfreply("container", AMF::AMF0_DDV_CONTAINER);
      amfreply.addContent(AMF::Object("", "onStatus")); //status reply
      amfreply.addContent(AMF::Object("", (double)playTransaction)); //same transaction ID
      amfreply.addContent(AMF::Object("", (double)0, AMF::AMF0_NULL)); //null - command info
      amfreply.addContent(AMF::Object("")); //info
      amfreply.getContentP(3)->addContent(AMF::Object("level", "status"));
      amfreply.getContentP(3)->addContent(AMF::Object("code", "NetStream.Play.Reset"));
      amfreply.getContentP(3)->addContent(AMF::Object("description", "Playing and resetting..."));
      amfreply.getContentP(3)->addContent(AMF::Object("details", "DDV"));
      amfreply.getContentP(3)->addContent(AMF::Object("clientid", (double)1337));
      sendCommand(amfreply, playMessageType, playStreamId);
      //send streamisrecorded if stream, well, is recorded.
      if (myMeta.vod) { //isMember("length") && Strm.metadata["length"].asInt() > 0){
        myConn.Send(RTMPStream::SendUSR(4, 1)); //send UCM StreamIsRecorded (4), stream 1
      }
      //send streambegin
      myConn.Send(RTMPStream::SendUSR(0, 1)); //send UCM StreamBegin (0), stream 1
      //and more reply
      amfreply = AMF::Object("container", AMF::AMF0_DDV_CONTAINER);
      amfreply.addContent(AMF::Object("", "onStatus")); //status reply
      amfreply.addContent(AMF::Object("", (double)playTransaction)); //same transaction ID
      amfreply.addContent(AMF::Object("", (double)0, AMF::AMF0_NULL)); //null - command info
      amfreply.addContent(AMF::Object("")); //info
      amfreply.getContentP(3)->addContent(AMF::Object("level", "status"));
      amfreply.getContentP(3)->addContent(AMF::Object("code", "NetStream.Play.Start"));
      amfreply.getContentP(3)->addContent(AMF::Object("description", "Playing!"));
      amfreply.getContentP(3)->addContent(AMF::Object("details", "DDV"));
      amfreply.getContentP(3)->addContent(AMF::Object("clientid", (double)1337));
      sendCommand(amfreply, playMessageType, playStreamId);
      RTMPStream::chunk_snd_max = 102400; //100KiB
      myConn.Send(RTMPStream::SendCTL(1, RTMPStream::chunk_snd_max)); //send chunk size max (msg 1)
      //send dunno?
      myConn.Send(RTMPStream::SendUSR(32, 1)); //send UCM no clue?, stream 1

      return;
    } //seek
    if ((amfData.getContentP(0)->StrValue() == "pauseRaw") || (amfData.getContentP(0)->StrValue() == "pause")) {
      int playMessageType = messageType;
      int playStreamId = streamId;
      if (amfData.getContentP(3)->NumValue()) {
        parseData = false;
        //send a status reply
        AMF::Object amfReply("container", AMF::AMF0_DDV_CONTAINER);
        amfReply.addContent(AMF::Object("", "onStatus")); //status reply
        amfReply.addContent(amfData.getContent(1)); //same transaction ID
        amfReply.addContent(AMF::Object("", (double)0, AMF::AMF0_NULL)); //null - command info
        amfReply.addContent(AMF::Object("")); //info
        amfReply.getContentP(3)->addContent(AMF::Object("level", "status"));
        amfReply.getContentP(3)->addContent(AMF::Object("code", "NetStream.Pause.Notify"));
        amfReply.getContentP(3)->addContent(AMF::Object("description", "Pausing playback"));
        amfReply.getContentP(3)->addContent(AMF::Object("details", "DDV"));
        amfReply.getContentP(3)->addContent(AMF::Object("clientid", (double)1337));
        sendCommand(amfReply, playMessageType, playStreamId);
      } else {
        parseData = true;
        //send a status reply
        AMF::Object amfReply("container", AMF::AMF0_DDV_CONTAINER);
        amfReply.addContent(AMF::Object("", "onStatus")); //status reply
        amfReply.addContent(amfData.getContent(1)); //same transaction ID
        amfReply.addContent(AMF::Object("", (double)0, AMF::AMF0_NULL)); //null - command info
        amfReply.addContent(AMF::Object("")); //info
        amfReply.getContentP(3)->addContent(AMF::Object("level", "status"));
        amfReply.getContentP(3)->addContent(AMF::Object("code", "NetStream.Unpause.Notify"));
        amfReply.getContentP(3)->addContent(AMF::Object("description", "Resuming playback"));
        amfReply.getContentP(3)->addContent(AMF::Object("details", "DDV"));
        amfReply.getContentP(3)->addContent(AMF::Object("clientid", (double)1337));
        sendCommand(amfReply, playMessageType, playStreamId);
      }
      return;
    } //seek

#if DEBUG >= 2
    fprintf(stderr, "AMF0 command not processed!\n%s\n", amfData.Print().c_str());
#endif
  } //parseAMFCommand

  void OutRTMP::bufferPacket(JSON::Value & pack){
    if (!trackMap.count(pack["trackid"].asInt())){
      //declined track;
      return;
    }
    if (myMeta.tracks[pack["trackid"].asInt()].type != "video"){
      if ((pack["time"].asInt() - bookKeeping[trackMap[pack["trackid"].asInt()]].lastKeyTime) >= 5000){
        pack["keyframe"] = 1LL;
        bookKeeping[trackMap[pack["trackid"].asInt()]].lastKeyTime = pack["time"].asInt();
      }
    }
    pack["trackid"] = trackMap[pack["trackid"].asInt()];
    long long unsigned int tNum = pack["trackid"].asInt();
    if (!bookKeeping.count(tNum)){
      return;
    }
    int pageNum = bookKeeping[tNum].pageNum;
    std::string tmp = pack.toNetPacked();
    if (bookKeeping[tNum].curOffset > 8388608 && pack.isMember("keyframe") && pack["keyframe"]){
      Util::sleep(500);
      //open new page
      char nextPage[100];
      sprintf(nextPage, "%s%llu_%d", streamName.c_str(), tNum, bookKeeping[tNum].pageNum + bookKeeping[tNum].keyNum);
      curPages[tNum].init(nextPage, 26 * 1024 * 1024);
      bookKeeping[tNum].pageNum += bookKeeping[tNum].keyNum;
      bookKeeping[tNum].keyNum = 0;
      bookKeeping[tNum].curOffset = 0;
    }
    if (bookKeeping[tNum].curOffset + tmp.size() < curPages[tNum].len){
      bookKeeping[tNum].keyNum += (pack.isMember("keyframe") && pack["keyframe"]);
      memcpy(curPages[tNum].mapped + bookKeeping[tNum].curOffset, tmp.data(), tmp.size());
      bookKeeping[tNum].curOffset += tmp.size();
    }else{
      bookKeeping[tNum].curOffset += tmp.size();
      DEBUG_MSG(DLVL_WARN, "Can't buffer frame on page %d, track %llu, time %lld, keyNum %d, offset %llu", pageNum, tNum, pack["time"].asInt(), bookKeeping[tNum].pageNum + bookKeeping[tNum].keyNum, bookKeeping[tNum].curOffset);
      ///\todo Open next page plx
    }
    playerConn.keepAlive();
  }


  void OutRTMP::negotiatePushTracks() {
    char * tmp = playerConn.getData();
    if (!tmp){
      DEBUG_MSG(DLVL_FAIL, "No userpage allocated");
      return;
    }
    memset(tmp, 0, 30);
    unsigned int i = 0;
    for (std::map<int, DTSC::Track>::iterator it = meta_out.tracks.begin(); it != meta_out.tracks.end() && i < 5; it++){
      DEBUG_MSG(DLVL_DEVEL, "Negotiating tracknum for id %d", it->first);
      (tmp + 6 * i)[0] = 0x80;
      (tmp + 6 * i)[1] = 0x00;
      (tmp + 6 * i)[2] = 0x00;
      (tmp + 6 * i)[3] = 0x00;
      (tmp + 6 * i)[4] = (it->first >> 8) & 0xFF;
      (tmp + 6 * i)[5] = (it->first) & 0xFF;
      i++;
    }
    playerConn.keepAlive();
    bool gotAllNumbers = false;
    while (!gotAllNumbers){
      Util::sleep(100);
      gotAllNumbers = true;
      i = 0;
      for (std::map<int, DTSC::Track>::iterator it = meta_out.tracks.begin(); it != meta_out.tracks.end() && i < 5; it++){
        unsigned long tNum = (((long)(tmp + (6 * i))[0]) << 24) |  (((long)(tmp + (6 * i))[1]) << 16) | (((long)(tmp + (6 * i))[2]) << 8) | (long)(tmp + (6 * i))[3];
        unsigned short oldNum = (((long)(tmp + (6 * i))[4]) << 8) | (long)(tmp + (6 * i))[5];
        if( tNum & 0x80000000){
          gotAllNumbers = false;
          break;
        }else{
          DEBUG_MSG(DLVL_DEVEL, "Mapped %d -> %lu", oldNum, tNum);
          trackMap[oldNum] = tNum;
        }
        i++;
      }
    }
    for (std::map<int, int>::iterator it = trackMap.begin(); it != trackMap.end(); it++){
      char tmp[100];
      sprintf( tmp, "liveStream_%s%d", streamName.c_str(), it->second);
      metaPages[it->second].init(std::string(tmp), 8 * 1024 * 1024);
      DTSC::Meta tmpMeta = meta_out;
      tmpMeta.tracks.clear();
      tmpMeta.tracks[it->second] = meta_out.tracks[it->first];
      tmpMeta.tracks[it->second].trackID = it->second;
      JSON::Value tmpVal = tmpMeta.toJSON();
      std::string tmpStr = tmpVal.toNetPacked();
      memcpy(metaPages[it->second].mapped, tmpStr.data(), tmpStr.size());
      DEBUG_MSG(DLVL_DEVEL, "Written meta for track %d", it->second);
    }
    gotAllNumbers = false;
    while (!gotAllNumbers){
      Util::sleep(100);
      gotAllNumbers = true;
      i = 0;
      unsigned int j = 0;
      //update Metadata;
      JSON::Value jsonMeta;
      JSON::fromDTMI((const unsigned char*)streamIndex.mapped + 8, streamIndex.len - 8, j, jsonMeta);
      myMeta = DTSC::Meta(jsonMeta);
      tmp = playerConn.getData();
      for (std::map<int, DTSC::Track>::iterator it = meta_out.tracks.begin(); it != meta_out.tracks.end() && i < 5; it++){
        unsigned long tNum = (((long)(tmp + (6 * i))[0]) << 24) |  (((long)(tmp + (6 * i))[1]) << 16) | (((long)(tmp + (6 * i))[2]) << 8) | (long)(tmp + (6 * i))[3];
        if( tNum == 0xFFFFFFFF){
          DEBUG_MSG(DLVL_DEVEL, "Skipping a declined track");
          i++;
          continue;
        }
        if(!myMeta.tracks.count(tNum)){
          gotAllNumbers = false;
          break;
        }
        i++;
      }
    }
    i = 0;
    tmp = playerConn.getData();
    for (std::map<int, DTSC::Track>::iterator it = meta_out.tracks.begin(); it != meta_out.tracks.end() && i < 5; it++){
      unsigned long tNum = ((long)(tmp[6*i]) << 24) |  ((long)(tmp[6 * i + 1]) << 16) | ((long)(tmp[6 * i + 2]) << 8) | tmp[6 * i + 3];
      if( tNum == 0xFFFFFFFF){
        tNum = ((long)(tmp[6 * i + 4]) << 8) |  (long)tmp[6 * i + 5];
        DEBUG_MSG(DLVL_WARN, "Buffer declined track %i", trackMap[tNum]);
        trackMap.erase(tNum);
        tmp[6*i] = 0;
        tmp[6*i+1] = 0;
        tmp[6*i+2] = 0;
        tmp[6*i+3] = 0;
        tmp[6*i+4] = 0;
        tmp[6*i+5] = 0;
      }else{
        char firstPage[100];
        sprintf(firstPage, "%s%lu_%d", streamName.c_str(), tNum, 0);
        curPages[tNum].init(firstPage, 8 * 1024 * 1024);
        bookKeeping[tNum] = DTSCPageData();
        DEBUG_MSG(DLVL_WARN, "Buffer accepted track %lu", tNum);
      }
      i++;
    }
  }

  ///\brief Gets and parses one RTMP chunk at a time.
  ///\param inputBuffer A buffer filled with chunk data.
  void OutRTMP::parseChunk(Socket::Buffer & inputBuffer) {
    //for DTSC conversion
    static std::stringstream prebuffer; // Temporary buffer before sending real data
    //for chunk parsing
    static RTMPStream::Chunk next;
    static FLV::Tag F;
    static AMF::Object amfdata("empty", AMF::AMF0_DDV_CONTAINER);
    static AMF::Object amfelem("empty", AMF::AMF0_DDV_CONTAINER);
    static AMF::Object3 amf3data("empty", AMF::AMF3_DDV_CONTAINER);
    static AMF::Object3 amf3elem("empty", AMF::AMF3_DDV_CONTAINER);

    while (next.Parse(inputBuffer)) {

      //send ACK if we received a whole window
      if ((RTMPStream::rec_cnt - RTMPStream::rec_window_at > RTMPStream::rec_window_size)) {
        RTMPStream::rec_window_at = RTMPStream::rec_cnt;
        myConn.Send(RTMPStream::SendCTL(3, RTMPStream::rec_cnt)); //send ack (msg 3)
      }

      switch (next.msg_type_id) {
        case 0: //does not exist
#if DEBUG >= 2
          fprintf(stderr, "UNKN: Received a zero-type message. Possible data corruption? Aborting!\n");
#endif
          while (inputBuffer.size()) {
            inputBuffer.get().clear();
          }
          stop();
          myConn.close();
          break; //happens when connection breaks unexpectedly
        case 1: //set chunk size
          RTMPStream::chunk_rec_max = ntohl(*(int *)next.data.c_str());
#if DEBUG >= 5
          fprintf(stderr, "CTRL: Set chunk size: %i\n", RTMPStream::chunk_rec_max);
#endif
          break;
        case 2: //abort message - we ignore this one
#if DEBUG >= 5
          fprintf(stderr, "CTRL: Abort message\n");
#endif
          //4 bytes of stream id to drop
          break;
        case 3: //ack
#if DEBUG >= 8
          fprintf(stderr, "CTRL: Acknowledgement\n");
#endif
          RTMPStream::snd_window_at = ntohl(*(int *)next.data.c_str());
          RTMPStream::snd_window_at = RTMPStream::snd_cnt;
          break;
        case 4: {
            //2 bytes event type, rest = event data
            //types:
            //0 = stream begin, 4 bytes ID
            //1 = stream EOF, 4 bytes ID
            //2 = stream dry, 4 bytes ID
            //3 = setbufferlen, 4 bytes ID, 4 bytes length
            //4 = streamisrecorded, 4 bytes ID
            //6 = pingrequest, 4 bytes data
            //7 = pingresponse, 4 bytes data
            //we don't need to process this
#if DEBUG >= 5
            short int ucmtype = ntohs(*(short int *)next.data.c_str());
            switch (ucmtype) {
              case 0:
                fprintf(stderr, "CTRL: UCM StreamBegin %i\n", ntohl(*((int *)(next.data.c_str() + 2))));
                break;
              case 1:
                fprintf(stderr, "CTRL: UCM StreamEOF %i\n", ntohl(*((int *)(next.data.c_str() + 2))));
                break;
              case 2:
                fprintf(stderr, "CTRL: UCM StreamDry %i\n", ntohl(*((int *)(next.data.c_str() + 2))));
                break;
              case 3:
                fprintf(stderr, "CTRL: UCM SetBufferLength %i %i\n", ntohl(*((int *)(next.data.c_str() + 2))), ntohl(*((int *)(next.data.c_str() + 6))));
                break;
              case 4:
                fprintf(stderr, "CTRL: UCM StreamIsRecorded %i\n", ntohl(*((int *)(next.data.c_str() + 2))));
                break;
              case 6:
                fprintf(stderr, "CTRL: UCM PingRequest %i\n", ntohl(*((int *)(next.data.c_str() + 2))));
                break;
              case 7:
                fprintf(stderr, "CTRL: UCM PingResponse %i\n", ntohl(*((int *)(next.data.c_str() + 2))));
                break;
              default:
                fprintf(stderr, "CTRL: UCM Unknown (%hi)\n", ucmtype);
                break;
            }
#endif
          }
          break;
        case 5: //window size of other end
#if DEBUG >= 5
          fprintf(stderr, "CTRL: Window size\n");
#endif
          RTMPStream::rec_window_size = ntohl(*(int *)next.data.c_str());
          RTMPStream::rec_window_at = RTMPStream::rec_cnt;
          myConn.Send(RTMPStream::SendCTL(3, RTMPStream::rec_cnt)); //send ack (msg 3)
          break;
        case 6:
#if DEBUG >= 5
          fprintf(stderr, "CTRL: Set peer bandwidth\n");
#endif
          //4 bytes window size, 1 byte limit type (ignored)
          RTMPStream::snd_window_size = ntohl(*(int *)next.data.c_str());
          myConn.Send(RTMPStream::SendCTL(5, RTMPStream::snd_window_size)); //send window acknowledgement size (msg 5)
          break;
        case 8: //audio data
        case 9: //video data
        case 18: {//meta data
          if (!isInitialized) {
            DEBUG_MSG(DLVL_MEDIUM, "Received useless media data\n");
            myConn.close();
            break;
          }
          if (streamReset) {
            //reset push data to empty, in case stream properties change
            meta_out.reset();
            preBuf.clear();
            sending = false;
            counter = 0;
            streamReset = false;
          }
          F.ChunkLoader(next);
          JSON::Value pack_out = F.toJSON(meta_out);
          if ( !pack_out.isNull()){
            if ( !sending){
              counter++;
              if (counter > 8){
                sending = true;
                negotiatePushTracks();
                for (std::deque<JSON::Value>::iterator it = preBuf.begin(); it != preBuf.end(); it++){
                  bufferPacket((*it));
                }
                preBuf.clear(); //clear buffer
                bufferPacket(pack_out);
              }else{
                preBuf.push_back(pack_out);
              }
            }else{
              bufferPacket(pack_out);
            }
          }
          break;
        }
        case 15:
          DEBUG_MSG(DLVL_MEDIUM, "Received AMF3 data message");
          break;
        case 16:
          DEBUG_MSG(DLVL_MEDIUM, "Received AMF3 shared object");
          break;
        case 17: {
            DEBUG_MSG(DLVL_MEDIUM, "Received AMF3 command message");
            if (next.data[0] != 0) {
              next.data = next.data.substr(1);
              amf3data = AMF::parse3(next.data);
#if DEBUG >= 5
              amf3data.Print();
#endif
            } else {
              DEBUG_MSG(DLVL_MEDIUM, "Received AMF3-0 command message");
              next.data = next.data.substr(1);
              amfdata = AMF::parse(next.data);
              parseAMFCommand(amfdata, 17, next.msg_stream_id);
            } //parsing AMF0-style
          }
          break;
        case 19:
          DEBUG_MSG(DLVL_MEDIUM, "Received AMF0 shared object");
          break;
        case 20: { //AMF0 command message
            amfdata = AMF::parse(next.data);
            parseAMFCommand(amfdata, 20, next.msg_stream_id);
          }
          break;
        case 22:
          DEBUG_MSG(DLVL_MEDIUM, "Received aggregate message");
          break;
        default:
          DEBUG_MSG(DLVL_FAIL, "Unknown chunk received! Probably protocol corruption, stopping parsing of incoming data.");
          break;
      }
    }
  }
}

