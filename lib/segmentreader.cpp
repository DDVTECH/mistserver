#include "segmentreader.h"
#include "timing.h"

#ifdef SSL
#include "mbedtls/aes.h"
#endif

/// Helper function for printing encryption keys in hex format
static std::string printhex(const char *data, size_t len){
  static const char *const lut = "0123456789ABCDEF";
  std::string output;
  output.reserve(2 * len);
  for (size_t i = 0; i < len; ++i){
    const unsigned char c = data[i];
    output.push_back(lut[c >> 4]);
    output.push_back(lut[c & 15]);
  }
  return output;
}



namespace Mist{
  SegmentReader::SegmentReader(){
    progressCallback = 0;
    isOpen = false;
#ifdef SSL
    encrypted = false;
#endif
    currBuf = 0;
    packetPtr = 0;
    mp4PacksLeft = 0;
    lastMoof = 0;
  }

  void SegmentReader::onProgress(bool (*callback)(uint8_t)){
    progressCallback = callback;
    segDL.onProgress(callback);
  }

  void SegmentReader::reset(){
    tsStream.clear();
  }

  /// Reads the segment at least up to position _offset.
  /// Returns true if the position is available, false otherwise.
  bool SegmentReader::readTo(size_t _offset){
    // Have it? Return true right away
    if (currBuf->size() >= _offset){return true;}

    // Buffered? Just return false - we can't download more.
    if (buffered){return false;}

    // Past end of file? Always return false.
    if (_offset > currBuf->rsize()){return false;}

#ifdef SSL
    // Encrypted? Round up to nearest multiple of 16
    if (encrypted && _offset % 16){
      _offset = ((size_t)(_offset / 16) + 1) * 16;
      // Clip to size of file
      if (_offset > currBuf->rsize()){_offset = currBuf->rsize();}
    }
#endif

    // Attempt to download what we need
    size_t retries = 0;
    while (currBuf->size() < _offset){
      size_t preSize = getDataCallbackPos();
      if (!segDL){
        if (!segDL.isSeekable()){return false;}
        // Only retry/resume if seekable and allocated size greater than current size
        if (currBuf->rsize() > currBuf->size()){
          // Seek to current position to resume
          if (retries++ > 5){
            segDL.close();
            return false;
          }
          segDL.seek(getDataCallbackPos());
        }
      }
      segDL.readSome(_offset - currBuf->size(), *this);

      // Sleep if we made no progress
      if (getDataCallbackPos() == preSize){
        Util::sleep(5);
        if (progressCallback && !progressCallback(0)){return false;}
      }
    }
    return true;
  }

  void SegmentReader::initializeMetadata(DTSC::Meta &meta, size_t tid, size_t mappingId){
    if (parser == STRM_TS){
      tsStream.initializeMetadata(meta, tid, mappingId);
      return;
    }

    if (parser == STRM_MP4){
      for (std::deque<MP4::TrackHeader>::iterator it = mp4Headers.begin(); it != mp4Headers.end(); ++it){
        if (it->trackId != tid){continue;}
        size_t tNumber = meta.addTrack();
        INFO_MSG("Found track %zu of type %s -> %s", tNumber, it->sType.c_str(), it->codec.c_str());
        meta.setID(tNumber, mappingId);
        meta.setCodec(tNumber, it->codec);
        meta.setInit(tNumber, it->initData);
        meta.setLang(tNumber, it->lang);
        if (it->trackType == "video"){
          meta.setType(tNumber, "video");
          meta.setWidth(tNumber, it->vidWidth);
          meta.setHeight(tNumber, it->vidHeight);
        }
        if (it->trackType == "audio"){
          meta.setType(tNumber, "audio");
          meta.setChannels(tNumber, it->audChannels);
          meta.setRate(tNumber, it->audRate);
          meta.setSize(tNumber, it->audSize);
        }
      }
      return;
    }
  }

