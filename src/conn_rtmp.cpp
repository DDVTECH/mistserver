/// \file Connector_RTMP/main.cpp
/// Contains the main code for the RTMP Connector

#include <iostream>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <getopt.h>
#include <sstream>
#include "../lib/socket.h"
#include "../lib/flv_tag.h"
#include "../lib/amf.h"
#include "../lib/rtmpchunks.h"

/// Holds all functions and data unique to the RTMP Connector
namespace Connector_RTMP{

  //for connection to server
  bool ready4data = false; ///< Set to true when streaming starts.
  bool inited = false; ///< Set to true when ready to connect to Buffer.
  bool nostats = false; ///< Set to true if no stats should be sent anymore (push mode).
  bool stopparsing = false; ///< Set to true when all parsing needs to be cancelled.

  Socket::Connection Socket; ///< Socket connected to user
  Socket::Connection SS; ///< Socket connected to server
  std::string streamname; ///< Stream that will be opened
  void parseChunk(std::string & buffer);///< Parses a single RTMP chunk.
  void sendCommand(AMF::Object & amfreply, int messagetype, int stream_id);///< Sends a RTMP command either in AMF or AMF3 mode.
  void parseAMFCommand(AMF::Object & amfdata, int messagetype, int stream_id);///< Parses a single AMF command message.
  int Connector_RTMP(Socket::Connection conn);
};//Connector_RTMP namespace;


/// Main Connector_RTMP function
int Connector_RTMP::Connector_RTMP(Socket::Connection conn){
  Socket = conn;
  FLV::Tag tag, init_tag;
  DTSC::Stream Strm;
  bool stream_inited = false;//true if init data for audio/video was sent

  //first timestamp set
  RTMPStream::firsttime = RTMPStream::getNowMS();

  RTMPStream::handshake_in.reserve(1537);
  Socket.read((char*)RTMPStream::handshake_in.c_str(), 1537);
  RTMPStream::rec_cnt += 1537;

  if (RTMPStream::doHandshake()){
    Socket.write(RTMPStream::handshake_out);
    Socket.read((char*)RTMPStream::handshake_in.c_str(), 1536);
    RTMPStream::rec_cnt += 1536;
    #if DEBUG >= 4
    fprintf(stderr, "Handshake succcess!\n");
    #endif
  }else{
    #if DEBUG >= 1
    fprintf(stderr, "Handshake fail!\n");
    #endif
    return 0;
  }

  unsigned int lastStats = 0;
  conn.setBlocking(false);

  while (Socket.connected()){
    usleep(10000);//sleep 10ms to prevent high CPU usage
    if (Socket.spool()){
      parseChunk(Socket.Received());
    }
    if (ready4data){
      if (!inited){
        //we are ready, connect the socket!
        SS = Socket::getStream(streamname);
        if (!SS.connected()){
          #if DEBUG >= 1
          fprintf(stderr, "Could not connect to server!\n");
          #endif
          Socket.close();//disconnect user
          break;
        }
        #if DEBUG >= 3
        fprintf(stderr, "Everything connected, starting to send video data...\n");
        #endif
        inited = true;
      }
      if (inited && !nostats){
        unsigned int now = time(0);
        if (now != lastStats){
          lastStats = now;
          std::string stat = "S "+Socket.getStats("RTMP");
          SS.write(stat);
        }
      }
      if (SS.spool()){
        if (Strm.parsePacket(SS.Received())){
          //sent init data if needed
          if (!stream_inited){
            if (Strm.metadata.getContentP("audio") && Strm.metadata.getContentP("audio")->getContentP("init")){
              init_tag.DTSCAudioInit(Strm);
              Socket.write(RTMPStream::SendMedia(init_tag));
            }
            if (Strm.metadata.getContentP("video") && Strm.metadata.getContentP("video")->getContentP("init")){
              init_tag.DTSCVideoInit(Strm);
              Socket.write(RTMPStream::SendMedia(init_tag));
            }
            stream_inited = true;
          }
          //sent a tag
          tag.DTSCLoader(Strm);
          Socket.write(RTMPStream::SendMedia(tag));
          #if DEBUG >= 8
          fprintf(stderr, "Sent tag to %i: [%u] %s\n", Socket.getSocket(), tag.tagTime(), tag.tagType().c_str());
          #endif
        }
      }
    }
  }
  SS.close();
  Socket.close();
  #if DEBUG >= 1
  if (FLV::Parse_Error){fprintf(stderr, "FLV Parse Error: %s\n", FLV::Error_Str.c_str());}
  fprintf(stderr, "User %i disconnected.\n", conn.getSocket());
  if (inited){
    fprintf(stderr, "Status was: inited\n");
  }else{
    if (ready4data){
      fprintf(stderr, "Status was: ready4data\n");
    }else{
      fprintf(stderr, "Status was: connected\n");
    }
  }
  #endif
  return 0;
}//Connector_RTMP

