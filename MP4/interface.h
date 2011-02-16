#include "box_includes.h"

class Interface {
  public:
    Interface();
    ~Interface();
    void link();
    uint32_t GetContentSize();
    uint8_t * GetContents();

    void SetWidth( uint16_t NewWidth );
    void SetHeight( uint16_t NewHeight );
    void SetDurationTime( uint32_t NewDuration, uint32_t Track );
    void SetTimeScale( uint32_t NewUnitsPerSecond, uint32_t Track );
    void AddSTTSEntry( uint32_t SampleCount, uint32_t SampleDelta, uint32_t Track );
    void EmptySTTS( uint32_t Track );
    void AddSTSCEntry( uint32_t FirstChunk, uint32_t SamplesPerChunk, uint32_t Track );
    void EmptySTSC( uint32_t Track );
    void SetOffsets( std::vector<uint32_t> NewOffsets, uint32_t Track );
    void SetData( uint8_t * Data, uint32_t DataSize );

    std::string GenerateLiveBootstrap( uint32_t CurMediaTime );
  private:
    void SetStaticDefaults();
    void UpdateContents();
    void WriteSTTS( uint32_t Track );
    void WriteSTSC( uint32_t Track );
    bool AllBoxesExist();
    uint16_t Width;
    uint16_t Height;
    std::vector<uint32_t> Duration;
    std::vector<uint32_t> UnitsPerSecond;
    std::vector<stts_record> sttsvide;
    std::vector<stts_record> sttssoun;
    std::vector<stsc_record> stscvide;
    std::vector<stsc_record> stscsoun;
    Box_ftyp * ftyp;
    Box_moov * moov;
    Box_mvhd * mvhd;
    Box_trak * trak_vide;
    Box_tkhd * tkhd_vide;
    Box_mdia * mdia_vide;
    Box_mdhd * mdhd_vide;
    Box_hdlr * hdlr_vide;
    Box_minf * minf_vide;
    Box_vmhd * vmhd_vide;
    Box_dinf * dinf_vide;
    Box_dref * dref_vide;
    Box_url * url_vide;
    Box_stbl * stbl_vide;
    Box_stts * stts_vide;
    Box_stsc * stsc_vide;
    Box_stco * stco_vide;
    Box_stsd * stsd_vide;
    Box_avcC * avcC_vide;
    Box_trak * trak_soun;
    Box_tkhd * tkhd_soun;
    Box_mdia * mdia_soun;
    Box_mdhd * mdhd_soun;
    Box_hdlr * hdlr_soun;
    Box_minf * minf_soun;
    Box_smhd * smhd_soun;
    Box_dinf * dinf_soun;
    Box_dref * dref_soun;
    Box_url * url_soun;
    Box_stbl * stbl_soun;
    Box_stts * stts_soun;
    Box_stsc * stsc_soun;
    Box_stco * stco_soun;
    Box_stsd * stsd_soun;
    Box_esds * esds_soun;
    Box_rtmp * rtmp;
    Box_amhp * amhp;
    Box_mvex * mvex;
    Box_trex * trex_vide;
    Box_trex * trex_soun;
    Box_afra * afra;
    Box_abst * abst;
    Box_asrt * asrt;
    Box_afrt * afrt;
    Box_moof * moof;
    Box_mfhd * mfhd;
    Box_traf * traf_vide;
    Box_tfhd * tfhd_vide;
    Box_trun * trun_vide;
    Box_traf * traf_soun;
    Box_tfhd * tfhd_soun;
    Box_trun * trun_soun;
};//Interface class