  /// Attempts to read a single TS packet from the current segment, setting packetPtr on success
  bool SegmentReader::readNext(DTSC::Packet & thisPacket, uint64_t bytePos){
    while (*this){
      if (parser == STRM_UNKN){
        if (!readTo(189)){
          WARN_MSG("File format detection failed: could not read at least 189 bytes!");
          return false;
        }
        if ((*currBuf)[0] == 0x47 && (*currBuf)[188] == 0x47){
          parser = STRM_TS;
          continue;
        }
        if (!memcmp(*currBuf + 4, "ftyp", 4) || !memcmp(*currBuf + 4, "styp", 4) || !memcmp(*currBuf + 4, "moof", 4) || !memcmp(*currBuf + 4, "moov", 4)){
          parser = STRM_MP4;
          continue;
        }
        WARN_MSG("File format detection failed: unable to recognize file format!");
        return false;
      }

      if (parser == STRM_TS){
        if (currBuf->size() == currBuf->rsize() && offset + 188 > currBuf->size()){tsStream.finish();}
        if (tsStream.hasPacketOnEachTrack() || (currBuf->size() == currBuf->rsize() && offset + 188 > currBuf->size())){
          if (!tsStream.hasPacket()){return false;}
          tsStream.getEarliestPacket(thisPacket);
          return true;
        }
        if (!readTo(offset + 188)){return false;}
        tsStream.parse(*currBuf + offset, bytePos);
        offset += 188;
      }

      if (parser == STRM_MP4){
        if (mp4PacksLeft){
          std::deque<size_t>::iterator pIt = mp4PackNo.begin();
          for (std::deque<MP4::TrackHeader>::iterator it = mp4Headers.begin(); it != mp4Headers.end(); ++it){
            if (*pIt < it->size()){
              uint64_t prtBpos = 0, prtTime = 0;
              uint32_t prtBlen = 0;
              int32_t prtTimeOff = 0;
              bool prtKey = false;
              it->getPart(*pIt, &prtBpos, &prtBlen, &prtTime, &prtTimeOff, &prtKey, lastMoof);
              // Increase/decrease counters
              --mp4PacksLeft;
              ++(*pIt);
              // Abort reading if we cannot read this part, try the next part
              if (!readTo(prtBpos + prtBlen)){continue;}
              // Fill the packet and return true
              thisPacket.genericFill(prtTime, prtTimeOff, it->trackId, *currBuf + prtBpos, prtBlen, bytePos, prtKey);
              return true;
            }
            ++pIt;
          }
        }
        if (!mp4PacksLeft){
          // Read more boxes!
          if (offset >= currBuf->rsize()){return false;}
          if (!readTo(offset + 12)){
            INFO_MSG("Could not read next MP4 box!");
            return false;
          }
          std::string boxType = std::string(*currBuf+offset+4, 4);
          uint64_t boxSize = MP4::calcBoxSize(*currBuf+offset);
          if (!readTo(offset + boxSize)){
            INFO_MSG("Could not read next MP4 box!");
            return false;
          }
          if (boxType == "moov"){
            mp4PacksLeft = 0;
            mp4Headers.clear();
            mp4PackNo.clear();
            MP4::Box moovBox(*currBuf+offset, false);
            std::deque<MP4::TRAK> trak = ((MP4::MOOV*)&moovBox)->getChildren<MP4::TRAK>();
            for (std::deque<MP4::TRAK>::iterator trakIt = trak.begin(); trakIt != trak.end(); trakIt++){
              mp4Headers.push_back(MP4::TrackHeader());
              mp4PackNo.push_back(0);
              mp4Headers.rbegin()->read(*trakIt);
              mp4PacksLeft += mp4Headers.rbegin()->size();
            }
            std::deque<MP4::TREX> trex = ((MP4::MOOV*)&moovBox)->getChild<MP4::MVEX>().getChildren<MP4::TREX>();
            for (std::deque<MP4::TREX>::iterator trexIt = trex.begin(); trexIt != trex.end(); trexIt++){
              for (std::deque<MP4::TrackHeader>::iterator it = mp4Headers.begin(); it != mp4Headers.end(); ++it){
                if (it->trackId == trexIt->getTrackID()){it->read(*trexIt);}
              }
            }
            MEDIUM_MSG("Read moov box");
          }
          if (boxType == "moof"){
            if (!mp4Headers.size()){
              FAIL_MSG("Attempting to read moof box without reading moov box first!");
              return false;
            }
            lastMoof = offset;
            MP4::Box moofBox(*currBuf+offset, false);
            // Indicate that we're reading the next moof box to all track headers
            for (std::deque<MP4::TrackHeader>::iterator it = mp4Headers.begin(); it != mp4Headers.end(); ++it){
              it->nextMoof();
            }
            // Loop over traf boxes inside the moof box, but them in our header parser
            std::deque<MP4::TRAF> trafs = ((MP4::MOOF*)&moofBox)->getChildren<MP4::TRAF>();
            for (std::deque<MP4::TRAF>::iterator t = trafs.begin(); t != trafs.end(); ++t){
              if (!(t->getChild<MP4::TFHD>())){
                WARN_MSG("Could not find thfd box inside traf box!");
                continue;
              }
              uint32_t trackId = t->getChild<MP4::TFHD>().getTrackID();
              for (std::deque<MP4::TrackHeader>::iterator it = mp4Headers.begin(); it != mp4Headers.end(); ++it){
                if (it->trackId == trackId){
                  it->read(*t);
                  break;
                }
              }
            }
            mp4PacksLeft = 0;
            std::deque<size_t>::iterator pIt = mp4PackNo.begin();
            for (std::deque<MP4::TrackHeader>::iterator it = mp4Headers.begin(); it != mp4Headers.end(); ++it){
              mp4PacksLeft += it->size();
              (*pIt) = 0;
              ++pIt;
            }
          }
          offset += boxSize;
        }
      }
    }
    return false;
  }

