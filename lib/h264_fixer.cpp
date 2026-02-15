#include "h264_fixer.h"

namespace h264{
  // WARNING: this is not good for performance, and normally code for handling
  // MPEG-like bitstreams uses either 8-bit I/O with 32-bit accumulator or even
  // 32-bit I/O with 64-bit accumulation. BUT a) this is a PoC and b) processing
  // is about a few bytes out of every slice (up to a few slices/frame), so really
  // not such a big of a problem and c) here we both load _and_ store to alter
  // the stream - in such cases "bigger than bit" I/O can easily get hairy

  // Note: we use so called "stream order", so stream bit 0 is MSb of first byte,
  // stream bit 7 is LSb of first byte, stream bit 8 is MSb of second byte, and
  // so on

  // Load bit #src_offset in stream order from src, and return it as most
  // significant bit of unsigned char
  char fixer::load_bit(const char* src, size_t &src_offset){
    // break down stream bit offset into byte offset/bit index pair
    size_t src_byte_offset = src_offset >> 3;
    size_t src_bit_index = src_offset & 0x7;
    ++src_offset;
    char bit = src[src_byte_offset] << src_bit_index; // bit in MSb now
    return bit & 0x80;
  }

  // Store bit in #dst_offset in stream order from dst, it is assumed that the bit
  // value is in most significant bit of unsigned char (so 0x80 for 1, 0 for 0)
  void fixer::store_bit(char* dst, size_t &dst_offset, const char bit){
    // break down stream bit offset into byte offset/bit index pair
    size_t dst_byte_offset = dst_offset >> 3;
    size_t dst_bit_index = dst_offset & 0x7;
    ++dst_offset;
    char byte = dst[dst_byte_offset];
    // clear value we want to set
    char mask = ~(0x80 >> dst_bit_index);
    byte &= mask;
    // OR in new bit
    byte |= (bit >> dst_bit_index);
    // store altered byte
    dst[dst_byte_offset] = byte;
  }

  // convenience routine
  void fixer::copy_bit(char* dst, size_t& dst_offset, const char* src, size_t& src_offset){
    char bit = load_bit(src, src_offset);
    store_bit(dst, dst_offset, bit);
  }

  void fixer::process_sps(const char* nal, size_t size){
    h264::spsUnit* sps = new h264::spsUnit(nal, size);
    printf("SPS id %u\n", (int)sps->seqParameterSetId);
    if (H264_NUMBER_OF_SPS_ALLOWED <= sps->seqParameterSetId){
      fprintf(stderr, "Bad SPS id\n");
      delete sps;
      exit(-1);
    }
    // store sps info in proper place
    if (spses[sps->seqParameterSetId]) delete spses[sps->seqParameterSetId];
    spses[sps->seqParameterSetId] = sps;
  }

  void fixer::process_pps(const char* nal, size_t size){
    h264::ppsUnit* pps = new h264::ppsUnit(nal, size);
    printf("PPS id %u SPS used %u\n", (int)pps->picParameterSetId, (int)pps->seqParameterSetId);
    if (H264_NUMBER_OF_PPS_ALLOWED <= pps->picParameterSetId){
      fprintf(stderr, "Bad PPS id\n");
      delete pps;
      exit(-1);
    }
    if (H264_NUMBER_OF_SPS_ALLOWED <= pps->seqParameterSetId){
      fprintf(stderr, "PPS trying to invoke SPS id out of range\n");
      delete pps;
      exit(-1);
    }
    if (!spses[pps->seqParameterSetId]){
      fprintf(stderr, "PPS trying to invoke not existent SPS\n");
      exit(-1);
    }
    // store pps info in proper place
    if (ppses[pps->picParameterSetId]) delete ppses[pps->picParameterSetId];
    ppses[pps->picParameterSetId] = pps;
  }

  // See H.264 standard edition 2003 "7.4.1.2.4" for details
  bool fixer::are_different(const h264::codedSliceUnit& a, const h264::codedSliceUnit& b){
    if (a.picParameterSetId != b.picParameterSetId) return true;
    if (a.fieldPicFlag != b.fieldPicFlag) return true;
    if (a.bottomFieldFlag != b.bottomFieldFlag) return true;
    if ((!a.getRefNalIdc() || !b.getRefNalIdc()) &&
        (a.getRefNalIdc() != b.getRefNalIdc())) return true;
    if ((0 == a.picOrderCntType) && (0 == b.picOrderCntType)){
      if (a.picOrderCntLsb != b.picOrderCntLsb) return true;
      if (a.deltaPicOrderCntBottom != b.deltaPicOrderCntBottom) return true;
    }
    if ((1 == a.picOrderCntType) && (1 == b.picOrderCntType)){
      if (a.deltaPicOrderCnt[0] != b.deltaPicOrderCnt[0]) return true;
      if (a.deltaPicOrderCnt[1] != b.deltaPicOrderCnt[1]) return true;
    }
    if (((5 == a.getType()) || (5 == b.getType())) && (a.getType() != b.getType()))
      return true;
    if ((5 == a.getType()) && (5 == b.getType()) && (a.idrPicId != b.idrPicId)) return true;
    return false;
  }

