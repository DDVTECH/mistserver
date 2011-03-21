#include <stdint.h>
#include <iostream>
#include <string>
#include "../util/MP4/box_includes.h"

void Parse( Box * source ,std::string PrintOffset ) {
  if( source->header.BoxType == 0x61627374 ) {
    uint8_t Version = source->Payload[0];
    uint32_t Flags = (source->Payload[1] << 16) + (source->Payload[2] << 8) + (source->Payload[3]); //uint24_t
    uint32_t BootstrapInfoVersion = (source->Payload[4] << 24) + (source->Payload[5] << 16) +(source->Payload[6] << 8) + (source->Payload[7]);
    uint8_t Profile = (source->Payload[8] >> 6); //uint2_t
    uint8_t Live = ((source->Payload[8] >> 5 ) & 0x1); //uint1_t
    uint8_t Update = ((source->Payload[8] >> 4 ) & 0x1); //uint1_t
    uint8_t Reserved = (source->Payload[8] & 0x4); //uint4_t
    uint32_t Timescale = (source->Payload[9] << 24) + (source->Payload[10] << 16) +(source->Payload[11] << 8) + (source->Payload[12]);
    uint32_t CurrentMediaTime_Upperhalf = (source->Payload[13] << 24) + (source->Payload[14] << 16) +(source->Payload[15] << 8) + (source->Payload[16]);
    uint32_t CurrentMediaTime_Lowerhalf = (source->Payload[17] << 24) + (source->Payload[18] << 16) +(source->Payload[19] << 8) + (source->Payload[20]);
    uint32_t SmpteTimeCodeOffset_Upperhalf = (source->Payload[21] << 24) + (source->Payload[22] << 16) +(source->Payload[23] << 8) + (source->Payload[24]);
    uint32_t SmpteTimeCodeOffset_Lowerhalf = (source->Payload[25] << 24) + (source->Payload[26] << 16) +(source->Payload[27] << 8) + (source->Payload[28]);

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
    while( source->Payload[CurrentOffset] != '\0' ) { MovieIdentifier += source->Payload[CurrentOffset]; CurrentOffset ++; }
    CurrentOffset ++;
    ServerEntryCount = source->Payload[CurrentOffset];
    CurrentOffset ++;
    for( uint8_t i = 0; i < ServerEntryCount; i++ ) {
      temp = "";
      while( source->Payload[CurrentOffset] != '\0' ) { temp += source->Payload[CurrentOffset]; CurrentOffset ++; }
      ServerEntryTable.push_back(temp);
      CurrentOffset++;
    }
    QualityEntryCount = source->Payload[CurrentOffset];
    CurrentOffset ++;
    for( uint8_t i = 0; i < QualityEntryCount; i++ ) {
      temp = "";
      while( source->Payload[CurrentOffset] != '\0' ) { temp += source->Payload[CurrentOffset]; CurrentOffset ++; }
      QualityEntryTable.push_back(temp);
      CurrentOffset++;
    }
    while( source->Payload[CurrentOffset] != '\0' ) { DrmData += source->Payload[CurrentOffset]; CurrentOffset ++; }
    CurrentOffset ++;
    while( source->Payload[CurrentOffset] != '\0' ) { MetaData += source->Payload[CurrentOffset]; CurrentOffset ++; }
    CurrentOffset ++;
    SegmentRunTableCount = source->Payload[CurrentOffset];
    CurrentOffset ++;
    for( uint8_t i = 0; i < SegmentRunTableCount; i++ ) {
      TempSize = (source->Payload[CurrentOffset] << 24) + (source->Payload[CurrentOffset+1]<< 16) + (source->Payload[CurrentOffset+2]<< 8) + (source->Payload[CurrentOffset+3]);
      TempBox = new Box( &source->Payload[CurrentOffset], TempSize );
      SegmentRunTableEntries.push_back(TempBox);
      CurrentOffset += TempSize;
    }
    FragmentRunTableCount = source->Payload[CurrentOffset];
    CurrentOffset ++;
    for( uint8_t i = 0; i < FragmentRunTableCount; i++ ) {
      TempSize = (source->Payload[CurrentOffset] << 24) + (source->Payload[CurrentOffset+1]<< 16) + (source->Payload[CurrentOffset+2]<< 8) + (source->Payload[CurrentOffset+3]);
      TempBox = new Box( &source->Payload[CurrentOffset], TempSize );
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
      Parse( SegmentRunTableEntries[i], PrintOffset+"      " );
    }
    std::cerr << PrintOffset << "  FragmentRunTableCount: " << (int)FragmentRunTableCount << "\n";
    std::cerr << PrintOffset << "  FragmentRunTableEntries:\n";
    for( uint32_t i = 0; i < FragmentRunTableEntries.size( ); i++ ) {
      std::cerr << PrintOffset << "    " << i+1 << ": ";
      Parse( FragmentRunTableEntries[i], PrintOffset+"      " );
    }

  } else if ( source->header.BoxType == 0x61737274 ) {
    uint8_t Version = source->Payload[0];
    uint32_t Flags = (source->Payload[1] << 16) + (source->Payload[2] << 8) + (source->Payload[3]); //uint24_t
    uint8_t QualityEntryCount;
    std::vector<std::string> QualitySegmentUrlModifiers;
    uint32_t SegmentRunEntryCount;
    std::vector< std::pair<uint32_t,uint32_t> > SegmentRunEntryTable;

    uint32_t CurrentOffset = 4;
    std::string temp;
    std::pair<uint32_t,uint32_t> TempPair;
    QualityEntryCount = source->Payload[CurrentOffset];
    CurrentOffset ++;
    for( uint8_t i = 0; i < QualityEntryCount; i++ ) {
      temp = "";
      while( source->Payload[CurrentOffset] != '\0' ) { temp += source->Payload[CurrentOffset]; CurrentOffset ++; }
      QualitySegmentUrlModifiers.push_back(temp);
      CurrentOffset++;
    }
    SegmentRunEntryCount = (source->Payload[CurrentOffset] << 24) + (source->Payload[CurrentOffset+1] << 16) + (source->Payload[CurrentOffset+2] << 8) + (source->Payload[CurrentOffset+3]);
    CurrentOffset +=4;
    for( uint8_t i = 0; i < SegmentRunEntryCount; i++ ) {
      TempPair.first = (source->Payload[CurrentOffset] << 24) + (source->Payload[CurrentOffset+1] << 16) + (source->Payload[CurrentOffset+2] << 8) + (source->Payload[CurrentOffset+3]);
      CurrentOffset+=4;
      TempPair.second = (source->Payload[CurrentOffset] << 24) + (source->Payload[CurrentOffset+1] << 16) + (source->Payload[CurrentOffset+2] << 8) + (source->Payload[CurrentOffset+3]);
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
    std::cerr << PrintOffset << "  SegmentRunEntryCount: " << (int)SegmentRunEntryCount << "\n";
    std::cerr << PrintOffset << "  SegmentRunEntryTable:\n";
    for( uint32_t i = 0; i < SegmentRunEntryTable.size( ); i++ ) {
      std::cerr << PrintOffset << "    " << i+1 << ":\n";
      std::cerr << PrintOffset << "      FirstSegment: " << SegmentRunEntryTable[i].first << "\n";
      std::cerr << PrintOffset << "      FragmentsPerSegment: " << SegmentRunEntryTable[i].second << "\n";
    }
  } else if ( source->header.BoxType == 0x61667274 ) {
    uint8_t Version = source->Payload[0];
    uint32_t Flags = (source->Payload[1] << 16) + (source->Payload[2] << 8) + (source->Payload[3]); //uint24_t
    uint32_t TimeScale = (source->Payload[4] << 24) + (source->Payload[5] << 16) + (source->Payload[6] << 8) + (source->Payload[7]);
    uint8_t QualityEntryCount;
    std::vector<std::string> QualitySegmentUrlModifiers;
    uint32_t FragmentRunEntryCount;
    std::vector<afrt_fragmentrunentry> FragmentRunEntryTable;

    uint32_t CurrentOffset = 8;
    std::string temp;
    afrt_fragmentrunentry TempEntry;
    QualityEntryCount = source->Payload[CurrentOffset];
    CurrentOffset ++;
    for( uint8_t i = 0; i < QualityEntryCount; i++ ) {
      temp = "";
      while( source->Payload[CurrentOffset] != '\0' ) { temp += source->Payload[CurrentOffset]; CurrentOffset ++; }
      QualitySegmentUrlModifiers.push_back(temp);
      CurrentOffset++;
    }
    FragmentRunEntryCount = (source->Payload[CurrentOffset] << 24) + (source->Payload[CurrentOffset+1] << 16) + (source->Payload[CurrentOffset+2] << 8) + (source->Payload[CurrentOffset+3]);
    CurrentOffset +=4;
    for( uint8_t i = 0; i < FragmentRunEntryCount; i ++ ) {
      TempEntry.FirstFragment = (source->Payload[CurrentOffset] << 24) + (source->Payload[CurrentOffset+1] << 16) + (source->Payload[CurrentOffset+2] << 8) + (source->Payload[CurrentOffset+3]);
      CurrentOffset +=4;
      CurrentOffset +=4;
      TempEntry.FirstFragmentTimestamp = (source->Payload[CurrentOffset] << 24) + (source->Payload[CurrentOffset+1] << 16) + (source->Payload[CurrentOffset+2] << 8) + (source->Payload[CurrentOffset+3]);
      CurrentOffset +=4;
      TempEntry.FragmentDuration = (source->Payload[CurrentOffset] << 24) + (source->Payload[CurrentOffset+1] << 16) + (source->Payload[CurrentOffset+2] << 8) + (source->Payload[CurrentOffset+3]);
      CurrentOffset +=4;
      if( TempEntry.FragmentDuration == 0 ) {
        TempEntry.DiscontinuityIndicator = source->Payload[CurrentOffset];
        CurrentOffset++;
      }
      FragmentRunEntryTable.push_back(TempEntry);
    }

    std::cerr << "Box_AFRT:\n";
    std::cerr << PrintOffset << "  Version: " << (int)Version << "\n";
    std::cerr << PrintOffset << "  Flags: " << (int)Flags << "\n";
    std::cerr << PrintOffset << "  Timescale: " << (int)TimeScale << "\n";
    std::cerr << PrintOffset << "  QualityEntryCount: " << (int)QualityEntryCount << "\n";
    std::cerr << PrintOffset << "  QualitySegmentUrlModifiers:\n";
    for( uint32_t i = 0; i < QualitySegmentUrlModifiers.size( ); i++ ) {
      std::cerr << PrintOffset << "    " << i+1 << ": " << QualitySegmentUrlModifiers[i] << "\n";
    }
    std::cerr << PrintOffset << "  FragmentRunEntryCount: " << (int)FragmentRunEntryCount << "\n";
    std::cerr << PrintOffset << "  FragmentRunEntryTable:\n";
    for( uint32_t i = 0; i < FragmentRunEntryTable.size( ); i++ ) {
      std::cerr << PrintOffset << "    " << i+1 << ":\n";
      std::cerr << PrintOffset << "      FirstFragment: " << FragmentRunEntryTable[i].FirstFragment << "\n";
      std::cerr << PrintOffset << "      FirstFragmentTimestamp: " << FragmentRunEntryTable[i].FirstFragmentTimestamp << "\n";
      std::cerr << PrintOffset << "      FragmentDuration: " << FragmentRunEntryTable[i].FragmentDuration << "\n";
      if( FragmentRunEntryTable[i].FragmentDuration == 0 ) {
        std::cerr << PrintOffset << "      DiscontinuityIndicator: " << (int)FragmentRunEntryTable[i].DiscontinuityIndicator << "\n";
      }
    }
  } else {
    std::cerr << "BoxType '"
              << (char)(source->header.BoxType >> 24)
              << (char)((source->header.BoxType << 8) >> 24)
              << (char)((source->header.BoxType << 16) >> 24)
              << (char)((source->header.BoxType << 24) >> 24)
              << "' not yet implemented!\n";
  }
}

int main( ) {
  std::string temp;
  bool validinp = true;
  char thischar;
  while(validinp) {
    thischar = std::cin.get( );
    if(std::cin.good( ) ) {
      temp += thischar;
    } else {
      validinp = false;
    }
  }
  Box * TestBox = new Box((uint8_t*)temp.c_str( ), temp.size( ));
  Parse( TestBox, "" );
  delete TestBox;
}