  void SegmentReader::setInit(const std::string & data){
    char * ptr = (char *)data.data();
    size_t len = data.size();
    size_t offset = 0;
    while (offset + 8 <= len){
      std::string boxType = std::string(ptr+offset+4, 4);
      uint64_t boxSize = MP4::calcBoxSize(ptr+offset);
      if (boxType == "moov"){
        mp4PacksLeft = 0;
        mp4Headers.clear();
        mp4PackNo.clear();
        MP4::Box moovBox(ptr+offset, false);
        std::deque<MP4::TRAK> trak = ((MP4::MOOV*)&moovBox)->getChildren<MP4::TRAK>();
        for (std::deque<MP4::TRAK>::iterator trakIt = trak.begin(); trakIt != trak.end(); trakIt++){
          mp4Headers.push_back(MP4::TrackHeader());
          mp4PackNo.push_back(0);
          mp4Headers.rbegin()->read(*trakIt);
          mp4PacksLeft += mp4Headers.rbegin()->size();
        }
        std::deque<MP4::TREX> trex = ((MP4::MOOV*)&moovBox)->getChild<MP4::MVEX>().getChildren<MP4::TREX>();
        for (std::deque<MP4::TREX>::iterator trexIt = trex.begin(); trexIt != trex.end(); trexIt++){
          for (std::deque<MP4::TrackHeader>::iterator it = mp4Headers.begin(); it != mp4Headers.end(); ++it){
            if (it->trackId == trexIt->getTrackID()){it->read(*trexIt);}
          }
        }
        MEDIUM_MSG("Read moov box");
      }
      offset += boxSize;
    }
  }

  /// Stores data in currBuf, decodes if/as necessary, in whole 16-byte blocks
  void SegmentReader::dataCallback(const char *ptr, size_t size){
#ifdef SSL
    if (encrypted){
      // Try to complete a 16-byte remainder
      if (decBuffer.size()){
        size_t toAppend = 16 - decBuffer.size();
        decBuffer.append(ptr, toAppend);
        if (decBuffer.size() != 16){
          //Not enough data yet
          return;
        }
        // Decode 16 bytes
        currBuf->allocate(currBuf->size() + 16);
        mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, 16, tmpIvec, (const unsigned char *)(char*)decBuffer,
                              ((unsigned char *)(char *)*currBuf) + currBuf->size());
        currBuf->append(0, 16);
        // Clear remainder
        decBuffer.truncate(0);
        // Shift buffers
        ptr += toAppend;
        size -= toAppend;
      }
      // Decode any multiple of 16 bytes
      size_t toDecode = ((size_t)(size / 16)) * 16;
      if (toDecode){
        currBuf->allocate(currBuf->size() + toDecode);
        mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, toDecode, tmpIvec, (const unsigned char *)ptr,
                              ((unsigned char *)(char *)*currBuf) + currBuf->size());
        currBuf->append(0, toDecode);
        // Shift buffers
        ptr += toDecode;
        size -= toDecode;
      }
      // Store remainder, if needed
      if (size){decBuffer.append(ptr, size);}
      return;
    }
