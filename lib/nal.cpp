#include "nal.h"
#include "bitstream.h"
#include "defines.h"
#include <iostream>
#include <iomanip>
#include <math.h>//for log
namespace h264 {

  ///empty constructor of NAL
  NAL::NAL() {

  }

  ///Constructor capable of directly inputting NAL data from a string
  ///\param InputData the nal input data, as a string
  NAL::NAL(std::string & InputData) {
    ReadData(InputData);
  }

  ///Gets the raw NAL unit data, as a string
  ///\return the raw NAL unit data
  std::string NAL::getData() {
    return MyData;
  }

  ///Reads data from a string,either raw or annexed
  ///\param InputData the h264 data, as string
  ///\param raw a boolean that determines whether the string contains raw h264 data or not
  bool NAL::ReadData(std::string & InputData, bool raw) {
    if (raw) {
      MyData = InputData;
      InputData.clear();
      return true;
    }
    std::string FullAnnexB;
    FullAnnexB += (char)0x00;
    FullAnnexB += (char)0x00;
    FullAnnexB += (char)0x00;
    FullAnnexB += (char)0x01;
    std::string ShortAnnexB;
    ShortAnnexB += (char)0x00;
    ShortAnnexB += (char)0x00;
    ShortAnnexB += (char)0x01;
    if (InputData.size() < 3) {
      //DEBUG_MSG(DLVL_DEVEL, "fal1");
      return false;
    }
    bool AnnexB = false;
    if (InputData.substr(0, 3) == ShortAnnexB) {

      AnnexB = true;
    }
    if (InputData.substr(0, 4) == FullAnnexB) {
      InputData.erase(0, 1);
      AnnexB = true;
    }
    if (AnnexB) {
      MyData = "";
      InputData.erase(0, 3); //Intro Bytes
      int Location = std::min(InputData.find(ShortAnnexB), InputData.find(FullAnnexB));
      MyData = InputData.substr(0, Location);
      InputData.erase(0, Location);
    } else {
      if (InputData.size() < 4) {
        DEBUG_MSG(DLVL_DEVEL, "fal2");
        return false;
      }
      unsigned int UnitLen = (InputData[0] << 24) + (InputData[1] << 16) + (InputData[2] << 8) + InputData[3];
      if (InputData.size() < 4 + UnitLen) {
        DEBUG_MSG(DLVL_DEVEL, "fal3");
        return false;
      }
      InputData.erase(0, 4); //Remove Length
      MyData = InputData.substr(0, UnitLen);
      InputData.erase(0, UnitLen); //Remove this unit from the string


    }
    //DEBUG_MSG(DLVL_DEVEL, "tru");
    return true;
  }

  ///Returns an annex B prefix
  ///\param LongIntro determines whether it is a short or long annex B
  ///\return the desired annex B prefix
  std::string NAL::AnnexB(bool LongIntro) {
    std::string Result;
    if (MyData.size()) {
      if (LongIntro) {
        Result += (char)0x00;
      }
      Result += (char)0x00;
      Result += (char)0x00;
      Result += (char)0x01; //Annex B Lead-In
      Result += MyData;
    }
    return Result;
  }

  ///Returns raw h264 data as Size Prepended
  ///\return the h264 data as Size prepended
  std::string NAL::SizePrepended() {
    std::string Result;
    if (MyData.size()) {
      int DataSize = MyData.size();
      Result += (char)((DataSize & 0xFF000000) >> 24);
      Result += (char)((DataSize & 0x00FF0000) >> 16);
      Result += (char)((DataSize & 0x0000FF00) >> 8);
      Result += (char)(DataSize & 0x000000FF); //Size Lead-In
      Result += MyData;
    }
    return Result;
  }

  ///returns the nal unit type
  ///\return the nal unit type
  int NAL::Type() {
    return (MyData[0] & 0x1F);
  }

  SPS::SPS(std::string & input, bool raw) : NAL() {
    ReadData(input, raw);
  }

