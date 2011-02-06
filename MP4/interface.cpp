#include "interface.h"

Interface::Interface() {
  //Initializing local data
  Width = 0;
  Height = 0;
  //Creating the boxes
  ftyp = new Box_ftyp();
  moov = new Box_moov();
  mvhd = new Box_mvhd();
  trak_vide = new Box_trak();
  tkhd_vide = new Box_tkhd();
  mdia_vide = new Box_mdia();
  mdhd_vide = new Box_mdhd();
  hdlr_vide = new Box_hdlr();
  minf_vide = new Box_minf();
  vmhd_vide = new Box_vmhd();
  dinf_vide = new Box_dinf();
  dref_vide = new Box_dref();
  url_vide = new Box_url();
  stbl_vide = new Box_stbl();
  stts_vide = new Box_stts();
  stsc_vide = new Box_stsc();
  stco_vide = new Box_stco();
  stsd_vide = new Box_stsd();
  avcC_vide = new Box_avcC();
  trak_soun = new Box_trak();
  tkhd_soun = new Box_tkhd();
  mdia_soun = new Box_mdia();
  mdhd_soun = new Box_mdhd();
  hdlr_soun = new Box_hdlr();
  minf_soun = new Box_minf();
  smhd_soun = new Box_smhd();
  dinf_soun = new Box_dinf();
  dref_soun = new Box_dref();
  url_soun = new Box_url();
  stbl_soun = new Box_stbl();
  stts_soun = new Box_stts();
  stsc_soun = new Box_stsc();
  stco_soun = new Box_stco();
  stsd_soun = new Box_stsd();
  esds_soun = new Box_esds();
  rtmp = new Box_rtmp();
  amhp = new Box_amhp();
  mvex = new Box_mvex();
  trex_vide = new Box_trex();
  trex_soun = new Box_trex();
  //Set some values we already know won't change once the boxes have been created
  SetStaticDefaults();
}

Interface::~Interface() {
  //Deleting the boxes if they still exist.
  if( trex_vide ) { delete trex_vide; trex_vide = NULL; }
  if( trex_soun ) { delete trex_soun; trex_soun = NULL; }
  if( mvex ) { delete mvex; mvex = NULL; }
  if( amhp ) { delete amhp; amhp = NULL; }
  if( rtmp ) { delete rtmp; rtmp = NULL; }
  if( esds_soun ) { delete esds_soun; esds_soun = NULL; }
  if( stsd_soun ) { delete stsd_soun; stsd_soun = NULL; }
  if( stco_soun ) { delete stco_soun; stco_soun = NULL; }
  if( stsc_soun ) { delete stsc_soun; stsc_soun = NULL; }
  if( stts_soun ) { delete stts_soun; stts_soun = NULL; }
  if( stbl_soun ) { delete stbl_soun; stbl_soun = NULL; }
  if( url_soun ) { delete url_soun; url_soun = NULL; }
  if( dref_soun ) { delete dref_soun; dref_soun = NULL; }
  if( dinf_soun ) { delete dinf_soun; dinf_soun = NULL; }
  if( minf_soun ) { delete minf_soun; minf_soun = NULL; }
  if( hdlr_soun ) { delete hdlr_soun; hdlr_soun = NULL; }
  if( mdhd_soun ) { delete mdhd_soun; mdhd_soun = NULL; }
  if( mdia_soun ) { delete mdia_soun; mdia_soun = NULL; }
  if( tkhd_soun ) { delete tkhd_soun; tkhd_soun = NULL; }
  if( trak_soun ) { delete trak_soun; trak_soun = NULL; }
  if( avcC_vide ) { delete avcC_vide; avcC_vide = NULL; }
  if( stsd_vide ) { delete stsd_vide; stsd_vide = NULL; }
  if( stco_vide ) { delete stco_vide; stco_vide = NULL; }
  if( stsc_vide ) { delete stsc_vide; stsc_vide = NULL; }
  if( stts_vide ) { delete stts_vide; stts_vide = NULL; }
  if( stbl_vide ) { delete stbl_vide; stbl_vide = NULL; }
  if( url_vide ) { delete url_vide; url_vide = NULL; }
  if( dref_vide ) { delete dref_vide; dref_vide = NULL; }
  if( dinf_vide ) { delete dinf_vide; dinf_vide = NULL; }
  if( minf_vide ) { delete minf_vide; minf_vide = NULL; }
  if( hdlr_vide ) { delete hdlr_vide; hdlr_vide = NULL; }
  if( mdhd_vide ) { delete mdhd_vide; mdhd_vide = NULL; }
  if( mdia_vide ) { delete mdia_vide; mdia_vide = NULL; }
  if( tkhd_vide ) { delete tkhd_vide; tkhd_vide = NULL; }
  if( trak_vide ) { delete trak_vide; trak_vide = NULL; }
  if( mvhd ) { delete mvhd; mvhd = NULL; }
  if( moov ) { delete moov; moov = NULL; }
  if( ftyp ) { delete ftyp; ftyp = NULL; }
}

