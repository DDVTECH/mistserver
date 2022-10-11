/*
 * This file adds AAC input capabilities
 * Dependent on lib/adts and lib/urireader
 * 
 * Input can be any AAC file which consists of ADTS frames.
 * 
 * NOTE some .AAC files are containers (eg .M4A) with an AAC audio track in it.
 *      Since these files do not consist solely of ADTS frames, they can not be parsed
 * 
 * NOTE All output AAC's are MPEG-4, while inputs can be MPEG-2. This will cause
 *      a slight different header (FFF1 instead of FFF0) but this is fine
 * 
 * NOTE: The adts_buffer_fullness and number_of_raw_data_blocks_in_frame are different
 *       in the input and output file
 * 
 * NOTE: sometimes AAC files have metadata at the end. This gets removed for now.
 * 
 * 
 * Other useful info for debugging:
 * ADTS Fixed Header Structure:
 *  Item                                # Bits        Note   Bit#    Byte#
 * syncword                             12            0xFFF  0-11    0-1  (bits 0-15)
 * ID                                   1                    12      1    (bits 8-15)
 * layer                                2                    13-14   1    (bits 8-15)
 * protection_absent                    1                    15      2    (bits 16-23)
 * profile_ObjectType                    2                    16-17   2    (bits 16-23)
 * sampling_frequency_index             4                    18-21   2    (bits 16-23)
 * private_bit                          1                    22      2    (bits 16-23)
 * channel_configuration                 3                    23-25   2-3  (bits 16-31)
 * original_copy                        1                    26      3    (bits 24-31)
 * home                                 1                    27      3    (bits 24-31)
 *                                  == 28 bits                  
 * ADTS Variable Header Structure
 * Item                                 # Bits        Note
 * copyright_identification_bit          1                    28      3     (bits 24-31)
 * copyright_identification_start        1                    29      3     (bits 24-31)
 * aac_frame_length                     13                   30-42   3-4-5 (bits 24-47)
 * adts_buffer_fullness                  11                   43-53   5-6   (bits 40-55)
 * number_of_raw_data_blocks_in_frame   2                    54-55   6     (bits 48-55)
 *                                  == 28 bits
 * The rest of the ADTS data is the data itself with CRC info for detecting
 *   changes in sent/received files
 * 
 */

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <mist/defines.h>
#include <mist/stream.h>
#include <mist/util.h>
#include <string>
#include <sys/stat.h>  //for stat
#include <sys/types.h> //for stat
#include <unistd.h>    //for stat
#include "input_aac.h"     

namespace Mist{
  inputAAC::inputAAC(Util::Config *cfg) : Input(cfg){
    capa["name"] = "AAC";
    capa["desc"] = "Allows loading AAC files";
    capa["source_match"] = "/*.aac";
    capa["source_file"] = "$source";
    capa["priority"] = 9;
    capa["codecs"]["audio"].append("AAC");
    thisTime = 0;
    // init filePos at 1, else a 15 bit mismatch in expected frame size occurs
    // dtsc.ccp +- line 215
    // (  bpos, if >= 0, adds 9 bytes (integer type) and 6 bytes (2+namelen)  )
    // but at line 224: (packBytePos ? 15 : 0)
    filePos = 1;
    audioTrack = INVALID_TRACK_ID;
  }

  inputAAC::~inputAAC(){}

  bool inputAAC::checkArguments(){
    if (!config->getString("streamname").size()){
      if (config->getString("output") == "-"){
        std::cerr << "Output to stdout not yet supported" << std::endl;
        return false;
      }
    }else{
      if (config->getString("output") != "-"){
        std::cerr << "File output in player mode not supported" << std::endl;
        return false;
      }
    }
    return true;
  }

  bool inputAAC::preRun(){
    inFile.open(config->getString("input"));
    if (!inFile || inFile.isEOF()){return false;}
    
    struct stat statData;
    lastModTime = 0;
    
    if (stat(config->getString("input").c_str(), &statData) != -1){
      lastModTime = statData.st_mtime;
    }
    return true;
  }

  // Overrides the default keepRunning function to shut down
  // if the file disappears or changes, by polling the file's mtime.
  // If neither applies, calls the original function.
  bool inputAAC::keepRunning(){
    struct stat statData;
    if (stat(config->getString("input").c_str(), &statData) == -1){
      INFO_MSG("Shutting down because input file disappeared");
      return false;
    }
    if (lastModTime != statData.st_mtime){
      INFO_MSG("Shutting down because input file changed");
      return false;
    }
    return Input::keepRunning();
  }
  

