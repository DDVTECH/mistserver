/// \file flac_analyser.cpp
/// Contains the code for the FLAC Analysing tool.
#include <iostream>
#include <string>
#include <vector>

#include "analyser_flac.h"
#include <mist/bitfields.h>
#include <mist/checksum.h>
#include <mist/flac.h>

AnalyserFLAC::AnalyserFLAC(Util::Config &conf) : Analyser(conf){
  headerParsed = false;
  curPos = 0;
  bufferSize = 0;

  ptr = (char *)flacBuffer.c_str();
  prev_header_size = 0;
  pos = NULL;
  forceFill = false;
  sampleNo = 0;
  sampleRate = 1;
}

bool AnalyserFLAC::readMagicPacket(){
  char magic[4];
  if (fread(magic, 4, 1, stdin) != 1){
    FAIL_MSG("Could not read magic word - aborting!");
    return false;
  }
  if (!FLAC::is_header(magic)){
    FAIL_MSG("Not a FLAC file - aborting!");
    return false;
  }
  curPos = 4;
  return true;
}

void AnalyserFLAC::init(Util::Config &conf){
  Analyser::init(conf);
}

bool AnalyserFLAC::readMeta(){
  if (!readMagicPacket()){return false;}
  bool lastMeta = false;
  Util::ResizeablePointer flacmeta;
  flacmeta.allocate(4);
  while (!feof(stdin) && !lastMeta){
    flacmeta.truncate(0);
    if (fread(flacmeta, 4, 1, stdin) != 1){
      FAIL_MSG("Could not read metadata block header - aborting!");
      return 1;
    }
    flacmeta.append(0, 4);
    curPos += 4;

    lastMeta = FLAC::MetaBlock(flacmeta, 4).getLast(); // check for last metadata block flag
    std::string mType = FLAC::MetaBlock(flacmeta, 4).getType();
    flacmeta.allocate(FLAC::MetaBlock(flacmeta, 4).getSize() + 4);

    // This variable is not created earlier because flacmeta.allocate updates the pointer and would invalidate it
    FLAC::MetaBlock mb(flacmeta, 4);

    if (fread(((char *)flacmeta) + 4, mb.getSize(), 1, stdin) != 1){
      FAIL_MSG("Could not read metadata block contents: %s", strerror(errno));
      return false;
    }
    flacmeta.append(0, mb.getSize());
    curPos += mb.getSize();

    if (mType == "STREAMINFO"){
      FLAC::StreamInfo si(flacmeta, flacmeta.size());
      sampleRate = si.getSampleRate();
      si.toPrettyString(std::cout);
    }else if (mType == "VORBIS_COMMENT"){
      FLAC::VorbisComment(flacmeta, flacmeta.size()).toPrettyString(std::cout);
    }else if (mType == "PICTURE"){
      FLAC::Picture(flacmeta, flacmeta.size()).toPrettyString(std::cout);
    }else{
      mb.toPrettyString(std::cout);
    }
  }
  headerParsed = true;
  return true;
}

bool AnalyserFLAC::parsePacket(){
  if (feof(stdin) && flacBuffer.size() < 100){return false;}
  if (!headerParsed && !readMeta()){return false;}

  uint64_t needed = 40000;

  // fill up buffer as we go
  if (flacBuffer.length() < 8100 || forceFill){
    forceFill = false;

    for (int i = 0; i < needed; i++){
      char tmp = std::cin.get();
      bufferSize++;
      flacBuffer += tmp;

      if (!std::cin.good()){
        INFO_MSG("End, process remaining buffer data: %zu bytes", flacBuffer.size());

        if (flacBuffer.size() < 1){
          FAIL_MSG("eof");
          return false;
        }

        break;
      }
    }

    start = &flacBuffer[0];
    end = &flacBuffer[flacBuffer.size()];
  }

  if (!pos){pos = start + 1;}

  while (pos < end - 5){
    uint16_t tmp = (*pos << 8) | *(pos + 1);

    if (tmp == 0xfff8){// check sync code
      char *a = pos;
      char u = *(a + 4);

      int utfv = FLAC::utfVal(start + 4); // read framenumber

      if (utfv + 1 != FLAC::utfVal(a + 4)){
        HIGH_MSG("framenr: %d, end framenr: %d, size: %zu curPos: %" PRIu64,
                 FLAC::utfVal(start + 4), FLAC::utfVal(a + 4), (size_t)(pos - start), curPos);
      }else{
        DONTEVEN_MSG("framenr: %d, end framenr: %d, size: %zu curPos: %" PRIu64,
                     FLAC::utfVal(start + 4), FLAC::utfVal(a + 4), (size_t)(pos - start), curPos);
      }

      int skip = FLAC::utfBytes(u);

      prev_header_size = skip + 4;
      if (checksum::crc8(0, pos, prev_header_size) == *(a + prev_header_size)){
        // checksum pass, valid startframe found
        if (((utfv + 1 != FLAC::utfVal(a + 4))) && (utfv != 0)){
          HIGH_MSG("error frame, found: %d, expected: %d, ignore.. ", FLAC::utfVal(a + 4), utfv + 1);
        }else{

          FLAC::Frame f(start);
          curPos += (pos - start);
          flacBuffer.erase(0, pos - start);

          start = &flacBuffer[0];
          end = &flacBuffer[flacBuffer.size()];
          pos = start + 2;
          std::cout << f.toPrettyString();
          sampleNo += f.samples();
          mediaTime = sampleNo / (sampleRate / 10);
          return true;
        }
      }else{
        HIGH_MSG("Checksum mismatch! %x - %x, curPos: %" PRIu64, *(a + prev_header_size),
                 checksum::crc8(0, pos, prev_header_size), curPos + (pos - start));
      }
    }
    pos++;
  }

  if (std::cin.good()){
    forceFill = true;
    return true;
  }

  return false;
}
