/// \file Connector_RTSP/main.cpp
/// Contains the main code for the RTSP Connector

#include <queue>
#include <cmath>
#include <ctime>
#include <cstdio>
#include <string>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <getopt.h>
#include <iostream>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include "../util/socket.h"
#include "../util/flv_tag.h"
#include "../util/http_parser.h"
//JRTPLIB
#include "rtp.h"

std::string ReadNALU( ) {
  static char Separator[3] = { (char)0x00, (char)0x00, (char)0x01 };
  std::string Buffer;
  std::string Result;
  do {
    Buffer += std::cin.get();
  } while ( std::cin.good() && ( Buffer.find( Separator,0,3 ) == std::string::npos ) );
  if( !std::cin.good() ) { return ""; }
  Result = Buffer.substr(0, Buffer.find( Separator,0,3 ) );
  while( *(Result.end() - 1) == (char)0x00 ) { Result.erase( Result.end() - 1 ); }
  if( Result.size() == 0 ) { Result = ReadNALU( ); }
  return Result;
}

/// The main function of the connector
/// \param conn A connection with the client
int RTSP_Handler( Socket::Connection conn ) {
  bool ready4data = false;
  FLV::Tag tag;///< Temporary tag buffer for incoming video data.
  bool PlayVideo = false;
  bool PlayAudio = true;
  bool InitVideo = false;
  bool InitAudio = true;
  bool VideoMeta = false;
  int RTPClientPort;
  int RTCPClientPort;
  bool inited;
  jrtplib::RTPSession VideoSession;
  jrtplib::RTPSessionParams VideoParams;
  jrtplib::RTPUDPv4TransmissionParams VideoTransParams;
  std::string PreviousRequest = "";
  Socket::Connection ss(-1);
  HTTP::Parser HTTP_R, HTTP_S;
  bool PerRequest = false;
  while(conn.connected() && !FLV::Parse_Error) {
    if( HTTP_R.Read(conn ) ) {
      fprintf( stderr, "REQUEST:\n%s\n", HTTP_R.BuildRequest().c_str() );
      if( HTTP_R.GetHeader( "User-Agent" ).find( "RealMedia Player Version" ) != std::string::npos) {
        PerRequest = true;
      }
      HTTP_S.protocol = "RTSP/1.0";
      if( HTTP_R.method == "OPTIONS" ) {
        HTTP_S.SetHeader( "CSeq", HTTP_R.GetHeader( "CSeq" ).c_str() );
        HTTP_S.SetHeader( "Public", "DESCRIBE, SETUP, TEARDOWN, PLAY" );
        HTTP_S.SetBody( "\r\n\r\n" );
        fprintf( stderr, "RESPONSE:\n%s\n", HTTP_S.BuildResponse( "200", "OK" ).c_str() );
        conn.write( HTTP_S.BuildResponse( "200", "OK" ) );
      } else if ( HTTP_R.method == "DESCRIBE" ) {
        if( HTTP_R.GetHeader( "Accept" ).find( "application/sdp" ) == std::string::npos ) {
          fprintf( stderr, "RESPONSE:\n%s\n", HTTP_S.BuildResponse( "501", "Not Implemented" ).c_str() );
          conn.write( HTTP_S.BuildResponse( "501", "Not Implemented" ) );
        } else {
          HTTP_S.SetHeader( "CSeq", HTTP_R.GetHeader( "CSeq" ).c_str() );
          HTTP_S.SetHeader( "Content-Type", "application/sdp" );
          HTTP_S.SetBody( "v=0\r\no=- 0 0 IN IP4 ddvtech.com\r\ns=Fifa Test\r\nc=IN IP4 127.0.0.1\r\nt=0 0\r\na=recvonly\r\nm=video 0 RTP/AVP 98\r\na=rtpmap:98 H264/700000\r\n\r\n" );//a=fmtp:98 packetization-mode=1\r\n\r\n" );
          fprintf( stderr, "RESPONSE:\n%s\n", HTTP_S.BuildResponse( "200", "OK" ).c_str() );
          conn.write( HTTP_S.BuildResponse( "200", "OK" ) );
        }
      } else if ( HTTP_R.method == "SETUP" ) {
        std::string temp = HTTP_R.GetHeader("Transport");
        int ClientRTPLoc = temp.find( "client_port=" ) + 12;
        int PortSpacer = temp.find( "-", ClientRTPLoc );
        int PortEnd = ( temp.find( ";", PortSpacer ) );
        RTPClientPort = atoi( temp.substr( ClientRTPLoc, ( PortSpacer - ClientRTPLoc ) ).c_str() );
        RTCPClientPort = atoi( temp.substr( PortSpacer + 1 , ( PortEnd - ( PortSpacer + 1 ) ) ).c_str() );
        if( HTTP_S.GetHeader( "Session" ) != "" ) {
          fprintf( stderr, "RESPONSE:\n%s\n", HTTP_S.BuildResponse( "459", "Aggregate Operation Not Allowed" ).c_str() );
          conn.write( HTTP_S.BuildResponse( "459", "Aggregate Operation Not Allowed" ) );
        } else {
          HTTP_S.SetHeader( "CSeq", HTTP_R.GetHeader( "CSeq" ).c_str() );
          HTTP_S.SetHeader( "Session", time(NULL) );
          HTTP_S.SetHeader( "Transport", HTTP_R.GetHeader( "Transport" ) + ";server_port=50000-50001" );
          HTTP_S.SetBody( "\r\n\r\n" );
          fprintf( stderr, "RESPONSE:\n%s\n", HTTP_S.BuildResponse( "200", "OK" ).c_str() );
          conn.write( HTTP_S.BuildResponse( "200", "OK" ) );
        }
      } else if( HTTP_R.method == "PLAY" ) {
        if( HTTP_R.GetHeader( "Range" ).substr(0,4) != "npt=" ) {
          fprintf( stderr, "RESPONSE:\n%s\n", HTTP_S.BuildResponse( "501", "Not Implemented" ).c_str() );
          conn.write( HTTP_S.BuildResponse( "501", "Not Implemented" ) );
        } else {
          HTTP_S.SetHeader( "CSeq", HTTP_R.GetHeader( "CSeq" ).c_str() );
          HTTP_S.SetHeader( "Session", HTTP_R.GetHeader( "Session" ) );
          HTTP_S.SetHeader( "Range", HTTP_R.GetHeader( "Range" ) );
          HTTP_S.SetHeader( "RTP-Info", "url=" + HTTP_R.url + ";seq=0;rtptime=0" );
          HTTP_S.SetBody( "\r\n\r\n" );
          fprintf( stderr, "RESPONSE:\n%s\n", HTTP_S.BuildResponse( "200", "OK" ).c_str() );
          conn.write( HTTP_S.BuildResponse( "200", "OK" ) );
          PlayVideo = true;
        }
      } else if( HTTP_R.method == "TEARDOWN" ) {
          HTTP_S.SetHeader( "CSeq", HTTP_R.GetHeader( "CSeq" ).c_str() );
          HTTP_S.SetBody( "\r\n\r\n" );
          fprintf( stderr, "RESPONSE:\n%s\n", HTTP_S.BuildResponse( "200", "OK" ).c_str() );
          conn.write( HTTP_S.BuildResponse( "200", "OK" ) );
          PlayVideo = false;
      } else {
        fprintf( stderr, "RESPONSE:\n%s\n", HTTP_S.BuildResponse( "501", "Not Implemented" ).c_str() );
        conn.write( HTTP_S.BuildResponse( "501", "Not Implemented" ) );
      }
      HTTP_R.Clean();
      HTTP_S.Clean();
      if( PerRequest ) {
        conn.close();
      }
/*      if( ( PlayVideo ) && !inited ) {
        ss = Socket::Connection("/tmp/shared_socket_fifa");
        if (!ss.connected()){
          #if DEBUG >= 1
          fprintf(stderr, "Could not connect to server!\n");
          #endif
          conn.close();
          break;
        }
        #if DEBUG >= 3
        fprintf(stderr, "Everything connected, starting to send video data...\n");
        #endif
        inited = true;
      }*/
    }
    if( PlayVideo ) {
      if( !InitVideo ) {
        VideoParams.SetOwnTimestampUnit( 1.0/90000.0 );
        VideoParams.SetMaximumPacketSize( 10000 );
        VideoTransParams.SetPortbase( 50000 );
        int VideoStatus = VideoSession.Create( VideoParams, &VideoTransParams );
        if( VideoStatus < 0 ) {
          std::cerr << jrtplib::RTPGetErrorString( VideoStatus ) << std::endl;
          exit( -1 );
        } else {
          std::cerr << "Created video session\n";
        }
        uint8_t localip[]={127,0,0,1};
        jrtplib::RTPIPv4Address addr(localip,RTPClientPort);
        
        VideoStatus = VideoSession.AddDestination(addr);
        if (VideoStatus < 0) {
          std::cerr << jrtplib::RTPGetErrorString(VideoStatus) << std::endl;
          exit(-1);
        } else {
          std::cerr << "Destination Set\n";
        }
        VideoSession.SetDefaultPayloadType(98);
        VideoSession.SetDefaultMark(false);
        VideoSession.SetDefaultTimestampIncrement(0);
        InitVideo = true;
      }
//      std::cerr << "Retrieving NALU from stdin\n";
      std::string VideoBuf = ReadNALU( );
      if( VideoBuf == "" ) {
        jrtplib::RTPTime delay = jrtplib::RTPTime(10.0);
        VideoSession.BYEDestroy(delay,"Out of data",11);
        conn.close();
      } else {
//        std::cerr << "NALU Retrieved:\n";
//        std::cerr << "\t" << (int)VideoBuf[0] << " " << (int)VideoBuf[2]
//        << " " << (int)VideoBuf[3] << " " << (int)VideoBuf[4] << "\n";
        VideoSession.SendPacket( VideoBuf.c_str(), VideoBuf.size(), 98, false, ( 700000.0 / VideoBuf.size() ) );
      }
    }
/*
    switch (ss.ready()){
      case -1:
        conn.close();
        #if DEBUG >= 1
        fprintf(stderr, "Source socket is disconnected.\n");
        #endif
        break;
      case 0: break;//not ready yet
      default:
        if (tag.SockLoader(ss)){//able to read a full packet?
          if( tag.data[ 0 ] == 0x09 ) {
            if( ( ( tag.data[ 11 ] & 0x0F ) == 7 ) ) { //&& ( tag.data[ 12 ] == 1 ) ) {
              fprintf(stderr, "Video contains NALU\n" );
            }
          }
          if( tag.data[ 0 ] == 0x08 ) {
            if( ( tag.data[ 11 ] == 0xAF ) && ( tag.data[ 12 ] == 0x01 ) ) {
              fprintf(stderr, "Audio Contains Raw AAC\n");
            }
          }          
        }
        break;
    }*/
  }
  return 0;
}

#define DEFAULT_PORT 554
#define MAINHANDLER RTSP_Handler
#define CONFIGSECT RTSP
#include "../util/server_setup.cpp"
