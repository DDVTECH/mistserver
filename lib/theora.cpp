#include "theora.h"
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <sstream>

namespace theora{
  bool header::checkDataSize(unsigned int size){
    if (size > datasize){
      void* tmp = realloc(data,size);
      if (tmp){
        data = (char*)tmp;
        datasize = size;
        return true;
      }else{
        return false;
      }
    }else{
      return true;
    }
  }
  
  uint32_t header::getInt32(size_t index){
    if (datasize >= (index + 3)){
      return (data[index] << 24) + (data[index + 1] << 16) + (data[index + 2] << 8) + data[index + 3];
    }
    return 0;
  }

  uint32_t header::getInt24(size_t index){
    if (datasize >= (index + 2)){
      return 0 + (data[index] << 16) + (data[index + 1] << 8) + data[index + 2];
    }
    return 0;
  }

  uint16_t header::getInt16(size_t index){
    if (datasize >= (index + 1)){
      return 0 + (data[index] << 8) + data[index + 1];
    }
    return 0;
  }

  uint32_t header::commentLen(size_t index){
    if (datasize >= index + 3){
      return data[index] + (data[index + 1] << 8) + (data[index + 2] << 16) + (data[index + 3] << 24);
    }
    return 0;
  }
  
  header::header(){
    data = NULL;
    datasize = 0;
  }

  header::header(char * newData, unsigned int length){
    data = NULL;
    datasize = 0;
    read(newData, length);
  }

  bool header::validateIdentificationHeader(){
    if (datasize != 42){return false;}
    if (getHeaderType() != 0){return false;}
    if (getVMAJ() != 3){return false;}
    if (getVMIN() != 2){return false;}
    if (getFMBW() == 0){return false;}
    if (getFMBH() == 0){return false;}
    if ((short)getPICW() > getFMBW() * 16){return false;}
    if ((short)getPICH() > getFMBH() * 16){return false;}
    if ((short)getPICX() > (getFMBW() * 16) - (short)getPICW()){return false;}
    if ((short)getPICY() > (getFMBH() * 16) - (short)getPICH()){return false;}
    if (getFRN() == 0){return false;}
    if (getFRD() == 0){return false;}
    return true;
  }
  
  bool header::read(char* newData, unsigned int length){
    if (length < 7){
      return false;
    }
    if (! (newData[0] & 0x80)){
      return false;
    }
    if(memcmp(newData+1, "theora", 6)!=0){
      return false;
    }
    if (checkDataSize(length)){
      memcpy(data, newData, length);
    }else{
      return false;
    }
    switch(getHeaderType()){
      case 0:
        return validateIdentificationHeader();
        break;
      case 1:
        ///\todo Read Comment header
        break;
      case 2:
        ///\todo Read Setup Header
        break;
    }
    return true;
  }
  
  int header::getHeaderType(){
    return (data[0] & 0x7F);
  }

  char header::getVMAJ(){
    if (getHeaderType() == 0){return data[7];}
    return 0;
  }

  char header::getVMIN(){
    if (getHeaderType() == 0){return data[8];}
    return 0;
  }

  char header::getVREV(){
    if (getHeaderType() == 0){return data[9];}
    return 0;
  }

  short header::getFMBW(){
    if (getHeaderType() == 0){return getInt16(10);}
    return 0;
  }

  short header::getFMBH(){
    if (getHeaderType() == 0){return getInt16(12);}
    return 0;
  }

  char header::getPICX(){
    if (getHeaderType() == 0){return data[20];}
    return 0;
  }

  char header::getPICY(){
    if (getHeaderType() == 0){return data[21];}
    return 0;
  }
  
  char header::getKFGShift(){
    if (getHeaderType() == 0){return (getInt16(40) >> 5) & 0x1F;}
    return 0;
  }
  
  long unsigned int header::getFRN(){
    if (getHeaderType() == 0){return getInt32(22);}
    return 0;
  }
 
   long unsigned int header::getPICH(){
    if (getHeaderType() == 0){return getInt24(17);}
    return 0;
  }
 
  long unsigned int header::getPICW(){
    if (getHeaderType() == 0){return getInt24(14);}
    return 0;
  } 
  
  long unsigned int header::getFRD(){
    if (getHeaderType() == 0){return getInt32(26);}
    return 0;
  }

  long unsigned int header::getPARN(){
    if (getHeaderType() == 0){return getInt24(30);}
    return 0;
  }

  long unsigned int header::getPARD(){
    if (getHeaderType() == 0){return getInt24(33);}
    return 0;
  }

  char header::getCS(){
    if (getHeaderType() == 0){return data[36];}
    return 0;
  }

  long unsigned int header::getNOMBR(){
    if (getHeaderType() == 0){return getInt24(37);}
    return 0;
  }

  char header::getQUAL(){
    if (getHeaderType() == 0){return (data[40] >> 3) & 0x1F;}
    return 0;
  }

