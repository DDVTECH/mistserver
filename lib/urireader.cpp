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

  HTTP::URL localURIResolver(){
    char workDir[512];
    getcwd(workDir, 512);
    return HTTP::URL(std::string("file://") + workDir + "/");
  }

  void URIReader::init(){
    handle = -1;
    mapped = 0;
    myURI = localURIResolver();
    originalUrl = myURI;
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

  bool URIReader::open(const std::string &reluri){return open(originalUrl.link(reluri));}

  /// Internal callback function, used to buffer data.
  void URIReader::dataCallback(const char *ptr, size_t size){allData.append(ptr, size);}

  size_t URIReader::getDataCallbackPos() const{return allData.size();}

  bool URIReader::open(const int fd){
    close();
    myURI = HTTP::URL("file://-");
    originalUrl = myURI;
    downer.getSocket().open(-1, fd);
    stateType = HTTP::Stream;
    startPos = 0;
    endPos = std::string::npos;
    totalSize = std::string::npos;
    return true;
  }

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
      downer.clean();
      curPos = pos;
      injectHeaders(originalUrl, "GET", downer);
      if (!downer.getRangeNonBlocking(myURI, pos, 0)){
        FAIL_MSG("Error making range request");
        return false;
      }
      return true;
    }
    return false;
  }

  std::string URIReader::getHost() const{
    if (stateType == HTTP::File){return "";}
    return downer.getSocket().getHost();
  }

  std::string URIReader::getBinHost() const{
    if (stateType == HTTP::File){return std::string("\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000", 16);}
    return downer.getSocket().getBinHost();
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
    // Files read from the memory-mapped file
    if (stateType == HTTP::File){
      // Simple bounds check, don't read beyond the end of the file
      uint64_t dataLen = ((wantedLen + curPos) > totalSize) ? totalSize - curPos : wantedLen;
      cb.dataCallback(mapped + curPos, dataLen);
      curPos += dataLen;
      return;
    }
    // HTTP-based read from the Downloader
    if (stateType == HTTP::HTTP){
      // Note: this function returns true if the full read was completed only.
      // It's the reason this function returns void rather than bool.
      downer.continueNonBlocking(cb);
      return;
    }
    // Everything else uses the socket directly
    int s = downer.getSocket().Received().bytes(wantedLen);
    if (!s){
      // Only attempt to read more if nothing was in the buffer
      if (downer.getSocket() && downer.getSocket().spool()){
        s = downer.getSocket().Received().bytes(wantedLen);
      }else{
        Util::sleep(50);
        return;
      }
    }
    // Future optimization: augment the Socket::Buffer to handle a Util::DataCallback as argument.
    // Would remove the need for this extra copy here.
    Util::ResizeablePointer buf;
    downer.getSocket().Received().remove(buf, s);
    cb.dataCallback(buf, s);
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
    downer.clean();
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

  const HTTP::URL &URIReader::getURI() const{return originalUrl;}

  size_t URIReader::getSize() const{return totalSize;}

}// namespace HTTP
