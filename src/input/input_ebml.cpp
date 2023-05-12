#include "input_ebml.h"

#include <mist/bitfields.h>
#include <mist/defines.h>
#include <mist/ebml_socketglue.h>
#include <mist/procs.h>

namespace Mist{

  InputEBML::InputEBML(Util::Config *cfg) : Input(cfg) {
    capa["name"] = "EBML";
    capa["desc"] = "Allows loading MKV, MKA, MK3D, MKS and WebM files for Video on Demand, or "
                   "accepts live streams in those formats over standard input.";
    capa["source_match"].append("/*.mkv");
    capa["source_match"].append("/*.mka");
    capa["source_match"].append("/*.mk3d");
    capa["source_match"].append("/*.mks");
    capa["source_match"].append("/*.webm");
    capa["source_match"].append("http://*.mkv");
    capa["source_match"].append("http://*.mka");
    capa["source_match"].append("http://*.mk3d");
    capa["source_match"].append("http://*.mks");
    capa["source_match"].append("http://*.webm");
    capa["source_match"].append("https://*.mkv");
    capa["source_match"].append("https://*.mka");
    capa["source_match"].append("https://*.mk3d");
    capa["source_match"].append("https://*.mks");
    capa["source_match"].append("https://*.webm");
    capa["source_match"].append("s3+http://*.mkv");
    capa["source_match"].append("s3+http://*.mka");
    capa["source_match"].append("s3+http://*.mk3d");
    capa["source_match"].append("s3+http://*.mks");
    capa["source_match"].append("s3+http://*.webm");
    capa["source_match"].append("s3+https://*.mkv");
    capa["source_match"].append("s3+https://*.mka");
    capa["source_match"].append("s3+https://*.mk3d");
    capa["source_match"].append("s3+https://*.mks");
    capa["source_match"].append("s3+https://*.webm");
    capa["source_prefill"].append("/");
    capa["source_prefill"].append("http://");
    capa["source_prefill"].append("https://");
    capa["source_prefill"].append("s3+http://");
    capa["source_prefill"].append("s3+https://");
    capa["source_prefill"].append("mkv-exec:");
#if defined(__CYGWIN__)
    capa["source_syntax"].append("/cygdrive/[DRIVE/path/to/][file_name]");
#else
    capa["source_syntax"].append("/[path/to/][file_name]");
#endif
    capa["source_syntax"].append("http://[address]");
    capa["source_syntax"].append("https://[address]");
    capa["source_syntax"].append("s3+http://[address]");
    capa["source_syntax"].append("s3+https://[address]");
    capa["source_syntax"].append("mkv-exec:[COMMAND]");
    capa["source_help"]["default"] = "Location where MistServer can find the input file.";
    capa["source_help"]["mkv-exec:[COMMAND]"] = "MistServer will execute the command as if it's ran in the terminal and will expect to receive matroska data from the command.";
    capa["source_match"].append("mkv-exec:*");
    capa["always_match"].append("mkv-exec:*");
    capa["source_file"] = "$source";
    capa["priority"] = 9;
    capa["codecs"]["video"].append("H264");
    capa["codecs"]["video"].append("HEVC");
    capa["codecs"]["video"].append("VP8");
    capa["codecs"]["video"].append("VP9");
    capa["codecs"]["video"].append("AV1");
    capa["codecs"]["video"].append("theora");
    capa["codecs"]["video"].append("MPEG2");
    capa["codecs"]["video"].append("JPEG");
    capa["codecs"]["audio"].append("opus");
    capa["codecs"]["audio"].append("vorbis");
    capa["codecs"]["audio"].append("AAC");
    capa["codecs"]["audio"].append("PCM");
    capa["codecs"]["audio"].append("ALAW");
    capa["codecs"]["audio"].append("ULAW");
    capa["codecs"]["audio"].append("MP2");
    capa["codecs"]["audio"].append("MP3");
    capa["codecs"]["audio"].append("AC3");
    capa["codecs"]["audio"].append("FLOAT");
    capa["codecs"]["audio"].append("DTS");
    capa["codecs"]["audio"].append("FLAC");
    capa["codecs"]["metadata"].append("JSON");
    capa["codecs"]["subtitle"].append("subtitle");
    lastClusterBPos = 0;
    totalBytes = 0;
    readBufferOffset = 0;
    readPos = 0;
    readingMinimal = true;
    firstRead = true;
  }

  bool InputEBML::checkArguments(){
    if (!config->getString("streamname").size()){
      if (config->getString("output") == "-"){
        Util::logExitReason(ER_FORMAT_SPECIFIC, "Output to stdout not yet supported");
        return false;
      }
    }else{
      if (config->getString("output") != "-"){
        Util::logExitReason(ER_FORMAT_SPECIFIC, "File output in player mode not supported");
        return false;
      }
    }
    return true;
  }

