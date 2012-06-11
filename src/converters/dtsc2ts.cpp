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
  char charBuffer[1024*10];
  unsigned int charCount;
  std::string StrData;
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
  while( std::cin.good() ) {
    std::cin.read(charBuffer, 1024*10);
    charCount = std::cin.gcount();
    StrData.append(charBuffer, charCount);
    if ( DTSCStream.parsePacket( StrData ) ) {
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
      } else if( DTSCStream.lastType() == DTSC::AUDIO ) {
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
  }
  return 0;
}