  ///computes SPS data from an SPS nal unit, and saves them in a useful
  ///more human-readable format in the parameter spsmeta
  ///The function is based on the analyzeSPS() function. If more data needs to be stored in sps meta,
  ///refer to that function to determine which variable comes at which place (as all couts have been removed).
  ///\param spsmeta the sps metadata, in which data from the sps is stored
  ///\todo some h264 sps data types are not supported (due to them containing matrixes and have never been encountered in practice). If needed, these need to be implemented
  SPSMeta SPS::getCharacteristics() {
    SPSMeta result;

    //For calculating width
    unsigned int widthInMbs = 0;
    unsigned int cropHorizontal = 0;
    
    //For calculating height
    bool mbsOnlyFlag = 0;
    unsigned int heightInMapUnits = 0; 
    unsigned int cropVertical = 0;

    Utils::bitstream bs;
    for (unsigned int i = 1; i < MyData.size(); i++) {
      if (i + 2 < MyData.size() && MyData.substr(i, 3) == std::string("\000\000\003", 3)) {
        bs <<  MyData.substr(i, 2);
        i += 2;
      } else {
        bs << MyData.substr(i, 1);
      }
    }

    char profileIdc = bs.get(8);
    //Start skipping unused data
    bs.skip(16);
    bs.getUExpGolomb();
    if (profileIdc == 100 || profileIdc == 110 || profileIdc == 122 || profileIdc == 244 || profileIdc == 44 || profileIdc == 83 || profileIdc == 86 || profileIdc == 118 || profileIdc == 128) {
      //chroma format idc
      if (bs.getUExpGolomb() == 3) {
        bs.skip(1);
      }
      bs.getUExpGolomb();
      bs.getUExpGolomb();
      bs.skip(1);
      if (bs.get(1)) {
        DEBUG_MSG(DLVL_DEVEL, "Scaling matrix not implemented yet");
      }
    }
    bs.getUExpGolomb();
    unsigned int pic_order_cnt_type = bs.getUExpGolomb();
    if (!pic_order_cnt_type) {
      bs.getUExpGolomb();
    } else if (pic_order_cnt_type == 1) {
      DEBUG_MSG(DLVL_DEVEL, "This part of the implementation is incomplete(2), to be continued. If this message is shown, contact developers immediately.");
    }
    bs.getUExpGolomb();
    bs.skip(1);
    //Stop skipping data and start doing usefull stuff

    
    widthInMbs = bs.getUExpGolomb() + 1;
    heightInMapUnits = bs.getUExpGolomb() + 1;

    mbsOnlyFlag = bs.get(1);//Gets used in height calculation
    if (!mbsOnlyFlag) {
      bs.skip(1);
    }
    bs.skip(1);
    //cropping flag
    if (bs.get(1)) {
      cropHorizontal = bs.getUExpGolomb();//leftOffset
      cropHorizontal += bs.getUExpGolomb();//rightOffset
      cropVertical = bs.getUExpGolomb();//topOffset
      cropVertical += bs.getUExpGolomb();//bottomOffset
    }

    //vuiParameters
    if (bs.get(1)) {
      //Skipping all the paramters we dont use
      if (bs.get(1)){
        if (bs.get(8) == 255) {
          bs.skip(32);
        }
      }
      if (bs.get(1)) {
        bs.skip(1);
      }
      if (bs.get(1)) {
        bs.skip(4);
        if (bs.get(1)) {
          bs.skip(24);
        }
      }
      if (bs.get(1)) {
        bs.getUExpGolomb();
        bs.getUExpGolomb();
      }

      //Decode timing info
      if (bs.get(1)) {
        unsigned int unitsInTick = bs.get(32);
        unsigned int timeScale = bs.get(32);
        result.fps = (double)timeScale / (2 * unitsInTick);
        bs.skip(1);
      }
    }

    result.width = (widthInMbs * 16) - (cropHorizontal * 2);
    result.height = ((mbsOnlyFlag ? 1 : 2) * heightInMapUnits * 16) - (cropVertical * 2);
    return result;
  }

