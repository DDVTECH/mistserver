#include "downloader.h"
#include "defines.h"
#include "encode.h"
#include "timing.h"

namespace HTTP{

  Downloader::Downloader(){
    progressCallback = 0;
    connectedPort = 0;
    dataTimeout = 5;
    retryCount = 5;
    ssl = false;
    proxied = false;
    char *p = getenv("http_proxy");
    if (p){
      proxyUrl = HTTP::URL(p);
      proxied = true;
      MEDIUM_MSG("Proxying through %s", proxyUrl.getUrl().c_str());
    }
  }

  bool Downloader::isProxied() const{return proxied;}

  /// Returns a reference to the internal HTTP::Parser body element
  std::string &Downloader::data(){return H.body;}

  /// Returns a const reference to the internal HTTP::Parser body element
  const std::string &Downloader::const_data() const{return H.body;}

  /// Returns the status text of the HTTP Request.
  std::string &Downloader::getStatusText(){return H.method;}

  /// Returns the status code of the HTTP Request.
  uint32_t Downloader::getStatusCode(){return atoi(H.url.c_str());}

  /// Returns true if the HTTP Request is OK
  bool Downloader::isOk(){return (getStatusCode() == 200);}

  /// Returns the given header from the response, or empty string if it does not exist.
  std::string Downloader::getHeader(const std::string &headerName){
    return H.GetHeader(headerName);
  }

  /// Simply turns link into a HTTP::URL and calls get(const HTTP::URL&)
  bool Downloader::get(const std::string &link, Util::DataCallback &cb){
    HTTP::URL uri(link);
    return get(uri, 6, cb);
  }

  /// Sets an extra (or overridden) header to be sent with outgoing requests.
  void Downloader::setHeader(const std::string &name, const std::string &val){
    extraHeaders[name] = val;
  }

  /// Clears all extra/override headers for outgoing requests.
  void Downloader::clearHeaders(){extraHeaders.clear();}

  /// Returns a reference to the internal HTTP class instance.
  Parser &Downloader::getHTTP(){return H;}

  /// Returns a reference to the internal Socket::Connection class instance.
  Socket::Connection &Downloader::getSocket(){return S;}

  Downloader::~Downloader(){S.close();}

  /// Sends a request for the given URL, does no waiting.
  void Downloader::doRequest(const HTTP::URL &link, const std::string &method, const std::string &body){
    if (!canRequest(link)){return;}
    bool needSSL = (link.protocol == "https");
    H.Clean();
    // Reconnect if needed
    if (!proxied || needSSL){
      if (!getSocket() || link.host != connectedHost || link.getPort() != connectedPort || needSSL != ssl){
        getSocket().close();
        connectedHost = link.host;
        connectedPort = link.getPort();
#ifdef SSL
        if (needSSL){
          S.open(connectedHost, connectedPort, true, true);
        }else{
          S.open(connectedHost, connectedPort, true);
        }
#else
        S.open(connectedHost, connectedPort, true);
#endif
      }
    }else{
      if (!getSocket() || proxyUrl.host != connectedHost || proxyUrl.getPort() != connectedPort || needSSL != ssl){
        getSocket().close();
        connectedHost = proxyUrl.host;
        connectedPort = proxyUrl.getPort();
        S.open(connectedHost, connectedPort, true);
      }
    }
    ssl = needSSL;
    if (!getSocket()){
      H.method = S.getError();
      return; // socket is closed
    }
    if (proxied && !ssl){
      H.url = link.getProxyUrl();
      if (link.port.size()){
        H.SetHeader("Host", link.host + ":" + link.port);
      }else{
        H.SetHeader("Host", link.host);
      }
    }else{
      H.url = "/" + Encodings::URL::encode(link.path, "/:=@[]");
      if (link.args.size()){H.url += "?" + link.args;}
      if (link.port.size()){
        H.SetHeader("Host", link.host + ":" + link.port);
      }else{
        H.SetHeader("Host", link.host);
      }
    }

    if (method.size()){H.method = method;}
    H.SetHeader("User-Agent", "MistServer " PACKAGE_VERSION);
    H.SetHeader("X-Version", PACKAGE_VERSION);
    H.SetHeader("Accept", "*/*");
    if (authStr.size() && (link.user.size() || link.pass.size())){
      H.auth(link.user, link.pass, authStr);
    }
    if (proxyAuthStr.size() && (proxyUrl.user.size() || proxyUrl.pass.size())){
      H.auth(proxyUrl.user, proxyUrl.pass, proxyAuthStr, "Proxy-Authorization");
    }
    if (extraHeaders.size()){
      for (std::map<std::string, std::string>::iterator it = extraHeaders.begin();
           it != extraHeaders.end(); ++it){
        H.SetHeader(it->first, it->second);
      }
    }

    nbLink = link;
    H.SendRequest(getSocket(), body);
    H.Clean();
  }