  void fixer::fix_frame_num(char* buffer, size_t& offset, size_t& size, const h264::spsUnit &sps, size_t frame_num){
    const char* nal = buffer + offset;
    // IMPORTANT: parsing code is taken from codedSliceUnit ctors, see there for
    // comments/explanations
    Utils::bitstream bs;
    size_t l = size < 16 ? size : 16;
    for (size_t i = 1; i < l; i++){
      if (i + 2 < l && (memcmp(nal + i, "\000\000\003", 3) == 0)){ // Emulation prevention bytes
        // Yes, we increase i here
        bs.append(nal + i, 2);
        i += 2;
      }else{
        // No we don't increase i here
        bs.append(nal + i, 1);
      }
    }
    bs.getUExpGolomb(); // firstMbInSlice
    bs.getUExpGolomb(); // sliceType
    bs.getUExpGolomb(); // ppsId
    size_t frame_num_offset = bs.getOffset() + 8;
    size_t frame_num_size = sps.log2MaxFrameNumMinus4 + 4;
    size_t bytes = (frame_num_offset + frame_num_size) / 8 + 1;
    // create copy
    std::vector<char> altered;
    altered.insert(altered.end(), bytes, 0);
    ::memcpy(altered.data(), nal, bytes);
    // Now, frame_num can be as high as 16 bits, so be careful not to run into
    // endianness issues. To that effect we store frame_num value in stream order
    // inside uint32_t type value...
    uint32_t frame_num_value = frame_num << (32 - frame_num_size);
    for (size_t i = 0; i < frame_num_size; i++){
      // ...and read bit by bit manually off that value...
      store_bit(altered.data(), frame_num_offset, (frame_num_value >> 24) & 0x80);
      // ...bit shifting it on every iteration
      frame_num_value <<= 1;
    }

    // Emulation prevention bytes
    size_t count = emulation_prevention_bytes_count(altered.data(), bytes);
    assert(count <= offset);
    offset -= count;
    copy_with_emulation_prevention(altered.data(), bytes, buffer + offset);
    size += count;
  }

