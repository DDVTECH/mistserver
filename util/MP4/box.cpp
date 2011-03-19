#pragma once

#include <stdint.h>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <string>
#include <vector>

struct BoxHeader {
  uint32_t TotalSize;
  uint32_t BoxType;
};//BoxHeader struct

class Box {
  public:
    Box();
    Box(uint32_t BoxType);
    Box(uint8_t * Content, uint32_t length);
    ~Box();
    void SetBoxType(uint32_t BoxType);
    uint32_t GetBoxType();
    void SetPayload(uint32_t Size, uint8_t * Data, uint32_t Index = 0);
    uint32_t GetPayloadSize();
    uint8_t * GetPayload();
    uint8_t * GetPayload(uint32_t Index, uint32_t & Size);
    uint32_t GetBoxedDataSize();
    uint8_t * GetBoxedData( );
    static uint8_t * uint32_to_uint8( uint32_t data );
    static uint8_t * uint16_to_uint8( uint16_t data );
    static uint8_t * uint8_to_uint8( uint8_t data );
    BoxHeader GetHeader( );
    void ResetPayload( );
    void Parse( std::string PrintOffset = "" );
  private:
    BoxHeader header;
    uint8_t * Payload;
    uint32_t PayloadSize;
};//Box Class

Box::Box() {
  Payload = NULL;
  PayloadSize = 0;
}

Box::Box(uint32_t BoxType) {
  header.BoxType = BoxType;
  Payload = NULL;
  PayloadSize = 0;
}

Box::Box(uint8_t * Content, uint32_t length) {
  header.TotalSize = (Content[0] << 24) + (Content[1] << 16) + (Content[2] << 8) + (Content[3]);
  if(header.TotalSize != length) { std::cerr << "Warning: length sizes differ\n"; }
  header.BoxType = (Content[4] << 24) + (Content[5] << 16) + (Content[6] << 8) + (Content[7]);
  std::cerr << "Created new box with type \""
            << (char)(header.BoxType >> 24)
            << (char)((header.BoxType << 8) >> 24)
            << (char)((header.BoxType << 16) >> 24)
            << (char)((header.BoxType << 24) >> 24)
            << "\"\n";
  PayloadSize = length-8;
  Payload = new uint8_t[PayloadSize];
  memcpy( Payload, &Content[8], PayloadSize );
}

Box::~Box() {
}

void Box::SetBoxType(uint32_t BoxType) {
  header.BoxType = BoxType;
}

uint32_t Box::GetBoxType() {
  return header.BoxType;
}

void Box::SetPayload(uint32_t Size, uint8_t * Data, uint32_t Index) {
  uint8_t * tempchar = NULL;
  if ( Index + Size > PayloadSize ) {
    if ( Payload ) {
      tempchar = new uint8_t[PayloadSize];
      memcpy( tempchar, Payload, PayloadSize );
      delete Payload;
    }
    PayloadSize = Index + Size;
    Payload = new uint8_t[PayloadSize];
    if( tempchar ) {
      memcpy( Payload, tempchar, Index );
    } else {
      for(uint32_t i = 0; i < Index; i++) { Payload[i] = 0; }
    }
    memcpy( &Payload[Index], Data, Size );
    header.TotalSize = PayloadSize + 8;
    if( tempchar ) {
      delete tempchar;
    }
  } else {
    memcpy( &Payload[Index], Data, Size );
  }
}

uint32_t Box::GetPayloadSize() {
  return PayloadSize;
}

uint8_t * Box::GetPayload() {
  uint8_t * temp = new uint8_t[PayloadSize];
  memcpy( temp, Payload, PayloadSize );
  return temp;
}

uint8_t * Box::GetPayload(uint32_t Index, uint32_t & Size) {
  if(Index > PayloadSize) { return NULL; }
  if(Index + Size > PayloadSize) { Size = PayloadSize - Index; }
  uint8_t * temp = new uint8_t[Size - Index];
  memcpy( temp, &Payload[Index], Size - Index );
  return temp;
}

uint32_t Box::GetBoxedDataSize() {
  return header.TotalSize;
}