  bool InputEBML::needsLock(){
    // Streamed input requires no lock, non-streamed does
    if (!standAlone){return false;}
    if (config->getString("input") == "-" || config->getString("input").substr(0, 9) == "mkv-exec:"){return false;}
    return Input::needsLock();
  }

  bool InputEBML::preRun(){
    if (config->getString("input").substr(0, 9) == "mkv-exec:"){
      standAlone = false;
      std::deque<std::string> args;
      Util::shellSplit(config->getString("input").substr(9), args);

      int fin = -1, fout = -1;
      pid_t inProc = Util::Procs::StartPiped(args, &fin, &fout, 0);
      if (fout == -1){
        Util::logExitReason(ER_EXEC_FAILURE, "Unable to start mkv-exec process `%s`",
                            config->getString("input").substr(9).c_str());
        return false;
      }
      dup2(fout, 0);
      inFile.open(0);
      inFile.binaryMode();
      INFO_MSG("Reading from process %d: %s", inProc, config->getString("input").substr(9).c_str());
      return true;
    }
    if (config->getString("input") == "-"){
      standAlone = false;
      inFile.open(0);
    }else{
      // open File
      inFile.open(config->getString("input"));
      if (!inFile){
        Util::logExitReason(ER_READ_START_FAILURE, "Opening input '%s' failed", config->getString("input").c_str());
        return false;
      }
      standAlone = inFile.isSeekable();
    }
    inFile.binaryMode();
    return true;
  }

  void InputEBML::dataCallback(const char *ptr, size_t size){
    readBuffer.append(ptr, size);
    totalBytes += size;
  }
  size_t InputEBML::getDataCallbackPos() const{return readPos + readBuffer.size();}

  bool InputEBML::readElement(){
    uint32_t needed = EBML::Element::needBytes(readBuffer + readBufferOffset, readBuffer.size() - readBufferOffset, readingMinimal);
    if (!firstRead && readBuffer.size() >= needed + readBufferOffset){
      readBufferOffset += needed;
      needed = EBML::Element::needBytes(readBuffer + readBufferOffset, readBuffer.size() - readBufferOffset, readingMinimal);
      readingMinimal = true;
      if (readBuffer.size() >= needed + readBufferOffset){
        // Make sure TrackEntry types are read whole
        if (readingMinimal && EBML::Element(readBuffer + readBufferOffset).getID() == EBML::EID_TRACKENTRY){
          readingMinimal = false;
          needed = EBML::Element::needBytes(readBuffer + readBufferOffset, readBuffer.size() - readBufferOffset, readingMinimal);
        }
      }
    }

    while (readBuffer.size() < needed + readBufferOffset && config->is_active){
      if (!readBuffer.allocate(needed + readBufferOffset)){return false;}
      if (!inFile){return false;}
      int64_t toRead = needed - readBuffer.size() + readBufferOffset;

      if (standAlone){
        //If we have more than 10MiB buffered and are more than 10MiB into the buffer, shift the first 4MiB off the buffer.
        //This prevents infinite growth of the read buffer for large files, but allows for some re-use of data.
        if (readBuffer.size() >= 10*1024*1024 && readBufferOffset > 10*1024*1024){
          readBuffer.shift(4*1024*1024);
          readBufferOffset -= 4*1024*1024;
          readPos += 4*1024*1024;
        }
      }else{
        //For non-standalone mode, we know we're always live streaming, and can always cut off what we've shifted
        if (readBufferOffset){
          readBuffer.shift(readBufferOffset);
          readPos += readBufferOffset;
          readBufferOffset = 0;
        }
      }

      size_t preSize = readBuffer.size();
      inFile.readSome(toRead, *this);
      if (readBuffer.size() == preSize){
        Util::sleep(5);
        continue;
      }

      needed = EBML::Element::needBytes(readBuffer + readBufferOffset, readBuffer.size() - readBufferOffset, readingMinimal);
      if (readBuffer.size() >= needed + readBufferOffset){
        // Make sure TrackEntry types are read whole
        if (readingMinimal && EBML::Element(readBuffer + readBufferOffset).getID() == EBML::EID_TRACKENTRY){
          readingMinimal = false;
          needed = EBML::Element::needBytes(readBuffer + readBufferOffset, readBuffer.size() - readBufferOffset, readingMinimal);
        }
      }
    }
    EBML::Element E(readBuffer + readBufferOffset);
    if (E.getID() == EBML::EID_CLUSTER){
      if (!inFile.isSeekable()){
        lastClusterBPos = 0;
      }else{
        int64_t bp = readPos + readBufferOffset;
        if (bp == -1 && errno == ESPIPE){
          lastClusterBPos = 0;
        }else{
          lastClusterBPos = bp;
        }
      }
      DONTEVEN_MSG("Found a cluster at position %" PRIu64, lastClusterBPos);
    }
    firstRead = false;
    return true;
  }

