#pragma once
#include "h264.h"
#include "nal.h"

#define H264_NUMBER_OF_SPS_ALLOWED 32
#define H264_NUMBER_OF_PPS_ALLOWED 256

namespace h264{
  class fixer{
  private:
    // When true, all NALs are written to output without processing, this happens
    // after IDR slice showed up in stream
    bool bypass;
    // SPS and PPS catalogue. Usually streams have just one SPS and PPS, each with
    // the id equal to zero, but standard-compliant handling requires us to work
    // with multiple SPSes/PPSes. Standard allows for as much as 32 SPSes and 256
    // PPSes
    h264::spsUnit *spses[H264_NUMBER_OF_SPS_ALLOWED];
    h264::ppsUnit *ppses[H264_NUMBER_OF_PPS_ALLOWED];
    // This is used to keep information of previous slice, to be able to tell
    // slices that belong to different frames apart - very important for correct
    // processing of multislice frames
    h264::codedSliceUnit *previous_slice;

    // We only convert first I frame slices to IDR frame, so need to keep track
    // of that
    h264::codedSliceUnit *converted_i_slice;
    bool first_i_slice_converted;
    // Keep track of frame_num across multislice frames
    size_t frame_num;

    // bit I/O
    char load_bit(const char *src, size_t &src_offset);
    void store_bit(char *dst, size_t &dst_offset, const char bit);
    void copy_bit(char *dst, size_t &dst_offset, const char *src, size_t &src_offset);
    // NAL processing
    void process_sps(const char *nal, size_t size);
    void process_pps(const char *nal, size_t size);
    bool are_different(const h264::codedSliceUnit &a, const h264::codedSliceUnit &b);
    // actual fixing routines
    void fix_frame_num(char *buffer, size_t &offset, size_t &size, const h264::spsUnit &sps, size_t frame_num);
    void i_to_idr(char *buffer, size_t &offset, size_t &size, const h264::spsUnit &sps,
                  const h264::ppsUnit &pps, const h264::codedSliceUnit &slice);
    // IMPORTANT: here the buffer is passed with certain reserve (at least 4 bytes
    // is needed) in front of actual NAL data which begins at the offset and has
    // size bytes. In case the NAL needs to "grow", offset is reduced so that most
    // of the NAL doesn't have to be moved at all
    void process_slice(char *buffer, size_t &offset, size_t &size);
    // emulation prevention
    size_t emulation_prevention_bytes_count(const char *nal, size_t size);
    void copy_with_emulation_prevention(const char *src, size_t size, char *dst);

  public:
    fixer();
    ~fixer();
    void process_nal(char *buffer, size_t &offser, size_t &size);
  };
}// namespace h264
