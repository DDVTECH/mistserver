#pragma once
#include <cstdlib>
#include <string>
#include <vector>
#include <deque>
#include <sstream>
#include "dtsc.h"
#include "theora.h"
#include "vorbis.h"
#include "socket.h"

namespace OGG {

  class oggSegment {
    public:
      oggSegment();
      std::string dataString;
      int isKeyframe;
      long long unsigned int lastKeyFrameSeen;
      long long unsigned int framesSinceKeyFrame;
      unsigned int frameNumber;
      long long unsigned int timeStamp;
  };

  enum oggCodec {THEORA, VORBIS, OPUS};

  enum HeaderType {
    Plain = 0,
    Continued = 1,
    BeginOfStream = 2,
    EndOfStream = 4
  };

  std::deque<unsigned int> decodeXiphSize(char * data, size_t len);




  class Page {
    public:
      Page();
      Page(const Page & rhs);
      void operator = (const Page & rhs);
      bool read(std::string & newData);
      bool read(FILE * inFile);
      bool getSegment(unsigned int index, std::string & ret);
      const char * getSegment(unsigned int index);
      unsigned long getSegmentLen(unsigned int index);
      void setMagicNumber();
      char getVersion();
      void setVersion(char newVal = 0);
      char getHeaderType();
      void setHeaderType(char newVal);
      long long unsigned int getGranulePosition();
      void setGranulePosition(long long unsigned int newVal);
      long unsigned int getBitstreamSerialNumber();
      void setBitstreamSerialNumber(long unsigned int newVal);
      long unsigned int getCRCChecksum();
      void setCRCChecksum(long unsigned int newVal);
      long unsigned int getPageSequenceNumber();
      void setPageSequenceNumber(long unsigned int newVal);
      char getPageSegments();//get the amount of page segments
      inline void setPageSegments(char newVal);//set the amount of page segments
      int getPayloadSize();
      const std::deque<std::string> & getAllSegments();

      bool possiblyContinued();

      std::string toPrettyString(size_t indent = 0);

      long unsigned int calcChecksum();
      bool verifyChecksum();
      unsigned int calcPayloadSize();
      //void clear();
      void clear(char HeaderType, long long unsigned int GranPos, long unsigned int BSN, long unsigned int PSN);
      void prepareNext(bool continueMe = false);//prepare the page to become the next page
      bool setPayload(char * newData, unsigned int length); //probably obsolete
      unsigned int addSegment(const std::string & content); //add a segment to the page, returns added bytes
      unsigned int addSegment(const char * content, unsigned int length); //add a segment to the page, returns added bytes
      void sendTo(Socket::Connection & destination, int calcGranule = -2); //combines all data and sends it to socket
      unsigned int setNextSegmentTableEntry(unsigned int entrySize);//returns the size that could not be added to the table
      unsigned int overFlow();//returns the amount of bytes that don't fit in this page from the segments;

      long long unsigned int calculateGranule(oggSegment & currentSegment);
      bool shouldSend();
      void vorbisStuff();//process most recent segment
      long long unsigned int totalFrames;
      int granules;
      OGG::oggCodec codec;
      std::deque<oggSegment> oggSegments; //used for ogg output
      unsigned int pageSequenceNumber;

      unsigned int framesSeen;
      unsigned int lastKeyFrame;
      unsigned int firstSample;//for vorbis, to handle "when to send the page"
      unsigned int sampleRate;//for vorbis, to handle the sampleRate
      int prevBlockFlag;
      char blockSize[2];
      std::deque<vorbis::mode> vorbisModes;//modes for vorbis
      unsigned int split;             //KFGShift for theora
    private:
      char data[282];//Fulldata
      std::deque<std::string> segments;


  };

  class oggTrack {
    public:
      oggTrack() : KFGShift(0), lastTime(0), parsedHeaders(false), lastPageOffset(0), nxtSegment(0){ }
      oggCodec codec;
      std::string name;
      std::string contBuffer;//buffer for continuing pages
      long long unsigned int dtscID;
      char KFGShift;
      double lastTime;
      long long unsigned int lastGran;
      bool parsedHeaders;
      long long unsigned int lastPageOffset;      
      unsigned int nxtSegment;
      double msPerFrame;
      Page myPage;
      //Codec specific elements
      //theora
      // theora::header idHeader;//needed to determine keyframe //bullshit?
      //vorbis
      std::deque<vorbis::mode> vModes;
      char channels;
      long long unsigned int blockSize[2];
      //unsigned long getBlockSize(unsigned int vModeIndex);
  };

  class headerPages {
    public:
      std::map <long long unsigned int, unsigned int> DTSCID2OGGSerial;
      std::map <long long unsigned int, unsigned int> DTSCID2seqNum;
      std::string parsedPages;
  };
}


