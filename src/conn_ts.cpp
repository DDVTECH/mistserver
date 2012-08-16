#define DEBUG 10

/// \file conn_ts.cpp
/// Contains the main code for the TS Connector

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
#include <mist/socket.h>
#include <mist/dtsc.h>
#include <mist/ts_packet.h>
#include <mist/config.h>

/// The main function of the connector
/// \param conn A connection with the client
/// \param streamname The name of the stream
int TS_Handler( Socket::Connection conn, std::string streamname ) {
  std::string ToPack;
  std::string DTMIData;
  TS::Packet PackData;
  DTSC::Stream DTSCStream;
  int PacketNumber = 0;
  uint64_t TimeStamp = 0;
  int ThisNaluSize;
  char VideoCounter = 0;
  char AudioCounter = 0;
  bool WritePesHeader;
  bool IsKeyFrame;
  bool FirstKeyFrame = true;
  bool FirstIDRInKeyFrame;
  
  bool inited = false;
  Socket::Connection ss;
  
  while(conn.connected()) {
    if( !inited ) {
      ss = Socket::Connection("/tmp/mist/stream_" + streamname );
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
        ss.spool();
        if ( DTSCStream.parsePacket( ss.Received() ) ) {
          if( DTSCStream.lastType() == DTSC::VIDEO ) {
            DTMIData = DTSCStream.lastData();
            if( DTSCStream.getPacket(0).getContent("keyframe").NumValue() ) {
              IsKeyFrame = true;
              FirstIDRInKeyFrame = true;
            } else {
              IsKeyFrame = false;
              FirstKeyFrame = false;
            }
            #if DEBUG >= 3
   		    fprintf(stderr, "Videoframe received -- Keyframe: %d\n", IsKeyFrame );
			#endif
            TimeStamp = (DTSCStream.getPacket(0).getContent("time").NumValue() * 27000 );
            int TSType;
            bool FirstPic = true;
            while( DTMIData.size() ) {
              ThisNaluSize =  (DTMIData[0] << 24) + (DTMIData[1] << 16) +
                              (DTMIData[2] << 8) + DTMIData[3];
              DTMIData.erase(0,4);//Erase the first four characters;
              TSType = (int)DTMIData[0];
              if( TSType == 0x25 ) {
                if( FirstPic ) {
                  ToPack.append(TS::PPS,24);
                  ToPack.append(TS::SPS,8);
                  FirstPic = false;
                } 
                if( IsKeyFrame ) {
                  if( !FirstKeyFrame && FirstIDRInKeyFrame ) {
                    ToPack.append(TS::NalHeader,4);
                    FirstIDRInKeyFrame = false;
                  } else {
                    ToPack.append(TS::ShortNalHeader,3);
                  }
                }
              } else if ( TSType == 0x21 ) {
                if( FirstPic ) {
                  ToPack.append(TS::NalHeader,4);
                  FirstPic = false;
                } else {
                  ToPack.append(TS::ShortNalHeader,3);
                }
              } else {
                ToPack.append(TS::NalHeader,4);
              }
              ToPack.append(DTMIData,0,ThisNaluSize);
              DTMIData.erase(0,ThisNaluSize);
            }
            WritePesHeader = true;
            while( ToPack.size() ) {
              if ( ( PacketNumber % 42 ) == 0 ) {
                PackData.DefaultPAT();
                #if DEBUG >= 3
				fprintf(stderr, "\tSending PAT\n" );
				#endif
                conn.write( PackData.ToString() );
              } else if ( ( PacketNumber % 42 ) == 1 ) {
                PackData.DefaultPMT();
                #if DEBUG >= 3
				fprintf(stderr, "\tSending PMT\n" );
				#endif
                conn.write( PackData.ToString() );
              } else {
                PackData.Clear();
                PackData.PID( 0x100 );
                PackData.ContinuityCounter( VideoCounter );
                VideoCounter ++;
                if( WritePesHeader ) {
                  PackData.UnitStart( 1 );
                  if( IsKeyFrame ) {
                    PackData.RandomAccess( 1 );
                    PackData.PCR( TimeStamp );
                  } else {
                    PackData.AdaptationField( 0x01 );
                  }
                  PackData.AddStuffing( 184 - (20+ToPack.size()) );
                  PackData.PESVideoLeadIn( ToPack.size() );
                  WritePesHeader = false;
                } else {
                  PackData.AdaptationField( 0x01 );
                  PackData.AddStuffing( 184 - (ToPack.size()) );
                }
                PackData.FillFree( ToPack );
                #if DEBUG >= 3
				fprintf(stderr, "\tSending Video Data\n" );
				#endif
                conn.write( PackData.ToString() );
              }
              PacketNumber ++;
              conn.spool();
            }
          }
          if( DTSCStream.lastType() == DTSC::AUDIO ) {
            WritePesHeader = true;
            DTMIData = DTSCStream.lastData();
            ToPack = TS::GetAudioHeader( DTMIData.size() );
            ToPack += DTMIData;
            TimeStamp = DTSCStream.getPacket(0).getContent("time").NumValue() * 900;
            while( ToPack.size() ) {
              if ( ( PacketNumber % 42 ) == 0 ) {
                PackData.DefaultPAT();
                conn.write( PackData.ToString() );
              } else if ( ( PacketNumber % 42 ) == 1 ) {
                PackData.DefaultPMT();
                conn.write( PackData.ToString() );
              } else {
                PackData.Clear();
                PackData.PID( 0x101 );
                PackData.ContinuityCounter( AudioCounter );
                AudioCounter ++;
                if( WritePesHeader ) {
                  PackData.UnitStart( 1 );
                  PackData.RandomAccess( 1 );
                  PackData.AddStuffing( 184 - (14 + ToPack.size()) );
                  PackData.PESAudioLeadIn( ToPack.size(), TimeStamp );
                  WritePesHeader = false;
                } else {
                  PackData.AdaptationField( 0x01 );
                  PackData.AddStuffing( 184 - (ToPack.size()) );
                }
                PackData.FillFree( ToPack );
                conn.write( PackData.ToString() );
              }
              PacketNumber ++;
              conn.spool();
            }
          }          
        }
        break;
    }
  }
  return 0;
}

int main(int argc, char ** argv){
  Util::Config conf(argv[0], PACKAGE_VERSION);
  conf.addOption("streamname",JSON::fromString("{\"arg\":\"string\",\"arg_num\":1,\"help\":\"The name of the stream that this connector will transmit.\"}"));
  conf.addConnectorOptions(8888);
  conf.parseArgs(argc, argv);
  Socket::Server server_socket = Socket::Server(conf.getInteger("listen_port"), conf.getString("listen_interface"));
  if (!server_socket.connected()){return 1;}
  conf.activate();
  
  while (server_socket.connected() && conf.is_active){
    Socket::Connection S = server_socket.accept();
    if (S.connected()){//check if the new connection is valid
fprintf(stderr,"Incoming connection\n");
      pid_t myid = fork();
      if (myid == 0){//if new child, start MAINHANDLER
        return TS_Handler(S,conf.getString("streamname"));
      }else{//otherwise, do nothing or output debugging text
        #if DEBUG >= 3
        fprintf(stderr, "Spawned new process %i for socket %i\n", (int)myid, S.getSocket());
        #endif
      }
    }
  }//while connected
  server_socket.close();
  return 0;
}//main