/// Tries to get and parse one RTMP chunk at a time.
void Connector_RTMP::parseChunk(std::string & inbuffer){
  //for DTSC conversion
  static DTSC::DTMI meta_out;
  static std::stringstream prebuffer; // Temporary buffer before sending real data
  static bool sending = false;
  static unsigned int counter = 0;
  //for chunk parsing
  static RTMPStream::Chunk next;
  FLV::Tag F;
  static AMF::Object amfdata("empty", AMF::AMF0_DDV_CONTAINER);
  static AMF::Object amfelem("empty", AMF::AMF0_DDV_CONTAINER);
  static AMF::Object3 amf3data("empty", AMF::AMF3_DDV_CONTAINER);
  static AMF::Object3 amf3elem("empty", AMF::AMF3_DDV_CONTAINER);

  while (next.Parse(inbuffer)){

    //send ACK if we received a whole window
    if ((RTMPStream::rec_cnt - RTMPStream::rec_window_at > RTMPStream::rec_window_size)){
      RTMPStream::rec_window_at = RTMPStream::rec_cnt;
      Socket.write(RTMPStream::SendCTL(3, RTMPStream::rec_cnt));//send ack (msg 3)
    }

    switch (next.msg_type_id){
      case 0://does not exist
        #if DEBUG >= 2
        fprintf(stderr, "UNKN: Received a zero-type message. This is an error.\n");
        #endif
        break;//happens when connection breaks unexpectedly
      case 1://set chunk size
        RTMPStream::chunk_rec_max = ntohl(*(int*)next.data.c_str());
        #if DEBUG >= 4
        fprintf(stderr, "CTRL: Set chunk size: %i\n", RTMPStream::chunk_rec_max);
        #endif
        break;
      case 2://abort message - we ignore this one
        #if DEBUG >= 4
        fprintf(stderr, "CTRL: Abort message\n");
        #endif
        //4 bytes of stream id to drop
        break;
      case 3://ack
        #if DEBUG >= 4
        fprintf(stderr, "CTRL: Acknowledgement\n");
        #endif
        RTMPStream::snd_window_at = ntohl(*(int*)next.data.c_str());
        RTMPStream::snd_window_at = RTMPStream::snd_cnt;
        break;
      case 4:{
        #if DEBUG >= 4
        short int ucmtype = ntohs(*(short int*)next.data.c_str());
        fprintf(stderr, "CTRL: User control message %hi\n", ucmtype);
        #endif
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
      } break;
      case 5://window size of other end
        #if DEBUG >= 4
        fprintf(stderr, "CTRL: Window size\n");
        #endif
        RTMPStream::rec_window_size = ntohl(*(int*)next.data.c_str());
        RTMPStream::rec_window_at = RTMPStream::rec_cnt;
        Socket.write(RTMPStream::SendCTL(3, RTMPStream::rec_cnt));//send ack (msg 3)
        break;
      case 6:
        #if DEBUG >= 4
        fprintf(stderr, "CTRL: Set peer bandwidth\n");
        #endif
        //4 bytes window size, 1 byte limit type (ignored)
        RTMPStream::snd_window_size = ntohl(*(int*)next.data.c_str());
        Socket.write(RTMPStream::SendCTL(5, RTMPStream::snd_window_size));//send window acknowledgement size (msg 5)
        break;
      case 8://audio data
      case 9://video data
      case 18://meta data
        if (SS.connected()){
          F.ChunkLoader(next);
          DTSC::DTMI pack_out = F.toDTSC(meta_out);
          if (!pack_out.isEmpty()){
            if (!sending){
              counter++;
              if (counter > 8){
                sending = true;
                meta_out.Pack(true);//pack metadata
                meta_out.packed.replace(0, 4, DTSC::Magic_Header);//prepare proper header
                SS.write(meta_out.packed);//write header/metadata
                SS.write(prebuffer.str());//write buffer
                prebuffer.str("");//clear buffer
                SS.write(pack_out.Pack(true));//simply write
              }else{
                prebuffer << pack_out.Pack(true);//buffer
              }
            }else{
              SS.write(pack_out.Pack(true));//simple write
            }
          }
        }else{
          #if DEBUG >= 4
          fprintf(stderr, "Received useless media data\n");
          #endif
          Socket.close();
        }
        break;
      case 15:
        #if DEBUG >= 4
        fprintf(stderr, "Received AFM3 data message\n");
        #endif
        break;
      case 16:
        #if DEBUG >= 4
        fprintf(stderr, "Received AFM3 shared object\n");
        #endif
        break;
      case 17:{
        #if DEBUG >= 4
        fprintf(stderr, "Received AFM3 command message\n");
        #endif
        if (next.data[0] != 0){
          next.data = next.data.substr(1);
          amf3data = AMF::parse3(next.data);
          #if DEBUG >= 4
          amf3data.Print();
          #endif
        }else{
          #if DEBUG >= 4
          fprintf(stderr, "Received AFM3-0 command message\n");
          #endif
          next.data = next.data.substr(1);
          amfdata = AMF::parse(next.data);
          parseAMFCommand(amfdata, 17, next.msg_stream_id);
        }//parsing AMF0-style
        } break;
      case 19:
        #if DEBUG >= 4
        fprintf(stderr, "Received AFM0 shared object\n");
        #endif
        break;
      case 20:{//AMF0 command message
        amfdata = AMF::parse(next.data);
        parseAMFCommand(amfdata, 20, next.msg_stream_id);
      } break;
      case 22:
        #if DEBUG >= 4
        fprintf(stderr, "Received aggregate message\n");
        #endif
        break;
      default:
        #if DEBUG >= 1
        fprintf(stderr, "Unknown chunk received! Probably protocol corruption, stopping parsing of incoming data.\n");
        #endif
        Connector_RTMP::stopparsing = true;
        break;
    }
  }
}//parseChunk

