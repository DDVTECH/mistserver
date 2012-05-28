#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstdlib>
#include <cstdio>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include "../../lib/ts_packet.h" //TS support
#include "../../lib/dtsc.h" //DTSC support

int main( ) {
  char ShortNALUHeader[3] = {0x00,0x00,0x01};
  char NALUHeader[4] = {0x00,0x00,0x00,0x01};
  char PPS_SPS[32] = {0x00,0x00,0x00,0x01,
                      0x27,0x4D,0x40,0x1F,
                      0xA9,0x18,0x0A,0x00,
                      0xB7,0x60,0x0D,0x40,
                      0x40,0x40,0x4C,0x2B,
                      0x5E,0xF7,0xC0,0x40,
                      0x00,0x00,0x00,0x01,
                      0x28,0xCE,0x09,0xC8};
  char charBuffer[1024*10];
  unsigned int charCount;
  std::string StrData;
  TS_Packet PackData;
  DTSC::Stream DTSCStream;
  int TSPackNum = 0;
  std::string DTMIData;
  std::string NaluData;
  bool WritePesHeader;
  bool IsKeyFrame;
  int TimeStamp = 0;
  int ThisNaluSize;
  int VidContinuity = 0;
  while( std::cin.good() ) {
    std::cin.read(charBuffer, 1024*10);
    charCount = std::cin.gcount();
    StrData.append(charBuffer, charCount);
    if ( DTSCStream.parsePacket( StrData ) ) {
      if( DTSCStream.lastType() == DTSC::VIDEO ) {
        DTMIData = DTSCStream.lastData();
        if( DTSCStream.getPacket(0).getContent("keyframe").NumValue() ) {
          IsKeyFrame = true;
          TimeStamp = DTSCStream.getTime();
        } else {
          IsKeyFrame = false;
          TimeStamp = 0;
        }
        std::string ToPack;
        int TSType;
        bool FirstIDRPic = true;
        bool FirstNonIDRPic = true;
        while( DTMIData.size() ) {
          ThisNaluSize = (DTMIData[0] << 24) + (DTMIData[1] << 16) + (DTMIData[2] << 8) + DTMIData[3];
          DTMIData.erase(0,4);//Erase the first four characters;
          TSType = (int)DTMIData[0];
          if( TSType == 0x25 ) {
            if( FirstIDRPic ) {
              ToPack.append(PPS_SPS,32);
              FirstIDRPic = false;
              FirstNonIDRPic = false;
            }
            ToPack.append(ShortNALUHeader,3);
          } else if ( TSType == 0x21 ) {
            if( FirstNonIDRPic ) {
              ToPack.append(NALUHeader,4);
              FirstNonIDRPic = false;
              FirstIDRPic = false;
            } else {
              ToPack.append(ShortNALUHeader,3);
            }
          } else {
            ToPack.append(NALUHeader,4);
          }
          ToPack.append(DTMIData,0,ThisNaluSize);
          DTMIData.erase(0,ThisNaluSize);
        }
        WritePesHeader = true;
        fprintf( stderr, "PESHeader: %d, PackNum: %2d, ToPackSize: %d\n", WritePesHeader, TSPackNum, ToPack.size() );
        while( ToPack.size() ) {
          if ( ( TSPackNum % 42 ) == 0 ) {
            PackData.DefaultPAT();
            std::cout << PackData.ToString();
          } else if ( ( TSPackNum % 42 ) == 1 ) {
            PackData.DefaultPMT();
            std::cout << PackData.ToString();
          } else {
            PackData.Clear();
            PackData.PID( 0x100 );
            PackData.ContinuityCounter( VidContinuity++ );
            if( WritePesHeader ) {
              PackData.UnitStart( 1 );
              if( IsKeyFrame ) {
                PackData.AdaptionField( 0x03 );
                PackData.RandomAccess( 1 );
                PackData.PCR( TimeStamp );
              }
              PackData.AddStuffing( 184 - (20+ToPack.size()) );
              PackData.PESLeadIn( ToPack.size() );
              WritePesHeader = false;
            } else {
              PackData.AdaptionField( 0x01 );
              PackData.AddStuffing( 184 - (ToPack.size()) );
            }
            PackData.FillFree( ToPack );
            std::cout << PackData.ToString();
          }
          TSPackNum++;
        }
      }
    }
  }
  return 0;
}
