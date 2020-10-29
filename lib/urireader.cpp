#include "defines.h"
#include "shared_memory.h"
#include "timing.h"
#include "urireader.h"
#include "util.h"
#include <sys/mman.h>
#include <sys/stat.h>

namespace HTTP{

  void URIReader::init(){
    handle = -1;
    mapped = 0;
    char workDir[512];
    getcwd(workDir, 512);
    myURI = HTTP::URL(std::string("file://") + workDir + "/");
    cbProgress = 0;
    minLen = 1;
    maxLen = std::string::npos;
    startPos = 0;
    supportRangeRequest = false;
    endPos = std::string::npos;
    totalSize = std::string::npos;
    stateType = HTTP::Closed;
    clearPointer = true;
    curPos = 0;
    bufPos = 0;
  }

  URIReader::URIReader(){init();}

  URIReader::URIReader(const HTTP::URL &uri){
    init();
    open(uri);
  }

  URIReader::URIReader(const std::string &reluri){
    init();
    open(reluri);
  }

  bool URIReader::open(const std::string &reluri){return open(myURI.link(reluri));}

  /// Internal callback function, used to buffer data.
  void URIReader::dataCallback(const char *ptr, size_t size){allData.append(ptr, size);}

  bool URIReader::open(const HTTP::URL &uri){
    myURI = uri;
    curPos = 0;
    allData.truncate(0);
    bufPos = 0;

    if (!myURI.protocol.size() || myURI.protocol == "file"){
      close();
      if (!myURI.path.size() || myURI.path == "-"){
        downer.getSocket().open(-1, fileno(stdin));
        stateType = HTTP::Stream;
        startPos = 0;

        endPos = std::string::npos;
        totalSize = std::string::npos;
        if (!downer.getSocket()){
          FAIL_MSG("Could not open '%s': %s", myURI.getUrl().c_str(), downer.getSocket().getError().c_str());
          stateType = HTTP::Closed;
          return false;
        }
        return true;
      }else{
        handle = ::open(myURI.getFilePath().c_str(), O_RDONLY);
        if (handle == -1){
          FAIL_MSG("Opening file '%s' failed: %s", myURI.getFilePath().c_str(), strerror(errno));
          stateType = HTTP::Closed;
          return false;
        }

        struct stat buffStats;
        int xRes = fstat(handle, &buffStats);
        if (xRes < 0){
          FAIL_MSG("Checking size of '%s' failed: %s", myURI.getFilePath().c_str(), strerror(errno));
          stateType = HTTP::Closed;
          return false;
        }
        totalSize = buffStats.st_size;
        //        INFO_MSG("size: %llu", totalSize);

        mapped = (char *)mmap(0, totalSize, PROT_READ, MAP_SHARED, handle, 0);
        if (mapped == MAP_FAILED){
          FAIL_MSG("Memory-mapping file '%s' failed: %s", myURI.getFilePath().c_str(), strerror(errno));
          mapped = 0;
          stateType = HTTP::Closed;
          return false;
        }
        startPos = 0;

        stateType = HTTP::File;
        return true;
      }
    }

    // HTTP, stream or regular download?
    if (myURI.protocol == "http" || myURI.protocol == "https"){
      stateType = HTTP::HTTP;

      // Send HEAD request to determine range request is supported, and get total length
      downer.clearHeaders();
      if (userAgentOverride.size()){downer.setHeader("User-Agent", userAgentOverride);}
      if (!downer.head(myURI) || !downer.isOk()){
        FAIL_MSG("Error getting URI info for '%s': %" PRIu32 " %s", myURI.getUrl().c_str(),
                 downer.getStatusCode(), downer.getStatusText().c_str());
        if (!downer.isOk()){return false;}
        supportRangeRequest = false;
        totalSize = std::string::npos;
      }else{
        supportRangeRequest = (downer.getHeader("Accept-Ranges").size() > 0);
        std::string header1 = downer.getHeader("Content-Length");
        if (header1.size()){totalSize = atoi(header1.c_str());}
        myURI = downer.lastURL();
      }

      // streaming mode when size is unknown
      if (!supportRangeRequest){
        MEDIUM_MSG("URI get without range request: %s, totalsize: %zu", myURI.getUrl().c_str(), totalSize);
        downer.getNonBlocking(myURI);
      }else{
        MEDIUM_MSG("URI get with range request: %s, totalsize: %zu", myURI.getUrl().c_str(), totalSize);
        if (!downer.getRangeNonBlocking(myURI, curPos, 0)){
          FAIL_MSG("error loading url: %s", myURI.getUrl().c_str());
        }
      }

      if (!downer.getSocket()){
        FAIL_MSG("Could not open '%s': %s", myURI.getUrl().c_str(), downer.getStatusText().c_str());
        stateType = HTTP::Closed;
        return false;
      }
      return true;
    }

    FAIL_MSG("URI type not implemented: %s", myURI.getUrl().c_str());
    return false;
  }

  // seek to pos, return true if succeeded.
  bool URIReader::seek(const uint64_t pos){
    if (isSeekable()){
      allData.truncate(0);
      bufPos = 0;
      if (stateType == HTTP::File){
        curPos = pos;
        return true;
      }else if (stateType == HTTP::HTTP && supportRangeRequest){
        INFO_MSG("SEEK: RangeRequest to %" PRIu64, pos);
        if (!downer.getRangeNonBlocking(myURI.getUrl(), pos, 0)){
          FAIL_MSG("error loading request");
        }
      }
    }

    return false;
  }

