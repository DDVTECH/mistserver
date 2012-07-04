#include "nal.h"

NAL_Unit::NAL_Unit( ) {
  
}

NAL_Unit::NAL_Unit( std::string & InputData ) {
  ReadData( InputData );
}

bool NAL_Unit::ReadData( std::string & InputData ) {
  bool AnnexB = false;
  if( InputData[0] == 0x00 && InputData[1] == 0x00 ) {
    if( InputData[2] == 0x01 ) {
      AnnexB = true;
    }
    if( InputData[2] == 0x00 && InputData[3] == 0x01 ) {
      InputData.erase(0,1);
      AnnexB = true;
    }
  }
  if( AnnexB ) {
    MyData = "";
    InputData.erase(0,3);//Intro Bytes
    bool FinalByteRead = false;
    while( !FinalByteRead ) {
      MyData += InputData[0];
      InputData.erase(0,1);
      if( InputData[0] == 0x00 && InputData[1] == 0x00 ) {
        if( InputData[2] == 0x01 ) {
          FinalByteRead = true;
        }
        if( InputData[2] == 0x00 && InputData[3] == 0x01 ) {
          InputData.erase(0,1);
          FinalByteRead= true;
        }
      }
    }
  } else {
    if( InputData.size() < 4 ) { return false; }
    int UnitLen = (InputData[0] << 24) + (InputData[1] << 16) + (InputData[2] << 8) + InputData[3];
    if( InputData.size() < 4+UnitLen ) { return false; }
    InputData.erase(0,4);//Remove Length
    MyData = InputData.substr(0,UnitLen);
    InputData.erase(0,UnitLen);//Remove this unit from the string
  }
  return true;
}

std::string NAL_Unit::AnnexB( bool LongIntro ) {
  std::string Result;
  if( MyData.size() ) {
    if( LongIntro ) { Result += (char)0x00; }
    Result += (char)0x00;
    Result += (char)0x00;
    Result += (char)0x01;//Annex B Lead-In
    Result += MyData;
  }
  return Result;
}

std::string NAL_Unit::SizePrepended( ) {
  std::string Result;
  if( MyData.size() ) {
    int DataSize = MyData.size();
    Result += (char)( ( DataSize & 0xFF000000 ) >> 24 );
    Result += (char)( ( DataSize & 0x00FF0000 ) >> 16 );
    Result += (char)( ( DataSize & 0x0000FF00 ) >> 8 );
    Result += (char)(   DataSize & 0x000000FF );//Size Lead-In
    Result += MyData;
  }
  return Result;
}

int NAL_Unit::Type( ) {
  return ( MyData[0] & 0x1F );
}
