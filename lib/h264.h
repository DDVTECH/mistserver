#include <deque>
#include <string>

#include "nal.h"

namespace h264 {

  std::deque<nalu::nalData> analysePackets(const char * data, unsigned long len);

  ///Struct containing pre-calculated metadata of an SPS nal unit. Width and height in pixels, fps in Hz
  struct SPSMeta {
    unsigned int width;
    unsigned int height;
    double fps;
    uint8_t profile;
    uint8_t level;
    bool sep_col_plane;
    uint8_t cnt_type;
    bool gaps;///< Gaps in frame num allowed flag
    bool mbs_only;///<MBS only flag
    uint16_t log2_max_frame_num;
    uint16_t log2_max_order_cnt;
    uint16_t max_ref_frames;///<Maximum number of reference frames
  };

  ///Class for analyzing generic nal units
  class NAL {
    public:
      NAL();
      NAL(std::string & InputData);
      bool ReadData(std::string & InputData, bool raw = false);
      std::string AnnexB(bool LongIntro = false);
      std::string SizePrepended();
      int Type();
      std::string getData();
    protected:
      unsigned int chroma_format_idc;///<the value of chroma_format_idc
      std::string MyData;///<The h264 nal unit data
  };
  //NAL class

  ///Special instance of NAL class for analyzing SPS nal units
  class SPS: public NAL {
    public:
      SPS(): NAL() {};
      SPS(std::string & InputData, bool raw = false);
      SPSMeta getCharacteristics();
      void analyzeSPS();
  };

  ///Special instance of NAL class for analyzing PPS nal units
  class PPS: public NAL {
    public:
      PPS(): NAL() {};
      PPS(std::string & InputData): NAL(InputData) {};
      void analyzePPS();
  };


  class sequenceParameterSet {
    public:
      sequenceParameterSet(const char * _data = NULL, unsigned long _dataLen = 0);
      void fromDTSCInit(const std::string & dtscInit);
      SPSMeta getCharacteristics() const;
    private:
      const char * data;
      unsigned long dataLen;
  };

}
