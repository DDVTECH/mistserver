/// \file Connector_RTSP/main.cpp
/// Contains the main code for the RTSP Connector

#include <queue>
#include <cmath>
#include <ctime>
#include <cstdio>
#include <string>
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

/// The main function of the connector
/// \param conn A connection with the client
int RTSP_Handler( Socket::Connection conn ) {
  bool ready4data = false;
  FLV::Tag tag;///< Temporary tag buffer for incoming video data.
  bool inited = false;
  bool firstvideo = true;
  std::string PreviousRequest = "";
  Socket::Connection ss(-1);
  HTTP::Parser HTTP_R, HTTP_S;
  bool PerRequest = false;
  while(conn.connected() && !FLV::Parse_Error) {
    if( HTTP_R.Read(conn, ready4data) ) {
      fprintf( stderr, "REQUEST:\n%s\n", HTTP_R.BuildRequest().c_str() );
      if( HTTP_R.GetHeader( "User-Agent" ).find( "RealMedia Player Version" ) != std::string::npos) {
        PerRequest = true;
      }
      HTTP_S.protocol = "RTSP/1.0";
      if( HTTP_R.method == "OPTIONS" ) {
        HTTP_S.SetHeader( "CSeq", HTTP_R.GetHeader( "CSeq" ).c_str() );
        HTTP_S.SetHeader( "Public", "DESCRIBE, SETUP, TEARDOWN, PLAY" );
        HTTP_S.SetBody( "\r\n" );
        fprintf( stderr, "RESPONSE:\n%s\n", HTTP_S.BuildResponse( "200", "OK" ).c_str() );
        conn.write( HTTP_S.BuildResponse( "200", "OK" ) );
      } else if ( HTTP_R.method == "DESCRIBE" ) {
        if( HTTP_R.GetHeader( "Accept" ).find( "application/sdp" ) == std::string::npos ) {
          fprintf( stderr, "RESPONSE:\n%s\n", HTTP_S.BuildResponse( "501", "Not Implemented" ).c_str() );
          conn.write( HTTP_S.BuildResponse( "501", "Not Implemented" ) );
        } else {
          HTTP_S.SetHeader( "CSeq", HTTP_R.GetHeader( "CSeq" ).c_str() );
          HTTP_S.SetHeader( "Content-Type", "application/sdp" );
          HTTP_S.SetBody( "v=0\r\no=- 0 0 IN IP4 ddvtech.com\r\ns=Fifa Test\r\nc=IN IP4 127.0.0.1\r\nt=0 0\r\na=recvonly\r\nm=video 0 RTP/AVP 98\r\na=rtpmap:98 H264/90000\na=fmtp:98 packetization-mode=1\r\n\r\n" );
          fprintf( stderr, "RESPONSE:\n%s\n", HTTP_S.BuildResponse( "200", "OK" ).c_str() );
          conn.write( HTTP_S.BuildResponse( "200", "OK" ) );
        }
      } else if ( HTTP_R. method == "SETUP" ) {
        std::string temp = HTTP_R.GetHeader("Transport");
        int ClientRTPLoc = temp.find( "client_port=" ) + 12;
        int PortSpacer = temp.find( "-", ClientRTPLoc );
        int PortEnd = ( temp.find( ";", PortSpacer ) );
        int RTPClientPort = atoi( temp.substr( ClientRTPLoc, ( PortSpacer - ClientRTPLoc ) ).c_str() );
        int RTCPClientPort = atoi( temp.substr( PortSpacer + 1 , ( PortEnd - ( PortSpacer + 1 ) ) ).c_str() );
        if( HTTP_S.GetHeader( "Session" ) != "" ) {
          fprintf( stderr, "RESPONSE:\n%s\n", HTTP_S.BuildResponse( "459", "Aggregate Operation Not Allowed" ).c_str() );
          conn.write( HTTP_S.BuildResponse( "459", "Aggregate Operation Not Allowed" ) );
        } else {
          HTTP_S.SetHeader( "CSeq", HTTP_R.GetHeader( "CSeq" ).c_str() );
          HTTP_S.SetHeader( "Session", time(NULL) );
          fprintf( stderr, "RESPONSE:\n%s\n", HTTP_S.BuildResponse( "200", "OK" ).c_str() );
          conn.write( HTTP_S.BuildResponse( "200", "OK" ) );
        }
      } else {
        fprintf( stderr, "RESPONSE:\n%s\n", HTTP_S.BuildResponse( "501", "Not Implemented" ).c_str() );
        conn.write( HTTP_S.BuildResponse( "501", "Not Implemented" ) );
      }
      HTTP_R.Clean();
      HTTP_S.Clean();
      if( PerRequest ) {
        conn.close();
      }
    }
/*    if( !inited ) {
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
    }
    
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