void Interface::link( ) {
  //Linking Video Track
  stsd_vide->AddContent(avcC_vide->GetBox());
  stbl_vide->AddContent(stsd_vide->GetBox(),3);
  stbl_vide->AddContent(stco_vide->GetBox(),2);
  stbl_vide->AddContent(stsc_vide->GetBox(),1);
  stbl_vide->AddContent(stts_vide->GetBox());
  dref_vide->AddContent(url_vide->GetBox());
  dinf_vide->AddContent(dref_vide->GetBox());
  minf_vide->AddContent(stbl_vide->GetBox(),2);
  minf_vide->AddContent(dinf_vide->GetBox(),1);
  minf_vide->AddContent(vmhd_vide->GetBox());
  mdia_vide->AddContent(minf_vide->GetBox(),2);
  mdia_vide->AddContent(hdlr_vide->GetBox(),1);
  mdia_vide->AddContent(mdhd_vide->GetBox());
  trak_vide->AddContent(mdia_vide->GetBox(),1);
  trak_vide->AddContent(tkhd_vide->GetBox());

  //Linking Sound Track
  stsd_soun->AddContent(esds_soun->GetBox());
  stbl_soun->AddContent(stsd_soun->GetBox(),3);
  stbl_soun->AddContent(stco_soun->GetBox(),2);
  stbl_soun->AddContent(stsc_soun->GetBox(),1);
  stbl_soun->AddContent(stts_soun->GetBox());
  dref_soun->AddContent(url_soun->GetBox());
  dinf_soun->AddContent(dref_soun->GetBox());
  minf_soun->AddContent(stbl_soun->GetBox(),2);
  minf_soun->AddContent(dinf_soun->GetBox(),1);
  minf_soun->AddContent(smhd_soun->GetBox());
  mdia_soun->AddContent(minf_soun->GetBox(),2);
  mdia_soun->AddContent(hdlr_soun->GetBox(),1);
  mdia_soun->AddContent(mdhd_soun->GetBox());
  trak_soun->AddContent(mdia_soun->GetBox(),1);
  trak_soun->AddContent(tkhd_soun->GetBox());

  //Linking total file
  moov->AddContent(trak_soun->GetBox(),2);
  moov->AddContent(trak_vide->GetBox(),1);
  moov->AddContent(mvhd->GetBox());

  rtmp->AddContent(amhp->GetBox());
}

uint32_t Interface::GetContentSize( ) {
  return ftyp->GetBox( )->GetBoxedDataSize( ) + moov->GetBox( )->GetBoxedDataSize( );
}

uint8_t * Interface::GetContents( ) {
  uint8_t * Result = new uint8_t[GetContentSize( )];
  memcpy(Result,ftyp->GetBox( )->GetBoxedData( ),ftyp->GetBox( )->GetBoxedDataSize( ));
  memcpy(&Result[ftyp->GetBox( )->GetBoxedDataSize( )],moov->GetBox( )->GetBoxedData( ),moov->GetBox( )->GetBoxedDataSize( ));
  return Result;
}

