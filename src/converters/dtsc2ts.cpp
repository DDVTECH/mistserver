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

std::string GetAudioHeader( int FrameLen ) {
	char StandardHeader[7] = {0xFF,0xF1,0x4C,0x80,0x00,0x1F,0xFC};
	FrameLen += 7;
	StandardHeader[3] = ( StandardHeader[3] & 0xFC ) + ( ( FrameLen & 0x00001800 ) >> 11 );
	StandardHeader[4] = ( ( FrameLen & 0x000007F8 ) >> 3 );
	StandardHeader[5] = ( StandardHeader[5] & 0x3F ) + ( ( FrameLen & 0x00000007 ) << 5 );
	return std::string(StandardHeader,7);
}

int main( ) {
  char charBuffer[1024*10];
  unsigned int charCount;
  std::string StrData;
  TS::Packet PackData;
  std::string ToPack;
  DTSC::Stream DTSCStream;
  int TSPackNum = 0;
  std::string DTMIData;
  bool WritePesHeader;
  bool IsKeyFrame;
  uint64_t TimeStamp = 0;
  int ThisNaluSize;
  int VidContinuity = 0;
  int AudioContinuity = 0;
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
          ThisNaluSize = (DTMIData[0] << 24) + (DTMIData[1] << 16) + (DTMIData[2] << 8) + DTMIData[3];
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
          if ( ( TSPackNum % 210 ) == 0 ) {
            PackData.FFMpegHeader();
            PackData.ToString();
          }
          if ( ( TSPackNum % 42 ) == 0 ) {
            PackData.DefaultPAT();
            PackData.ToString();
          } else if ( ( TSPackNum % 42 ) == 1 ) {
            PackData.DefaultPMT();
			PackData.ToString();
          } else {
            PackData.Clear();
            PackData.PID( 0x100 );
            PackData.ContinuityCounter( VidContinuity );
			VidContinuity ++;
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
          TSPackNum++;
        }
      } else if( DTSCStream.lastType() == DTSC::AUDIO ) {
        WritePesHeader = true;
        DTMIData = DTSCStream.lastData();
		ToPack = GetAudioHeader( DTMIData.size() );
		ToPack += DTMIData;
		TimeStamp = DTSCStream.getPacket(0).getContent("time").NumValue() * 900;
		PackData.Clear();
  		while( ToPack.size() ) {
          if ( ( TSPackNum % 210 ) == 0 ) {
            PackData.FFMpegHeader();
            PackData.ToString();
          }
          if ( ( TSPackNum % 42 ) == 0 ) {
            PackData.DefaultPAT();
            PackData.ToString();
          } else if ( ( TSPackNum % 42 ) == 1 ) {
            PackData.DefaultPMT();
            PackData.ToString();
          } else {
            PackData.Clear();
            PackData.PID( 0x101 );
            PackData.ContinuityCounter( AudioContinuity );
            AudioContinuity ++;
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
          TSPackNum++;
	    }
	  }
    }
  }
  return 0;
}
