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
  a = conf.getInteger("filter");
  headerParsed = false;
  curPos = 0;
  bufferSize = 0;

  ptr = (char *)flacBuffer.c_str();
  prev_header_size = 0;
  pos = NULL;
  forceFill = false;
}

bool AnalyserFLAC::readMagicPacket(){
  char magic[4];
  if (fread(magic, 4, 1, stdin) != 1){
    std::cout << "Could not read magic word - aborting!" << std::endl;
    return false;
  }
  if (FLAC::is_header(magic)){
    std::cout << "Found magic packet" << std::endl;
    curPos = 4;
    return true;
  }
  std::cout << "Not a FLAC file - aborting!" << std::endl;
  return false;
}

void AnalyserFLAC::init(Util::Config &conf){
  Analyser::init(conf);
  JSON::Value opt;
  opt["long"] = "filter";
  opt["short"] = "F";
  opt["arg"] = "num";
  opt["default"] = "0";
  opt["help"] =
      "Only print information about this tag type (8 = audio, 9 = video, 18 = meta, 0 = all)";
  conf.addOption("filter", opt);
  opt.null();

  if (feof(stdin)){
    WARN_MSG("cannot open stdin");
    return;
  }
}

bool AnalyserFLAC::readMeta(){
  if (!readMagicPacket()){return false;}

  bool lastMeta = false;
  char metahead[4];
  while (!feof(stdin) && !lastMeta){
    if (fread(metahead, 4, 1, stdin) != 1){
      std::cout << "Could not read metadata block header - aborting!" << std::endl;
      return 1;
    }
    curPos += 4;

    lastMeta = (metahead[0] & 0x80); // check for last metadata block flag
    std::string mType;
    switch (metahead[0] & 0x7F){
    case 0: mType = "STREAMINFO"; break;
    case 1: mType = "PADDING"; break;
    case 2: mType = "APPLICATION"; break;
    case 3: mType = "SEEKTABLE"; break;
    case 4: mType = "VORBIS_COMMENT"; break;
    case 5: mType = "CUESHEET"; break;
    case 6: mType = "PICTURE"; break;
    case 127: mType = "INVALID"; break;
    default: mType = "UNKNOWN"; break;
    }
    unsigned int bytes = Bit::btoh24(metahead + 1);
    curPos += bytes;
    fseek(stdin, bytes, SEEK_CUR);
    std::cout << "Found metadata block of type " << mType << ", skipping " << bytes << " bytes" << std::endl;
    if (mType == "STREAMINFO"){FAIL_MSG("streaminfo");}
  }
  INFO_MSG("last metadata");
  headerParsed = true;
  return true;
}

bool AnalyserFLAC::parsePacket(){
  if (feof(stdin) && flacBuffer.size() < 100){
    stop();
    return false;
  }

  if (!headerParsed){
    if (!readMeta()){
      stop();
      return false;
    }
  }
  uint64_t needed = 40000;

  // fill up buffer as we go
  if (flacBuffer.length() < 8100 || forceFill){
    forceFill = false;

    for (int i = 0; i < needed; i++){
      char tmp = std::cin.get();
      bufferSize++;
      flacBuffer += tmp;

      if (!std::cin.good()){
        WARN_MSG("End, process remaining buffer data: %zu bytes", flacBuffer.size());

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
        FAIL_MSG("framenr: %d, end framenr: %d, size: %zu curPos: %" PRIu64, FLAC::utfVal(start + 4),
                 FLAC::utfVal(a + 4), (size_t)(pos - start), curPos);
      }else{
        INFO_MSG("framenr: %d, end framenr: %d, size: %zu curPos: %" PRIu64, FLAC::utfVal(start + 4),
                 FLAC::utfVal(a + 4), (size_t)(pos - start), curPos);
      }

      int skip = FLAC::utfBytes(u);

      prev_header_size = skip + 4;
      if (checksum::crc8(0, pos, prev_header_size) == *(a + prev_header_size)){
        // checksum pass, valid startframe found
        if (((utfv + 1 != FLAC::utfVal(a + 4))) && (utfv != 0)){
          WARN_MSG("error frame, found: %d, expected: %d, ignore.. ", FLAC::utfVal(a + 4), utfv + 1);
        }else{

          FLAC::Frame f(start);
          curPos += (pos - start);
          flacBuffer.erase(0, pos - start);

          start = &flacBuffer[0];
          end = &flacBuffer[flacBuffer.size()];
          pos = start + 2;
          return true;
        }
      }else{
        WARN_MSG("Checksum mismatch! %x - %x, curPos: %" PRIu64, *(a + prev_header_size),
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
