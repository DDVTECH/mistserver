//This line will make ftello/fseeko work with 64 bits numbers
#define _FILE_OFFSET_BITS 64

#include "util.h"
#include <stdio.h>
#include <iostream>

namespace Util {
  bool stringScan(const std::string & src, const std::string & pattern, std::deque<std::string> & result){
    result.clear();
    std::deque<size_t> positions;
    size_t pos = pattern.find("%", 0);
    while (pos != std::string::npos){
      positions.push_back(pos);
      pos = pattern.find("%", pos + 1);
    }
    if (positions.size() == 0){
      return false;
    }
    size_t sourcePos = 0;
    size_t patternPos = 0;
    std::deque<size_t>::iterator posIter = positions.begin();
    while (sourcePos != std::string::npos){
    //Match first part of the string
      if (pattern.substr(patternPos, *posIter - patternPos) != src.substr(sourcePos, *posIter - patternPos)){
        break;
      }
      sourcePos += *posIter - patternPos;
      std::deque<size_t>::iterator nxtIter = posIter + 1;
      if (nxtIter != positions.end()){
        patternPos = *posIter+2;
        size_t tmpPos = src.find(pattern.substr(*posIter+2, *nxtIter - patternPos), sourcePos);
        result.push_back(src.substr(sourcePos, tmpPos - sourcePos));
        sourcePos = tmpPos;
      }else{
        result.push_back(src.substr(sourcePos));
        sourcePos = std::string::npos;
      }
      posIter++;
    }
    return result.size() == positions.size();
  }

  /// 64-bits version of ftell
  uint64_t ftell(FILE * stream){
    /// \TODO Windows implementation (e.g. _ftelli64 ?)
    return ftello(stream);
  }

  /// 64-bits version of fseek
  uint64_t fseek(FILE * stream, uint64_t offset, int whence){
    /// \TODO Windows implementation (e.g. _fseeki64 ?)
    return fseeko(stream, offset, whence);
  }


}

