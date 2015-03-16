#pragma once
#include<sys/types.h>
#include<stdint.h>
#include<string>

namespace theora {

  bool isHeader(const char * newData, unsigned int length);
  class header {
    public:      
      ~header();
      header(char * newData, unsigned int length); // if managed is true, this class manages the data pointer
      int getHeaderType();
      char getVMAJ();
      char getVMIN();
      char getVREV();
      short getFMBW();
      short getFMBH();
      long unsigned int getPICW();//movie width
      long unsigned int getPICH();//movie height
      char getPICX();
      char getPICY();
      long unsigned int getFRN();//frame rate numerator
      long unsigned int getFRD();//frame rate denominator
      long unsigned int getPARN();
      long unsigned int getPARD();
      char getCS();
      long unsigned int getNOMBR();
      char getQUAL();
      char getPF();
      char getKFGShift();
      std::string getVendor();
      long unsigned int getNComments();
      std::string getUserComment(size_t index);
      char getLFLIMS(size_t index);
      std::string toPrettyString(size_t indent = 0); //update this, it should check (header) type and output relevant stuff only.
      //long long unsigned int parseGranuleUpper(long long unsigned int granPos);
      //long long unsigned int parseGranuleLower(long long unsigned int granPos);
      ///\todo put this back in pravate
      unsigned int datasize;
      bool isHeader();
      char getFTYPE();//for frames
    protected:
      uint32_t getInt32(size_t index);
      uint32_t getInt24(size_t index);
      uint16_t getInt16(size_t index);
      uint32_t commentLen(size_t index);
    private:
      char * data;
      bool checkDataSize(unsigned int size);      
  };
  /*
    class frame{ // we don't need this. I hope.
      public:
        frame();
        ~frame();
        bool read(const char* newData, unsigned int length);
        char getFTYPE();
        char getNQIS();
        char getQIS(size_t index);
        std::string toPrettyString(size_t indent = 0);
      private:
        char * data;
        unsigned int datasize;
        bool checkDataSize(unsigned int size);
    };*/
}