  void fixer::i_to_idr(char* buffer, size_t& offset, size_t& size, const h264::spsUnit& sps,
                       const h264::ppsUnit& pps, const h264::codedSliceUnit& slice){
    const char* nal = buffer + offset;
    // IMPORTANT: parsing code is taken from codedSliceUnit ctors, see there for
    // comments/explanations
    Utils::bitstream bs;
    size_t l = size < 64 ? size : 64;
    for (size_t i = 1; i < l; i++){
      if (i + 2 < l && (memcmp(nal + i, "\000\000\003", 3) == 0)){ // Emulation prevention bytes
        // Yes, we increase i here
        bs.append(nal + i, 2);
        i += 2;
      }else{
        // No we don't increase i here
        bs.append(nal + i, 1);
      }
    }
    ///////////////////////
    // Part I: parsing. Process the structure of the slice header to get stream
    // bit offsets for syntax elements of interest
    bs.getUExpGolomb(); // firstMbInSlice
    bs.getUExpGolomb(); // sliceType
    bs.getUExpGolomb(); // ppsId

    // (save frame_num offset in the process)
    size_t frame_num_offset = bs.getOffset() + 8;
    bs.get(sps.log2MaxFrameNumMinus4 + 4); // frameNum
    if (!sps.frameMbsOnlyFlag){
      bs.get(1); // fieldPicFlag
      if (slice.fieldPicFlag){
        bs.get(1); // bottomFieldFlag
      }
    }

    // ...and save the bit offset for IDR picture id (note: we know that there is
    // no IDR pic id as this is not an IDR frame - we want to convert it to one)
    size_t idr_pic_id_offset = bs.getOffset() + 8;

    // Now continue going through the slice header util we get into dec_ref_pic_marking()
    // syntax
    if (0 == sps.picOrderCntType){
      bs.get(sps.log2MaxPicOrderCntLsbMinus4 + 4); // picOrderCntLsb
      if (pps.bottomFieldPicOrderInFramePresentFlag && !slice.fieldPicFlag){
        bs.getExpGolomb(); // deltaPicOrderCntBottom
      }
    }

    if ((1 == sps.picOrderCntType) && !sps.deltaPicOrderAlwaysZeroFlag){
      bs.getExpGolomb(); // deltaPicOrderCnt[0]
      if (pps.bottomFieldPicOrderInFramePresentFlag && !slice.fieldPicFlag){
        bs.getExpGolomb(); // deltaPicOrderCnt[1]
      }
    }

    if (pps.redundantPicCntPresentFlag){
      bs.getUExpGolomb(); // redundantPicCnt
    }

    // Now:
    // Slice type ain't B, so no direct_spatial_mv_pred_flag
    // Slice type ain't P, SP or B, so no num_ref_idx_* stuff
    // Slice type is I, and is not B, so empty ref_pic_list_reordering()
    // No weighted prediction stuff, either
    // BUT we have: dec_ref_pic_marking()
    // Now we are in non-IDR slice so adaptive_ref_pic_marking_mode_flag should
    // be here. Note that we assume that it is zero. This bit will become
    // no_output_of_prior_pics_flag in the IDR slice
    bool adaptiveRefPicMarkingModeFlag = bs.get(1);
    if (adaptiveRefPicMarkingModeFlag){
      fprintf(stderr, "Can't change I slice to IDR slice, "
                      "adaptive_ref_pic_marking_mode_flag is nonzero!\n");
      exit(-1);
    }
    // OK, save offset again
    size_t long_term_reference_flag_offset = bs.getOffset() + 8;

    ///////////////////////////
    // Part II: alter the slice header injecting/changing bit fields
    // buffer for altered header
    std::vector<char> altered;
    // Need that many bytes in the altered. First +1 because flag may be not
    // bit aligned, and second +1 because we will add one byte of data
    size_t bytes = long_term_reference_flag_offset / 8 + 1 + 1;
    altered.insert(altered.end(), bytes, 0);
    // this is excessive, but cheaper than copying first part bit by bit
    ::memcpy(altered.data(), nal, bytes);

    // Change 1: alter slice type
    char start_code = altered[0] & 0xe0; // preserve upper 3 bits
    altered[0] = start_code | 5;         // IDR slice now

    // Change 2: alter frame_num - standard says IDR picture frame_num should be 0
    size_t frame_num_size = sps.log2MaxFrameNumMinus4 + 4;
    for (size_t i = 0; i < frame_num_size; i++){
      store_bit(altered.data(), frame_num_offset, 0);
    }

    // setup stream copying at where idr_pic_id should be
    size_t write_offset = idr_pic_id_offset;
    size_t read_offset = idr_pic_id_offset;

    // Change 3: inject idr_pic_id which is expected in IDR slice header
    // We want 7-bit field, 7-bit exp-Golomb codes are binary 0001xxx, so will use
    // 0001000 here. Note that value is shifted to top 7 bits of the byte, simply
    // following bit stream convention
    char idr_pic_id = 0x10;
    size_t idr_read_offset = 0;
    for (size_t i = 0; i < 7; i++){
      copy_bit(altered.data(), write_offset, &idr_pic_id, idr_read_offset);
    }

    // copy part between idr_pc_id and long_term_reference_flag without changes
    // Note that the stream will be bit-shifted, hence bit copy operation
    for (size_t i = idr_pic_id_offset; i < long_term_reference_flag_offset; i++){
      copy_bit(altered.data(), write_offset, nal, read_offset);
    }

    // Change 4: inject long_term_reference_flag which is expected in IDR slice
    // header. Along with bit that used to be adaptive_ref_pic_marking_mode_flag
    // it will make up new dec_ref_pic_marking() value for IDR frame, with both
    // no_output_of_prior_pics_flag and long_term_reference_flag set to zero
    store_bit(altered.data(), write_offset, 0);

    // finally, copy enough bits to get to byte-aligned address so we can use
    // memcpy below
    while (write_offset & 0x7){
      copy_bit(altered.data(), write_offset, nal, read_offset);
    }

    // Emulation prevention bytes
    size_t altered_size = write_offset / 8;
    size_t count = emulation_prevention_bytes_count(altered.data(), altered_size) + 1;
    assert(count <= offset);
    offset -= count;
    copy_with_emulation_prevention(altered.data(), altered_size, buffer + offset);
    size += count;
  }

