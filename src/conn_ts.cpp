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
#include <mist/stream.h>

/// The main function of the connector
/// \param conn A connection with the client
/// \param streamname The name of the stream
int TS_Handler( Socket::Connection conn, std::string streamname ) {
  std::string ToPack;
  TS::Packet PackData;
  DTSC::Stream Strm;
  int PacketNumber = 0;
  uint64_t TimeStamp = 0;
  int ThisNaluSize;
  char VideoCounter = 0;
  char AudioCounter = 0;
  bool WritePesHeader;
  bool IsKeyFrame;
  bool FirstKeyFrame = true;
  bool FirstIDRInKeyFrame;
  
  std::string myPPS;
  std::string mySPS;
  
  bool inited = false;
  Socket::Connection ss;
  
  while(conn.connected()) {
    if( !inited ) {
      ss = Util::Stream::getStream(streamname);
      if (!ss.connected()){
        #if DEBUG >= 1
        fprintf(stderr, "Could not connect to server!\n");
        #endif
        conn.close();
        break;
      }
      ss.SendNow( "p\n" );
      #if DEBUG >= 3
      fprintf(stderr, "Everything connected, starting to send video data...\n");
      #endif
      inited = true;
    }
    fprintf( stderr, "Are we inited? %d\n", inited );
    if (ss.spool()){
      while (Strm.parsePacket(ss.Received())){
        if( mySPS == "" && myPPS == "" ) {
          int SPSLen = ((Strm.metadata["video"]["init"].asString()[6])<<8) + Strm.metadata["video"]["init"].asString()[7];
          mySPS = Strm.metadata["video"]["init"].asString().substr(8,SPSLen);
          fprintf( stderr, "mySPS.size(): %d ( given %d )\n", mySPS.size(), SPSLen );
          int PPSLen = ((Strm.metadata["video"]["init"].asString()[8+SPSLen+1])<<8) + Strm.metadata["video"]["init"].asString()[8+SPSLen+2];
          myPPS = Strm.metadata["video"]["init"].asString().substr(8+SPSLen+3,PPSLen);
          fprintf( stderr, "myPPS.size(): %d ( given %d )\n", myPPS.size(), PPSLen );
        }
        if( Strm.lastType() == DTSC::VIDEO ) {
          fprintf( stderr, "VideoFrame\n" );
          if( Strm.getPacket(0).isMember("keyframe") ) {
            IsKeyFrame = true;
            FirstIDRInKeyFrame = true;
          } else {
            IsKeyFrame = false;
            FirstKeyFrame = false;
          }
          TimeStamp = ( Strm.getPacket(0)["time"].asInt() );
          if( IsKeyFrame ) { fprintf( stderr, "  Keyframe, timeStamp: %u\n", TimeStamp ); }
          int TSType;
          bool FirstPic = true;
          while( Strm.lastData().size() > 4) {
            fprintf( stderr, "    Loop-iter\n" );
            ThisNaluSize =  (Strm.lastData()[0] << 24) + (Strm.lastData()[1] << 16) +
                            (Strm.lastData()[2] << 8) + Strm.lastData()[3];
            Strm.lastData().erase(0,4);//Erase the first four characters;
            TSType = (int)Strm.lastData()[0];
            if( TSType == 0x05 ) {
              if( FirstPic ) {
                ToPack.append(myPPS);
                ToPack.append(mySPS);
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
            } else if ( TSType == 0x01 ) {
              if( FirstPic ) {
                ToPack.append(TS::NalHeader,4);
                FirstPic = false;
              } else {
                ToPack.append(TS::ShortNalHeader,3);
              }
            } else {
              ToPack.append(TS::NalHeader,4);
            }
            ToPack.append(Strm.lastData(),0,ThisNaluSize);
            Strm.lastData().erase(0,ThisNaluSize);
          }
          WritePesHeader = true;
          while( ToPack.size() ) {
            if ( ( PacketNumber % 42 ) == 0 ) {
              fprintf( stderr, "    Sending PAT\n" );
              PackData.DefaultPAT();
              fprintf( stderr, "    Constructed PAT\n" );
              conn.SendNow( PackData.ToString(), 188 );
              fprintf( stderr, "    Sent PAT\n" );
            } else if ( ( PacketNumber % 42 ) == 1 ) {
              PackData.DefaultPMT();
              conn.SendNow( PackData.ToString(), 188 );
            } else {
              PackData.Clear();
              PackData.PID( 0x100 );
              PackData.ContinuityCounter( VideoCounter );
              VideoCounter ++;
              if( WritePesHeader ) {
                PackData.UnitStart( 1 );
                if( IsKeyFrame ) {
                  PackData.RandomAccess( 1 );
                  PackData.PCR( TimeStamp * 27000 );
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
              conn.SendNow( PackData.ToString(), 188 );
            }
            PacketNumber ++;
          }
        } else if( Strm.lastType() == DTSC::AUDIO ) {
          WritePesHeader = true;
          ToPack = TS::GetAudioHeader( Strm.lastData().size() );
          ToPack += Strm.lastData();
          TimeStamp = Strm.getPacket(0)["time"].asInt() * 81000;
          while( ToPack.size() ) {
            if ( ( PacketNumber % 42 ) == 0 ) {
              PackData.DefaultPAT();
              conn.SendNow( PackData.ToString(), 188 );
            } else if ( ( PacketNumber % 42 ) == 1 ) {
              PackData.DefaultPMT();
              conn.SendNow( PackData.ToString(), 188 );
            } else {
              PackData.Clear();
              PackData.PID( 0x101 );
              PackData.ContinuityCounter( AudioCounter );
              AudioCounter ++;
              if( WritePesHeader ) {
                PackData.UnitStart( 1 );
                //PackData.RandomAccess( 1 );
                PackData.AddStuffing( 184 - (14 + ToPack.size()) );
                PackData.RandomAccess( 1 );
                PackData.PESAudioLeadIn( ToPack.size(), TimeStamp );
                WritePesHeader = false;
              } else {
                PackData.AdaptationField( 0x01 );
                PackData.AddStuffing( 184 - (ToPack.size()) );
              }
              PackData.FillFree( ToPack );
              conn.SendNow( PackData.ToString(), 188 );
            }
            PacketNumber ++;
          }
        }
      }
    }
  }
  fprintf( stderr, "Exiting\n" );
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