  // Reads the first frame to init track
  // Then calls getNext untill all other frames have been added to the DTSH file
  bool inputAAC::readHeader(){
    char *aacData;
    char *aacFrame;
    uint64_t frameSize = 0;
    size_t bytesRead = 0;

    if (!inFile || inFile.isEOF()){
      INFO_MSG("Could not open input stream");
      return false;
    }
    
    DONTEVEN_MSG("Parsing first ADTS frame...");
    
    // Read fixed + variable header
    inFile.readSome(aacData, bytesRead, 6);
    if (bytesRead < 6){
      WARN_MSG("Not enough bytes left in buffer. Quitting...");
      // Dump for debug purposes
      INFO_MSG("Header contains bytes: %x %x %x %x %x %x", aacData[0]
      , aacData[1], aacData[2], aacData[3], aacData[4], aacData[5]);
      return false;
    }
    // Confirm syncword (= FFF)
    if (aacData[0] != 0xFF || (aacData[1] & 0xF0) != 0xF0){
      WARN_MSG("Invalid sync word at start of header");
      return false;
    }
    // Calculate the starting position of the next frame
    frameSize = (((aacData[3] & 0x03) << 11) | (aacData[4] << 3) | ((aacData[5] >> 5) & 0x07));
    // Copy AAC header info
    aacFrame = (char*)malloc(frameSize);
    for (int i = 0; i < 6; i++){
      aacFrame[i] = aacData[i];
    }
    
    // Read the rest of the AAC frame
    inFile.readSome(aacData, bytesRead, frameSize - 6);
    if (bytesRead < frameSize - 6){
      WARN_MSG("Not enough bytes left in buffer.");
      WARN_MSG("Wanted %" PRIu64 " bytes but read %zu bytes...", frameSize - 6, bytesRead);
    }
    for (int i = 0; i < (frameSize - 6); i++){
      aacFrame[i+6] = aacData[i];
    }
    
    // Create ADTS object of complete frame info
    aac::adts adtsPack(aacFrame, frameSize);
    if (!adtsPack){
      WARN_MSG("Could not parse ADTS package!");   
      return false;
    }
    
    // Init track info
    meta.reInit(config->getString("streamname"));
    audioTrack = meta.addTrack();
    meta.setID(audioTrack, audioTrack);
    meta.setInit(audioTrack, adtsPack.getInit());
    meta.setType(audioTrack, "audio");
    meta.setCodec(audioTrack, "AAC");
    meta.setRate(audioTrack, adtsPack.getFrequency());
    meta.setChannels(audioTrack, adtsPack.getChannelCount());
 
    // Add current frame info
    thisPacket.genericFill(thisTime, 0, audioTrack, adtsPack.getPayload(), adtsPack.getPayloadSize(), filePos, false);
    meta.update(thisPacket);
        
    // Update internal variables
    thisTime += (adtsPack.getSampleCount() * 1000) / adtsPack.getFrequency();
    filePos += frameSize;
    
    // Parse the rest of the ADTS frames
    getNext(audioTrack);
    while (thisPacket){
      meta.update(thisPacket);
      getNext(audioTrack);
    }
    
    if (!inFile.seek(0))
      ERROR_MSG("Could not seek back to position 0!");
    thisTime = 0;
    // Export DTSH to file
    Socket::Connection outFile;
    int tmpFd = open("/dev/null", O_RDWR);
    outFile.open(tmpFd);
    Util::Procs::socketList.insert(tmpFd);
    genericWriter(config->getString("input") + ".dtsh", &outFile, false);
    if (outFile){M.send(outFile, false, M.getValidTracks(), false);}

    return true;
  }
  