  void URIReader::readAll(size_t (*dataCallback)(const char *data, size_t len)){
    while (!isEOF()){readSome(dataCallback, 419430);}
  }

  /// Read all function, with use of callbacks
  void URIReader::readAll(Util::DataCallback &cb){
    while (!isEOF()){readSome(1048576, cb);}
  }

  /// Read all blocking function, which internally uses the Nonblocking function.
  void URIReader::readAll(char *&dataPtr, size_t &dataLen){
    if (getSize() != std::string::npos){allData.allocate(getSize());}
    while (!isEOF()){
      readSome(10046, *this);
      bufPos = allData.size();
    }
    dataPtr = allData;
    dataLen = allData.size();
  }

  void httpBodyCallback(const char *ptr, size_t size){INFO_MSG("callback");}

  void URIReader::readSome(size_t (*dataCallback)(const char *data, size_t len), size_t wantedLen){
    /// TODO: Implement
  }

  // readsome with callback
  void URIReader::readSome(size_t wantedLen, Util::DataCallback &cb){
    if (isEOF()){return;}
    if (stateType == HTTP::File){
      //      dataPtr = mapped + curPos;
      uint64_t dataLen = 0;

      if (wantedLen < totalSize){
        if ((wantedLen + curPos) > totalSize){
          dataLen = totalSize - curPos; // restant
          // INFO_MSG("file curpos: %llu, dataLen: %llu, totalSize: %llu ", curPos, dataLen, totalSize);
        }else{
          dataLen = wantedLen;
        }
      }else{
        dataLen = totalSize;
      }

      std::string t = std::string(mapped + curPos, dataLen);
      cb.dataCallback(t.c_str(), dataLen);

      curPos += dataLen;

    }else if (stateType == HTTP::HTTP){
      downer.continueNonBlocking(cb);
      if (curPos == downer.const_data().size()){
        Util::sleep(50);
      }
      curPos = downer.const_data().size();
    }else{// streaming mode
      int s;
      static int totaal = 0;
      if ((downer.getSocket() && downer.getSocket().spool())){// || downer.getSocket().Received().size() > 0){
        s = downer.getSocket().Received().bytes(wantedLen);
        std::string buf = downer.getSocket().Received().remove(s);

        cb.dataCallback(buf.data(), s);
        totaal += s;
      }else{
        Util::sleep(50);
      }
    }
  }

  /// Readsome blocking function.
  void URIReader::readSome(char *&dataPtr, size_t &dataLen, size_t wantedLen){
    // Clear the buffer if we're finished with it
    if (allData.size() && bufPos == allData.size()){
      allData.truncate(0);
      bufPos = 0;
    }
    // Read more data if needed
    while (allData.size() < wantedLen + bufPos && *this){
      readSome(wantedLen - (allData.size() - bufPos), *this);
    }
    // Return wantedLen bytes if we have them
    if (allData.size() >= wantedLen + bufPos){
      dataPtr = allData + bufPos;
      dataLen = wantedLen;
      bufPos += wantedLen;
      return;
    }
    // Ok, we have a short count. Return the amount we actually got.
    dataPtr = allData + bufPos;
    dataLen = allData.size() - bufPos;
    bufPos = allData.size();
  }

  void URIReader::close(){
    // Close downloader socket if open
    downer.getSocket().close();
    // Unmap file if mapped
    if (mapped){
      munmap(mapped, totalSize);
      mapped = 0;
      totalSize = 0;
    }
    // Close file handle if open
    if (handle != -1){
      ::close(handle);
      handle = -1;
    }
  }

  void URIReader::onProgress(bool (*progressCallback)(uint8_t)){
    /// TODO: Implement
  }

  void URIReader::setBounds(size_t newMinLen, size_t newMaxLen){
    minLen = newMinLen;
    maxLen = newMaxLen;
  }

  bool URIReader::isSeekable() const{
    if (stateType == HTTP::HTTP){
      if (supportRangeRequest && totalSize != std::string::npos){return true;}
    }
    return (stateType == HTTP::File);
  }

  bool URIReader::isEOF() const{
    if (stateType == HTTP::File){
      return (curPos >= totalSize);
    }else if (stateType == HTTP::Stream){
      if (!downer.getSocket() && !downer.getSocket().Received().available(1)){return true;}
      return false;
    }else if (stateType == HTTP::HTTP){
      if (!downer.getSocket() && !downer.getSocket().Received().available(1) && !isSeekable()){
        if (allData.size() && bufPos < allData.size()){return false;}
        return true;
      }
      if ((totalSize > 0 && curPos >= totalSize) || downer.completed() || (!totalSize && !downer.getSocket())){
        if (allData.size() && bufPos < allData.size()){return false;}
        return true;
      }
      return false;
    }
    return true;
  }

  uint64_t URIReader::getPos(){return curPos;}

  const HTTP::URL &URIReader::getURI() const{return myURI;}

  size_t URIReader::getSize() const{return totalSize;}

}// namespace HTTP
