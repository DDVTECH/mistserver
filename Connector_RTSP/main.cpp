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

/// Reads a single NALU from std::cin. Expected is H.264 Bytestream format.
/// \return The Nalu data.
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
  FLV::Tag tag;///< Temporary tag buffer for incoming video data.
  bool PlayVideo = false;
  bool PlayAudio = true;
  jrtplib::RTPSession VideoSession;
  jrtplib::RTPSessionParams VideoParams;
  jrtplib::RTPUDPv6TransmissionParams VideoTransParams;
  std::string PreviousRequest = "";
  Socket::Connection ss(-1);
  HTTP::Parser HTTP_R, HTTP_S;
  bool PerRequest = false;
  while(conn.connected() && !FLV::Parse_Error) {
    if( HTTP_R.Read(conn ) ) {
      fprintf( stderr, "REQUEST:\n%s\n", HTTP_R.BuildRequest().c_str() );
      HTTP_S.protocol = "RTSP/1.0";
      if( HTTP_R.method == "OPTIONS" ) {
        HTTP_S.SetHeader( "CSeq", HTTP_R.GetHeader( "CSeq" ).c_str() );
        HTTP_S.SetHeader( "Public", "OPTIONS, DESCRIBE, SETUP, TEARDOWN, PLAY" );
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
          /// \todo Retrieve presence of video and audio data, and process into response
          /// \todo Retrieve Packetization mode ( is 0 for now ). Where can I retrieve this?
          HTTP_S.SetBody( "v=0\r\no=- 0 0 IN IP4 ddvtech.com\r\ns=Fifa Test\r\nc=IN IP4 127.0.0.1\r\nt=0 0\r\na=recvonly\r\nm=video 0 RTP/AVP 98\r\na=control:rtsp://localhost/fifa/video\r\na=rtpmap:98 H264/90000\r\na=fmtp:98 packetization-mode=0\r\n\r\n");//m=audio 0 RTP/AAP 96\r\na=control:rtsp://localhost/fifa/audio\r\na=rtpmap:96 mpeg4-generic/16000/2\r\n\r\n");
          fprintf( stderr, "RESPONSE:\n%s\n", HTTP_S.BuildResponse( "200", "OK" ).c_str() );
          conn.write( HTTP_S.BuildResponse( "200", "OK" ) );
        }
      } else if ( HTTP_R.method == "SETUP" ) {
        std::string temp = HTTP_R.GetHeader("Transport");
        int ClientRTPLoc = temp.find( "client_port=" ) + 12;
        int PortSpacer = temp.find( "-", ClientRTPLoc );
        int RTPClientPort = atoi( temp.substr( ClientRTPLoc, ( PortSpacer - ClientRTPLoc ) ).c_str() );
        if( HTTP_S.GetHeader( "Session" ) != "" ) {
          fprintf( stderr, "RESPONSE:\n%s\n", HTTP_S.BuildResponse( "459", "Aggregate Operation Not Allowed" ).c_str() );
          conn.write( HTTP_S.BuildResponse( "459", "Aggregate Operation Not Allowed" ) );
        } else {
          HTTP_S.SetHeader( "CSeq", HTTP_R.GetHeader( "CSeq" ).c_str() );
          HTTP_S.SetHeader( "Session", time(NULL) );
          /// \todo "Random" generation of server_ports
          if( HTTP_R.url.find( "audio" ) != std::string::npos ) {
            HTTP_S.SetHeader( "Transport", HTTP_R.GetHeader( "Transport" ) + ";server_port=50002-50003" );
          } else {
            HTTP_S.SetHeader( "Transport", HTTP_R.GetHeader( "Transport" ) + ";server_port=50000-50001" );
            VideoParams.SetOwnTimestampUnit( ( 1.0 / 29.917 ) * 90000.0 );
            VideoParams.SetMaximumPacketSize( 10000 );
            //pick the right port here
            VideoTransParams.SetPortbase( 50000 );
            int VideoStatus = VideoSession.Create( VideoParams, &VideoTransParams, jrtplib::RTPTransmitter::IPv6UDPProto  );
            if( VideoStatus < 0 ) {
              std::cerr << jrtplib::RTPGetErrorString( VideoStatus ) << std::endl;
              exit( -1 );
            } else {
              std::cerr << "Created video session\n";
            }
            /// \todo retrieve other client than localhost --> Socket::Connection has no support for this yet?
            
            uint8_t localip[32];
            int status = inet_pton( AF_INET6, conn.getHost().c_str(), localip ) ;
            std::cerr << "Status: " <<  status << "\n";
            jrtplib::RTPIPv6Address addr(localip,RTPClientPort);
            
            VideoStatus = VideoSession.AddDestination(addr);
            if (VideoStatus < 0) {
              std::cerr << jrtplib::RTPGetErrorString(VideoStatus) << std::endl;
              exit(-1);
            } else {
              std::cerr << "Destination Set\n";
            }
            VideoSession.SetDefaultPayloadType(98);
            VideoSession.SetDefaultMark(false);
            VideoSession.SetDefaultTimestampIncrement( ( 1.0 / 29.917 ) * 90000 );
          }
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
    }
    if( PlayVideo ) {
      /// \todo Select correct source
      std::string VideoBuf = ReadNALU( );
      if( VideoBuf == "" ) {
        jrtplib::RTPTime delay = jrtplib::RTPTime(10.0);
        VideoSession.BYEDestroy(delay,"Out of data",11);
        conn.close();
      } else {
        VideoSession.SendPacket( VideoBuf.c_str(), VideoBuf.size(), 98, false, ( 1.0 / 29.917 ) * 90000 );
//        jrtplib::RTPTime delay( ( 1.0 / 29.917 ) * 90000 );
//        jrtplib::RTPTime::Wait( delay );
      }
    }
  }
  return 0;
}

#define DEFAULT_PORT 554
#define MAINHANDLER RTSP_Handler
#define CONFIGSECT RTSP
#include "../util/server_setup.cpp"
