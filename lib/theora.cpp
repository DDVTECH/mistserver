#include<theora.h>
#include<stdlib.h>
#include<string.h>
#include <arpa/inet.h>

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
  
  /// Gets the 32 bits integer at the given index.
  /// Attempts to resize the data pointer if the index is out of range.
  /// Returns zero if resizing failed.
  uint32_t header::getInt32(size_t index){
    /*if (index + 3 >= datasize){
      if ( !reserve(index, 0, 4)){
        return 0;
      }
      setInt32(0, index);
    }*/
    uint32_t result;
    memcpy((char*) &result, data + index, 4);
    return ntohl(result);
  }
  
  header::header(){
    data = NULL;
    datasize = 0;
  }
  
  bool header::read(char* newData, unsigned int length){
    if (length < 7){
      return false;
    }
    if(memcmp(newData+1, "theora", 6)!=0){
      return false;
    }
    switch(newData[0]){
      case 0x80:
        //if (length != 42) return false;
        break;
      case 0x81:
        break;
      case 0x82:
        break;
      default:
        return false;
        break;
    };
    if (checkDataSize(length)){
      memcpy(data, newData, length);
    }else{
      return false;
    }
    return true;
  }
  
  int header::getHeaderType(){
    switch(data[0]){
      case 0x80:
        return 0;
        break;
      case 0x81:
        return 1;
        break;
      case 0x82:
        return 2;
        break;
      default:
        return -1;
        break;
    };
  }
  
  long unsigned int header::getFRN(){
    if (getHeaderType() == 0){
      return getInt32(22);
    }
    return 0;
  }
  
  long unsigned int header::getFRD(){
    if (getHeaderType() == 0){
      return getInt32(26);
    }
    return 0;
  }

}