void Connector_RTMP::sendCommand(AMF::Object & amfreply, int messagetype, int stream_id){
  if (messagetype == 17){
    Socket.write(RTMPStream::SendChunk(3, messagetype, stream_id, (char)0+amfreply.Pack()));
  }else{
    Socket.write(RTMPStream::SendChunk(3, messagetype, stream_id, amfreply.Pack()));
  }
}//sendCommand

void Connector_RTMP::parseAMFCommand(AMF::Object & amfdata, int messagetype, int stream_id){
  #if DEBUG >= 4
  fprintf(stderr, "Received command: %s\n", amfdata.Print().c_str());
  #endif
  #if DEBUG >= 3
  fprintf(stderr, "AMF0 command: %s\n", amfdata.getContentP(0)->StrValue().c_str());
  #endif
  if (amfdata.getContentP(0)->StrValue() == "connect"){
    double objencoding = 0;
    if (amfdata.getContentP(2)->getContentP("objectEncoding")){
      objencoding = amfdata.getContentP(2)->getContentP("objectEncoding")->NumValue();
    }
    fprintf(stderr, "Object encoding set to %e\n", objencoding);
    #if DEBUG >= 4
    int tmpint;
    if (amfdata.getContentP(2)->getContentP("videoCodecs")){
      tmpint = (int)amfdata.getContentP(2)->getContentP("videoCodecs")->NumValue();
      if (tmpint & 0x04){fprintf(stderr, "Sorensen video support detected\n");}
      if (tmpint & 0x80){fprintf(stderr, "H264 video support detected\n");}
    }
    if (amfdata.getContentP(2)->getContentP("audioCodecs")){
      tmpint = (int)amfdata.getContentP(2)->getContentP("audioCodecs")->NumValue();
      if (tmpint & 0x04){fprintf(stderr, "MP3 audio support detected\n");}
      if (tmpint & 0x400){fprintf(stderr, "AAC audio support detected\n");}
    }
    #endif
    RTMPStream::chunk_snd_max = 4096;
    Socket.write(RTMPStream::SendCTL(1, RTMPStream::chunk_snd_max));//send chunk size max (msg 1)
    Socket.write(RTMPStream::SendCTL(5, RTMPStream::snd_window_size));//send window acknowledgement size (msg 5)
    Socket.write(RTMPStream::SendCTL(6, RTMPStream::rec_window_size));//send rec window acknowledgement size (msg 6)
    Socket.write(RTMPStream::SendUSR(0, 1));//send UCM StreamBegin (0), stream 1
    //send a _result reply
    AMF::Object amfreply("container", AMF::AMF0_DDV_CONTAINER);
    amfreply.addContent(AMF::Object("", "_result"));//result success
    amfreply.addContent(amfdata.getContent(1));//same transaction ID
    amfreply.addContent(AMF::Object(""));//server properties
    amfreply.getContentP(2)->addContent(AMF::Object("fmsVer", "FMS/3,0,1,123"));
    amfreply.getContentP(2)->addContent(AMF::Object("capabilities", (double)31));
    //amfreply.getContentP(2)->addContent(AMF::Object("mode", (double)1));
    amfreply.addContent(AMF::Object(""));//info
    amfreply.getContentP(3)->addContent(AMF::Object("level", "status"));
    amfreply.getContentP(3)->addContent(AMF::Object("code", "NetConnection.Connect.Success"));
    amfreply.getContentP(3)->addContent(AMF::Object("description", "Connection succeeded."));
    amfreply.getContentP(3)->addContent(AMF::Object("clientid", 1337));
    amfreply.getContentP(3)->addContent(AMF::Object("objectEncoding", objencoding));
    //amfreply.getContentP(3)->addContent(AMF::Object("data", AMF::AMF0_ECMA_ARRAY));
    //amfreply.getContentP(3)->getContentP(4)->addContent(AMF::Object("version", "3,5,4,1004"));
    #if DEBUG >= 4
    amfreply.Print();
    #endif
    sendCommand(amfreply, messagetype, stream_id);
    //send onBWDone packet - no clue what it is, but real server sends it...
    //amfreply = AMF::Object("container", AMF::AMF0_DDV_CONTAINER);
    //amfreply.addContent(AMF::Object("", "onBWDone"));//result
    //amfreply.addContent(amfdata.getContent(1));//same transaction ID
    //amfreply.addContent(AMF::Object("", (double)0, AMF::AMF0_NULL));//null
    //sendCommand(amfreply, messagetype, stream_id);
    return;
  }//connect
  if (amfdata.getContentP(0)->StrValue() == "createStream"){
    //send a _result reply
    AMF::Object amfreply("container", AMF::AMF0_DDV_CONTAINER);
    amfreply.addContent(AMF::Object("", "_result"));//result success
    amfreply.addContent(amfdata.getContent(1));//same transaction ID
    amfreply.addContent(AMF::Object("", (double)0, AMF::AMF0_NULL));//null - command info
    amfreply.addContent(AMF::Object("", (double)1));//stream ID - we use 1
    #if DEBUG >= 4
    amfreply.Print();
    #endif
    sendCommand(amfreply, messagetype, stream_id);
    Socket.write(RTMPStream::SendUSR(0, 1));//send UCM StreamBegin (0), stream 1
    return;
  }//createStream
  if ((amfdata.getContentP(0)->StrValue() == "closeStream") || (amfdata.getContentP(0)->StrValue() == "deleteStream")){
    if (SS.connected()){SS.close();}
    return;
  }
  if ((amfdata.getContentP(0)->StrValue() == "getStreamLength") || (amfdata.getContentP(0)->StrValue() == "getMovLen")){
    //send a _result reply
    AMF::Object amfreply("container", AMF::AMF0_DDV_CONTAINER);
    amfreply.addContent(AMF::Object("", "_result"));//result success
    amfreply.addContent(amfdata.getContent(1));//same transaction ID
    amfreply.addContent(AMF::Object("", (double)0, AMF::AMF0_NULL));//null - command info
    amfreply.addContent(AMF::Object("", (double)0));//zero length
    #if DEBUG >= 4
    amfreply.Print();
    #endif
    sendCommand(amfreply, messagetype, stream_id);
    return;
  }//getStreamLength
  if ((amfdata.getContentP(0)->StrValue() == "publish")){
    if (amfdata.getContentP(3)){
      streamname = amfdata.getContentP(3)->StrValue();
      SS = Socket::getStream(streamname);
      if (!SS.connected()){
        #if DEBUG >= 1
        fprintf(stderr, "Could not connect to server!\n");
        #endif
        Socket.close();//disconnect user
        return;
      }
      SS.write("P "+Socket.getHost()+'\n');
      nostats = true;
      #if DEBUG >= 4
      fprintf(stderr, "Connected to buffer, starting to send data...\n");
      #endif
    }
    //send a _result reply
    AMF::Object amfreply("container", AMF::AMF0_DDV_CONTAINER);
    amfreply.addContent(AMF::Object("", "_result"));//result success
    amfreply.addContent(amfdata.getContent(1));//same transaction ID
    amfreply.addContent(AMF::Object("", (double)0, AMF::AMF0_NULL));//null - command info
    amfreply.addContent(AMF::Object("", 1, AMF::AMF0_BOOL));//publish success?
    #if DEBUG >= 4
    amfreply.Print();
    #endif
    sendCommand(amfreply, messagetype, stream_id);
    Socket.write(RTMPStream::SendUSR(0, 1));//send UCM StreamBegin (0), stream 1
    //send a status reply
    amfreply = AMF::Object("container", AMF::AMF0_DDV_CONTAINER);
    amfreply.addContent(AMF::Object("", "onStatus"));//status reply
    amfreply.addContent(AMF::Object("", 0, AMF::AMF0_NUMBER));//same transaction ID
    amfreply.addContent(AMF::Object("", (double)0, AMF::AMF0_NULL));//null - command info
    amfreply.addContent(AMF::Object(""));//info
    amfreply.getContentP(3)->addContent(AMF::Object("level", "status"));
    amfreply.getContentP(3)->addContent(AMF::Object("code", "NetStream.Publish.Start"));
    amfreply.getContentP(3)->addContent(AMF::Object("description", "Stream is now published!"));
    amfreply.getContentP(3)->addContent(AMF::Object("clientid", (double)1337));
    #if DEBUG >= 4
    amfreply.Print();
    #endif
    sendCommand(amfreply, messagetype, stream_id);
    return;
  }//getStreamLength
  if (amfdata.getContentP(0)->StrValue() == "checkBandwidth"){
    //send a _result reply
    AMF::Object amfreply("container", AMF::AMF0_DDV_CONTAINER);
    amfreply.addContent(AMF::Object("", "_result"));//result success
    amfreply.addContent(amfdata.getContent(1));//same transaction ID
    amfreply.addContent(AMF::Object("", (double)0, AMF::AMF0_NULL));//null - command info
    amfreply.addContent(AMF::Object("", (double)0, AMF::AMF0_NULL));//null - command info
    #if DEBUG >= 4
    amfreply.Print();
    #endif
    sendCommand(amfreply, messagetype, stream_id);
    return;
  }//checkBandwidth
  if ((amfdata.getContentP(0)->StrValue() == "play") || (amfdata.getContentP(0)->StrValue() == "play2")){
    //send streambegin
    streamname = amfdata.getContentP(3)->StrValue();
    Socket.write(RTMPStream::SendUSR(0, 1));//send UCM StreamBegin (0), stream 1
    //send a status reply
    AMF::Object amfreply("container", AMF::AMF0_DDV_CONTAINER);
    amfreply.addContent(AMF::Object("", "onStatus"));//status reply
    amfreply.addContent(amfdata.getContent(1));//same transaction ID
    amfreply.addContent(AMF::Object("", (double)0, AMF::AMF0_NULL));//null - command info
    amfreply.addContent(AMF::Object(""));//info
    amfreply.getContentP(3)->addContent(AMF::Object("level", "status"));
    amfreply.getContentP(3)->addContent(AMF::Object("code", "NetStream.Play.Reset"));
    amfreply.getContentP(3)->addContent(AMF::Object("description", "Playing and resetting..."));
    amfreply.getContentP(3)->addContent(AMF::Object("details", "DDV"));
    amfreply.getContentP(3)->addContent(AMF::Object("clientid", (double)1337));
    #if DEBUG >= 4
    amfreply.Print();
    #endif
    sendCommand(amfreply, messagetype, stream_id);
    amfreply = AMF::Object("container", AMF::AMF0_DDV_CONTAINER);
    amfreply.addContent(AMF::Object("", "onStatus"));//status reply
    amfreply.addContent(amfdata.getContent(1));//same transaction ID
    amfreply.addContent(AMF::Object("", (double)0, AMF::AMF0_NULL));//null - command info
    amfreply.addContent(AMF::Object(""));//info
    amfreply.getContentP(3)->addContent(AMF::Object("level", "status"));
    amfreply.getContentP(3)->addContent(AMF::Object("code", "NetStream.Play.Start"));
    amfreply.getContentP(3)->addContent(AMF::Object("description", "Playing!"));
    amfreply.getContentP(3)->addContent(AMF::Object("details", "DDV"));
    amfreply.getContentP(3)->addContent(AMF::Object("clientid", (double)1337));
    #if DEBUG >= 4
    amfreply.Print();
    #endif
    sendCommand(amfreply, messagetype, stream_id);
    RTMPStream::chunk_snd_max = 102400;//100KiB
    Socket.write(RTMPStream::SendCTL(1, RTMPStream::chunk_snd_max));//send chunk size max (msg 1)
    Connector_RTMP::ready4data = true;//start sending video data!
    return;
  }//createStream

  #if DEBUG >= 2
  fprintf(stderr, "AMF0 command not processed! :(\n");
  #endif
}//parseAMFCommand


// Load main server setup file, default port 1935, handler is Connector_RTMP::Connector_RTMP
#define DEFAULT_PORT 1935
#define MAINHANDLER Connector_RTMP::Connector_RTMP
#define CONFIGSECT RTMP
#include "server_setup.h"