  /// Do a HEAD request to download the HTTP headers only, returns true on success
  bool Downloader::head(const HTTP::URL &link, uint8_t maxRecursiveDepth){
    if (!canRequest(link)){return false;}
    size_t loop = retryCount + 1; // max 5 attempts
    while (--loop){// loop while we are unsuccessful
      MEDIUM_MSG("Retrieving %s (%zu/%" PRIu32 ")", link.getUrl().c_str(), retryCount - loop + 1, retryCount);
      doRequest(link, "HEAD");
      if (!getSocket()){
        FAIL_MSG("Could not retrieve %s: %s", link.getUrl().c_str(), getSocket().getError().c_str());
        return false;
      }
      H.headerOnly = true;
      uint64_t reqTime = Util::bootSecs();
      while (getSocket() && Util::bootSecs() < reqTime + dataTimeout){
        // No data? Wait for a second or so.
        if (!getSocket().spool()){
          if (progressCallback != 0){
            if (!progressCallback()){
              WARN_MSG("Download aborted by callback");
              H.headerOnly = false;
              return false;
            }
          }
          Util::sleep(250);
          continue;
        }
        // Data! Check if we can parse it...
        if (H.Read(getSocket())){
          H.headerOnly = false;
          if (shouldContinue()){
            if (maxRecursiveDepth == 0){
              FAIL_MSG("Maximum recursion depth reached");
              return false;
            }
            if (!canContinue(link)){return false;}
            if (getStatusCode() >= 300 && getStatusCode() < 400){
              return head(link.link(getHeader("Location")), --maxRecursiveDepth);
            }else{
              return head(link, --maxRecursiveDepth);
            }
          }

          if(H.protocol == "HTTP/1.0"){
            getSocket().close();
          }

          H.headerOnly = false;
          return true; // Success!
        }
        // reset the data timeout
        if (reqTime != Util::bootSecs()){
          if (progressCallback != 0){
            if (!progressCallback()){
              WARN_MSG("Download aborted by callback");
              H.headerOnly = false;
              return false;
            }
          }
          reqTime = Util::bootSecs();
        }
      }
      H.headerOnly = false;

      if (getSocket()){
        FAIL_MSG("Timeout while retrieving %s (%zu/%" PRIu32 ")", link.getUrl().c_str(),
                 retryCount - loop + 1, retryCount);
        H.Clean();
        getSocket().close();
      }else{
        if (retryCount - loop + 1 > 2){
          INFO_MSG("Lost connection while retrieving %s (%zu/%" PRIu32 ")", link.getUrl().c_str(), retryCount - loop + 1, retryCount);
        }else{
          MEDIUM_MSG("Lost connection while retrieving %s (%zu/%" PRIu32 ")", link.getUrl().c_str(), retryCount - loop + 1, retryCount);
        }
        H.Clean();
      }
      Util::sleep(500); // wait a bit before retrying
    }
    FAIL_MSG("Could not retrieve %s", link.getUrl().c_str());
    return false;
  }

  bool Downloader::getRangeNonBlocking(const HTTP::URL &link, size_t byteStart, size_t byteEnd, Util::DataCallback &cb){
    char tmp[32];
    if (byteEnd <= 0){// get range from byteStart til eof
      sprintf(tmp, "bytes=%zu-", byteStart);
    }else{
      sprintf(tmp, "bytes=%zu-%zu", byteStart, byteEnd - 1);
    }
    setHeader("Range", tmp);
    return getNonBlocking(link, 6);
  }

  bool Downloader::getRange(const HTTP::URL &link, size_t byteStart, size_t byteEnd, Util::DataCallback &cb){
    char tmp[32];
    if (byteEnd <= 0){// get range from byteStart til eof
      sprintf(tmp, "bytes=%zu-", byteStart);
    }else{
      sprintf(tmp, "bytes=%zu-%zu", byteStart, byteEnd - 1);
    }
    setHeader("Range", tmp);
    return get(link, 6, cb);
  }

