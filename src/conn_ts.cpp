/// \file conn_ts.cpp
/// Contains the main code for the TS Connector
/// \todo Check data to be sent for video
/// \todo Handle audio packets

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
#include "../lib/socket.h"
#include "../lib/dtsc.h"
#include "../lib/ts_packet.h"

/// The main function of the connector
/// \param conn A connection with the client
int TS_Handler( Socket::Connection conn ) {
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
  
  while(conn.connected()) {// && !FLV::Parse_Error) {
    if( !inited ) {
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
        ss.spool();
        if ( DTSCStream.parsePacket( conn.Received() ) ) {
          if( DTSCStream.lastType() == DTSC::VIDEO ) {
            DTMIData = DTSCStream.lastData();
            if( DTSCStream.getPacket(0).getContent("keyframe").NumValue() ) {
              IsKeyFrame = true;
              FirstIDRInKeyFrame = true;
            } else {
              IsKeyFrame = false;
              FirstKeyFrame = false;
            }
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
                PackData.ToString();
              } else if ( ( PacketNumber % 42 ) == 1 ) {
                PackData.DefaultPMT();
                PackData.ToString();
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
                PackData.ToString();
              }
              PacketNumber ++;
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
                PackData.ToString();
              } else if ( ( PacketNumber % 42 ) == 1 ) {
                PackData.DefaultPMT();
                PackData.ToString();
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
                PackData.ToString();
              }
              PacketNumber ++;
            }
          }          
        }
        break;
    }
  }
  return 0;
}

#define DEFAULT_PORT 8888
#define MAINHANDLER TS_Handler
#define CONFIGSECT TS
#include "server_setup.h"
