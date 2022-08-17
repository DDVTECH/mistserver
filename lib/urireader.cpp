#include "defines.h"
#include "shared_memory.h"
#include "timing.h"
#include "urireader.h"
#include "util.h"
#include "encode.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <cstdlib>
#include <ctime>

namespace HTTP{

#ifdef SSL
  inline bool s3CalculateSignature(std::string& signature, const std::string method, const std::string date, const std::string& requestPath, const std::string& accessKey, const std::string& secret) {
    std::string toSign = method + "\n\n\n" + date + "\n" + requestPath;
    unsigned char signatureBytes[MBEDTLS_MD_MAX_SIZE];
    const int sha1Size = 20;
    mbedtls_md_context_t md_ctx = {0};
    // TODO: When we use MBEDTLS_MD_SHA512 ? Consult documentation/code
    const mbedtls_md_info_t *md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
    if (!md_info){ FAIL_MSG("error s3 MBEDTLS_MD_SHA1 unavailable"); return false; }
    int status = mbedtls_md_setup(&md_ctx, md_info, 1);
    if(status != 0) { FAIL_MSG("error s3 mbedtls_md_setup error %d", status); return false; }
    status = mbedtls_md_hmac_starts(&md_ctx, (const unsigned char *)secret.c_str(), secret.size());
    if(status != 0) { FAIL_MSG("error s3 mbedtls_md_hmac_starts error %d", status); return false; }
    status = mbedtls_md_hmac_update(&md_ctx, (const unsigned char *)toSign.c_str(), toSign.size());
    if(status != 0) { FAIL_MSG("error s3 mbedtls_md_hmac_update error %d", status); return false; }
    status = mbedtls_md_hmac_finish(&md_ctx, signatureBytes);
    if(status != 0) { FAIL_MSG("error s3 mbedtls_md_hmac_finish error %d", status); return false; }
    std::string base64encoded = Encodings::Base64::encode(std::string((const char*)signatureBytes, sha1Size));
    signature = "AWS " + accessKey + ":" + base64encoded;
    return true;
  }
#endif // ifdef SSL


  inline HTTP::URL injectHeaders(const HTTP::URL& url, const std::string & method, HTTP::Downloader & downer) {
#ifdef SSL
    // Input url == s3+https://s3_key:secret@storage.googleapis.com/alexk-dms-upload-test/testvideo.ts
    // Transform to: 
    // url=https://storage.googleapis.com/alexk-dms-upload-test/testvideo.ts
    // header Date: ${Util::getDateString(()}
    // header Authorization: AWS ${url.user}:${signature}
    if (url.protocol.size() > 3 && url.protocol.substr(0, 3) == "s3+"){
      std::string date = Util::getDateString();
      HTTP::URL newUrl = url;
      // remove "s3+" prefix
      newUrl.protocol = newUrl.protocol.erase(0, 3); 
      // Use user and pass to create signature and remove from HTTP request
      std::string accessKey(url.user), secret(url.pass);
      newUrl.user = "";
      newUrl.pass = "";
      std::string requestPath = "/" + Encodings::URL::encode(url.path, "/:=@[]#?&");
      if(url.args.size()) requestPath += "?" + url.args;
      std::string authLine;
      // Calculate Authorization data
      if(method.size() && s3CalculateSignature(authLine, method, date, requestPath, accessKey, secret)) {
        downer.setHeader("Date", date);
        downer.setHeader("Authorization", authLine);
      }
      return newUrl;
    }
#endif // ifdef SSL
    return url;
  }


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
    close();
    myURI = uri;
    originalUrl = myURI;

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

    // prepare for s3 and http
#ifdef SSL
    // In case of s3 URI we prepare HTTP request with AWS authorization and rely on HTTP logic below
    if (myURI.protocol == "s3+https" || myURI.protocol == "s3+http"){
      // Check fallback to global credentials in env vars
      bool haveCredentials = myURI.user.size() && myURI.pass.size();
      if(!haveCredentials) {
        // Use environment variables
        char * envValue = std::getenv("S3_ACCESS_KEY_ID");
        if(envValue == NULL) {
          FAIL_MSG("error s3 uri without credentials. Consider setting S3_ACCESS_KEY_ID env var");
          return false;
        }
        myURI.user = envValue;
        envValue = std::getenv("S3_SECRET_ACCESS_KEY");
        if(envValue == NULL) {
          FAIL_MSG("error s3 uri without credentials. Consider setting S3_SECRET_ACCESS_KEY env var");
          return false;
        }
        myURI.pass = envValue;
      }
      myURI = injectHeaders(originalUrl, "", downer);
      // Do not return, continue to HTTP case
    }
#endif // ifdef SSL

    // HTTP, stream or regular download?
    if (myURI.protocol == "http" || myURI.protocol == "https"){
      stateType = HTTP;
      downer.clearHeaders();

      // One set of headers specified for HEAD request
      injectHeaders(originalUrl, "HEAD", downer);
      // Send HEAD request to determine range request is supported, and get total length
      if (userAgentOverride.size()){downer.setHeader("User-Agent", userAgentOverride);}
      if (!downer.head(myURI) || !downer.isOk()){
        FAIL_MSG("Error getting URI info for '%s': %" PRIu32 " %s", myURI.getUrl().c_str(),
                 downer.getStatusCode(), downer.getStatusText().c_str());
        // Close the socket, and clean up the buffer
        downer.getSocket().close();
        downer.getSocket().Received().clear();
        allData.truncate(0);
        if (!downer.isOk()){return false;}
        supportRangeRequest = false;
        totalSize = std::string::npos;
      }else{
        supportRangeRequest = (downer.getHeader("Accept-Ranges").size() > 0);
        std::string header1 = downer.getHeader("Content-Length");
        if (header1.size()){totalSize = atoi(header1.c_str());}
        myURI = downer.lastURL();
      }

      // Other set of headers specified for GET request
      injectHeaders(originalUrl, "GET", downer);
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
    //Seeking in a non-seekable source? No-op, always fails.
    if (!isSeekable()){return false;}

    //Reset internal buffers
    allData.truncate(0);
    bufPos = 0;

    //Files always succeed because we use memmap
    if (stateType == HTTP::File){
      curPos = pos;
      return true;
    }

    //HTTP-based needs to do a range request
    if (stateType == HTTP::HTTP && supportRangeRequest){
      downer.getSocket().close();
      downer.getSocket().Received().clear();
      injectHeaders(originalUrl, "GET", downer);
      if (!downer.getRangeNonBlocking(myURI.getUrl(), pos, 0)){
        FAIL_MSG("Error making range request");
        return false;
      }
      curPos = pos;
      return true;
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
      if ((downer.getSocket() && downer.getSocket().spool())){// || downer.getSocket().Received().size() > 0){
        s = downer.getSocket().Received().bytes(wantedLen);
        std::string buf = downer.getSocket().Received().remove(s);

        cb.dataCallback(buf.data(), s);
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
    while (allData.size() < wantedLen + bufPos && *this && !downer.completed()){
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
    //Wipe internal state
    curPos = 0;
    allData.truncate(0);
    bufPos = 0;
    // Close downloader socket if open
    downer.getSocket().close();
    downer.getSocket().Received().clear();
    downer.getHTTP().Clean();
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
      if ((totalSize != std::string::npos && curPos >= totalSize) || downer.completed() || (totalSize == std::string::npos && !downer.getSocket())){
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