  ///Analyzes an SPS nal unit, and prints the values of all existing fields
  ///\todo some h264 sps data types are not supported (due to them containing matrixes and have not yet been encountered in practice). If needed, these need to be implemented
  void SPS::analyzeSPS() {
    if (Type() != 7) {
      DEBUG_MSG(DLVL_DEVEL, "This is not an SPS, but type %d", Type());
      return;
    }
    Utils::bitstream bs;
    //put rbsp bytes in mydata
    for (unsigned int i = 1; i < MyData.size(); i++) {
      //DEBUG_MSG(DLVL_DEVEL, "position %u out of %lu",i,MyData.size());
      if (i + 2 < MyData.size() && MyData.substr(i, 3) == std::string("\000\000\003", 3)) {
        bs <<  MyData.substr(i, 2);
        //DEBUG_MSG(DLVL_DEVEL, "0x000003 encountered at i = %u",i);
        i += 2;
      } else {
        bs << MyData.substr(i, 1);
      }
    }
    //bs contains all rbsp bytes, now we can analyze them
    std::cout << "seq_parameter_set_data()" << std::endl;
    std::cout << std::hex  << std::setfill('0') << std::setw(2);
    char profileIdc = bs.get(8);
    std::cout << "profile idc: " << (unsigned int) profileIdc << std::endl;
    std::cout << "constraint_set0_flag: " << bs.get(1) << std::endl;
    std::cout << "constraint_set1_flag: " << bs.get(1) << std::endl;
    std::cout << "constraint_set2_flag: " << bs.get(1) << std::endl;
    std::cout << "constraint_set3_flag: " << bs.get(1) << std::endl;
    std::cout << "constraint_set4_flag: " << bs.get(1) << std::endl;
    std::cout << "constraint_set5_flag: " << bs.get(1) << std::endl;
    std::cout << "reserved_zero_2bits: " << bs.get(2) << std::endl;
    std::cout << "level idc: " << bs.get(8) << std::endl;
    std::cout << "seq_parameter_set_id: " << bs.getUExpGolomb() << std::endl;
    if (profileIdc == 100 || profileIdc == 110 || profileIdc == 122 || profileIdc == 244 || profileIdc == 44 || profileIdc == 83 || profileIdc == 86 || profileIdc == 118 || profileIdc == 128) {
      chroma_format_idc = bs.getUExpGolomb();
      std::cout << "chroma_format_idc: " << chroma_format_idc << std::endl;
      if (chroma_format_idc == 3) {
        std::cout << "separate_colour_plane_flag" << bs.get(1) << std::endl;
      }
      std::cout << "bit_depth_luma_minus8: " << bs.getUExpGolomb() << std::endl;
      std::cout << "bit_depth_chroma_minus8: " << bs.getUExpGolomb() << std::endl;
      std::cout << "qpprime_y_zero_transform_bypass_flag: " << bs.get(1) << std::endl;
      unsigned int seq_scaling_matrix_present_flag = bs.get(1);
      std::cout << "seq_scaling_matrix_present_flag: " << seq_scaling_matrix_present_flag << std::endl;
      if (seq_scaling_matrix_present_flag) {
        for (unsigned int i = 0; i < ((chroma_format_idc != 3) ? 8 : 12) ; i++) {
          unsigned int seq_scaling_list_present_flag = bs.get(1);
          std::cout << "seq_scaling_list_present_flag: " << seq_scaling_list_present_flag << std::endl;
          DEBUG_MSG(DLVL_DEVEL, "not implemented, ending");
          return;
          if (seq_scaling_list_present_flag) {
            //LevelScale4x4( m, i, j ) = weightScale4x4( i, j ) * normAdjust4x4( m, i, j )
            //
            if (i < 6) {
              //scaling)list(ScalingList4x4[i],16,UseDefaultScalingMatrix4x4Flag[i]
            } else {
              //scaling)list(ScalingList4x4[i-6],64,UseDefaultScalingMatrix4x4Flag[i-6]

            }
          }
        }
      }
    }
    std::cout << "log2_max_frame_num_minus4: " << bs.getUExpGolomb() << std::endl;
    unsigned int pic_order_cnt_type = bs.getUExpGolomb();
    std::cout << "pic_order_cnt_type: " << pic_order_cnt_type << std::endl;
    if (pic_order_cnt_type == 0) {
      std::cout << "log2_max_pic_order_cnt_lsb_minus4: " << bs.getUExpGolomb() << std::endl;
    } else if (pic_order_cnt_type == 1) {
      DEBUG_MSG(DLVL_DEVEL, "This part of the implementation is incomplete(2), to be continued. If this message is shown, contact developers immediately.");
      return;
    }
    std::cout << "max_num_ref_frames: " << bs.getUExpGolomb() << std::endl;
    std::cout << "gaps_in_frame_num_allowed_flag: " << bs.get(1) << std::endl;
    std::cout << "pic_width_in_mbs_minus1: " << bs.getUExpGolomb() << std::endl;
    std::cout << "pic_height_in_map_units_minus1: " << bs.getUExpGolomb() << std::endl;
    unsigned int frame_mbs_only_flag = bs.get(1);
    std::cout << "frame_mbs_only_flag: " << frame_mbs_only_flag << std::endl;
    if (frame_mbs_only_flag == 0) {
      std::cout << "mb_adaptive_frame_field_flag: " << bs.get(1) << std::endl;
    }
    std::cout << "direct_8x8_inference_flag: " << bs.get(1) << std::endl;
    unsigned int frame_cropping_flag = bs.get(1);
    std::cout << "frame_cropping_flag: " << frame_cropping_flag << std::endl;
    if (frame_cropping_flag != 0) {
      std::cout << "frame_crop_left_offset: " << bs.getUExpGolomb() << std::endl;
      std::cout << "frame_crop_right_offset: " << bs.getUExpGolomb() << std::endl;
      std::cout << "frame_crop_top_offset: " << bs.getUExpGolomb() << std::endl;
      std::cout << "frame_crop_bottom_offset: " << bs.getUExpGolomb() << std::endl;
    }
    unsigned int vui_parameters_present_flag = bs.get(1);
    std::cout << "vui_parameters_present_flag: " << vui_parameters_present_flag << std::endl;
    if (vui_parameters_present_flag != 0) {
      //vuiParameters
      unsigned int aspect_ratio_info_present_flag = bs.get(1);
      std::cout << "aspect_ratio_info_present_flag: " << aspect_ratio_info_present_flag << std::endl;
      if (aspect_ratio_info_present_flag != 0) {
        unsigned int aspect_ratio_idc = bs.get(8);
        std::cout << "aspect_ratio_idc: " << aspect_ratio_idc << std::endl;
        if (aspect_ratio_idc == 255) {
          std::cout << "sar_width: " << bs.get(16) << std::endl;
          std::cout << "sar_height: " << bs.get(16) << std::endl;
        }
      }
      unsigned int overscan_info_present_flag = bs.get(1);
      std::cout << "overscan_info_present_flag: " << overscan_info_present_flag << std::endl;
      if (overscan_info_present_flag) {
        std::cout << "overscan_appropriate_flag: " << bs.get(1) << std::endl;
      }

      unsigned int video_signal_type_present_flag = bs.get(1);
      std::cout << "video_signal_type_present_flag: " << video_signal_type_present_flag << std::endl;
      if (video_signal_type_present_flag) {
        std::cout << "video_format: " << bs.get(3) << std::endl;
        std::cout << "video_full_range_flag: " << bs.get(1) << std::endl;
        unsigned int colour_description_present_flag = bs.get(1);
        std::cout << "colour_description_present_flag: " << colour_description_present_flag << std::endl;
        if (colour_description_present_flag) {
          std::cout << "colour_primaries: " << bs.get(8) << std::endl;
          std::cout << "transfer_characteristics: " << bs.get(8) << std::endl;
          std::cout << "matrix_coefficients: " << bs.get(8) << std::endl;
        }
      }
      unsigned int chroma_loc_info_present_flag = bs.get(1);
      std::cout << "chroma_loc_info_present_flag: " << chroma_loc_info_present_flag << std::endl;
      if (chroma_loc_info_present_flag) {
        std::cout << "chroma_sample_loc_type_top_field: " << bs.getUExpGolomb() << std::endl;
        std::cout << "chroma_sample_loc_type_bottom_field: " << bs.getUExpGolomb() << std::endl;
      }
      unsigned int timing_info_present_flag = bs.get(1);
      std::cout << "timing_info_present_flag: " << timing_info_present_flag << std::endl;
      if (timing_info_present_flag) {
        std::cout << "num_units_in_tick: " << bs.get(32) << std::endl;
        std::cout << "time_scale: " << bs.get(32) << std::endl;
        std::cout << "fixed_frame_rate_flag: " << bs.get(1) << std::endl;
      }
      unsigned int nal_hrd_parameters_present_flag = bs.get(1);
      std::cout << "nal_hrd_parameters_present_flag: " << nal_hrd_parameters_present_flag << std::endl;
      if (nal_hrd_parameters_present_flag) {
        unsigned int cpb_cnt_minus1 = bs.getUExpGolomb();
        std::cout << "cpb_cnt_minus1: " << cpb_cnt_minus1 << std::endl;
        std::cout << "bit_rate_scale: " << bs.get(4) << std::endl;
        std::cout << "cpb_rate_scale: " << bs.get(4) << std::endl;
        for (unsigned int ssi = 0; ssi <= cpb_cnt_minus1 ; ssi++) {
          std::cout << "bit_rate_value_minus1[" << ssi << "]: " << bs.getUExpGolomb() << std::endl;
          std::cout << "cpb_size_value_minus1[" << ssi << "]: " << bs.getUExpGolomb() << std::endl;
          std::cout << "cbr_flag[" << ssi << "]: " << bs.get(1) << std::endl;
        }
        std::cout << "initial_cpb_removal_delay_length_minus1: " << bs.get(5) << std::endl;
        std::cout << "cpb_removal_delay_length_minus1: " << bs.get(5) << std::endl;
        std::cout << "dpb_output_delay_length_minus1: " << bs.get(5) << std::endl;
        std::cout << "time_offset_length: " << bs.get(5) << std::endl;
      }
      unsigned int vcl_hrd_parameters_present_flag = bs.get(1);
      std::cout << "vcl_hrd_parameters_present_flag: " << vcl_hrd_parameters_present_flag << std::endl;
      if (vcl_hrd_parameters_present_flag) {
        unsigned int cpb_cnt_minus1 = bs.getUExpGolomb();
        std::cout << "cpb_cnt_minus1: " << cpb_cnt_minus1 << std::endl;
        std::cout << "bit_rate_scale: " << bs.get(4) << std::endl;
        std::cout << "cpb_rate_scale: " << bs.get(4) << std::endl;
        for (unsigned int ssi = 0; ssi <= cpb_cnt_minus1 ; ssi++) {
          std::cout << "bit_rate_value_minus1[" << ssi << "]: " << bs.getUExpGolomb() << std::endl;
          std::cout << "cpb_size_value_minus1[" << ssi << "]: " << bs.getUExpGolomb() << std::endl;
          std::cout << "cbr_flag[" << ssi << "]: " << bs.get(1) << std::endl;
        }
        std::cout << "initial_cpb_removal_delay_length_minus1: " << bs.get(5) << std::endl;
        std::cout << "cpb_removal_delay_length_minus1: " << bs.get(5) << std::endl;
        std::cout << "dpb_output_delay_length_minus1: " << bs.get(5) << std::endl;
        std::cout << "time_offset_length: " << bs.get(5) << std::endl;
      }
      if (nal_hrd_parameters_present_flag || vcl_hrd_parameters_present_flag) {
        std::cout << "low_delay_hrd_flag: " << bs.get(1) << std::endl;
      }
      std::cout << "pic_struct_present_flag: " << bs.get(1) << std::endl;
      unsigned int bitstream_restriction_flag = bs.get(1);
      std::cout << "bitstream_restriction_flag: " << bitstream_restriction_flag << std::endl;
      if (bitstream_restriction_flag) {
        std::cout << "motion_vectors_over_pic_boundaries_flag: " << bs.get(1) << std::endl;
        std::cout << "max_bytes_per_pic_denom: " << bs.getUExpGolomb() << std::endl;
        std::cout << "max_bits_per_mb_denom: " << bs.getUExpGolomb() << std::endl;
        std::cout << "log2_max_mv_length_horizontal: " << bs.getUExpGolomb() << std::endl;
        std::cout << "log2_max_mv_length_vertical: " << bs.getUExpGolomb() << std::endl;
        std::cout << "num_reorder_frames: " << bs.getUExpGolomb() << std::endl;
        std::cout << "max_dec_frame_buffering: " << bs.getUExpGolomb() << std::endl;
      }
    }
    std::cout << std::dec << std::endl;
    //DEBUG_MSG(DLVL_DEVEL, "SPS analyser");
  }