  char header::getPF(){
    if (getHeaderType() == 0){return (data[41] >> 3) & 0x03;}
    return 0;
  }

  std::string header::getVendor(){
    if (getHeaderType() != 1){return "";}
    return std::string(data + 11, commentLen(7));
  }

  long unsigned int header::getNComments(){
    if (getHeaderType() != 1){return 0;}
    int offset = 11 + commentLen(7);
    return commentLen(offset);
  }

  char header::getLFLIMS(size_t index){
    if (getHeaderType() != 2){return 0;}
    if (index >= 64){return 0;}
    char NBITS = (data[0] >> 5) & 0x07;
    return NBITS;
  }

  std::string header::getUserComment(size_t index){
    if (index >= getNComments()){return "";}
    int offset = 11 + commentLen(7) + 4;
    for (size_t i = 0; i < index; i++){
      offset += 4 + commentLen(offset);
    }
    return std::string(data + offset + 4,commentLen(offset));
  }

  std::string header::toPrettyString(size_t indent){
    std::stringstream result;
    result << std::string(indent,' ') << "Theora header" << std::endl;
    result << std::string(indent+2,' ') << "HeaderType: " << getHeaderType() << std::endl;
    switch (getHeaderType()){
      case 0:
        result << std::string(indent+2,' ') << "VMAJ: " << (int)getVMAJ() << std::endl;
        result << std::string(indent+2,' ') << "VMIN: " << (int)getVMIN() << std::endl;
        result << std::string(indent+2,' ') << "VREV: " << (int)getVREV() << std::endl;
        result << std::string(indent+2,' ') << "FMBW: " << getFMBW() << std::endl;
        result << std::string(indent+2,' ') << "FMBH: " << getFMBH() << std::endl;
        result << std::string(indent+2,' ') << "PICH: " << getPICH() << std::endl;
        result << std::string(indent+2,' ') << "PICW: " << getPICW() << std::endl;
        result << std::string(indent+2,' ') << "PICX: " << (int)getPICX() << std::endl;
        result << std::string(indent+2,' ') << "PICY: " << (int)getPICY() << std::endl;
        result << std::string(indent+2,' ') << "FRN: " << getFRN() << std::endl;
        result << std::string(indent+2,' ') << "FRD: " << getFRD() << std::endl;
        result << std::string(indent+2,' ') << "PARN: " << getPARN() << std::endl;
        result << std::string(indent+2,' ') << "PARD: " << getPARD() << std::endl;
        result << std::string(indent+2,' ') << "CS: " << (int)getCS() << std::endl;
        result << std::string(indent+2,' ') << "NOMBR: " << getNOMBR() << std::endl;
        result << std::string(indent+2,' ') << "QUAL: " << (int)getQUAL() << std::endl;
        result << std::string(indent+2,' ') << "KFGShift: " << (int)getKFGShift() << std::endl;
        break;
      case 1:
        result << std::string(indent+2,' ') << "Vendor: " << getVendor() << std::endl;
        result << std::string(indent+2,' ') << "User Comments (" << getNComments() << "):" << std::endl;
        for (long unsigned int i = 0; i < getNComments(); i++){
          result << std::string(indent+4,' ') << "[" << i << "] " << getUserComment(i) << std::endl;
        }
        break;
      case 2:
        result << std::string(indent+2,' ') << "NBITS: " << (int)getLFLIMS(0) << std::endl;
    }
    return result.str();
  }

  frame::frame(){
    data = NULL;
    datasize = 0;
  }

  bool frame::checkDataSize(unsigned int size){
    if (size > datasize){
      void* tmp = realloc(data,size);
      if (tmp){
        data = (char*)tmp;
        datasize = size;
        return true;
      }else{
        return false;
      }
    }else{
      return true;
    }
  }

  bool frame::read(char* newData, unsigned int length){
    if (length < 7){
      return false;
    }
    if ((newData[0] & 0x80)){
      return false;
    }
    if (checkDataSize(length)){
      memcpy(data, newData, length);
    }else{
      return false;
    }
    return true;
  }

  char frame::getFTYPE(){
    return (data[0] >> 6) & 0x01;
  }

  char frame::getNQIS(){
    return 0;
  }

  char frame::getQIS(size_t index){
    if (index >= 3){return 0;}
    return 0;
  }

  long long unsigned int header::parseGranuleUpper(long long unsigned int granPos){
    return granPos >> getKFGShift();
  }

  long long unsigned int header::parseGranuleLower(long long unsigned int granPos){
    return (granPos & ((1 << getKFGShift()) - 1));
  }

  std::string frame::toPrettyString(size_t indent){
    std::stringstream result;
    result << std::string(indent,' ') << "Theora Frame" << std::endl;
    result << std::string(indent+2,' ') << "FType: " << (int)getFTYPE() << std::endl;
    return result.str();
  }
}
