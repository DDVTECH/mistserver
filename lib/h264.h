#pragma once
#include <deque>
#include <string>
#include <cstdlib>
#include <cstdio>

#include "nal.h"
#include "bitfields.h"
#include "bitstream.h"

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
      sequenceParameterSet(const char * _data = NULL, size_t _dataLen = 0);
      void fromDTSCInit(const std::string & dtscInit);
      SPSMeta getCharacteristics() const;
    private:
      const char * data;
      size_t dataLen;
  };

  bool isKeyframe(const char * data, uint32_t len);

  class nalUnit {
    public:
      nalUnit(const char * data, size_t len) : payload(data, len) {}
      uint8_t getType() { return payload[0] & 0x1F; }
      uint32_t getSize(){return payload.size();}
      virtual void toPrettyString(std::ostream & out){
        out << "Nal unit of type " << (((uint8_t)payload[0]) & 0x1F) << ", " << payload.size() << " bytes long" << std::endl;
      }
      void write(std::ostream & out){
        //always writes in annex_b style
        out.write("\000\000\000\001", 4);
        out.write(payload.data(), payload.size());
      }
      virtual std::string generate() { return ""; }
      virtual void setSPSNumber(size_t newNumber) {}
      virtual void setPPSNumber(size_t newNumber) {}
    protected:
      std::string payload;
  };

  class hrd_parameters {
    public:
      hrd_parameters() {}
      hrd_parameters(Utils::bitstream & bs);
      void toPrettyString(std::ostream & out, size_t indent = 0);
      
      uint64_t cpbCntMinus1;
      uint8_t bitRateScale;
      uint8_t cpbSizeScale;
  };

  class vui_parameters {
    public:
      vui_parameters() {};
      vui_parameters(Utils::bitstream & bs);
      void generate(Utils::bitWriter & bw);
      void toPrettyString(std::ostream & out, size_t indent = 0);
      
      bool aspectRatioInfoPresentFlag;
      uint8_t aspectRatioIdc;
      uint16_t sarWidth;
      uint16_t sarHeight;
      bool overscanInfoPresentFlag;
      bool overscanAppropriateFlag;
      bool videoSignalTypePresentFlag;
      uint8_t videoFormat;
      bool videoFullRangeFlag;
      bool colourDescriptionPresentFlag;
      uint8_t colourPrimaries;
      uint8_t transferCharacteristics;
      uint8_t matrixCoefficients;
      bool chromaLocInfoPresentFlag;
      uint64_t chromaSampleLocTypeTopField;
      uint64_t chromaSampleLocTypeBottomField;
      bool timingInfoPresentFlag;
      uint32_t numUnitsInTick;
      uint32_t timeScale;
      bool fixedFrameRateFlag;
      bool nalHrdParametersPresentFlag;

      bool vclHrdParametersPresentFlag;

      bool lowDelayHrdFlag;
      bool picStructPresentFlag;
      bool bitstreamRestrictionFlag;
      bool motionVectorsOverPicBoundariesFlag;
      uint64_t maxBytesPerPicDenom;
      uint64_t maxBitsPerMbDenom;
      uint64_t log2MaxMvLengthHorizontal;
      uint64_t log2MaxMvLengthVertical;
      uint64_t numReorderFrames;
      uint64_t maxDecFrameBuffering;
  };

  class spsUnit : public nalUnit {
    public:
      spsUnit(const char * data, size_t len);
      ~spsUnit(){
        if (scalingListPresentFlags != NULL){
          free(scalingListPresentFlags);
        }
      }
      std::string generate();
      void toPrettyString(std::ostream & out);
      void scalingList(uint64_t * scalingList, size_t sizeOfScalingList, bool & useDefaultScalingMatrixFlag, Utils::bitstream & bs);
      void setSPSNumber(size_t newNumber);
      uint8_t profileIdc;
      bool constraintSet0Flag;
      bool constraintSet1Flag;
      bool constraintSet2Flag;
      bool constraintSet3Flag;
      bool constraintSet4Flag;
      bool constraintSet5Flag;
      uint8_t levelIdc;
      uint64_t seqParameterSetId;
      
      uint64_t chromaFormatIdc;
      bool separateColourPlaneFlag;
      uint64_t bitDepthLumaMinus8;
      uint64_t bitDepthChromaMinus8;
      bool qpprimeYZeroTransformBypassFlag;
      bool seqScalingMatrixPresentFlag;
      //Here go scaling lists
      uint8_t * scalingListPresentFlags;

      uint64_t ** scalingList4x4;
      bool * useDefaultScalingMatrix4x4Flag;
      uint64_t ** scalingList8x8;
      bool * useDefaultScalingMatrix8x8Flag;


      uint64_t log2MaxFrameNumMinus4;
      uint64_t picOrderCntType;
      uint64_t log2MaxPicOrderCntLsbMinus4;
      
      //Here go values for pic_order_cnt_type == 1
      
      uint64_t maxNumRefFrames;
      bool gapsInFrameNumValueAllowedFlag;
      uint64_t picWidthInMbsMinus1; 
      uint64_t picHeightInMapUnitsMinus1; 
      bool frameMbsOnlyFlag;
      bool mbAdaptiveFrameFieldFlag;
      bool direct8x8InferenceFlag;
      bool frameCroppingFlag;
      uint64_t frameCropLeftOffset;
      uint64_t frameCropRightOffset;
      uint64_t frameCropTopOffset;
      uint64_t frameCropBottomOffset;
      bool vuiParametersPresentFlag;

      vui_parameters vuiParams;

      //DERIVATIVE VALUES
      uint8_t derived_subWidthC;
      uint8_t derived_subHeightC;
      uint8_t derived_mbWidthC;
      uint8_t derived_mbHeightC;
      uint64_t derived_bitDepth_Y; 
      uint64_t derived_qpBdOffset_Y; 
      uint64_t derived_bitDepth_C; 
      uint64_t derived_qpBdOffset_C; 
      uint64_t derived_rawMbBits;
      uint64_t derived_maxFrameNum;
      uint64_t derived_maxPicOrderCntLsb;
      uint64_t derived_picWidthInMbs;
      uint64_t derived_picWidthInSamples_L;
      uint64_t derived_picWidthInSamples_C;
      uint64_t derived_picHeightInMapUnits;
      uint64_t derived_picSizeInMapUnits;
      uint64_t derived_frameHeightInMbs;
      size_t derived_scalingListSize;
      size_t derived_scalingList4x4Amount;
      size_t derived_scalingList8x8Amount;
  };
  class ppsUnit : public nalUnit {
    public:
      ppsUnit(const char * data, size_t len, uint8_t chromaFormatIdc = 0);
      ~ppsUnit(){
        if (picScalingMatrixPresentFlags != NULL){
          free(picScalingMatrixPresentFlags);
        }
      }
      void scalingList(uint64_t * scalingList, size_t sizeOfScalingList, bool & useDefaultScalingMatrixFlag, Utils::bitstream & bs);
      void setPPSNumber(size_t newNumber);
      void setSPSNumber(size_t newNumber);
      void toPrettyString(std::ostream & out);
      std::string generate();
      
      uint64_t picParameterSetId;
      uint64_t seqParameterSetId;
      bool entropyCodingModeFlag;
      bool bottomFieldPicOrderInFramePresentFlag;
      uint64_t numSliceGroupsMinus1;
      uint64_t numrefIdx10DefaultActiveMinus1;
      uint64_t numrefIdx11DefaultActiveMinus1;
      bool weightedPredFlag;
      uint8_t weightedBipredIdc;
      int64_t picInitQpMinus26;
      int64_t picInitQsMinus26;
      int64_t chromaQpIndexOffset;
      bool deblockingFilterControlPresentFlag;
      bool constrainedIntraPredFlag;
      bool redundantPicCntPresentFlag;
      bool transform8x8ModeFlag;
      bool picScalingMatrixPresentFlag;
      //Here go scaling lists
      uint8_t * picScalingMatrixPresentFlags;

      uint64_t ** scalingList4x4;
      bool * useDefaultScalingMatrix4x4Flag;
      uint64_t ** scalingList8x8;
      bool * useDefaultScalingMatrix8x8Flag;

      int64_t secondChromaQpIndexOffset;

      size_t derived_scalingListSize;
      size_t derived_scalingList4x4Amount;
      size_t derived_scalingList8x8Amount;

      bool status_moreRBSP;
  };
  class codedSliceUnit : public nalUnit {
    public:
      codedSliceUnit(const char * data, size_t len);
      void setPPSNumber(size_t newNumber);
      void toPrettyString(std::ostream & out);

      uint64_t firstMbInSlice;
      uint64_t sliceType;
      uint64_t picParameterSetId;
  };

  class seiUnit : public nalUnit {
    public:
      seiUnit(const char * data, size_t len);
      void toPrettyString(std::ostream & out);

      uint32_t payloadType;
      uint32_t payloadSize;
  };



  nalUnit * nalFactory(FILE * in, bool annexb = true);
  nalUnit * nalFactory(const char * data, size_t len, size_t & offset, bool annexb = true);
}