  /// Downloads the given URL into 'H', returns true on success.
  /// Makes at most 5 attempts, and will wait no longer than 5 seconds without receiving data.
  bool Downloader::get(const HTTP::URL &link, uint8_t maxRecursiveDepth, Util::DataCallback &cb){
    if (!getNonBlocking(link, maxRecursiveDepth)){return false;}
    
    while (!continueNonBlocking(cb)){Util::sleep(100);}
    
    if (isComplete){return true;}
    
    FAIL_MSG("Could not retrieve %s", link.getUrl().c_str());
    return false;
  }

  // prepare a request to be handled in a nonblocking fashion by the continueNonbBocking()
  bool Downloader::getNonBlocking(const HTTP::URL &link, uint8_t maxRecursiveDepth){
    if (!canRequest(link)){return false;}
    nbLink = link;
    nbMaxRecursiveDepth = maxRecursiveDepth;
    nbLoop = retryCount + 1; // max 5 attempts
    isComplete = false;
    doRequest(nbLink);
    nbReqTime = Util::bootSecs();
    return true;
  }

  const HTTP::URL & Downloader::lastURL(){
    return nbLink;
  }

  // continue handling a request, origininally set up by the getNonBlocking() function
  // returns true if the request is complete
  bool Downloader::continueNonBlocking(Util::DataCallback &cb){
    while (true){
      if (!getSocket() && !isComplete){
        if (nbLoop < 2){
          FAIL_MSG("Exceeded retry limit while retrieving %s (%zu/%" PRIu32 ")",
                   nbLink.getUrl().c_str(), retryCount - nbLoop + 1, retryCount);
          Util::sleep(1000);
          return true;
        }
        nbLoop--;
        if (nbLoop == retryCount){
          MEDIUM_MSG("Retrieving %s (%zu/%" PRIu32 ")", nbLink.getUrl().c_str(),
                     retryCount - nbLoop + 1, retryCount);
        }else{
          if (retryCount - nbLoop + 1 > 2){
            INFO_MSG("Lost connection while retrieving %s (%zu/%" PRIu32 ")",
                     nbLink.getUrl().c_str(), retryCount - nbLoop + 1, retryCount);
          }else{
            MEDIUM_MSG("Lost connection while retrieving %s (%zu/%" PRIu32 ")",
                       nbLink.getUrl().c_str(), retryCount - nbLoop + 1, retryCount);
          }
        }

        if (H.hasHeader("Accept-Ranges") && getHeader("Accept-Ranges").size() > 0){
          getRangeNonBlocking(nbLink, H.currentLength, 0, cb);
          return true;
        }else{
          doRequest(nbLink);
        }

        if (!getSocket()){
          WARN_MSG("Aborting download: could not open connection");
          return true;
        }
        nbReqTime = Util::bootSecs();
      }

      if (Util::bootSecs() >= nbReqTime + dataTimeout){
        FAIL_MSG("Timeout while retrieving %s (%zu/%" PRIu32 ")", nbLink.getUrl().c_str(),
                 retryCount - nbLoop + 1, retryCount);
        getSocket().close();
        return false; // because we may have retries left
      }

      // No data? Wait for a second or so.
      if (!getSocket().spool()){
        if (progressCallback != 0){
          if (!progressCallback()){
            WARN_MSG("Download aborted by callback");
            return true;
          }
        }
        return false;
      }
      // Data! Check if we can parse it...
      if (H.Read(getSocket(), cb)){
        if (shouldContinue()){
          if (nbMaxRecursiveDepth == 0){
            FAIL_MSG("Maximum recursion depth reached");
            return true;
          }
          if (!canContinue(nbLink)){return false;}
          --nbMaxRecursiveDepth;
          if (getStatusCode() >= 300 && getStatusCode() < 400){
            nbLink = nbLink.link(getHeader("Location"));
          }
          doRequest(nbLink);
          return false;
        }

        isComplete = true; // Success
        return true;
      }
      // reset the data timeout
      if (nbReqTime != Util::bootSecs()){
        if (progressCallback != 0){
          if (!progressCallback()){
            WARN_MSG("Download aborted by callback");
            return true;
          }
        }
        nbReqTime = Util::bootSecs();
      }
    }
    WARN_MSG("Invalid connection state for HTTP request");
    return false; //we should never get here
  }

