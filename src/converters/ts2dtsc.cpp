#include <string>
#include "../../lib/ts_packet.h" //TS support
#include "../../lib/dtsc.h" //DTSC support
#include "../../lib/nal.h" //NAL Unit operations

//DTSC::DTMI MetaData
//DTSC::DTMI OutData
  //X  if( PID() == 0x00 ) --> PAT --> Extract first PMT PID()
  //X  if( PID() == PMTStream ) --> PMT --> Extract first video PMT() && Extract first audio PMT()
  //   if( PID() == AudioStream ) --> Audio --> Extract Timestamp IF keyframe --> DTSC
  //   if( PID() == VideoStream ) --> Video --> AnnexB_to_Regular --> Extract Timestamp IF keyframe --> Remove PPS? --> Remove SPS? --> DTSC


//Copied from FLV_TAG
void Meta_Put(DTSC::DTMI & meta, std::string cat, std::string elem, std::string val){
  if (meta.getContentP(cat) == 0){meta.addContent(DTSC::DTMI(cat));}
  meta.getContentP(cat)->addContent(DTSC::DTMI(elem, val));
}

void Meta_Put(DTSC::DTMI & meta, std::string cat, std::string elem, int val){
  if (meta.getContentP(cat) == 0){meta.addContent(DTSC::DTMI(cat));}
  meta.getContentP(cat)->addContent(DTSC::DTMI(elem, val));
}


int main( ) {
  char charBuffer[1024*10];
  unsigned int charCount;
  std::string StrData;
  TS::Packet TSData;
  int PMT_PID = -1;
  int VideoPID = -1;
  int AudioPID = -1;
  int VideoTime_Offset = -1;
  DTSC::DTMI Meta;
  DTSC::DTMI VideoOut;
  DTSC::DTMI AudioOut;
  //Default MetaData, not NEARLY all options used, because encoded in video rather than parameters
  //Combined with Stubdata, for alignment with original DTSC of testcase
  Meta_Put(Meta, "video", "codec", "H264");
  Meta_Put(Meta, "video", "width", 1280);
  Meta_Put(Meta, "video", "height", 720);
  Meta_Put(Meta, "video", "fpks", 2997000);
  Meta_Put(Meta, "video", "bps", 832794);
  
  Meta_Put(Meta, "audio", "codec", "AAC");
  Meta_Put(Meta, "audio", "bps", 24021);
  Meta_Put(Meta, "audio", "rate", 48000);
  Meta_Put(Meta, "audio", "size", 16);
  Meta_Put(Meta, "audio", "channels", 2);
  
  
  Meta.Pack(true);
  Meta.packed.replace(0, 4, DTSC::Magic_Header);
  std::cout << Meta.packed;
  std::string PrevType = "";
  while( std::cin.good() ) {
    std::cin.read(charBuffer, 1024*10);
    charCount = std::cin.gcount();
    StrData.append(charBuffer, charCount);
    while( TSData.FromString( StrData ) ) {
//      fprintf( stderr, "PID: %d\n", TSData.PID() );
      if( TSData.PID() == 0 ) {
        int TmpPMTPid = TSData.ProgramMapPID( );
        if( TmpPMTPid != -1 ) { PMT_PID = TmpPMTPid; }
//        fprintf( stderr, "\tPMT PID: %d\n", PMT_PID );
      }
      if( TSData.PID() == PMT_PID ) {
        TSData.UpdateStreamPID( VideoPID, AudioPID );
//        fprintf( stderr, "\tVideoStream: %d\n\tAudioStream: %d\n", VideoPID, AudioPID );
      }
      if( TSData.PID() == VideoPID ) {
        if( PrevType == "Audio" ) {
          fprintf( stderr, "\tVideopacket, sending audiobuffer\n" );
          std::string AudioData = AudioOut.getContent("data").StrValue();
          AudioData.erase(0,7);//remove the header
          AudioOut.addContent( DTSC::DTMI( "data", AudioData ) );
          std::cout << AudioOut.Pack(true);
          AudioOut = DTSC::DTMI();
          AudioOut.addContent( DTSC::DTMI( "datatype", "audio" ) );
        }
        if( TSData.UnitStart( ) && PrevType == "Video" ) {
          fprintf( stderr, "\tNew VideoPacket, Writing old\n" );
          std::string AnnexBData = VideoOut.getContent("data").StrValue();
          std::string NewData;
          NAL_Unit Transformer;
          int i = 0;
          while( Transformer.ReadData( AnnexBData ) ) {
            if( Transformer.Type() <= 6 || Transformer.Type() >= 10 ) { //Extract SPS/PPS/Separator Data
              NewData += Transformer.SizePrepended( );
            }
          }
          VideoOut.addContent( DTSC::DTMI( "data", NewData ) );
          if( VideoTime_Offset == -1 ) { 
            VideoTime_Offset = VideoOut.getContent("time").NumValue();
          }
          int VideoTime = VideoOut.getContent("time").NumValue();
          VideoOut.addContent( DTSC::DTMI( "time", VideoTime - VideoTime_Offset ) );
          std::cout << VideoOut.Pack(true);
          VideoOut = DTSC::DTMI();
        }
        TSData.toDTSC( "video", VideoOut );
        PrevType = "Video";
      }
      if( TSData.PID() == AudioPID ) {
        if( PrevType == "Video" ) {
          fprintf( stderr, "\tAudiopacket, sending videobuffer\n" );
          std::string AnnexBData = VideoOut.getContent("data").StrValue();
          std::string NewData;
          NAL_Unit Transformer;
          int i = 0;
          while( Transformer.ReadData( AnnexBData ) ) {
            if( Transformer.Type() < 6 || Transformer.Type() > 9 ) { //Extract SPS/PPS/SEI/Separator Data
              NewData += Transformer.SizePrepended( );
            }
          }
          VideoOut.addContent( DTSC::DTMI( "data", NewData ) );
          if( VideoTime_Offset == -1 ) { 
            VideoTime_Offset = VideoOut.getContent("time").NumValue();
          }
          int VideoTime = VideoOut.getContent("time").NumValue();
          VideoOut.addContent( DTSC::DTMI( "time", VideoTime - VideoTime_Offset ) );
          std::cout << VideoOut.Pack(true);
          VideoOut = DTSC::DTMI();
        }
        if( TSData.UnitStart( ) && PrevType == "Audio" ) {
          
          fprintf( stderr, "\tNew AudioPacket, Writing old\n" );
          std::string AudioData = AudioOut.getContent("data").StrValue();
          AudioData.erase(0,7);//remove the header
          AudioOut.addContent( DTSC::DTMI( "data", AudioData ) );
          std::cout << AudioOut.Pack(true);
          AudioOut = DTSC::DTMI();
          AudioOut.addContent( DTSC::DTMI( "datatype", "audio" ) );    
        }
        TSData.toDTSC( "audio", AudioOut );
        PrevType = "Audio";
      }
    }
  }
  return 0;
}
