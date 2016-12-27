/// \file ts_packet.h
/// Holds all headers for the TS Namespace.

#pragma once
#include <string>
#include <map>
#include <cmath>
#include <stdint.h>//for uint64_t
#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include "dtsc.h"
#include "checksum.h"

/// Holds all TS processing related code.
namespace TS {

  ///Class for reading and writing TS Streams. The class is capable of analyzing a packet of 188 bytes
  ///and calculating key values
  class Packet {
    public:
      //Constructors and fillers
      Packet();
      Packet(const Packet & rhs);
      ~Packet();      
      bool FromPointer(const char * data);
      bool FromFile(FILE * data);

      //Base properties
      void setPID(int NewPID);
      unsigned int getPID() const;
      void setContinuityCounter(int NewContinuity);
      int getContinuityCounter() const;
      void setPCR(int64_t NewVal);
      int64_t getPCR() const;
      int64_t getOPCR() const;
      void setAdaptationField(int NewVal);
      int getAdaptationField() const;
      int getAdaptationFieldLen() const;
      unsigned int getTransportScramblingControl() const;

      //Flags      
      void setUnitStart(bool newVal);
      bool getUnitStart() const;      
      void setRandomAccess(bool newVal);
      bool getRandomAccess() const;
      void setESPriority(bool newVal);
      bool getESPriority() const;
      
      void setDiscontinuity(bool newVal);
      bool hasDiscontinuity() const;
      bool hasPCR() const;
      bool hasOPCR() const;
      bool hasSplicingPoint() const;
      bool hasTransportError() const;
      bool hasPriority() const;

      //Helper functions
      operator bool() const;
      bool isPMT() const;
      void clear();
      void setDefaultPAT();      
      unsigned int getDataSize() const;
      
      unsigned int getBytesFree() const;      
      int fillFree(const char * PackageData, int maxLen);      
      void addStuffing();
      void updPos(unsigned int newPos);
      
      //PES helpers      
      static std::string & getPESVideoLeadIn(unsigned int len, unsigned long long PTS, unsigned long long offset, bool isAligned);      
      static std::string & getPESAudioLeadIn(unsigned int len, unsigned long long PTS);
      
      //Printers and writers
      std::string toPrettyString(size_t indent = 0, int detailLevel = 3) const;
      const char * getPayload() const;
      int getPayloadLength() const;      
      const char * checkAndGetBuffer() const;
      
    protected:
      char strBuf[189];
      unsigned int pos;
  };

  class ProgramAssociationTable : public Packet {
    public:
      ProgramAssociationTable & operator = (const Packet & rhs);
      char getOffset() const;
      char getTableId() const;
      short getSectionLength() const;
      short getTransportStreamId() const;
      char getVersionNumber() const;
      bool getCurrentNextIndicator() const;
      char getSectionNumber() const;
      char getLastSectionNumber() const;
      short getProgramCount() const;
      short getProgramNumber(short index) const;
      short getProgramPID(short index) const;
      int getCRC() const;
      void parsePIDs();
      std::string toPrettyString(size_t indent) const;
  };

  class ProgramDescriptors {
    public:
      ProgramDescriptors(const char * data, const uint32_t len);
      std::string getLanguage() const;
      std::string toPrettyString(size_t indent) const;
    private:
      const char * p_data;
      const uint32_t p_len;
  };

  class ProgramMappingEntry {
    public:
      ProgramMappingEntry(char * begin, char * end);
      
      operator bool() const;

      int getStreamType() const;
      void setStreamType(int newType);
      std::string getCodec() const;
      std::string getStreamTypeString() const;
      int getElementaryPid() const;
      void setElementaryPid(int newElementaryPid);
      int getESInfoLength() const;
      const char * getESInfo() const;
      void setESInfo(const std::string & newInfo);
      void advance();
    private:
      char* data;
      char* boundary;
  };

  class ProgramMappingTable : public Packet {
    public:
      ProgramMappingTable();
      ProgramMappingTable & operator = (const Packet & rhs);
      char getOffset() const;
      void setOffset(char newVal);
      char getTableId() const;
      void setTableId(char newVal);
      short getSectionLength() const;
      void setSectionLength(short newVal);
      short getProgramNumber() const;
      void setProgramNumber(short newVal);
      char getVersionNumber() const;
      void setVersionNumber(char newVal);
      bool getCurrentNextIndicator() const;
      void setCurrentNextIndicator(bool newVal);
      char getSectionNumber() const;
      void setSectionNumber(char newVal);
      char getLastSectionNumber() const;
      void setLastSectionNumber(char newVal);
      short getPCRPID() const;
      void setPCRPID(short newVal);
      short getProgramInfoLength() const;
      void setProgramInfoLength(short newVal);
      ProgramMappingEntry getEntry(int index) const;
      int getCRC() const;
      void calcCRC();
      std::string toPrettyString(size_t indent) const;
  };

