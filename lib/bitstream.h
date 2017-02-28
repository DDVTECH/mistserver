#pragma once
#include<string>
#include "defines.h"

namespace Utils {
  class bitstream {
    public:
      bitstream();
      bitstream & operator<< (std::string input) {
        append(input);
        return *this;
      };
      bitstream & operator<< (char input) {
        append(std::string(input, 1));
        return *this;
      };
      void append(const char * input, size_t bytes);
      void append(const std::string & input);
      long long unsigned int size();
      void skip(size_t count);
      long long unsigned int get(size_t count);
      long long unsigned int peek(size_t count);
      bool peekOffset(size_t peekOffset);
      void flush();
      void clear();
      long long int getExpGolomb();
      long long unsigned int getUExpGolomb();
      long long int peekExpGolomb();
      long long unsigned int peekUExpGolomb();

      static size_t bitSizeUExpGolomb(size_t value){
        size_t i = 1;
        size_t power = 2;
        while (power - 2 < value){
          i+= 2;
          power *= 2;
        }
        return i;
      }

    private:
      bool checkBufferSize(unsigned int size);
      long long unsigned int golombGetter();
      long long unsigned int golombPeeker();
      char * data;
      size_t offset;
      size_t dataSize;
      size_t bufferSize;
  };

  class bitWriter {
    public:
      bitWriter();
      ~bitWriter();
      size_t size();
      void append(uint64_t value, size_t bitLength);
      void appendExpGolomb(uint64_t value);
      void appendUExpGolomb(uint64_t value);
      static size_t UExpGolombEncodedSize(uint64_t value);
      std::string str() { return std::string(dataBuffer, (dataSize / 8) + (dataSize % 8 ? 1 : 0)); }
    protected:
      void reallocate(size_t newSize);
      void appendData(uint8_t data, size_t len);

      char * dataBuffer;
      //NOTE: ALL SIZES IN BITS!
      size_t bufferSize;

      size_t dataSize;
  };

  class bitstreamLSBF {
    public:
      bitstreamLSBF();
      bitstreamLSBF & operator<< (std::string input) {
        append(input);
        return *this;
      };
      void append(char * input, size_t bytes);
      void append(std::string & input);
      long long unsigned int size();
      void skip(size_t count);
      long long unsigned int get(size_t count);
      long long unsigned int peek(size_t count);
      void clear();
      std::string data;
    private:
      long long unsigned int readBuffer;
      unsigned int readBufferOffset;
      void fixData();
  };
}


