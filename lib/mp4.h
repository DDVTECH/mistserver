#pragma once
#include <string>
#include <stdint.h>
#include <vector>
#include "json.h"

/// Contains all MP4 format related code.
namespace MP4{

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
      void ResetPayload( );
    private:
      uint8_t * Payload;
      uint32_t PayloadSize;
  };//Box Class

  struct abst_serverentry {
    std::string ServerBaseUrl;
  };//abst_serverentry

  struct abst_qualityentry {
    std::string QualityModifier;
  };//abst_qualityentry

  /// ABST Box class
  class ABST: public Box {
    public:
      ABST() : Box(0x61627374){};
      void SetBootstrapVersion( uint32_t Version = 1 );
      void SetProfile( uint8_t Profile = 0 );
      void SetLive( bool Live = true );
      void SetUpdate( bool Update = false );
      void SetTimeScale( uint32_t Scale = 1000 );
      void SetMediaTime( uint32_t Time = 0 );
      void SetSMPTE( uint32_t Smpte = 0 );
      void SetMovieIdentifier( std::string Identifier = "" );
      void SetDRM( std::string Drm = "" );
      void SetMetaData( std::string MetaData = "" );
      void AddServerEntry( std::string Url = "", uint32_t Offset = 0 );
      void AddQualityEntry( std::string Quality = "", uint32_t Offset = 0 );
      void AddSegmentRunTable( Box * newSegment, uint32_t Offset = 0 );
      void AddFragmentRunTable( Box * newFragment, uint32_t Offset = 0 );
      void SetVersion( bool NewVersion = 0 );
      void WriteContent( );
    private:
      void SetDefaults( );
      void SetReserved( );
      uint32_t curBootstrapInfoVersion;
      uint8_t curProfile;
      bool isLive;
      bool isUpdate;
      bool Version;
      uint32_t curTimeScale;
      uint32_t curMediatime;//write as uint64_t
      uint32_t curSMPTE;//write as uint64_t
      std::string curMovieIdentifier;
      std::string curDRM;
      std::string curMetaData;
      std::vector<abst_serverentry> Servers;
      std::vector<abst_qualityentry> Qualities;
      std::vector<Box *> SegmentRunTables;
      std::vector<Box *> FragmentRunTables;
  };//ABST Box

  struct afrt_fragmentrunentry {
    uint32_t FirstFragment;
    uint32_t FirstFragmentTimestamp; //write as uint64_t
    uint32_t FragmentDuration;
    uint8_t DiscontinuityIndicator;//if FragmentDuration == 0
  };//afrt_fragmentrunentry


  /// AFRT Box class
  class AFRT : public Box {
    public:
      AFRT() : Box(0x61667274){};
      void SetUpdate( bool Update = false );
      void SetTimeScale( uint32_t Scale = 1000 );
      void AddQualityEntry( std::string Quality = "", uint32_t Offset = 0 );
      void AddFragmentRunEntry( uint32_t FirstFragment = 0, uint32_t FirstFragmentTimestamp = 0, uint32_t FragmentsDuration = 1, uint8_t Discontinuity = 0, uint32_t Offset = 0 );
      void WriteContent( );
    private:
      void SetDefaults( );
      bool isUpdate;
      uint32_t curTimeScale;
      std::vector<std::string> QualitySegmentUrlModifiers;
      std::vector<afrt_fragmentrunentry> FragmentRunEntryTable;
  };//AFRT Box

  struct asrt_segmentrunentry {
    uint32_t FirstSegment;
    uint32_t FragmentsPerSegment;
  };//abst_qualityentry

  /// ASRT Box class
  class ASRT : public Box {
    public:
      ASRT() : Box(0x61737274){};
      void SetUpdate( bool Update = false );
      void AddQualityEntry( std::string Quality = "", uint32_t Offset = 0 );
      void AddSegmentRunEntry( uint32_t FirstSegment = 0, uint32_t FragmentsPerSegment = 100, uint32_t Offset = 0 );
      void WriteContent( );
      void SetVersion( bool NewVersion = 0 );
    private:
      void SetDefaults( );
      bool isUpdate;
      bool Version;
      std::vector<std::string> QualitySegmentUrlModifiers;
      std::vector<asrt_segmentrunentry> SegmentRunEntryTable;
      Box * Container;
  };//ASRT Box

  std::string GenerateLiveBootstrap( JSON::Value & metadata );
  std::string mdatFold(std::string data);

};
