#include "defines.h"
#include "shared_memory.h"
#include "timing.h"
#include "urireader.h"
#include "util.h"
#include <sys/mman.h>
#include <sys/stat.h>

namespace HTTP{


  void URIReader::init(){
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
  }

  URIReader::URIReader(){
    init();
  }

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
  void URIReader::dataCallback(const char *ptr, size_t size){
    std::string t = std::string(ptr, size);
    allData.append(t.c_str(), size);
  }

  bool URIReader::open(const HTTP::URL &uri){
    myURI = uri;
    curPos = 0;

    if (!myURI.protocol.size() || myURI.protocol == "file"){
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
        int handle = ::open(myURI.getFilePath().c_str(), O_RDONLY);
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
      if (!downer.head(myURI) || !downer.isOk()){
        FAIL_MSG("Error getting URI info for '%s': %" PRIu32 " %s", myURI.getUrl().c_str(), downer.getStatusCode(), downer.getStatusText().c_str());
        if (!downer.isOk()){
          return false;
        }
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
    size_t s = 0;
    char *tmp = 0;
    std::string t;

    allData.allocate(68401307);

    while (!isEOF()){
      readSome(10046, *this);
      // readSome(1048576, *this);
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

      bool res = downer.continueNonBlocking(cb);

      if (res){
        if (downer.completed()){
          MEDIUM_MSG("completed");
        }else{
          if (supportRangeRequest){
            MEDIUM_MSG("do new range request, previous request not completed yet!, curpos: %zu, "
                       "length: %zu",
                       curPos, getSize());
          }
        }
      }else{
        Util::sleep(10);
      }
    }else{// streaming mode
      int s;
      static int totaal = 0;
      if ((downer.getSocket() && downer.getSocket().spool())){// || downer.getSocket().Received().size() > 0){
        s = downer.getSocket().Received().bytes(wantedLen);
        std::string buf = downer.getSocket().Received().remove(s);

        cb.dataCallback(buf.data(), s);
        totaal += s;
      }
    }
  }

  /// Readsome blocking function.
  void URIReader::readSome(char *&dataPtr, size_t &dataLen, size_t wantedLen){
    if (stateType == HTTP::File){

      dataPtr = mapped + curPos;

      if (wantedLen < totalSize){
        if ((wantedLen + curPos) > totalSize){
          dataLen = totalSize - curPos; // restant
        }else{
          dataLen = wantedLen;
        }
      }else{
        dataLen = totalSize;
      }

      curPos += dataLen;
    }else if (stateType == HTTP::HTTP){

      dataLen = downer.data().size();
      curPos += dataLen;
      dataPtr = (char *)downer.data().data();
    }else{
      if (clearPointer){
        rPtr.assign(0, 0);
        clearPointer = false;
        dataLen = 0;
        rPtr.allocate(wantedLen);
      }

      int s;
      bool run = true;
      while (downer.getSocket() && run){
        if (downer.getSocket().spool()){

          if (wantedLen < 8000){
            s = downer.getSocket().Received().bytes(wantedLen);
          }else{
            s = downer.getSocket().Received().bytes(8000);
          }

          std::string buf = downer.getSocket().Received().remove(s);
          rPtr.append(buf.c_str(), s);

          dataLen += s;
          curPos += s;

          if (rPtr.size() >= wantedLen){
            dataLen = rPtr.size();
            dataPtr = rPtr;
            //            INFO_MSG("laatste stukje, datalen: %llu, wanted: %llu", dataLen,
            //            wantedLen); dataCallback(ptr, len);
            clearPointer = true;
            run = false;
          }
          //}
        }else{
          //                  INFO_MSG("data not yet available!");
          return;
        }
      }

      // if (!downer.getSocket()){
      totalSize = curPos;
      dataLen = rPtr.size();
      //}
      // INFO_MSG("size: %llu, datalen: %llu", totalSize, rPtr.size());
      dataPtr = rPtr;
    }
  }

  void URIReader::close(){
    if (stateType == HTTP::File){
      if (mapped){
        munmap(mapped, totalSize);
        mapped = 0;
        totalSize = 0;
      }
    }else if (stateType == HTTP::Stream){
      downer.getSocket().close();
    }else if (stateType == HTTP::HTTP){
      downer.getSocket().close();
    }else{
      // INFO_MSG("already closed");
    }
  }

  void URIReader::onProgress(bool (*progressCallback)(uint8_t)){
    /// TODO: Implement
  }

  void URIReader::setBounds(size_t newMinLen, size_t newMaxLen){
    minLen = newMinLen;
    maxLen = newMaxLen;
  }

  bool URIReader::isSeekable(){
    if (stateType == HTTP::HTTP){

      if (supportRangeRequest && totalSize != std::string::npos){return true;}
    }

    return (stateType == HTTP::File);
  }

  bool URIReader::isEOF(){
    if (stateType == HTTP::File){
      return (curPos >= totalSize);
    }else if (stateType == HTTP::Stream){
      if (!downer.getSocket()){return true;}

      // if ((totalSize > 0) && (curPos >= totalSize)){return true;}
    }else if (stateType == HTTP::HTTP){
      // INFO_MSG("iseof, C: %s, seekable: %s", C?"connected":"disconnected", isSeekable()?"yes":"no");
      if (!downer.getSocket() && !downer.getSocket().Received().available(1) && !isSeekable()){
        return true;
      }
      if ((totalSize > 0) && (curPos >= totalSize)){return true;}

      // mark as complete if downer reports download is completed, or when socket connection is closed when totalsize is not known.
      if (downer.completed() || (!totalSize && !downer.getSocket())){
        // INFO_MSG("complete totalsize: %llu, %s", totalSize, downer.getSocket() ? "Connected" : "disconnected");
        return true;
      }

    }else{
      return true;
    }

    return false;
  }

  bool URIReader::isGood() const{
    return true;
    /// TODO: Implement
  }

  uint64_t URIReader::getPos(){return curPos;}

  const HTTP::URL &URIReader::getURI() const{return myURI;}

  size_t URIReader::getSize() const{return totalSize;}

}// namespace HTTP
