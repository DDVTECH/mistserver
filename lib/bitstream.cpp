#include "bitfields.h"
#include "bitstream.h"
#include "defines.h"
#include <stdlib.h>
#include <string.h>

namespace Utils{
  bitstream::bitstream(){
    data = NULL;
    offset = 0;
    dataSize = 0;
    bufferSize = 0;
  }

  bitstream::~bitstream(){
    if (!data){return;}
    free(data);
    bufferSize = 0;
    dataSize = 0;
    offset = 0;
  }

  bool bitstream::checkBufferSize(unsigned int size){
    if (size <= bufferSize){return true;}

    void *temp = realloc(data, size);
    if (!temp){return false;}

    data = (char *)temp;
    bufferSize = size;
    return true;
  }

  void bitstream::append(const char *input, size_t bytes){
    if (checkBufferSize(dataSize + bytes)){
      memcpy(data + dataSize, input, bytes);
      dataSize += bytes;
    }
  }

  void bitstream::append(const std::string &input){append((char *)input.c_str(), input.size());}

  bool bitstream::peekOffset(size_t peekOffset){
    peekOffset += offset;
    return ((data[peekOffset >> 3]) >> (7 - (peekOffset & 7))) & 1;
  }

  long long unsigned int bitstream::peek(size_t count){
    if (count > 64){
      DEBUG_MSG(DLVL_WARN, "Can not read %d bits into a long long unsigned int!", (int)count);
      // return 0;
    }
    if (count > size()){
      DEBUG_MSG(DLVL_ERROR, "Not enough bits left in stream. Left: %d requested: %d", (int)size(), (int)count);
      return 0;
    }
    long long unsigned int retval = 0;
    size_t curPlace = 0;
    size_t readSize;
    size_t readOff;
    char readBuff;
    while (curPlace < count){
      readBuff = data[(int)((offset + curPlace) / 8)];
      readSize = 8;
      readOff = (offset + curPlace) % 8; // the reading offset within the byte
      if (readOff != 0){
        // if we start our read not on the start of a byte
        // curplace and retval should both be 0
        // this should be the first read that aligns reading to bytes, if we read over the end of
        // read byte we cut the MSb off of the buffer by bit mask
        readSize -= readOff;                         // defining starting bit
        readBuff = readBuff & ((1 << readSize) - 1); // bitmasking
      }
      // up until here we assume we read to the end of the byte
      if (count - curPlace < readSize){// if we do not read to the end of the byte
        // we cut off the LSb off of the read buffer by bitshift
        readSize = count - curPlace;
        readBuff = readBuff >> (8 - readSize - readOff);
      }
      retval = (retval << readSize) + readBuff;
      curPlace += readSize;
    }
    return retval;
  }

  long long unsigned int bitstream::get(size_t count){
    if (count <= size()){
      long long unsigned int retVal;
      retVal = peek(count);
      skip(count);
      return retVal;
    }else{
      return 0;
    }
  }

  void bitstream::skip(size_t count){
    if (count <= size()){
      offset += count;
    }else{
      offset = dataSize * 8;
    }
  }

  long long unsigned int bitstream::size(){return (dataSize * 8) - offset;}

  void bitstream::clear(){
    dataSize = 0;
    offset = 0;
  }

  void bitstream::flush(){
    memmove(data, data + (offset / 8), dataSize - (offset / 8));
    dataSize -= offset / 8;
    offset %= 8;
  }

  long long unsigned int bitstream::golombPeeker(){
    for (size_t i = 0; i < 64 && i < size(); i++){
      if (peekOffset(i)){return peek((i * 2) + 1);}
    }
    return 0;
  }

  long long unsigned int bitstream::golombGetter(){
    for (size_t i = 0; i < 64 && i < size(); i++){
      if (peekOffset(i)){return get((i * 2) + 1);}
    }
    return 0;
  }

  long long int bitstream::getExpGolomb(){
    long long unsigned int temp = golombGetter();
    return (temp >> 1) * (1 - ((temp & 1) << 1)); // Is actually return (temp / 2) * (1 - (temp & 1) * 2);
  }

