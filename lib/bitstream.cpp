#include "bitstream.h"
#include "defines.h"
#include <stdlib.h>
#include <string.h>

namespace Utils {
  bitstream::bitstream() {
    data = NULL;
    offset = 0;
    dataSize = 0;
    bufferSize = 0;
  }

  bool bitstream::checkBufferSize(unsigned int size) {
    if (size > bufferSize) {
      void * temp = realloc(data, size);
      if (temp) {
        data = (char *) temp;
        bufferSize = size;
        return true;
      } else {
        return false;
      }
    } else {
      return true;
    }
  }

  void bitstream::append(const char * input, size_t bytes) {
    if (checkBufferSize(dataSize + bytes)) {
      memcpy(data + dataSize, input, bytes);
      dataSize += bytes;
    }
  }

  void bitstream::append(const std::string & input) {
    append((char *)input.c_str(), input.size());
  }

  bool bitstream::peekOffset(size_t peekOffset) {
    peekOffset += offset;
    return ((data[peekOffset >> 3]) >> (7 - (peekOffset & 7))) & 1;
  }

  long long unsigned int bitstream::peek(size_t count) {
    if (count  > 64) {
      DEBUG_MSG(DLVL_WARN, "Can not read %d bits into a long long unsigned int!", (int)count);
      //return 0;
    }
    if (count > size()) {
      DEBUG_MSG(DLVL_ERROR, "Not enough bits left in stream. Left: %d requested: %d", (int)size(), (int)count);
      return 0;
    }
    long long unsigned int retval = 0;
    size_t curPlace = 0;
    size_t readSize;
    size_t readOff;
    char readBuff;
    while (curPlace < count) {
      readBuff = data[(int)((offset + curPlace) / 8)];
      readSize = 8;
      readOff = (offset + curPlace) % 8; //the reading offset within the byte
      if (readOff != 0) {
        //if we start our read not on the start of a byte
        //curplace and retval should both be 0
        //this should be the first read that aligns reading to bytes, if we read over the end of read byte
        //we cut the MSb off of the buffer by bit mask
        readSize -= readOff;//defining starting bit
        readBuff = readBuff & ((1 << readSize) - 1);//bitmasking
      }
      //up until here we assume we read to the end of the byte
      if (count - curPlace < readSize) { //if we do not read to the end of the byte
        //we cut off the LSb off of the read buffer by bitshift
        readSize = count - curPlace;
        readBuff = readBuff >> (8 - readSize - readOff);
      }
      retval = (retval << readSize) + readBuff;
      curPlace += readSize;
    }
    return retval;
  }

  long long unsigned int bitstream::get(size_t count) {
    if (count <= size()) {
      long long unsigned int retVal;
      retVal = peek(count);
      skip(count);
      return retVal;
    } else {
      return 0;
    }
  }

  void bitstream::skip(size_t count) {
    if (count <= size()) {
      offset += count;
    } else {
      offset = dataSize * 8;
    }

  }

  long long unsigned int bitstream::size() {
    return (dataSize * 8) - offset;
  }

  void bitstream::clear() {
    dataSize = 0;
    offset = 0;
  }

  void bitstream::flush() {
    memmove(data, data + (offset / 8), dataSize - (offset / 8));
    dataSize -= offset / 8;
    offset %= 8;
  }

  long long unsigned int bitstream::golombPeeker() {
    for (size_t i = 0; i < 64 && i < size(); i++) {
      if (peekOffset(i)) {
        return peek((i * 2) + 1);
      }
    }
    return 0;
  }

  long long unsigned int bitstream::golombGetter() {
    for (size_t i = 0; i < 64 && i < size(); i++) {
      if (peekOffset(i)) {
        return get((i * 2) + 1);
      }
    }
    return 0;
  }

  long long int bitstream::getExpGolomb() {
    long long unsigned int temp = golombGetter();
    return (temp >> 1) * (1 - ((temp & 1) << 1)); //Is actually return (temp / 2) * (1 - (temp & 1) * 2);
  }

  long long unsigned int bitstream::getUExpGolomb() {
    return golombGetter() - 1;
  }