#endif
    currBuf->append(ptr, size);
  }

  size_t SegmentReader::getDataCallbackPos() const{
#ifdef SSL
    return startAtByte+currBuf->size()+decBuffer.size();
#else
    return startAtByte+currBuf->size();
#endif
  }

  /// Attempts to read a single TS packet from the current segment, setting packetPtr on success
  void SegmentReader::close(){
    packetPtr = 0;
    isOpen = false;
    segDL.close();
  }

  /// Loads the given segment URL into the segment buffer.
  bool SegmentReader::load(const std::string &path, uint64_t startAt, uint64_t stopAt, const char * ivec, const char * keyAES, Util::ResizeablePointer * bufPtr){
    tsStream.partialClear();
    lastMoof = 0;
    for (std::deque<MP4::TrackHeader>::iterator it = mp4Headers.begin(); it != mp4Headers.end(); ++it){
      it->nextMoof();
    }
    
    isOpen = false;
    parser = STRM_UNKN;
    if (ivec && keyAES && memcmp(keyAES, "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000", 16)){
#ifdef SSL
      encrypted = true;
      std::string hexKey = printhex(keyAES, 16);
      std::string hexIvec = printhex(ivec, 16);
      MEDIUM_MSG("Loading segment: %s, key: %s, ivec: %s", path.c_str(), hexKey.c_str(), hexIvec.c_str());
#else
      FAIL_MSG("Cannot read encrypted segment: %s", path.c_str());
      return false;
#endif
    }else{
      encrypted = false;
      MEDIUM_MSG("Loading segment: %s", path.c_str());
    }

    startAtByte = startAt;
    stopAtByte = stopAt;
    offset = 0;
    currBuf = bufPtr;

    // Is there at least one byte? Check if we need to resume or have a whole buffer
    // If reserved and total size match, assume we have the whole thing
    if (currBuf->size() && (currBuf->rsize() == currBuf->size())){
      buffered = true;
    }else{
      buffered = false;

      if (currBuf->size()){
        MEDIUM_MSG("Cache was incomplete (%zu/%" PRIu32 "), resuming", currBuf->size(), currBuf->rsize());
      }

      // We only re-open and seek if the opened URL doesn't match what we want already
      HTTP::URL A = segDL.getURI();
      HTTP::URL B = HTTP::localURIResolver().link(path);
      if (A != B){
        if (!segDL.open(path) || !segDL){
          FAIL_MSG("Could not open %s", path.c_str());
          return false;
        }
        if (!segDL){return false;}
      }

      // Non-seekable case is handled further down
      if (segDL.isSeekable() && startAtByte + currBuf->size()){
        //Seek to startAtByte position, since it's not the beginning of the file
        MEDIUM_MSG("Seeking to %" PRIu64 "", startAtByte + currBuf->size());
        segDL.seek(startAtByte + currBuf->size());
      }
    }

    if (!buffered){
      if (!currBuf->size() || !currBuf->rsize()){
        // Allocate full size if known
        if (stopAtByte || segDL.getSize() != std::string::npos){currBuf->allocate(stopAtByte?(stopAtByte - startAtByte):segDL.getSize());}
      }
      // Download full segment if not seekable, pretend it was cached all along
      if (!segDL.isSeekable()){
        currBuf->truncate(0);
        segDL.readAll(*this);
        if (startAtByte || stopAtByte){
          WARN_MSG("Wasting data: downloaded whole segment due to unavailability of range requests, but caching only part of it");
          if (startAtByte){currBuf->shift(startAtByte);}
          if (stopAtByte){currBuf->truncate(stopAtByte - startAtByte);}
        }
        buffered = true;
        segDL.close();
      }
    }

#ifdef SSL
    decBuffer.truncate(0);
    // If we have a non-null key, decrypt
    if (encrypted){
      // Load key
      mbedtls_aes_setkey_dec(&aes, (const unsigned char *)keyAES, 128);
      // Load initialization vector
      memcpy(tmpIvec, ivec, 16);
    }
#endif

    packetPtr = 0;
    isOpen = true;
    VERYHIGH_MSG("Segment opened: %s", path.c_str());
    return true;
  }

}// namespace Mist