  // Reads the ADTS frame at the current position then updates thisPacket
  // @param <idx> contains the trackID to which we want to add the ADTS payload
  void inputAAC::getNext(size_t idx){
    //packets should be initialised to null to ensure termination
    thisPacket.null();

    DONTEVEN_MSG("Parsing next ADTS frame...");
    // Temp variable which points to the urireader buffer so that we can copy this data
    char *aacData;
    // Will contain a local copy of uriReader/fileIn buffer in order to fill thisPacket
    char *aacFrame;
    // Temp variable which stores the frame size as defined in the first
    //  6 bytes of the ADTS frame
    uint64_t frameSize = 0;
    // Temp variable which gets incremented with bytesRead to indicate the start
    // pos of next ADTS frame
    size_t nextFramePos = filePos;
    // Temp var which indicates how many bytes the urireader put into the buffer
    size_t bytesRead = 0;
    // Amount of bytes to subtract from expected payload if the found payload
    // is smaller than the ADTS header specifies
    size_t disregardAmount = 0;
    
    if (!inFile || inFile.isEOF()){
      INFO_MSG("Reached EOF");
      return;
    }
    
    // Read fixed + variable header
    inFile.readSome(aacData, bytesRead, 6);
    if (bytesRead < 6){
      WARN_MSG("Not enough bytes left in buffer to extract a new ADTS frame");   
      WARN_MSG("Wanted 6 bytes but read %zu bytes...", bytesRead);
      WARN_MSG("Header contains bytes: %x %x %x %x %x %x", aacData[0]
      , aacData[1], aacData[2], aacData[3], aacData[4], aacData[5]);
      return;
    }
    // Confirm syncword (= FFF)
    if (aacData[0] != 0xFF || (aacData[1] & 0xF0) != 0xF0){
      // Check for APE tag (metadata, which we throw for now)
      if (aacData[0] == 0x41 && aacData[1] == 0x50 && aacData[2] == 0x45 && 
      aacData[3] == 0x54 && aacData[4] == 0x41 && aacData[5] == 0x47){
        inFile.readAll(aacData, bytesRead);
        INFO_MSG("Throwing out %zu bytes of metadata...", bytesRead);
        return;
      }
      WARN_MSG("Invalid sync word at start of header");
      return;
    }
    // Calculate the starting position of the next frame
    frameSize = (((aacData[3] & 0x03) << 11) | (aacData[4] << 3) | ((aacData[5] >> 5) & 0x07));
    nextFramePos += frameSize;
    // Copy AAC header info
    aacFrame = (char*)malloc(frameSize);
    for (int i = 0; i < 6; i++){
      aacFrame[i] = aacData[i];
    }
    // Read the rest of the AAC frame
    inFile.readSome(aacData, bytesRead, frameSize - 6);
    if (bytesRead < frameSize - 6){
      WARN_MSG("Not enough bytes left in buffer.");
      WARN_MSG("Wanted %" PRIu64 " bytes but read %zu bytes...", frameSize - 6, bytesRead);
      disregardAmount = frameSize - 6 - bytesRead;
    }
    for (int i = 0; i < (frameSize - 6); i++){
      aacFrame[i+6] = aacData[i];
    }
  
    // Create ADTS object of frame
    aac::adts adtsPack(aacFrame, frameSize);
    if (!adtsPack){
      WARN_MSG("Could not parse ADTS package!");
      WARN_MSG("Current frame info:");
      WARN_MSG("Current frame pos: %zu", filePos);
      WARN_MSG("Next frame pos: %zu", nextFramePos);
      WARN_MSG("Frame size expected: %" PRIu64, frameSize);
      WARN_MSG("Bytes read: %zu", bytesRead);
      WARN_MSG("ADTS getAACProfile: %li", adtsPack.getAACProfile());
      WARN_MSG("ADTS getFrequencyIndex: %li", adtsPack.getFrequencyIndex());
      WARN_MSG("ADTS getFrequency: %li", adtsPack.getFrequency());
      WARN_MSG("ADTS getChannelConfig: %li", adtsPack.getChannelConfig());
      WARN_MSG("ADTS getChannelCount: %li", adtsPack.getChannelCount());
      WARN_MSG("ADTS getHeaderSize: %li", adtsPack.getHeaderSize());
      WARN_MSG("ADTS getPayloadSize: %li", adtsPack.getPayloadSize());
      WARN_MSG("ADTS getCompleteSize: %li", adtsPack.getCompleteSize());
      WARN_MSG("ADTS getSampleCount: %li", adtsPack.getSampleCount());  
      return;
    }
    
    thisIdx = audioTrack;
    thisPacket.genericFill(thisTime, 0, thisIdx, adtsPack.getPayload(), adtsPack.getPayloadSize() - disregardAmount, filePos, false);
        
    //Update the internal timestamp
    thisTime += (adtsPack.getSampleCount() * 1000) / adtsPack.getFrequency();
    filePos = nextFramePos;
  }
  
   // Seeks to the filePos 
   // @param <seekTime> timestamp of the DTSH entry containing required file pos info
   // @param <idx> trackID of the AAC track
  void inputAAC::seek(uint64_t seekTime, size_t idx){
    if (audioTrack == INVALID_TRACK_ID){
      std::set<size_t> trks = meta.getValidTracks();
      if (trks.size()){
        audioTrack = *(trks.begin());
      }else{
        Util::logExitReason(ER_FORMAT_SPECIFIC, "no audio track in header");
        FAIL_MSG("No audio track in header - aborting");
        return;
      }
    }

    DTSC::Keys keys(M.keys(audioTrack));
    uint32_t keyIdx = M.getKeyIndexForTime(audioTrack, seekTime);
    // We minus the filePos by one, since we init it 1 higher
    inFile.seek(keys.getBpos(keyIdx)-1);
    thisTime = keys.getTime(keyIdx);
    DONTEVEN_MSG("inputAAC wants to seek to timestamp %" PRIu64 " on track %zu", seekTime, idx);
    DONTEVEN_MSG("inputAAC seeked to timestamp %" PRIu64 " with bytePos %zu", thisTime, keys.getBpos(keyIdx)-1);
  }
}// namespace Mist


    