  ///Prints the values of all the fields of a PPS nal unit in a human readable format.
  ///\todo some features, including analyzable matrices, are not implemented. They were never encountered in practice, so far
  void PPS::analyzePPS() {
    if (Type() != 8) {
      DEBUG_MSG(DLVL_DEVEL, "This is not a PPS, but type %d", Type());
      return;
    }
    Utils::bitstream bs;
    //put rbsp bytes in mydata
    for (unsigned int i = 1; i < MyData.size(); i++) {
      if (i + 2 < MyData.size() && MyData.substr(i, 3) == std::string("\000\000\003", 3)) {
        bs <<  MyData.substr(i, 2);
        i += 2;
      } else {
        bs << MyData.substr(i, 1);
      }
    }
    //bs contains all rbsp bytes, now we can analyze them
    std::cout << "pic_parameter_set_id: " << bs.getUExpGolomb() << std::endl;
    std::cout << "seq_parameter_set_id: " << bs.getUExpGolomb() << std::endl;
    std::cout << "entropy_coding_mode_flag: " << bs.get(1) << std::endl;
    std::cout << "bottom_field_pic_order_in_frame_present_flag: " << bs.get(1) << std::endl;
    unsigned int num_slice_groups_minus1 = bs.getUExpGolomb();
    std::cout << "num_slice_groups_minus1: " << num_slice_groups_minus1 << std::endl;
    if (num_slice_groups_minus1 > 0) {
      unsigned int slice_group_map_type = bs.getUExpGolomb();
      std::cout << "slice_group_map_type: " << slice_group_map_type << std::endl;
      if (slice_group_map_type == 0) {
        for (unsigned int ig = 0; ig <= num_slice_groups_minus1; ig++) {
          std::cout << "runlengthminus1[" << ig << "]: " << bs.getUExpGolomb() << std::endl;
        }
      } else if (slice_group_map_type == 2) {
        for (unsigned int ig = 0; ig <= num_slice_groups_minus1; ig++) {
          std::cout << "top_left[" << ig << "]: " << bs.getUExpGolomb() << std::endl;
          std::cout << "bottom_right[" << ig << "]: " << bs.getUExpGolomb() << std::endl;
        }
      } else if (slice_group_map_type == 3 || slice_group_map_type == 4 || slice_group_map_type == 5) {
        std::cout << "slice_group_change_direction_flag: " << bs.get(1) << std::endl;
        std::cout << "slice_group_change_rate_minus1: " << bs.getUExpGolomb() << std::endl;
      } else if (slice_group_map_type == 6) {
        unsigned int pic_size_in_map_units_minus1 = bs.getUExpGolomb();
        std::cout << "pic_size_in_map_units_minus1: " << pic_size_in_map_units_minus1 << std::endl;
        for (unsigned int i = 0; i <= pic_size_in_map_units_minus1; i++) {
          std::cout << "slice_group_id[" << i << "]: " << bs.get((unsigned int)(ceil(log(num_slice_groups_minus1 + 1) / log(2)))) << std::endl;
        }
      }
    }
    std::cout << "num_ref_idx_l0_default_active_minus1: " << bs.getUExpGolomb() << std::endl;
    std::cout << "num_ref_idx_l1_default_active_minus1: " << bs.getUExpGolomb() << std::endl;
    std::cout << "weighted_pred_flag: " << bs.get(1) << std::endl;
    std::cout << "weighted_bipred_idc: " << bs.get(2) << std::endl;
    std::cout << "pic_init_qp_minus26: " << bs.getExpGolomb() << std::endl;
    std::cout << "pic_init_qs_minus26: " << bs.getExpGolomb() << std::endl;
    std::cout << "chroma_qp_index_offset: " << bs.getExpGolomb() << std::endl;
    std::cout << "deblocking_filter_control_present_flag: " << bs.get(1) << std::endl;
    std::cout << "constrained_intra_pred_flag: " << bs.get(1) << std::endl;
    std::cout << "redundant_pic_cnt_present_flag: " << bs.get(1) << std::endl;
    //check for more data
    if (bs.size() == 0) {
      return;
    }
    unsigned int transform_8x8_mode_flag = bs.get(1);
    std::cout << "transform_8x8_mode_flag: " << transform_8x8_mode_flag << std::endl;
    unsigned int pic_scaling_matrix_present_flag = bs.get(1);
    std::cout << "pic_scaling_matrix_present_flag: " << pic_scaling_matrix_present_flag << std::endl;
    if (pic_scaling_matrix_present_flag) {
      for (unsigned int i = 0; i < 6 + ((chroma_format_idc != 3) ? 2 : 6)*transform_8x8_mode_flag ; i++) {
        unsigned int pic_scaling_list_present_flag = bs.get(1);
        std::cout << "pic_scaling_list_present_flag[" << i << "]: " << pic_scaling_list_present_flag << std::endl;
        if (pic_scaling_list_present_flag) {
          std::cout << "under development, pslpf" << std::endl;
          return;
          if (i < 6) {
            //scaling list(ScalingList4x4[i],16,UseDefaultScalingMatrix4x4Flag[ i ])
          } else {
            //scaling_list(ScalingList4x4[i],64,UseDefaultScalingMatrix4x4Flag[ i-6 ])
          }
        }
      }
    }
    std::cout << "second_chroma_qp_index_offset: " << bs.getExpGolomb() << std::endl;
  }

}

