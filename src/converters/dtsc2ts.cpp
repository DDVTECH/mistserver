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
  char AudioHeader_01[7] = {0xFF,0xF1,0x4C,0x80,0x01,0xDF,0xFC};
  char AudioHeader_45[7] = {0xFF,0xF1,0x4C,0x80,0x45,0xDF,0xFC};
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
  int AudioContinuity = 0;
  bool FirstKeyFrame = true;
  bool FirstIDRInKeyFrame;
  std::string AudioPack;
  uint64_t AudioTimestamp = 0;
  uint64_t NextAudioTimestamp = 0;
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
        TimeStamp = (DTSCStream.getPacket(0).getContent("time").NumValue() * 27000);
        if( TimeStamp < 0 ) { TimeStamp = 0; }
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
            if( IsKeyFrame ) {
			  if( FirstKeyFrame ) {
				ToPack.append(ShortNALUHeader,3);
			  } else {
				if( FirstIDRInKeyFrame ) {
				  ToPack.append(NALUHeader,4);
		          FirstIDRInKeyFrame = false;
				} else {
				  ToPack.append(ShortNALUHeader,3);
				}
	          }
	        } else {
			  ToPack.append(ShortNALUHeader,3); 
			}
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
				fprintf( stderr, "IsKeyFrame\n" );
                PackData.RandomAccess( 1 );
                PackData.PCR( TimeStamp );
              } else {
                PackData.AdaptionField( 0x01 );
              }
              fprintf( stderr, "Needed Stuffing: %d\n", 184 - (20+ToPack.size()) );
              PackData.AddStuffing( 184 - (20+ToPack.size()) );
              PackData.PESVideoLeadIn( ToPack.size(), TimeStamp / 300 );
              WritePesHeader = false;
            } else {
              PackData.AdaptionField( 0x01 );
			  fprintf( stderr, "Needed Stuffing: %d\n", 184 - (ToPack.size()) );
	          PackData.AddStuffing( 184 - (ToPack.size()) );
			}
            PackData.FillFree( ToPack );
            PackData.ToString();
		  }
	      fprintf( stderr, "PackNum: %d\n\tAdaptionField: %d\n==========\n", TSPackNum + (TSPackNum/210)+1 + 1, PackData.AdaptionField( ) );	
          TSPackNum++;
        }
      } else if( DTSCStream.lastType() == DTSC::AUDIO ) {
        WritePesHeader = true;
        DTMIData = DTSCStream.lastData();
		AudioPack = GetAudioHeader( DTMIData.size() );
		AudioPack += DTMIData;
		fprintf( stderr, "DTMIData: %d\n", DTMIData.size() );
		AudioTimestamp = DTSCStream.getPacket(0).getContent("time").NumValue() * 900;
		if( AudioTimestamp < 0 ) { AudioTimestamp = 0; }
		PackData.Clear();
  		while( AudioPack.size() ) {
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
              fprintf( stderr, "WritePes Needed Stuffing: %d\n", 184 - (14+AudioPack.size()) );
              PackData.AddStuffing( 184 - (14+ AudioPack.size()) );
              PackData.PESAudioLeadIn( AudioPack.size(), AudioTimestamp );
              WritePesHeader = false;
            } else {
              PackData.AdaptionField( 0x01 );
	  	      fprintf( stderr, "Needed Stuffing: %d\n", 184 - (AudioPack.size()) );
	          PackData.AddStuffing( 184 - (AudioPack.size()) );
		    }
            PackData.FillFree( AudioPack );
            PackData.ToString();
		  }
	      fprintf( stderr, "PackNum: %d\n\tAdaptionField: %d\n==========\n", TSPackNum + (TSPackNum/210)+1 + 1, PackData.AdaptionField( ) );	
          TSPackNum++;
	    }
	  }
    }
  }
  return 0;
}
