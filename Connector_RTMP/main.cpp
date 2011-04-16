/// \file Connector_RTMP/main.cpp
/// Contains the main code for the RTMP Connector

//debugging level 0 = nothing
//debugging level 1 = critical errors
//debugging level 2 = errors
//debugging level 3 = status information
//debugging level 4 = extremely verbose status information
#define DEBUG 4

#include <iostream>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/epoll.h>
#include <getopt.h>
#include "../util/ddv_socket.h"
#include "../util/flv_tag.h"
#include "../util/amf.h"
#include "../util/rtmpchunks.h"

/// Holds all functions and data unique to the RTMP Connector
namespace Connector_RTMP{
  
  //for connection to server
  bool ready4data = false; ///< Set to true when streaming starts.
  bool inited = false; ///< Set to true when ready to connect to Buffer.
  bool stopparsing = false; ///< Set to true when all parsing needs to be cancelled.
  
  DDV::Socket Socket; ///< Socket connected to user
  std::string streamname = "/tmp/shared_socket"; ///< Stream that will be opened
  void parseChunk();
  int Connector_RTMP(DDV::Socket conn);
};//Connector_RTMP namespace;


/// Main Connector_RTMP function
int Connector_RTMP::Connector_RTMP(DDV::Socket conn){
  Socket = conn;
  unsigned int ts;
  unsigned int fts = 0;
  unsigned int ftst;
  DDV::Socket SS;
  FLV::Tag tag;

  //first timestamp set
  RTMPStream::firsttime = RTMPStream::getNowMS();

  while (RTMPStream::handshake_in.size() < 1537){
    Socket.read(RTMPStream::handshake_in);
  }
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

  int retval;
  int poller = epoll_create(1);
  int sspoller = epoll_create(1);
  struct epoll_event ev;
  ev.events = EPOLLIN;
  ev.data.fd = Socket.getSocket();
  epoll_ctl(poller, EPOLL_CTL_ADD, Socket.getSocket(), &ev);
  struct epoll_event events[1];

  while (Socket.connected() && !FLV::Parse_Error){
    //only parse input if available or not yet init'ed
    //rightnow = getNowMS();
    retval = epoll_wait(poller, events, 1, 1);
    if ((retval > 0) || !ready4data){// || (snd_cnt - snd_window_at >= snd_window_size)
      switch (Socket.ready()){
        case -1: break; //disconnected
        case 0: break; //not ready yet
        default: parseChunk(); break; //new data is waiting
      }
    }
    if (ready4data){
      if (!inited){
        //we are ready, connect the socket!
        SS = DDV::Socket(streamname);
        if (!SS.connected()){
          #if DEBUG >= 1
          fprintf(stderr, "Could not connect to server!\n");
          #endif
          Socket.close();//disconnect user
          break;
        }
        ev.events = EPOLLIN;
        ev.data.fd = SS.getSocket();
        epoll_ctl(sspoller, EPOLL_CTL_ADD, SS.getSocket(), &ev);
        #if DEBUG >= 3
        fprintf(stderr, "Everything connected, starting to send video data...\n");
        #endif
        inited = true;
        }
      retval = epoll_wait(sspoller, events, 1, 1);
      switch (SS.ready()){
        case -1:
          #if DEBUG >= 1
          fprintf(stderr, "Source socket is disconnected.\n");
          #endif
          Socket.close();//disconnect user
          break;
        case 0: break;//not ready yet
        default:
          if (tag.SockLoader(SS)){//able to read a full packet?
            ts = tag.tagTime();
            if (ts != 0){
              if (fts == 0){fts = ts;ftst = RTMPStream::getNowMS();}
              ts -= fts;
              tag.tagTime(ts);
              ts += ftst;
            }else{
              ftst = RTMPStream::getNowMS();
              tag.tagTime(ftst);
            }
            Socket.write(RTMPStream::SendMedia((unsigned char)tag.data[0], (unsigned char *)tag.data+11, tag.len-15, ts));
            #if DEBUG >= 4
            fprintf(stderr, "Sent a tag to %i\n", Socket.getSocket());
            #endif
          }
          break;
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
void Connector_RTMP::parseChunk(){
  static RTMPStream::Chunk next;
  static std::string inbuffer;
  static AMF::Object amfdata("empty", AMF::AMF0_DDV_CONTAINER);
  static AMF::Object amfelem("empty", AMF::AMF0_DDV_CONTAINER);
  if (!Connector_RTMP::Socket.read(inbuffer)){return;} //try to get more data

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
      case 8:
        #if DEBUG >= 4
        fprintf(stderr, "Received audio data\n");
        #endif
        break;
      case 9:
        #if DEBUG >= 4
        fprintf(stderr, "Received video data\n");
        #endif
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
      case 17:
        #if DEBUG >= 4
        fprintf(stderr, "Received AFM3 command message\n");
        #endif
        break;
      case 18:
        #if DEBUG >= 4
        fprintf(stderr, "Received AFM0 data message\n");
        #endif
        break;
      case 19:
        #if DEBUG >= 4
        fprintf(stderr, "Received AFM0 shared object\n");
        #endif
        break;
      case 20:{//AMF0 command message
        bool parsed = false;
        amfdata = AMF::parse(next.data);
        #if DEBUG >= 4
        amfdata.Print();
        #endif
        if (amfdata.getContentP(0)->StrValue() == "connect"){
          #if DEBUG >= 4
          int tmpint;
          tmpint = amfdata.getContentP(2)->getContentP("videoCodecs")->NumValue();
          if (tmpint & 0x04){fprintf(stderr, "Sorensen video support detected\n");}
          if (tmpint & 0x80){fprintf(stderr, "H264 video support detected\n");}
          tmpint = amfdata.getContentP(2)->getContentP("audioCodecs")->NumValue();
          if (tmpint & 0x04){fprintf(stderr, "MP3 audio support detected\n");}
          if (tmpint & 0x400){fprintf(stderr, "AAC video support detected\n");}
          #endif
          Socket.write(RTMPStream::SendCTL(5, RTMPStream::snd_window_size));//send window acknowledgement size (msg 5)
          Socket.write(RTMPStream::SendUSR(0, 1));//send UCM StreamBegin (0), stream 1
          //send a _result reply
          AMF::Object amfreply("container", AMF::AMF0_DDV_CONTAINER);
          amfreply.addContent(AMF::Object("", "_result"));//result success
          amfreply.addContent(amfdata.getContent(1));//same transaction ID
  //        amfreply.addContent(AMFType("", (double)0, 0x05));//null - command info
          amfreply.addContent(AMF::Object(""));//server properties
          amfreply.getContentP(2)->addContent(AMF::Object("fmsVer", "FMS/3,5,4,1004"));
          amfreply.getContentP(2)->addContent(AMF::Object("capabilities", (double)127));
          amfreply.getContentP(2)->addContent(AMF::Object("mode", (double)1));
          amfreply.addContent(AMF::Object(""));//info
          amfreply.getContentP(3)->addContent(AMF::Object("level", "status"));
          amfreply.getContentP(3)->addContent(AMF::Object("code", "NetConnection.Connect.Success"));
          amfreply.getContentP(3)->addContent(AMF::Object("description", "Connection succeeded."));
          amfreply.getContentP(3)->addContent(AMF::Object("objectEncoding", (double)0));
          amfreply.getContentP(3)->addContent(AMF::Object("data", AMF::AMF0_ECMA_ARRAY));
          amfreply.getContentP(3)->getContentP(4)->addContent(AMF::Object("version", "3,5,4,1004"));
          #if DEBUG >= 4
          amfreply.Print();
          #endif
          Socket.write(RTMPStream::SendChunk(3, 20, next.msg_stream_id, amfreply.Pack()));
          //send onBWDone packet - no clue what it is, but real server sends it...
          amfreply = AMFType("container", AMF::AMF0_DDV_CONTAINER);
          amfreply.addContent(AMFType("", "onBWDone"));//result
          amfreply.addContent(AMFType("", (double)0));//zero
          amfreply.addContent(AMFType("", (double)0, AMF::AMF0_NULL));//null
          SendChunk(3, 20, next.msg_stream_id, amfreply.Pack());
          parsed = true;
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
          Socket.write(RTMPStream::SendChunk(3, 20, next.msg_stream_id, amfreply.Pack()));
          Socket.write(RTMPStream::SendUSR(0, 1));//send UCM StreamBegin (0), stream 1
          parsed = true;
        }//createStream
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
          Socket.write(RTMPStream::SendChunk(3, 20, next.msg_stream_id, amfreply.Pack()));
          parsed = true;
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
          Socket.write(RTMPStream::SendChunk(3, 20, 1, amfreply.Pack()));
          parsed = true;
        }//checkBandwidth
        if ((amfdata.getContentP(0)->StrValue() == "play") || (amfdata.getContentP(0)->StrValue() == "play2")){
          //send streambegin
          streamname = amfdata.getContentP(3)->StrValue();
          for (std::string::iterator i=streamname.end()-1; i>=streamname.begin(); --i){
            if (!isalpha(*i) && !isdigit(*i)){streamname.erase(i);}else{*i=tolower(*i);}
          }
          streamname = "/tmp/shared_socket_" + streamname;
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
          amfreply.getContentP(3)->addContent(AMF::Object("details", "PLS"));
          amfreply.getContentP(3)->addContent(AMF::Object("clientid", (double)1));
          #if DEBUG >= 4
          amfreply.Print();
          #endif
          Socket.write(RTMPStream::SendChunk(4, 20, next.msg_stream_id, amfreply.Pack()));
          amfreply = AMF::Object("container", AMF::AMF0_DDV_CONTAINER);
          amfreply.addContent(AMF::Object("", "onStatus"));//status reply
          amfreply.addContent(amfdata.getContent(1));//same transaction ID
          amfreply.addContent(AMF::Object("", (double)0, AMF::AMF0_NULL));//null - command info
          amfreply.addContent(AMF::Object(""));//info
          amfreply.getContentP(3)->addContent(AMF::Object("level", "status"));
          amfreply.getContentP(3)->addContent(AMF::Object("code", "NetStream.Play.Start"));
          amfreply.getContentP(3)->addContent(AMF::Object("description", "Playing!"));
          amfreply.getContentP(3)->addContent(AMF::Object("details", "PLS"));
          amfreply.getContentP(3)->addContent(AMF::Object("clientid", (double)1));
          #if DEBUG >= 4
          amfreply.Print();
          #endif
          Socket.write(RTMPStream::SendChunk(4, 20, 1, amfreply.Pack()));
  //No clue what this does. Most real servers send it, though...
  //        amfreply = AMFType("container", (unsigned char)0xFF);
  //        amfreply.addContent(AMFType("", "|RtmpSampleAccess"));//status reply
  //        amfreply.addContent(AMFType("", (double)1, 0x01));//bool true - audioaccess
  //        amfreply.addContent(AMFType("", (double)1, 0x01));//bool true - videoaccess
  //        SendChunk(4, 20, next.msg_stream_id, amfreply.Pack());
          RTMPStream::chunk_snd_max = 65536;//1024*1024;
          Socket.write(RTMPStream::SendCTL(1, RTMPStream::chunk_snd_max));//send chunk size max (msg 1)
          Connector_RTMP::ready4data = true;//start sending video data!
          parsed = true;
        }//createStream
        #if DEBUG >= 3
        fprintf(stderr, "AMF0 command: %s\n", amfdata.getContentP(0)->StrValue().c_str());
        #endif
        if (!parsed){
          #if DEBUG >= 2
          fprintf(stderr, "AMF0 command not processed! :(\n");
          #endif
        }
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


// Load main server setup file, default port 1935, handler is Connector_RTMP::Connector_RTMP
#define DEFAULT_PORT 1935
#define MAINHANDLER Connector_RTMP::Connector_RTMP
#define CONFIGSECT RTMP
#include "../util/server_setup.cpp"