  bool InputEBML::readExistingHeader(){
    if (!Input::readExistingHeader()) { return false; }
    if (!M.inputLocalVars.isMember("version") || M.inputLocalVars["version"].asInt() < 2){
      INFO_MSG("Header needs update, regenerating");
      return false;
    }
    return true;
  }

  bool InputEBML::readHeader(){
    if (!inFile){
      Util::logExitReason(ER_READ_START_FAILURE, "Reading header for '%s' failed: Could not open input stream", config->getString("input").c_str());
      return false;
    }
    if (!meta || (needsLock() && isSingular())){
      meta.reInit(isSingular() ? streamName : "");
    }

    parser.enableData(false);

    while (readElement()){
      if (!config->is_active){
        WARN_MSG("Aborting header generation due to shutdown: %s", Util::exitReason);
        return false;
      }
      EBML::Element E(readBuffer + readBufferOffset, readingMinimal);

      if (E.getID() == EBML::EID_CLUSTER){
        // Live streams stop parsing the header as soon as the first Cluster is encountered
        if (!needsLock()) { break; }
        //Set progress counter for non-live inputs
        if (streamStatus && streamStatus.len > 1 && inFile.getSize()){
          streamStatus.mapped[1] = (255 * (readPos + readBufferOffset)) / inFile.getSize();
        }
      }

      parser.parseElement(E, lastClusterBPos, meta);
      parser.fillPacketData(meta);
    }
    parser.finish();
    parser.fillPacketData(meta);
    parser.flush();
    parser.enableData(true);

    meta.inputLocalVars["version"] = 2;
    return true;
  }

  void InputEBML::postHeader(){
    parser.postHeader(meta);
  }

  void InputEBML::getNext(size_t idx){
    bool singleTrack = (idx != INVALID_TRACK_ID);
    size_t wantedID = singleTrack ? M.getID(idx) : 0;

    do {
      // Make sure we empty our buffer first
      while (parser.fillPacket(M, thisIdx, thisTime, thisPacket)) {
        if (!singleTrack || M.getID(thisIdx) == wantedID) { return; }
      }

      // Nothing buffered, attempt to read a new block
      if (!readElement()) {
        // If that fails, set the parser to finished so we can read the last few frames
        parser.finish();
        // Attempt to read from the parser again
        while (parser.fillPacket(M, thisIdx, thisTime, thisPacket)) {
          if (!singleTrack || M.getID(thisIdx) == wantedID) { return; }
        }
        // Nothing left? Set to empty and return. We reached end of stream.
        thisPacket.null();
        return;
      }
      // Success - feed it into the parser
      EBML::Element E(readBuffer + readBufferOffset);
      parser.parseElement(E, lastClusterBPos, meta);
    } while (config->is_active);

    // Aborted, return empty packet
    thisPacket.null();
  }

  void InputEBML::seek(uint64_t seekTime, size_t idx){
    parser.flush();
    uint64_t mainTrack = M.mainTrack();

    DTSC::Keys keys(M.keys(mainTrack));
    DTSC::Parts parts(M.parts(mainTrack));
    uint64_t seekPos = keys.getBpos(0);
    // Replay the parts of the previous keyframe, so the timestamps match up
    for (size_t i = 0; i < keys.getEndValid(); i++){
      if (keys.getTime(i) > seekTime){break;}
      DONTEVEN_MSG("Seeking to %" PRIu64 ", found %" PRIu64 "...", seekTime, keys.getTime(i));
      seekPos = keys.getBpos(i);
    }


    firstRead = true;
    if (readPos > seekPos || seekPos > readPos + readBuffer.size() + 4*1024*1024){
      readBuffer.truncate(0);
      readBufferOffset = 0;
      if (!inFile.seek(seekPos)){
        FAIL_MSG("Seek to %" PRIu64 " failed! Aborting load", seekPos);
      }
      readPos = inFile.getPos();
    }else{
      while (seekPos > readPos + readBuffer.size() && config->is_active){
        size_t preSize = readBuffer.size();
        inFile.readSome(seekPos - (readPos + readBuffer.size()), *this);
        if (readBuffer.size() == preSize){
          Util::sleep(5);
        }
      }
      if (seekPos > readPos + readBuffer.size()){
        Util::logExitReason(ER_READ_START_FAILURE, "Input file seek abort");
        config->is_active = false;
        readBufferOffset = 0;
        return;
      }
      readBufferOffset = seekPos - readPos;
    }


  }

}// namespace Mist
