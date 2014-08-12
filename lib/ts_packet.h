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

/// Holds all TS processing related code.
namespace TS {


  ///stores all the data of a pmt table. It must be mapped to a PID, and this is done in the function TS::getPMTTable(TS::Packet& packet)
  ///\todo Add more necessary variables, or find a more efficient way to store metadata
  struct pmtinfo {
    unsigned short streamtype;//the streamtype, 0x1b is h264, 0x0f is aac. These are used in aac, there may be (undiscovered) others
    unsigned int trackid;//track id
    std::string curPayload;//payload without PES/TS headers
    long long int lastPEStime;//the pes time of the packet that was last seen
  };


  ///Class for reading and writing TS Streams. The class is capable of analyzing a packet of 188 bytes
  ///and calculating key values
  class Packet {
    public:
      Packet();
      ~Packet();
      bool FromString(std::string & Data);
      bool FromPointer(char * Data);
      bool FromFile(FILE * data);
      void PID(int NewPID);
      unsigned int PID();
      void ContinuityCounter(int NewContinuity);
      int ContinuityCounter();
      void Clear();
      void PCR(int64_t NewVal);
      int64_t PCR();
      int64_t OPCR();
      void AdaptationField(int NewVal);
      int AdaptationField();
      int AdaptationFieldLen();
      void DefaultPAT();
      void DefaultPMT();
      int UnitStart();
      void UnitStart(int NewVal);
      int RandomAccess();
      void RandomAccess(int NewVal);

      int DiscontinuityIndicator();
      int elementaryStreamPriorityIndicator();
      int PCRFlag();
      int OPCRFlag();
      int splicingPointFlag();
      //int transportPrivateDataFlag();
      //int adaptationFieldExtensionFlag();

      int BytesFree();
      unsigned int getSyncByte();
      unsigned int getTransportErrorIndicator();
      unsigned int getPayloadUnitStartIndicator();
      unsigned int getTransportPriority();
      unsigned int getTransportScramblingControl();

      std::string toPrettyString(size_t indent = 0);
      std::string getStrBuf();
      const char * getBuffer();
      const char * getPayload();
      int getPayloadLength();

      const char * ToString();
      void PESVideoLeadIn(unsigned int NewLen, long long unsigned int PTS = 1);
      void PESAudioLeadIn(unsigned int NewLen, uint64_t PTS = 0);
      static void PESAudioLeadIn(std::string & toSend, long long unsigned int PTS);
      static void PESVideoLeadIn(std::string & toSend, long long unsigned int PTS);
      static std::string & getPESAudioLeadIn(unsigned int NewLen, long long unsigned int PTS);
      static std::string & getPESVideoLeadIn(unsigned int NewLen, long long unsigned int PTS);

      void FillFree(std::string & PackageData);
      int FillFree(const char * PackageData, int maxLen);
      unsigned int AddStuffing(int NumBytes);
    protected:
      std::string strBuf;///<The actual data
      //char Buffer[188];///< The actual data
  };

  class ProgramAssociationTable : public Packet {
    public:
      char getOffset();
      char getTableId();
      short getSectionLength();
      short getTransportStreamId();
      char getVersionNumber();
      bool getCurrentNextIndicator();
      char getSectionNumber();
      char getLastSectionNumber();
      short getProgramCount();
      short getProgramNumber(short index);
      short getProgramPID(short index);
      int getCRC();
      std::string toPrettyString(size_t indent);
  };

  class ProgramMappingTable : public Packet {
    public:
      char getOffset();
      void setOffset(char newVal);
      char getTableId();
      void setTableId(char newVal);
      short getSectionLength();
      void setSectionLength(short newVal);
      short getProgramNumber();
      void setProgramNumber(short newVal);
      char getVersionNumber();
      void setVersionNumber(char newVal);
      bool getCurrentNextIndicator();
      void setCurrentNextIndicator(bool newVal);
      char getSectionNumber();
      void setSectionNumber(char newVal);
      char getLastSectionNumber();
      void setLastSectionNumber(char newVal);
      short getPCRPID();
      void setPCRPID(short newVal);
      short getProgramInfoLength();
      void setProgramInfoLength(short newVal);
      short getProgramCount();
      void setProgramCount(short newVal);
      char getStreamType(short index);
      void setStreamType(char newVal, short index);
      short getElementaryPID(short index);
      void setElementaryPID(short newVal, short index);
      short getESInfoLength(short index);
      void setESInfoLength(short newVal,short index);
      int getCRC();
      void setCRC(int newVal);
      std::string toPrettyString(size_t indent);
  };

  /// Constructs an audio header to be used on each audio frame.
  /// The length of this header will ALWAYS be 7 bytes, and has to be
  /// prepended on each audio frame.
  /// \param FrameLen the length of the current audio frame.
  /// \param initData A string containing the initalization data for this track's codec.
  static inline std::string GetAudioHeader(int FrameLen, std::string initData) {
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

  /// A standard Program Mapping Table, as generated by FFMPEG.
  /// Contains both Audio and Video mappings, works also on video- or audio-only streams.
  //0x47 = sync byte
  //0x5000 = transport error(1) = 0, payload unit start(1) = 1, priority(1) = 0, PID(13) = 4096
  //0x10 = transportscrambling(2) = 0, adaptation(2) = 1, continuity(4) = 0
  //0x00 = pointer = 0
  //0x02 = table ID = 2 = PMT
  //0xB017 = section syntax(1) = 1, 0(1)=0, reserved(2) = 3, section_len(12) = 23
  //0x0001 = ProgNo = 1
  //0xC1 = reserved(2) = 3, version(5)=0, curr_next_indi(1) = 1
  //0x00 = section_number = 0
  //0x00 = last_section_no = 0
  //0xE100 = reserved(3) = 7, PCR_PID(13) = 0x100
  //0xF000 = reserved(4) = 15, proginfolen = 0
  //0x1B = streamtype = 27 = H264
  //0xE100 = reserved(3) = 7, elem_ID(13) = 0x100
  //0xF000 = reserved(4) = 15, es_info_len = 0
  //0x0F = streamtype = 15 = audio with ADTS transport syntax
  //0xE101 = reserved(3) = 7, elem_ID(13) = 0x101
  //0xF000 = reserved(4) = 15, es_info_len = 0
  //0x2F44B99B = CRC32
  static char PMT[188] = {0x47, 0x50, 0x00, 0x10, 0x00, 0x02, 0xB0, 0x17, 0x00, 0x01, 0xC1, 0x00, 0x00, 0xE1, 0x00, 0xF0, 0x00, 0x1B, 0xE1, 0x00,
                          0xF0, 0x00, 0x0F, 0xE1, 0x01, 0xF0, 0x00, 0x2F, 0x44, 0xB9, 0x9B, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                          0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                          0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                          0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                          0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                          0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                          0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
                         };

} //TS namespace

