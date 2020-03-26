/// \file analyser_h264.cpp
/// Reads H264 data and prints it in human-readable format.

#include "analyser_h264.h"
#include <mist/bitfields.h>
#include <mist/bitstream.h>
#include <mist/h264.h>
const int chunkSize = 8192;

void AnalyserH264::init(Util::Config &conf){
  Analyser::init(conf);
  JSON::Value opt;
  opt["long"] = "size-prepended";
  opt["short"] = "S";
  opt["help"] = "Parse size-prepended style instead of Annex B style";
  conf.addOption("size-prepended", opt);
  opt.null();
}

AnalyserH264::AnalyserH264(Util::Config &conf) : Analyser(conf){
  curPos = prePos = 0;
  sizePrepended = conf.getBool("size-prepended");
}

bool AnalyserH264::parsePacket(){
  while (dataBuffer.size() < neededBytes()) {
    uint64_t needed = neededBytes();
    uint64_t required = 0;
    dataBuffer.reserve(needed);

    while (dataBuffer.size() < needed ) {
      if (uri.isEOF()) {
        FAIL_MSG("End of file");
        return false;
      }
      required = needed - dataBuffer.size() - buffer.bytes(0xffffffff);
      if(required > chunkSize){
        uri.readSome(chunkSize, *this);
      }else{
        uri.readSome(required, *this);
      }

      dataBuffer.append(buffer.remove(buffer.bytes(0xffffffff)));
    }

    uint64_t appending = needed - dataBuffer.size();
    curPos += appending; 
  }

  size_t size = 0;
  h264::nalUnit *nalPtr = h264::nalFactory(dataBuffer.data(), dataBuffer.size(), size, !sizePrepended);
  if (!nalPtr){
    FAIL_MSG("Could not read a NAL unit at position %" PRIu64, prePos);
    return false;
  }
  HIGH_MSG("Read a %zu-byte NAL unit at position %" PRIu64, size, prePos);
  if (detail >= 2){nalPtr->toPrettyString(std::cout);}
  //SPS unit? Find the FPS, if any.
  if (nalPtr->getType() == 7){
    h264::spsUnit *sps = (h264::spsUnit*)nalPtr;
    if (sps->vuiParametersPresentFlag && sps->vuiParams.fixedFrameRateFlag){
      INFO_MSG("Frame rate: %f", sps->vuiParams.derived_fps);
    }else{
      INFO_MSG("Frame rate undetermined - assuming 25 FPS");
    }
  }
  dataBuffer.erase(0, size); // erase the NAL unit we just read
  prePos += size;
  ///\TODO update mediaTime with current timestamp
  return true;
}

uint64_t AnalyserH264::neededBytes(){
  // We buffer a megabyte if AnnexB
  if (!sizePrepended){
    if (isOpen()){return 1024 * 1024;}
    return dataBuffer.size();
  }
  // otherwise, buffer the exact size needed
  if (dataBuffer.size() < 4){return 4;}
  return Bit::btohl(dataBuffer.data()) + 4;
}

void AnalyserH264::dataCallback(const char *ptr, size_t size) {
  buffer.append(ptr, size);
}