  bool Downloader::post(const HTTP::URL &link, const std::string &payload, bool sync, uint8_t maxRecursiveDepth){
    if (!canRequest(link)){return false;}
    size_t loop = retryCount; // max 5 attempts
    while (--loop){// loop while we are unsuccessful
      MEDIUM_MSG("Posting to %s (%zu/%" PRIu32 ")", link.getUrl().c_str(), retryCount - loop + 1, retryCount);
      doRequest(link, "POST", payload);
      // Not synced? Ignore the response and immediately return true.
      if (!sync){return true;}
      uint64_t reqTime = Util::bootSecs();
      while (getSocket() && Util::bootSecs() < reqTime + dataTimeout){
        // No data? Wait for a second or so.
        if (!getSocket().spool()){
          if (progressCallback != 0){
            if (!progressCallback()){
              WARN_MSG("Download aborted by callback");
              return false;
            }
          }
          Util::sleep(250);
          continue;
        }
        // Data! Check if we can parse it...
        if (H.Read(getSocket())){
          if (shouldContinue()){
            if (maxRecursiveDepth == 0){
              FAIL_MSG("Maximum recursion depth reached");
              return false;
            }
            if (!canContinue(link)){return false;}
            if (getStatusCode() >= 300 && getStatusCode() < 400){
              return post(link.link(getHeader("Location")), payload, sync, --maxRecursiveDepth);
            }else{
              return post(link, payload, sync, --maxRecursiveDepth);
            }
          }
          return true; // Success!
        }
        // reset the data timeout
        if (reqTime != Util::bootSecs()){
          if (progressCallback != 0){
            if (!progressCallback()){
              WARN_MSG("Download aborted by callback");
              return false;
            }
          }
          reqTime = Util::bootSecs();
        }
      }
      if (getSocket()){
        FAIL_MSG("Timeout while retrieving %s", link.getUrl().c_str());
        return false;
      }
      Util::sleep(500); // wait a bit before retrying
    }
    FAIL_MSG("Could not retrieve %s", link.getUrl().c_str());
    return false;
  }

  bool Downloader::canRequest(const HTTP::URL &link){
    if (!link.host.size()){return false;}
    if (link.protocol != "http" && link.protocol != "https"){
      FAIL_MSG("Protocol not supported: %s", link.protocol.c_str());
      return false;
    }
#ifndef SSL
    if (link.protocol == "https"){
      FAIL_MSG("Protocol not supported: %s", link.protocol.c_str());
      return false;
    }
#endif
    return true;
  }

  bool Downloader::shouldContinue(){
    if (H.hasHeader("Set-Cookie")){
      std::string cookie = H.GetHeader("Set-Cookie");
      setHeader("Cookie", cookie.substr(0, cookie.find(';')));
    }
    uint32_t sCode = getStatusCode();
    if (sCode == 401 || sCode == 407 || (sCode >= 300 && sCode < 400)){return true;}
    return false;
  }

  bool Downloader::canContinue(const HTTP::URL &link){
    if (getStatusCode() == 401){
      // retry with authentication
      if (H.hasHeader("WWW-Authenticate")){authStr = H.GetHeader("WWW-Authenticate");}
      if (H.hasHeader("Www-Authenticate")){authStr = H.GetHeader("Www-Authenticate");}
      if (!authStr.size()){
        FAIL_MSG("Authentication required but no WWW-Authenticate header present");
        return false;
      }
      if (!link.user.size() && !link.pass.size()){
        FAIL_MSG("Authentication required but not included in URL");
        return false;
      }
      INFO_MSG("Authenticating...");
      return true;
    }
    if (getStatusCode() == 407){
      // retry with authentication
      if (H.hasHeader("Proxy-Authenticate")){proxyAuthStr = H.GetHeader("Proxy-Authenticate");}
      if (!proxyAuthStr.size()){
        FAIL_MSG("Proxy authentication required but no Proxy-Authenticate header present");
        return false;
      }
      if (!proxyUrl.user.size() && !proxyUrl.pass.size()){
        FAIL_MSG("Proxy authentication required but not included in URL");
        return false;
      }
      INFO_MSG("Authenticating proxy...");
      return true;
    }
    if (getStatusCode() >= 300 && getStatusCode() < 400){
      // follow redirect
      std::string location = getHeader("Location");
      if (!location.size()){return false;}
      INFO_MSG("Following redirect to %s", location.c_str());
      return true;
    }
    return false;
  }

}// namespace HTTP