  void fixer::process_slice(char* buffer, size_t& offset, size_t& size){
    // first parse, just to get pps_id
    h264::codedSliceUnit *slice = new h264::codedSliceUnit(buffer + offset, size);
    int pps_id = (int)slice->picParameterSetId;
    printf("Slice using PPS %d\n", pps_id);
    delete slice;

    // second, full parse now possible
    if (H264_NUMBER_OF_PPS_ALLOWED <= pps_id){
      fprintf(stderr, "Slice header trying to invoke PPS id out of range\n");
      exit(-1);
    }
    if (!ppses[pps_id]){
      fprintf(stderr, "Slice header trying to invoke not existent PPS\n");
      exit(-1);
    }
    const h264::ppsUnit &pps = *ppses[pps_id];
    const h264::spsUnit &sps = *spses[pps.seqParameterSetId];
    slice = new h264::codedSliceUnit(buffer + offset, size, sps, pps);

    if (previous_slice && slice->getRefNalIdc() && are_different(*previous_slice, *slice)){
      ++frame_num;
    }
    if (previous_slice) delete previous_slice;
    previous_slice = slice;

    // Decide what to do with the slice
    if ((2 == slice->sliceType) || (7 == slice->sliceType)){
      // this is non-IDR I slice, should we convert it?
      if (!first_i_slice_converted){ converted_i_slice = new h264::codedSliceUnit(*slice); }
      if (!first_i_slice_converted || !are_different(*converted_i_slice, *slice)){
        // gotta convert this slice because it belongs to first I frame
        printf("I slice will get converted to IDR slice\n");
        i_to_idr(buffer, offset, size, sps, pps, *slice);
        first_i_slice_converted = true;
      }else{
        // not the first I frame, just adjust frame_num
        printf("I slice will get frame_num changed from %u to %u\n", (unsigned)slice->frameNum,
               (unsigned)frame_num);
        fix_frame_num(buffer, offset, size, sps, frame_num);
      }
    }else{
      // other slice types, just adjust frame_num
      printf("Slice (%u) will get frame_num changed from %u to %u\n", (unsigned)slice->sliceType,
             (unsigned)slice->frameNum, (unsigned)frame_num);
      fix_frame_num(buffer, offset, size, sps, frame_num);
    }
  }

  size_t fixer::emulation_prevention_bytes_count(const char* nal, size_t size){
    uint32_t accumulator = 0xffffff;
    size_t extra_bytes = 0;
    for (size_t i = 0; i < size; i++){
      accumulator = (accumulator << 8) & 0xffffff;
      accumulator |= nal[i];
      if (accumulator <= 3){
        // one of illegal byte patterns, will need emulation prevention byte
        ++extra_bytes;
        accumulator = 0xffffff;
      }
    }
    return extra_bytes;
  }

  void fixer::copy_with_emulation_prevention(const char* nal, size_t size, char *dst){
    uint32_t accumulator = 0xffffff;
    // copy bytes including emulation prevention
    for (size_t i = 0; i < size; i++){
      accumulator = (accumulator << 8) & 0xffffff;
      accumulator |= nal[i];
      *dst++ = nal[i];
      if (accumulator <= 3){
        // one of illegal byte patterns, send emulation prevention byte
        *dst++ = 3;
        accumulator = 0xffffff;
      }
    }
  }

  fixer::fixer()
      : bypass(false), previous_slice(nullptr), converted_i_slice(nullptr),
        first_i_slice_converted(false), frame_num(0){
    // Initialize SPS and PPS arrays to detect problems
    for (int i = 0; i < H264_NUMBER_OF_SPS_ALLOWED; i++){ spses[i] = nullptr; }
    for (int i = 0; i < H264_NUMBER_OF_PPS_ALLOWED; i++){ ppses[i] = nullptr; }
  }

  fixer::~fixer(){
    for (int i = 0; i < H264_NUMBER_OF_SPS_ALLOWED; i++){
      if (spses[i]) delete spses[i];
    }
    for (int i = 0; i < H264_NUMBER_OF_PPS_ALLOWED; i++){
      if (ppses[i]) delete ppses[i];
    }
  }

  void fixer::process_nal(char *buffer, size_t &offset, size_t &size){
    if (bypass){
      // Bypass mode - do nothing. This is done after first IDR frame
      // appears, because there is nothing more to "fix"
      return;
    }

    // Not in bypass mode, see what we got
    size_t nal_unit_type = buffer[offset] & 0x1f;
    switch (nal_unit_type){
    case 1:
      // non-IDR slice (process_slice will write nal)
      process_slice(buffer, offset, size);
      break;
    case 2:
      // same handling for Partition A slice - it contains a slice header
      // so it is processed as any other non-IDR slice. Note that IDR slices
      // cannot be partitioned, this is forbidden by standard so we know we are
      // in non-IDR slice here
      process_slice(buffer, offset, size);
      break;
    case 5:
      // IDR slice ends processing. The very reason for this code here is to
      // convert first I slice into IDR slice and adjust following frames
      // accordingly. IDR slice starts new video sequence properly and nothing
      // needs to be changed anymore. This also works to protect us against
      // changing perfectly good streams, should they get passed to the fixer
      // (or fixer called twice, or whatever)
      printf("IDR SLICE, switching to bypass mode\n");
      bypass = true;
      break;
    case 7:
      // SPSes are processed, but read-only
      process_sps(buffer + offset, size);
      break;
    case 8:
      // Ditto
      process_pps(buffer + offset, size);
      break;
    default:
      // all other NAL types are of no interest
      break;
    }
  }
}// namespace h264