  class ServiceDescriptionEntry {
    public:
      ServiceDescriptionEntry(char * begin, char * end);
      operator bool() const;
      uint16_t getServiceID() const;
      void setServiceID(uint16_t newType);
      bool getEITSchedule() const;
      void setEITSchedule(bool val);
      bool getEITPresentFollowing() const;
      void setEITPresentFollowing(bool val);
      uint8_t getRunningStatus() const;
      void setRunningStatus(uint8_t val);
      bool getFreeCAM() const;
      void setFreeCAM(bool val);
      int getESInfoLength() const;
      const char * getESInfo() const;
      void setESInfo(const std::string & newInfo);
      void advance();
    private:
      char* data;
      char* boundary;
  };

  class ServiceDescriptionTable : public Packet {
    public:
      ServiceDescriptionTable();
      ServiceDescriptionTable & operator = (const Packet & rhs);
      char getOffset() const;
      void setOffset(char newVal);
      
      char getTableId() const;
      void setTableId(char newVal);
      short getSectionLength() const;
      void setSectionLength(short newVal);

      uint16_t getTSStreamID() const;
      void setTSStreamID(uint16_t newVal);
      uint8_t getVersionNumber() const;
      void setVersionNumber(uint8_t newVal);
      bool getCurrentNextIndicator() const;
      void setCurrentNextIndicator(bool newVal);
      uint8_t getSectionNumber() const;
      void setSectionNumber(uint8_t newVal);
      uint8_t getLastSectionNumber() const;
      void setLastSectionNumber(uint8_t newVal);
      uint16_t getOrigID() const;
      void setOrigID(uint16_t newVal);
      ServiceDescriptionEntry getEntry(int index) const;
      int getCRC() const;
      void calcCRC();
      std::string toPrettyString(size_t indent) const;
  };

  /// Constructs an audio header to be used on each audio frame.
  /// The length of this header will ALWAYS be 7 bytes, and has to be
  /// prepended on each audio frame.
  /// \param FrameLen the length of the current audio frame.
  /// \param initData A string containing the initalization data for this track's codec.
  static inline std::string getAudioHeader(int FrameLen, std::string initData) {
    char StandardHeader[7] = {0xFF, 0xF1, 0x00, 0x00, 0x00, 0x1F, 0xFC};
    FrameLen += 7;
    StandardHeader[2] = ((((initData[0] >> 3) - 1) << 6) & 0xC0); //AAC Profile - 1 ( First two bits )
    StandardHeader[2] |= ((((initData[0] & 0x07) << 1) | ((initData[1] >> 7) & 0x01)) << 2); //AAC Frequency Index
    StandardHeader[2] |= ((initData[1] & 0x20) >> 5); //AAC Channel Config
    StandardHeader[3] = ((initData[1] & 0x18) << 3); //AAC Channel Config (cont.)
    StandardHeader[3] |= ((FrameLen & 0x00001800) >> 11);
    StandardHeader[4] = ((FrameLen & 0x000007F8) >> 3);
    StandardHeader[5] |= ((FrameLen & 0x00000007) << 5);
    return std::string(StandardHeader, 7);
  }


  /// A standard Program Association Table, as generated by FFMPEG.
  /// Seems to be independent of the stream.
  //0x47 = sync byte
  //0x4000 = transport error(1) = 0, payload unit start(1) = 1, priority(1) = 0, PID(13) = 0
  //0x10 = transportscrambling(2) = 0, adaptation(2) = 1, continuity(4) = 0
  //0x00 = pointer = 0
  //0x00 = table ID = 0 = PAT
  //0xB00D = section syntax(1) = 1, 0(1)=0, reserved(2) = 3, section_len(12) = 13
  //0x0001 = transport stream id = 1
  //0xC1 = reserved(2) = 3, version(5)=0, curr_next_indi(1) = 1
  //0x00 = section_number = 0
  //0x00 = last_section_no = 0
  //0x0001 = ProgNo = 1
  //0xF000 = reserved(3) = 7, network pid = 4096
  //0x2AB104B2 = CRC32
  static char PAT[188] = {0x47, 0x40, 0x00, 0x10, 0x00, 0x00, 0xB0, 0x0D, 0x00, 0x01, 0xC1, 0x00, 0x00, 0x00, 0x01, 0xF0, 0x00, 0x2A, 0xB1, 0x04,
                          0xB2, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                          0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                          0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                          0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                          0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                          0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                          0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
                         };

  const char * createPMT(std::set<unsigned long>& selectedTracks, DTSC::Meta& myMeta, int contCounter=0);
  const char * createSDT(const std::string & streamName, int contCounter=0);

} //TS namespace

