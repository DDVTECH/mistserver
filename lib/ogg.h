#pragma once
#include <cstdlib>
#include <string>
#include <vector>
#include <deque>
#include "dtsc.h"
#include "theora.h"
#include "vorbis.h"
#include "json.h"
#include "checksum.h"

namespace OGG {

  enum HeaderType {
    Continued = 1,
    BeginOfStream = 2,
    EndOfStream = 4
  };

  class Page {
    public:
      Page();
      ~Page();
      bool read(std::string & newData);
      bool read(FILE * inFile);
      bool getSegment(unsigned int index, char * data, unsigned int & len);
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
      char * getSegmentTable();
      std::deque<unsigned int> & getSegmentTableDeque();
      bool setSegmentTable(std::vector<unsigned int> layout);
      void setSegmentTable(char * newVal, unsigned int length);
      char * getPage(); //returns complete page with header
      unsigned long int getPageSize();
      char * getFullPayload(); //returns all segments in the page
      int getPayloadSize();
      std::string toPrettyString(size_t indent = 0);
      void setInternalCodec(std::string myCodec);
      long unsigned int calcChecksum();
      bool clear();
      bool setPayload(char * newData, unsigned int length);
      void readDTSCVector(std::vector <JSON::Value> DTSCVec, unsigned int serial, unsigned int sequence);
    private:
      std::deque<unsigned int> segmentTableDeque;
      char * data; //pointer to the beginning of the Page data
      unsigned int datasize;//size of the allocated memory
      unsigned int dataSum;//size of the total segments
      bool checkDataSize(unsigned int size);
      std::string codec;//codec in the page
  };

  class headerPages {
    public:
      void readDTSCHeader(DTSC::Meta & meta);
      std::map <long long unsigned int, unsigned int> DTSCID2OGGSerial;
      std::map <long long unsigned int, unsigned int> DTSCID2seqNum;
      std::string parsedPages;
  };
}
