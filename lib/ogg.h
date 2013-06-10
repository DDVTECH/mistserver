#include<string>
#include<vector>
#include<deque>
#include"dtsc.h"

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
      std::deque<unsigned int> getSegmentTableDeque();
      bool setSegmentTable(std::vector<unsigned int> layout);
      void setSegmentTable(char* newVal, unsigned int length);
      unsigned long int getPageSize();
      char* getFullPayload();
      char* getSegment(long unsigned int);
      bool typeBOS();
      bool typeEOS();
      bool typeContinue();
      bool typeNone();
      std::string toPrettyString();
    private:
      long unsigned int calcChecksum();
      char* data;
      unsigned int datasize;
      unsigned int dataSum;
      bool checkDataSize(unsigned int size);
  };
}
