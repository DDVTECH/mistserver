#include "interface.h"

Interface::Interface() {
  ftyp = new Box_ftyp();
  moov = new Box_moov();
  mvhd = new Box_mvhd();
  trak_vide = new Box_trak();
  tkhd_vide = new Box_tkhd();
  mdia_vide = new Box_mdia();
  mdhd_vide = new Box_mdhd();
  hdlr_vide = new Box_hdlr();
  minf_vide = new Box_minf();
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
  dinf_soun = new Box_dinf();
  dref_soun = new Box_dref();
  url_soun = new Box_url();
  stbl_soun = new Box_stbl();
  stts_soun = new Box_stts();
  stsc_soun = new Box_stsc();
  stco_soun = new Box_stco();
  stsd_soun = new Box_stsd();
  esds_soun = new Box_esds();
}

Interface::~Interface() {
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
