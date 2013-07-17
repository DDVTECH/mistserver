#pragma once
#include<string>
#include<vector>
#include<deque>
#include"dtsc.h"
#include "theora.h"
#include "vorbis.h"

namespace OGG{
  class Page{
    public:
      Page();
      bool read(std::string & newData);
      long unsigned int getMagicNumber();
      void setMagicNumber();
      char getVersion();
      void setVersion(char newVal = 0);
      char getHeaderType();
      void setHeaderType(char newVal);
      long long unsigned int getGranulePosition();
      void setGranulePosition(long long unsigned int newVal);
      long unsigned int getBitstreamSerialNumber();
      void setBitstreamSerialNumber(long unsigned int newVal);
      long unsigned int getPageSequenceNumber();
      void setPageSequenceNumber(long unsigned int newVal);
      long unsigned int getCRCChecksum();
      void setCRCChecksum(long unsigned int newVal);
      char getPageSegments();
      inline void setPageSegments(char newVal);
      char* getSegmentTable();
      std::deque<unsigned int> & getSegmentTableDeque();
      bool setSegmentTable(std::vector<unsigned int> layout);
      void setSegmentTable(char* newVal, unsigned int length);
      unsigned long int getPageSize();
      char* getFullPayload();
      int getPayloadSize();
      bool typeBOS();
      bool typeEOS();
      bool typeContinue();
      bool typeNone();
      std::string toPrettyString(size_t indent = 0);
      void setInternalCodec(std::string myCodec);
      long unsigned int calcChecksum();
      void clear();
      
      bool setPayload(char* newData, unsigned int length);
    private:
      std::deque<unsigned int> segmentTableDeque;
      char* data;//pointer to the beginning of the Page data
      unsigned int datasize;//size of the complete page
      unsigned int dataSum;//size of the total segments
      bool checkDataSize(unsigned int size);
      std::string codec;//codec in the page
  };
}