uint8_t * Box::GetBoxedData( ) {
  uint8_t * temp = new uint8_t[header.TotalSize];
  memcpy( temp, uint32_to_uint8(header.TotalSize), 4 );
  memcpy( &temp[4], uint32_to_uint8(header.BoxType), 4 );
  memcpy( &temp[8], Payload, PayloadSize );
  return temp;
}


uint8_t * Box::uint32_to_uint8( uint32_t data ) {
  uint8_t * temp = new uint8_t[4];
  temp[0] = (data >> 24) & 0x000000FF;
  temp[1] = (data >> 16 ) & 0x000000FF;
  temp[2] = (data >> 8 ) & 0x000000FF;
  temp[3] = (data ) & 0x000000FF;
  return temp;
}

uint8_t * Box::uint16_to_uint8( uint16_t data ) {
  uint8_t * temp = new uint8_t[2];
  temp[0] = (data >> 8) & 0x00FF;
  temp[1] = (data  ) & 0x00FF;
  return temp;
}

uint8_t * Box::uint8_to_uint8( uint8_t data ) {
   uint8_t * temp = new uint8_t[1];
   temp[0] = data;
   return temp;
}

BoxHeader Box::GetHeader( ) {
  return header;
}

void Box::ResetPayload( ) {
  header.TotalSize -= PayloadSize;
  PayloadSize = 0;
  if(Payload) {
    delete Payload;
    Payload = NULL;
  }
}

