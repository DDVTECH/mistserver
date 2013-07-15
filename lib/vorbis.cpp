#include"vorbis.h"
#include<stdlib.h>
#include<string.h>
#include <arpa/inet.h>

namespace vorbis{
  header::header(){
    data = NULL;
    datasize = 0;
  }
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

  bool header::read(char* newData, unsigned int length){
    if (checkDataSize(length)){
      memcpy(data, newData, length);
    }else{
      return false;
    }
    return true;
  }
}