  long long int bitstream::peekExpGolomb() {
    long long unsigned int temp = golombPeeker();
    return (temp >> 1) * (1 - ((temp & 1) << 1)); //Is actually return (temp / 2) * (1 - (temp & 1) * 2);
  }

  long long unsigned int bitstream::peekUExpGolomb() {
    return golombPeeker() - 1;
  }


  bitWriter::bitWriter(){
    dataBuffer = NULL;
    bufferSize = 0;
    reallocate(0);
    dataSize = 0;
  }

  bitWriter::~bitWriter(){
    if (dataBuffer != NULL){
      free(dataBuffer);
    }
  }

  void bitWriter::reallocate(size_t newSize){
    size_t sizeBefore = bufferSize / 8;
    char * tmp;
    if (dataBuffer != NULL){
      tmp = (char*)realloc(dataBuffer, (newSize / 8) + 1);
    }else{
      tmp = (char*)malloc((newSize / 8) + 1);
    }
    if (tmp){
      dataBuffer = tmp;
      bufferSize = ((newSize / 8) + 1) * 8;
      memset(dataBuffer + sizeBefore, 0x00, (bufferSize / 8) - sizeBefore);
    }else{
      FAIL_MSG("Could not reallocate!!");
    }
  }

  size_t bitWriter::size() {
    return dataSize;
  }

  void bitWriter::append(uint64_t value, size_t bitLength){
    if (dataSize + bitLength > bufferSize){
      reallocate(dataSize + bitLength);
    }

    int64_t fullShift = (bitLength / 8) * 8;
    uint64_t firstMask = ((0x01ull << (bitLength % 8)) - 1) << fullShift;

    appendData( ((value & firstMask) >> fullShift), bitLength - fullShift);
    while (fullShift > 0) {
      fullShift -= 8;
      uint64_t mask = (0xFFull) << fullShift;
      appendData((value & mask) >> fullShift, 8);
    }
  }

  void bitWriter::appendData(uint8_t data, size_t len){
    size_t byteOffset = dataSize / 8;
    size_t bitOffset = dataSize % 8;
    if (len <= 8 - bitOffset){
      dataBuffer[byteOffset] |= (data << (8 - bitOffset - len));
      dataSize += len;
    }else{
      size_t shift = (len - (8 - bitOffset));
      dataBuffer[byteOffset] |= (data >> shift);
      dataSize += (len - shift);
      appendData(data, shift);
    }
  }

  size_t bitWriter::UExpGolombEncodedSize(uint64_t value){
    value ++;
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


//Note: other bitstream here
  bitstreamLSBF::bitstreamLSBF() {
    readBufferOffset = 0;
    readBuffer = 0;
  }

  void bitstreamLSBF::append(char * input, size_t bytes) {
    data.append(input, bytes);
    fixData();
  }

  void bitstreamLSBF::append(std::string & input) {
    data += input;
    fixData();
  }

  long long unsigned int bitstreamLSBF::size() {
    return data.size() * 8 + readBufferOffset;
  }

  long long unsigned int bitstreamLSBF::get(size_t count) {
    if (count <= 32 && count <= readBufferOffset) {
      long long unsigned int retval = readBuffer & (((long long unsigned int)1 << count) - 1);
      readBuffer = readBuffer >> count;
      readBufferOffset -= count;
      fixData();
      return retval;
    }
    return 42;
  }

  void bitstreamLSBF::skip(size_t count) {
    if (count <= 32 && count <= readBufferOffset) {
      readBuffer = readBuffer >> count;
      readBufferOffset -= count;
      fixData();
    }
  }

  long long unsigned int bitstreamLSBF::peek(size_t count) {
    if (count <= 32 && count <= readBufferOffset) {
      return readBuffer & ((1 << count) - 1);
    }
    return 0;
  }

  void bitstreamLSBF::clear() {
    data = "";
    readBufferOffset = 0;
    readBuffer = 0;
  }

  void bitstreamLSBF::fixData() {
    unsigned int pos=0;
    while (readBufferOffset <= 32 && data.size() != 0) {
      readBuffer |= (((long long unsigned int)data[pos]) << readBufferOffset);
      pos++;
      readBufferOffset += 8;
    }
    data.erase(0, pos);
  }
}
