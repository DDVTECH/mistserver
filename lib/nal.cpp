#include "nal.h"

NAL_Unit::NAL_Unit(){

}

NAL_Unit::NAL_Unit(std::string & InputData){
  ReadData(InputData);
}

bool NAL_Unit::ReadData(std::string & InputData){
  std::string FullAnnexB;
  FullAnnexB += (char)0x00;
  FullAnnexB += (char)0x00;
  FullAnnexB += (char)0x00;
  FullAnnexB += (char)0x01;
  std::string ShortAnnexB;
  ShortAnnexB += (char)0x00;
  ShortAnnexB += (char)0x00;
  ShortAnnexB += (char)0x01;
  if (InputData.size() < 3){
    return false;
  }
  bool AnnexB = false;
  if (InputData.substr(0, 3) == ShortAnnexB){
    AnnexB = true;
  }
  if (InputData.substr(0, 4) == FullAnnexB){
    InputData.erase(0, 1);
    AnnexB = true;
  }
  if (AnnexB){
    MyData = "";
    InputData.erase(0, 3); //Intro Bytes
    int Location = std::min(InputData.find(ShortAnnexB), InputData.find(FullAnnexB));
    MyData = InputData.substr(0, Location);
    InputData.erase(0, Location);
  }else{
    if (InputData.size() < 4){
      return false;
    }
    unsigned int UnitLen = (InputData[0] << 24) + (InputData[1] << 16) + (InputData[2] << 8) + InputData[3];
    if (InputData.size() < 4 + UnitLen){
      return false;
    }
    InputData.erase(0, 4); //Remove Length
    MyData = InputData.substr(0, UnitLen);
    InputData.erase(0, UnitLen); //Remove this unit from the string
  }
  return true;
}

std::string NAL_Unit::AnnexB(bool LongIntro){
  std::string Result;
  if (MyData.size()){
    if (LongIntro){
      Result += (char)0x00;
    }
    Result += (char)0x00;
    Result += (char)0x00;
    Result += (char)0x01; //Annex B Lead-In
    Result += MyData;
  }
  return Result;
}

std::string NAL_Unit::SizePrepended(){
  std::string Result;
  if (MyData.size()){
    int DataSize = MyData.size();
    Result += (char)((DataSize & 0xFF000000) >> 24);
    Result += (char)((DataSize & 0x00FF0000) >> 16);
    Result += (char)((DataSize & 0x0000FF00) >> 8);
    Result += (char)(DataSize & 0x000000FF); //Size Lead-In
    Result += MyData;
  }
  return Result;
}

int NAL_Unit::Type(){
  return (MyData[0] & 0x1F);
}
