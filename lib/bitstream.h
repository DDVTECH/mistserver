#include<string>

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
      void append(char * input, size_t bytes);
      void append(std::string input);
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
    private:
      bool checkBufferSize(unsigned int size);
      long long unsigned int golombGetter();
      long long unsigned int golombPeeker();
      char * data;
      size_t offset;
      size_t dataSize;
      size_t bufferSize;
  };

  class bitstreamLSBF {
    public:
      bitstreamLSBF();
      bitstreamLSBF & operator<< (std::string input) {
        append(input);
        return *this;
      };
      void append(char * input, size_t bytes);
      void append(std::string input);
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