void Box::Parse( std::string PrintOffset ) {
  if( header.BoxType == 0x61627374 ) {
    uint8_t Version = Payload[0];
    uint32_t Flags = (Payload[1] << 16) + (Payload[2] << 8) + (Payload[3]); //uint24_t
    uint32_t BootstrapInfoVersion = (Payload[4] << 24) + (Payload[5] << 16) +(Payload[6] << 8) + (Payload[7]);
    uint8_t Profile = (Payload[8] >> 6); //uint2_t
    uint8_t Live = (( Payload[8] >> 5 ) & 0x1); //uint1_t
    uint8_t Update = (( Payload[8] >> 4 ) & 0x1); //uint1_t
    uint8_t Reserved = ( Payload[8] & 0x4); //uint4_t
    uint32_t Timescale = (Payload[9] << 24) + (Payload[10] << 16) +(Payload[11] << 8) + (Payload[12]);
    uint32_t CurrentMediaTime_Upperhalf = (Payload[13] << 24) + (Payload[14] << 16) +(Payload[15] << 8) + (Payload[16]);
    uint32_t CurrentMediaTime_Lowerhalf = (Payload[17] << 24) + (Payload[18] << 16) +(Payload[19] << 8) + (Payload[20]);
    uint32_t SmpteTimeCodeOffset_Upperhalf = (Payload[21] << 24) + (Payload[22] << 16) +(Payload[23] << 8) + (Payload[24]);
    uint32_t SmpteTimeCodeOffset_Lowerhalf = (Payload[25] << 24) + (Payload[26] << 16) +(Payload[27] << 8) + (Payload[28]);

    std::string MovieIdentifier;
    uint8_t ServerEntryCount = -1;
    std::vector<std::string> ServerEntryTable;
    uint8_t QualityEntryCount = -1;
    std::vector<std::string> QualityEntryTable;
    std::string DrmData;
    std::string MetaData;
    uint8_t SegmentRunTableCount = -1;
    std::vector<Box*> SegmentRunTableEntries;
    uint8_t FragmentRunTableCount = -1;
    std::vector<Box*> FragmentRunTableEntries;

    uint32_t CurrentOffset = 29;
    uint32_t TempSize;
    Box* TempBox;
    std::string temp;
    while( Payload[CurrentOffset] != '\0' ) { MovieIdentifier += Payload[CurrentOffset]; CurrentOffset ++; }
    CurrentOffset ++;
    ServerEntryCount = Payload[CurrentOffset];
    CurrentOffset ++;
    for( uint8_t i = 0; i < ServerEntryCount; i++ ) {
      temp = "";
      while( Payload[CurrentOffset] != '\0' ) { temp += Payload[CurrentOffset]; CurrentOffset ++; }
      ServerEntryTable.push_back(temp);
      CurrentOffset++;
    }
    QualityEntryCount = Payload[CurrentOffset];
    CurrentOffset ++;
    for( uint8_t i = 0; i < QualityEntryCount; i++ ) {
      temp = "";
      while( Payload[CurrentOffset] != '\0' ) { temp += Payload[CurrentOffset]; CurrentOffset ++; }
      QualityEntryTable.push_back(temp);
      CurrentOffset++;
    }
    while( Payload[CurrentOffset] != '\0' ) { DrmData += Payload[CurrentOffset]; CurrentOffset ++; }
    CurrentOffset ++;
    while( Payload[CurrentOffset] != '\0' ) { MetaData += Payload[CurrentOffset]; CurrentOffset ++; }
    CurrentOffset ++;
    SegmentRunTableCount = Payload[CurrentOffset];
    CurrentOffset ++;
    for( uint8_t i = 0; i < SegmentRunTableCount; i++ ) {
      TempSize = (Payload[CurrentOffset] << 24) + (Payload[CurrentOffset+1]<< 16) + (Payload[CurrentOffset+2]<< 8) + (Payload[CurrentOffset+3]);
      TempBox = new Box( &Payload[CurrentOffset], TempSize );
      SegmentRunTableEntries.push_back(TempBox);
      CurrentOffset += TempSize;
    }
    FragmentRunTableCount = Payload[CurrentOffset];
    CurrentOffset ++;
    for( uint8_t i = 0; i < FragmentRunTableCount; i++ ) {
      TempSize = (Payload[CurrentOffset] << 24) + (Payload[CurrentOffset+1]<< 16) + (Payload[CurrentOffset+2]<< 8) + (Payload[CurrentOffset+3]);
      TempBox = new Box( &Payload[CurrentOffset], TempSize );
      FragmentRunTableEntries.push_back(TempBox);
      CurrentOffset += TempSize;
    }

    std::cerr << "Box_ABST:\n";
    std::cerr << PrintOffset << "  Version: " << (int)Version << "\n";
    std::cerr << PrintOffset << "  Flags: " << (int)Flags << "\n";
    std::cerr << PrintOffset << "  BootstrapInfoVersion: " << (int)BootstrapInfoVersion << "\n";
    std::cerr << PrintOffset << "  Profile: " << (int)Profile << "\n";
    std::cerr << PrintOffset << "  Live: " << (int)Live << "\n";
    std::cerr << PrintOffset << "  Update: " << (int)Update << "\n";
    std::cerr << PrintOffset << "  Reserved: " << (int)Reserved << "\n";
    std::cerr << PrintOffset << "  Timescale: " << (int)Timescale << "\n";
    std::cerr << PrintOffset << "  CurrentMediaTime: " << (int)CurrentMediaTime_Upperhalf << " " << CurrentMediaTime_Lowerhalf << "\n";
    std::cerr << PrintOffset << "  SmpteTimeCodeOffset: " << (int)SmpteTimeCodeOffset_Upperhalf << " " << SmpteTimeCodeOffset_Lowerhalf << "\n";
    std::cerr << PrintOffset << "  MovieIdentifier: " << MovieIdentifier << "\n";
    std::cerr << PrintOffset << "  ServerEntryCount: " << (int)ServerEntryCount << "\n";
    std::cerr << PrintOffset << "  ServerEntryTable:\n";
    for( uint32_t i = 0; i < ServerEntryTable.size( ); i++ ) {
      std::cerr << PrintOffset << "    " << i+1 << ": " << ServerEntryTable[i] << "\n";
    }
    std::cerr << PrintOffset << "  QualityEntryCount: " << (int)QualityEntryCount << "\n";
    std::cerr << PrintOffset << "  QualityEntryTable:\n";
    for( uint32_t i = 0; i < QualityEntryTable.size( ); i++ ) {
      std::cerr << PrintOffset << "    " << i+1 << ": " << QualityEntryTable[i] << "\n";
    }
    std::cerr << PrintOffset << "  DrmData: " << DrmData << "\n";
    std::cerr << PrintOffset << "  MetaData: " << MetaData << "\n";
    std::cerr << PrintOffset << "  SegmentRunTableCount: " << (int)SegmentRunTableCount << "\n";
    std::cerr << PrintOffset << "  SegmentRunTableEntries:\n";
    for( uint32_t i = 0; i < SegmentRunTableEntries.size( ); i++ ) {
      std::cerr << PrintOffset << "    " << i+1 << ": ";
      SegmentRunTableEntries[i]->Parse( PrintOffset+"      ");
    }
    std::cerr << PrintOffset << "  FragmentRunTableCount: " << (int)FragmentRunTableCount << "\n";
    std::cerr << PrintOffset << "  FragmentRunTableEntries:\n";
    for( uint32_t i = 0; i < FragmentRunTableEntries.size( ); i++ ) {
      std::cerr << PrintOffset << "    " << i+1 << ": ";
      FragmentRunTableEntries[i]->Parse( PrintOffset+"      ");
    }

  } else if ( header.BoxType == 0x61737274 ) {
    uint8_t Version = Payload[0];
    uint32_t Flags = (Payload[1] << 16) + (Payload[2] << 8) + (Payload[3]); //uint24_t
    uint8_t QualityEntryCount;
    std::vector<std::string> QualitySegmentUrlModifiers;
    uint32_t SegmentRunEntryCount;
    std::vector< std::pair<uint32_t,uint32_t> > SegmentRunEntryTable;

    uint32_t CurrentOffset = 4;
    std::string temp;
    std::pair<uint32_t,uint32_t> TempPair;
    QualityEntryCount = Payload[CurrentOffset];
    CurrentOffset ++;
    for( uint8_t i = 0; i < QualityEntryCount; i++ ) {
      temp = "";
      while( Payload[CurrentOffset] != '\0' ) { temp += Payload[CurrentOffset]; CurrentOffset ++; }
      QualitySegmentUrlModifiers.push_back(temp);
      CurrentOffset++;
    }
    SegmentRunEntryCount = Payload[CurrentOffset];
    CurrentOffset ++;
    for( uint8_t i = 0; i < SegmentRunEntryCount; i++ ) {
      TempPair.first = (Payload[CurrentOffset] << 24) + (Payload[CurrentOffset+1] << 16) + (Payload[CurrentOffset+2] << 8) + (Payload[CurrentOffset+3]);
      CurrentOffset+=4;
      TempPair.second = (Payload[CurrentOffset] << 24) + (Payload[CurrentOffset+1] << 16) + (Payload[CurrentOffset+2] << 8) + (Payload[CurrentOffset+3]);
      CurrentOffset+=4;
      SegmentRunEntryTable.push_back(TempPair);
    }

    std::cerr << "Box_ASRT:\n";
    std::cerr << PrintOffset << "  Version: " << (int)Version << "\n";
    std::cerr << PrintOffset << "  Flags: " << (int)Flags << "\n";
    std::cerr << PrintOffset << "  QualityEntryCount: " << (int)QualityEntryCount << "\n";
    std::cerr << PrintOffset << "  QualitySegmentUrlModifiers:\n";
    for( uint32_t i = 0; i < QualitySegmentUrlModifiers.size( ); i++ ) {
      std::cerr << PrintOffset << "    " << i+1 << ": " << QualitySegmentUrlModifiers[i] << "\n";
    }
    std::cerr << PrintOffset << "  SegmentRunEntryCount: " << (int)QualityEntryCount << "\n";
    std::cerr << PrintOffset << "  SegmentRunEntryTable:\n";
    for( uint32_t i = 0; i < SegmentRunEntryTable.size( ); i++ ) {
      std::cerr << PrintOffset << "    " << i+1 << ":\n";
      std::cerr << PrintOffset << "      FirstSegment: " << SegmentRunEntryTable[i].first << "\n";
      std::cerr << PrintOffset << "      FragmentsPerSegment: " << SegmentRunEntryTable[i].second << "\n";
    }
  } else {
    std::cerr << "BoxType '"
              << (char)(header.BoxType >> 24)
              << (char)((header.BoxType << 8) >> 24)
              << (char)((header.BoxType << 16) >> 24)
              << (char)((header.BoxType << 24) >> 24)
              << "' not yet implemented!\n";
  }
}