  long long unsigned int bitstream::getUExpGolomb(){return golombGetter() - 1;}

  long long int bitstream::peekExpGolomb(){
    long long unsigned int temp = golombPeeker();
    return (temp >> 1) * (1 - ((temp & 1) << 1)); // Is actually return (temp / 2) * (1 - (temp & 1) * 2);
  }

  long long unsigned int bitstream::peekUExpGolomb(){return golombPeeker() - 1;}

  bitWriter::bitWriter(){bitSize = 0;}

  size_t bitWriter::size() const{return bitSize;}

  void bitWriter::append(const std::string &val){
    for (size_t i = 0; i < val.size(); i++){append(val[i]);}
  }

  void bitWriter::append(uint64_t val, size_t bitLength){
    static char buf[9];

    uint32_t byteLength = ((bitSize + bitLength) / 8) + 1;
    while (byteLength > p.size()){p.append("", 1);}

    int bitShift = (64 - bitLength) - (bitSize % 8);

    if (bitShift >= 0){
      Bit::htobll(buf, val << bitShift);
    }else{
      Bit::htobll(buf, val >> (bitShift * -1));
      buf[8] = ((val << (8 + bitShift)) & 0xFF);
    }

    size_t adjustableBits = (bitSize % 8) + bitLength;
    size_t adjustableBytes = adjustableBits / 8 + (adjustableBits % 8 ? 1 : 0);

    for (int i = 0; i < adjustableBytes; i++){p[bitSize / 8 + i] |= buf[i];}

    bitSize += bitLength;
  }

  void bitWriter::clear(){
    p.assign("", 0);
    bitSize = 0;
  }

  size_t bitWriter::UExpGolombEncodedSize(uint64_t value){
    value++;
    size_t res = 1;
    size_t maxVal = 1;
    while (value > maxVal){
      maxVal = (maxVal << 1 | 0x01);
      res += 1;
    }
    return 2 * res - 1;
  }

  void bitWriter::appendExpGolomb(int64_t value){
    if (value < 0){
      value = value * -2;
    }else if (value > 0){
      value = (value * 2) - 1;
    }
    appendUExpGolomb(value);
  }

  void bitWriter::appendUExpGolomb(uint64_t value){
    append(value + 1, UExpGolombEncodedSize(value));
  }

  // Note: other bitstream here
  bitstreamLSBF::bitstreamLSBF(){
    readBufferOffset = 0;
    readBuffer = 0;
  }

  void bitstreamLSBF::append(char *input, size_t bytes){
    data.append(input, bytes);
    fixData();
  }

  void bitstreamLSBF::append(std::string &input){
    data += input;
    fixData();
  }

  long long unsigned int bitstreamLSBF::size(){return data.size() * 8 + readBufferOffset;}

  long long unsigned int bitstreamLSBF::get(size_t count){
    if (count <= 32 && count <= readBufferOffset){
      long long unsigned int retval = readBuffer & (((long long unsigned int)1 << count) - 1);
      readBuffer = readBuffer >> count;
      readBufferOffset -= count;
      fixData();
      return retval;
    }
    return 42;
  }

  void bitstreamLSBF::skip(size_t count){
    if (count <= 32 && count <= readBufferOffset){
      readBuffer = readBuffer >> count;
      readBufferOffset -= count;
      fixData();
    }
  }

  long long unsigned int bitstreamLSBF::peek(size_t count){
    if (count <= 32 && count <= readBufferOffset){return readBuffer & ((1 << count) - 1);}
    return 0;
  }

  void bitstreamLSBF::clear(){
    data = "";
    readBufferOffset = 0;
    readBuffer = 0;
  }

  void bitstreamLSBF::fixData(){
    unsigned int pos = 0;
    while (readBufferOffset <= 32 && data.size() != 0){
      readBuffer |= (((long long unsigned int)data[pos]) << readBufferOffset);
      pos++;
      readBufferOffset += 8;
    }
    data.erase(0, pos);
  }
}// namespace Utils