void Interface::UpdateContents( ) {
  if( !Width ) { fprintf(stderr,"WARNING: Width not set!\n"); }
  if( !Height ) { fprintf(stderr,"WARNING: Height not set!\n"); }
  if( !Duration.size() ) { fprintf(stderr,"WARNING: Duration not set!\n"); }
  if( !UnitsPerSecond.size() ) { fprintf(stderr,"WARNING: Timescale not set!\n"); }
  if( sttsvide.size() == 0 ) {
    fprintf(stderr,"WARNING: No video stts available!\n");
  } else { WriteSTTS( 1 ); }
  if( sttssoun.size() == 0 ) {
    fprintf(stderr,"WARNING: No sound stts available!\n");
  } else { WriteSTTS( 2 ); }
  if( stscvide.size() == 0 ) {
    fprintf(stderr,"WARNING: No video stsc available!\n");
  } else { WriteSTSC( 1 ); }
  if( stscsoun.size() == 0 ) {
    fprintf(stderr,"WARNING: No sound stsc available!\n");
  } else { WriteSTSC( 2 ); }
  stsd_vide->WriteContent( );
  stco_vide->WriteContent( );
  stsc_vide->WriteContent( );
  stts_vide->WriteContent( );
  stbl_vide->WriteContent( );
  dref_vide->WriteContent( );
  dinf_vide->WriteContent( );
  minf_vide->WriteContent( );
  mdia_vide->WriteContent( );

  stsd_soun->WriteContent( );
  stco_soun->WriteContent( );
  stsc_soun->WriteContent( );
  stts_soun->WriteContent( );
  stbl_soun->WriteContent( );
  dref_soun->WriteContent( );
  dinf_soun->WriteContent( );
  minf_soun->WriteContent( );
  mdia_soun->WriteContent( );

  trak_vide->WriteContent( );

  moov->WriteContent( );

  amhp->WriteContent( );
  rtmp->WriteContent( );
}

bool Interface::AllBoxesExist() {
  return ( ftyp && moov && mvhd && trak_vide && tkhd_vide && mdia_vide && mdhd_vide && hdlr_vide &&
  minf_vide && vmhd_vide && dinf_vide && dref_vide && url_vide && stbl_vide && stts_vide && stsc_vide &&
  stco_vide && stsd_vide && avcC_vide && trak_soun && tkhd_soun && mdia_soun && mdhd_soun && hdlr_soun &&
  minf_soun && smhd_soun && dinf_soun && dref_soun && url_soun && stbl_soun && stts_soun && stsc_soun &&
  stco_soun && stsd_soun && esds_soun && rtmp && amhp && mvex && trex_vide && trex_soun );
}

void Interface::SetWidth( uint16_t NewWidth ) {
  if( Width != NewWidth ) {
    Width = NewWidth;
    avcC_vide->SetWidth( Width );
    tkhd_vide->SetWidth( Width );
  }
}

void Interface::SetHeight( uint16_t NewHeight ) {
  if( Height != NewHeight ) {
    Height = NewHeight;
    avcC_vide->SetHeight( Height );
    tkhd_vide->SetHeight( Height );
  }
}

void Interface::SetDurationTime( uint32_t NewDuration, uint32_t Track ) {
  if( Duration.size() < Track ) { Duration.resize(Track+1); }
  if( Duration[Track] != NewDuration ) {
    Duration[Track] = NewDuration;
    switch( Track ) {
      case 0:
        mvhd->SetDurationTime( Duration[Track] );
        break;
      case 1:
        mdhd_vide->SetDurationTime( Duration[Track] );
        tkhd_vide->SetDurationTime( Duration[Track] );
        break;
      case 2:
        mdhd_soun->SetDurationTime( Duration[Track] );
        tkhd_soun->SetDurationTime( Duration[Track] );
        break;
      default:
        fprintf( stderr, "WARNING, Setting Duration for track %d does have any effect\n", Track );
        break;
    }
  }
}
void Interface::SetTimeScale( uint32_t NewUnitsPerSecond, uint32_t Track ) {
  if( UnitsPerSecond.size() < Track ) { UnitsPerSecond.resize(Track+1); }
  if( UnitsPerSecond[Track] != NewUnitsPerSecond ) {
    UnitsPerSecond[Track] = NewUnitsPerSecond;
    switch(Track) {
      case 0:
        mvhd->SetTimeScale( UnitsPerSecond[Track] );
        break;
      case 1:
        mdhd_vide->SetTimeScale( UnitsPerSecond[Track] );
        break;
      case 2:
        mdhd_soun->SetTimeScale( UnitsPerSecond[Track] );
        break;
      default:
        fprintf( stderr, "WARNING, Setting Timescale for track %d does have any effect\n", Track );
        break;
    }
  }
}

