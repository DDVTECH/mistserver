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
#include <sstream>
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
/// Function was used as a way of debugging data. FLV does not contain all the metadata we need, so we had to try different approaches.
/// \return The Nalu data.
/// \todo Throw this function away when everything works, it is not needed.
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

/// The main function of the connector.
/// Used by server_setup.cpp in the bottom of the file, to start up the Connector.
/// This function contains the while loop the accepts connections, and sends them data.
/// \param conn A connection with the client.
int RTSP_Handler( Socket::Connection conn ) {
  /// \todo Convert this to DTSC::DTMI, with an additional DTSC::Stream/
  FLV::Tag tag;// Temporary tag buffer for incoming video data. 
  bool PlayVideo = false;
  bool PlayAudio = true;
  //JRTPlib Objects to handle the RTP connection, which runs "parallel" to RTSP.
  jrtplib::RTPSession VideoSession;
  jrtplib::RTPSessionParams VideoParams;
  jrtplib::RTPUDPv6TransmissionParams VideoTransParams;
  std::string PreviousRequest = "";
  Socket::Connection ss(-1);
  HTTP::Parser HTTP_R, HTTP_S;
  //Some clients appear to expect a single request per connection. Don't know which ones.
  bool PerRequest = false;
  //The main loop of the function
  while(conn.connected() && !FLV::Parse_Error) {
    if( HTTP_R.Read(conn ) ) {
      //send Debug info to stderr.
      //send the appropriate responses on RTSP Commands.
      fprintf( stderr, "REQUEST:\n%s\n", HTTP_R.BuildRequest().c_str() );
      HTTP_S.protocol = "RTSP/1.0";
      if( HTTP_R.method == "OPTIONS" ) {
        //Always return the requested CSeq value.
        HTTP_S.SetHeader( "CSeq", HTTP_R.GetHeader( "CSeq" ).c_str() );
        //The minimal set of options required for RTSP, add new options here as well if we want to support these.
        HTTP_S.SetHeader( "Public", "OPTIONS, DESCRIBE, SETUP, TEARDOWN, PLAY" );
        //End the HTTP body, IMPORTANT!! Connection hangs otherwise!!
        HTTP_S.SetBody( "\r\n\r\n" );
        fprintf( stderr, "RESPONSE:\n%s\n", HTTP_S.BuildResponse( "200", "OK" ).c_str() );
        conn.write( HTTP_S.BuildResponse( "200", "OK" ) );
      } else if ( HTTP_R.method == "DESCRIBE" ) {
        ///\todo Implement DESCRIBE option.
        //Don't know if a 501 response is seen as valid. If it is, don't bother changing it.
        if( HTTP_R.GetHeader( "Accept" ).find( "application/sdp" ) == std::string::npos ) {
          fprintf( stderr, "RESPONSE:\n%s\n", HTTP_S.BuildResponse( "501", "Not Implemented" ).c_str() );
          conn.write( HTTP_S.BuildResponse( "501", "Not Implemented" ) );
        } else {
          HTTP_S.SetHeader( "CSeq", HTTP_R.GetHeader( "CSeq" ).c_str() );
          HTTP_S.SetHeader( "Content-Type", "application/sdp" );
          /// \todo Retrieve presence of video and audio data, and process into response. Can now easily be done through DTSC::DTMI
          /// \todo Retrieve Packetization mode ( is 0 for now ). I suppose this is the H264 packetization mode. Can maybe be retrieved from the docs on H64.
          /// \todo Send a valid SDP file.
          /// \todo Add audio to SDP file.
          //This is just a dummy with data that was supposedly right for our teststream.
          //SDP Docs: http://tools.ietf.org/html/rfc4566
          HTTP_S.SetBody( "v=0\r\n" //protocol version
              "o=- 0 0 IN IP4 ddvtech.com\r\n" //originator and session identifier (5.2):
                                               //username sess-id sess-version nettype addrtype unicast-addr
                                               //"-": no concept of User IDs, nettype IN(ternet)
                                               //IP4: following address is a FQDN for IPv4
              "s=Fifa Test\r\n" //session name (5.3)
              //"c" - destination is specified in SETUP per rfc2326 C.1.7, set null as recommended
              "c=IN IP4 0.0.0.0\r\n" //connection information -- not required if included in all media
                                     //nettype addrtype connection-address
              "t=0 0\r\n" //time the session is active: start-time stop-time; "0 0"=permanent session
              "a=recvonly\r\n"//zero or more session attribute lines
              "m=video 0 RTP/AVP 98\r\n"//media name and transport address: media port proto fmt ...
              "a=control:rtsp://localhost/fifa/video\r\n"//rfc2326 C.1.1, URL for aggregate control on session level
              "a=rtpmap:98 H264/90000\r\n"//rfc2326 C.1.3, dynamic payload type; see also http://tools.ietf.org/html/rfc1890#section-5
              "a=fmtp:98 packetization-mode=0"//codec-specific parameters
              "\r\n\r\n");//m=audio 0 RTP/AAP 96\r\na=control:rtsp://localhost/fifa/audio\r\na=rtpmap:96 mpeg4-generic/16000/2\r\n\r\n");
          //important information when supporting multiple streams http://tools.ietf.org/html/rfc2326#appendix-C.3
          fprintf( stderr, "RESPONSE:\n%s\n", HTTP_S.BuildResponse( "200", "OK" ).c_str() );
          conn.write( HTTP_S.BuildResponse( "200", "OK" ) );
        }
      } else if ( HTTP_R.method == "SETUP" ) {
        std::string temp = HTTP_R.GetHeader("Transport");
        //Extract the random UTP pair for video data ( RTP/RTCP)
        int ClientRTPLoc = temp.find( "client_port=" ) + 12;
        int PortSpacer = temp.find( "-", ClientRTPLoc );
        int RTPClientPort = atoi( temp.substr( ClientRTPLoc, ( PortSpacer - ClientRTPLoc ) ).c_str() );
        if( HTTP_S.GetHeader( "Session" ) != "" ) {
          //Return an error if a second client tries to connect with an already running stream.
          fprintf( stderr, "RESPONSE:\n%s\n", HTTP_S.BuildResponse( "459", "Aggregate Operation Not Allowed" ).c_str() );
          conn.write( HTTP_S.BuildResponse( "459", "Aggregate Operation Not Allowed" ) );
        } else {
          HTTP_S.SetHeader( "CSeq", HTTP_R.GetHeader( "CSeq" ).c_str() );
          HTTP_S.SetHeader( "Session", time(NULL) );
          /// \todo Add support for audio
//          if( HTTP_R.url.find( "audio" ) != std::string::npos ) {
//            HTTP_S.SetHeader( "Transport", HTTP_R.GetHeader( "Transport" ) + ";server_port=50002-50003" );
//          } else {
          //Stub data for testing purposes. This should now be extracted somehow from DTSC::DTMI
            VideoParams.SetOwnTimestampUnit( ( 1.0 / 29.917 ) * 90000.0 );
            VideoParams.SetMaximumPacketSize( 10000 );
          //create a JRTPlib session
            int VideoStatus;
            uint16_t pbase;
            //after 20 retries, just give up, most ports are likely in use
            int retries = 20;
            do {
            //pick the right port here in the range 5000 to 5000 + 2 * 500 = 6000
              pbase = 5000 + 2 * (rand() % 500);
              VideoTransParams.SetPortbase( pbase );
              VideoStatus = VideoSession.Create( VideoParams, &VideoTransParams, jrtplib::RTPTransmitter::IPv6UDPProto  );
            } while(VideoStatus < 0 && --retries > 0);
            if( VideoStatus < 0 ) {
              std::cerr << "Video session could not be created: " << jrtplib::RTPGetErrorString( VideoStatus ) << std::endl;
              exit( -1 );
            } else {
              std::cerr << "Created video session using ports " << pbase << " and " << (pbase+1) << "\n";
            }
          //send video data
            std::stringstream transport;
            transport << HTTP_R.GetHeader( "Transport" ) << ";server_port=" << pbase << "-" << (pbase+1);
            HTTP_S.SetHeader( "Transport", transport.str() );

          /// \todo Connect with clients other than localhost            
            uint8_t localip[32];
            int status = inet_pton( AF_INET6, conn.getHost().c_str(), localip ) ;
          //Debug info
            std::cerr << "Status: " <<  status << "\n";
            jrtplib::RTPIPv6Address addr(localip,RTPClientPort);
            
          //add the destination address to the VideoSession
            VideoStatus = VideoSession.AddDestination(addr);
            if (VideoStatus < 0) {
              std::cerr << "Destination could not be set: " << jrtplib::RTPGetErrorString(VideoStatus) << std::endl;
              exit(-1);
            } else {
              std::cerr << "Destination Set\n";
            }
          //Stub data for testing purposes.
          //Payload type should confirm with the SDP File. 98 == H264 / AVC
            VideoSession.SetDefaultPayloadType(98);
            VideoSession.SetDefaultMark(false);
          //We have no idea if this timestamp has to correspond with the OwnTimeStampUnit() above.
            VideoSession.SetDefaultTimestampIncrement( ( 1.0 / 29.917 ) * 90000 );
//          }
          HTTP_S.SetBody( "\r\n\r\n" );
          fprintf( stderr, "RESPONSE:\n%s\n", HTTP_S.BuildResponse( "200", "OK" ).c_str() );
          conn.write( HTTP_S.BuildResponse( "200", "OK" ) );
        }
      } else if( HTTP_R.method == "PLAY" ) {
        if( HTTP_R.GetHeader( "Range" ).substr(0,4) != "npt=" ) {
          //We do not support this, whatever it is. Not needed for minimal compliance.
          fprintf( stderr, "RESPONSE:\n%s\n", HTTP_S.BuildResponse( "501", "Not Implemented" ).c_str() );
          conn.write( HTTP_S.BuildResponse( "501", "Not Implemented" ) );
        } else {
          //Initializes for actual streaming over the SETUP connection.
          HTTP_S.SetHeader( "CSeq", HTTP_R.GetHeader( "CSeq" ).c_str() );
          HTTP_S.SetHeader( "Session", HTTP_R.GetHeader( "Session" ) );
          HTTP_S.SetHeader( "Range", HTTP_R.GetHeader( "Range" ) );
          HTTP_S.SetHeader( "RTP-Info", "url=" + HTTP_R.url + ";seq=0;rtptime=0" );
          HTTP_S.SetBody( "\r\n\r\n" );
          fprintf( stderr, "RESPONSE:\n%s\n", HTTP_S.BuildResponse( "200", "OK" ).c_str() );
          conn.write( HTTP_S.BuildResponse( "200", "OK" ) );
          //Used further down, to start streaming video.
          //PlayAudio = true;
          PlayVideo = true;
        }
      } else if( HTTP_R.method == "TEARDOWN" ) {
          //If we were sending any stream data at this point, stop it, but keep the setup.
          HTTP_S.SetHeader( "CSeq", HTTP_R.GetHeader( "CSeq" ).c_str() );
          HTTP_S.SetBody( "\r\n\r\n" );
          fprintf( stderr, "RESPONSE:\n%s\n", HTTP_S.BuildResponse( "200", "OK" ).c_str() );
          conn.write( HTTP_S.BuildResponse( "200", "OK" ) );
          //PlayAudio = false;
          PlayVideo = false;
      } else {
        //We do not implement other commands ( yet )
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
      /// \todo Select correct source. This should become the DTSC::DTMI or the DTSC::Stream, whatever seems more natural.
      std::string VideoBuf = ReadNALU( );
      if( VideoBuf == "" ) {
        //videobuffer is empty, no more data.
        jrtplib::RTPTime delay = jrtplib::RTPTime(10.0);
        VideoSession.BYEDestroy(delay,"Out of data",11);
        conn.close();
      } else {
        //Send a single NALU (H264 block) here.
        VideoSession.SendPacket( VideoBuf.c_str(), VideoBuf.size(), 98, false, ( 1.0 / 29.917 ) * 90000 );
        //we can add delays here as follows:
        //don't know if these are nescecary or not, but good for testing nonetheless
//        jrtplib::RTPTime delay( ( 1.0 / 29.917 ) * 90000 );
//        jrtplib::RTPTime::Wait( delay );
      }
    }
  }
  return 0;
}

//Set Default Port
#define DEFAULT_PORT 554
//Set the function that should be forked for each client
#define MAINHANDLER RTSP_Handler
//Set the section in the Config file, though we will not use this yet
#define CONFIGSECT RTSP
//Include the main functionality, as well as fork support and everything.
#include "../util/server_setup.cpp"
