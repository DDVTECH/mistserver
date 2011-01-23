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
    void SetDurationTime( uint32_t NewDuration );
    void SetTimeScale( uint32_t NewUnitsPerSecond );
    void AddSTTSEntry( uint32_t SampleCount, uint32_t SampleDelta );
    void EmptySTTS( );
  private:
    void SetStaticDefaults();
    void UpdateContents();
    void WriteSTTS( );
    bool AllBoxesExist();
    uint16_t Width;
    uint16_t Height;
    uint32_t Duration;
    uint32_t UnitsPerSecond;
    std::vector<stts_record> stts;
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
};//Interface class