void Interface::SetStaticDefaults() {
//  'vide' = 0x76696465
  hdlr_vide->SetHandlerType( 0x76696465 );
  hdlr_vide->SetName( "Video Track" );
//  'soun' = 0x736F756E
  hdlr_soun->SetHandlerType( 0x736F756E );
  hdlr_vide->SetName( "Audio Track" );
//  Set Track ID's
  tkhd_vide->SetTrackID( 1 );
  tkhd_soun->SetTrackID( 2 );
//  Set amhp entry
  amhp->AddEntry( 1, 0, 0 );
}

void Interface::AddSTTSEntry( uint32_t SampleCount, uint32_t SampleDelta, uint32_t Track ) {
  stts_record temp;
  temp.SampleCount = SampleCount;
  temp.SampleDelta = SampleDelta;
  switch(Track) {
    case 1:
      sttsvide.push_back(temp);
      break;
    case 2:
      sttssoun.push_back(temp);
      break;
    default:
      fprintf( stderr, "WARNING: Track %d does not exist, STTS not added\n", Track );
      break;
  }
}

void Interface::EmptySTTS( uint32_t Track ) {
  switch(Track) {
    case 1:
      sttsvide.clear();
      break;
    case 2:
      sttssoun.clear();
      break;
    default:
      fprintf( stderr, "WARNING: Track %d does not exist, STTS not cleared\n", Track );
      break;
  }
}

void Interface::WriteSTTS( uint32_t Track ) {
  switch( Track ) {
    case 1:
      for( int i = sttsvide.size() -1; i > 0; i -- ) {
        stts_vide->AddEntry(sttsvide[i].SampleCount,sttsvide[i].SampleDelta,i);
      }
      break;
    case 2:
      for( int i = sttssoun.size() -1; i > 0; i -- ) {
        stts_soun->AddEntry(sttssoun[i].SampleCount,sttssoun[i].SampleDelta,i);
      }
      break;
    default:
      fprintf( stderr, "WARNING: Track %d does not exist, STTS not written\n", Track );
      break;
  }
}

void Interface::AddSTSCEntry( uint32_t FirstChunk, uint32_t SamplesPerChunk, uint32_t Track ) {
  stsc_record temp;
  temp.FirstChunk = FirstChunk;
  temp.SamplesPerChunk = SamplesPerChunk;
  temp.SampleDescIndex = 1;
  switch(Track) {
    case 1:
      stscvide.push_back(temp);
      break;
    case 2:
      stscsoun.push_back(temp);
      break;
    default:
      fprintf( stderr, "WARNING: Track %d does not exist, STSC not added\n", Track );
      break;
  }
}

void Interface::EmptySTSC( uint32_t Track ) {
  switch(Track) {
    case 1:
      stscvide.clear();
      break;
    case 2:
      stscsoun.clear();
      break;
    default:
      fprintf( stderr, "WARNING: Track %d does not exist, STSC not cleared\n", Track );
      break;
  }
}

void Interface::WriteSTSC( uint32_t Track ) {
  switch( Track ) {
    case 1:
      for( int i = stscvide.size() -1; i > 0; i -- ) {
        stsc_vide->AddEntry(stscvide[i].FirstChunk,stscvide[i].SamplesPerChunk,1,i);
      }
      break;
    case 2:
      for( int i = stscsoun.size() -1; i > 0; i -- ) {
        stsc_soun->AddEntry(stscsoun[i].FirstChunk,stscsoun[i].SamplesPerChunk,1,i);
      }
      break;
    default:
      fprintf( stderr, "WARNING: Track %d does not exist, STSC not written\n", Track );
      break;
  }
}

void Interface::SetOffsets( std::vector<uint32_t> NewOffsets, uint32_t Track ) {
  switch( Track ) {
    case 1:
      stco_vide->SetOffsets( NewOffsets );
      break;
    case 2:
      stco_soun->SetOffsets( NewOffsets );
      break;
    default:
      fprintf( stderr, "WARNING: Track %d does not exist, Offsets not written\n", Track );
      break;
  }
}
