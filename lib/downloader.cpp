#include "downloader.h"
#include "defines.h"
#include "timing.h"
#include "encode.h"

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
  bool Downloader::get(const std::string &link){
    HTTP::URL uri(link);
    return get(uri);
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
  Socket::Connection &Downloader::getSocket(){
#ifdef SSL
    if (ssl){return S_SSL;}
#endif
    return S;
  }

  /// Sends a request for the given URL, does no waiting.
  void Downloader::doRequest(const HTTP::URL &link, const std::string &method, const std::string &body){
    if (!canRequest(link)){return;}
    bool needSSL = (link.protocol == "https");
    H.Clean();
    // Reconnect if needed
    if (!proxied || needSSL){
      if (!getSocket() || link.host != connectedHost || link.getPort() != connectedPort ||
          needSSL != ssl){
        getSocket().close();
        connectedHost = link.host;
        connectedPort = link.getPort();
#ifdef SSL
        if (needSSL){
          S_SSL = Socket::SSLConnection(connectedHost, connectedPort, true);
        }else{
          S = Socket::Connection(connectedHost, connectedPort, true);
        }
#else
        S = Socket::Connection(connectedHost, connectedPort, true);
#endif
      }
    }else{
      if (!getSocket() || proxyUrl.host != connectedHost || proxyUrl.getPort() != connectedPort ||
          needSSL != ssl){
        getSocket().close();
        connectedHost = proxyUrl.host;
        connectedPort = proxyUrl.getPort();
        S = Socket::Connection(connectedHost, connectedPort, true);
      }
    }
    ssl = needSSL;
    if (!getSocket()){
      return; // socket is closed
    }
    if (proxied && !ssl){
      H.url = link.getProxyUrl();
      if (proxyUrl.port.size()){
        H.SetHeader("Host", proxyUrl.host + ":" + proxyUrl.port);
      }else{
        H.SetHeader("Host", proxyUrl.host);
      }
    }else{
      H.url = "/" + Encodings::URL::encode(link.path);
      if (link.args.size()){H.url += "?" + link.args;}
      if (link.port.size()){
        H.SetHeader("Host", link.host + ":" + link.port);
      }else{
        H.SetHeader("Host", link.host);
      }
    }
    if (method.size()){
      H.method = method;
    }
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
    H.SendRequest(getSocket(), body);
    H.Clean();
  }

  /// Downloads the given URL into 'H', returns true on success.
  /// Makes at most 5 attempts, and will wait no longer than 5 seconds without receiving data.
  bool Downloader::get(const HTTP::URL &link, uint8_t maxRecursiveDepth){
    if (!canRequest(link)){return false;}
    unsigned int loop = retryCount+1; // max 5 attempts
    while (--loop){// loop while we are unsuccessful
      MEDIUM_MSG("Retrieving %s (%lu/%lu)", link.getUrl().c_str(), retryCount-loop+1, retryCount);
      doRequest(link);
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
              return get(link.link(getHeader("Location")), --maxRecursiveDepth);
            }else{
              return get(link, --maxRecursiveDepth);
            }
          }
          return true; // Success!
        }
        // reset the data timeout
        reqTime = Util::bootSecs();
      }
      if (getSocket()){
        FAIL_MSG("Timeout while retrieving %s (%lu/%lu)", link.getUrl().c_str(), retryCount-loop+1, retryCount);
        getSocket().close();
      }else{
        FAIL_MSG("Lost connection while retrieving %s (%lu/%lu)", link.getUrl().c_str(), retryCount-loop+1, retryCount);
      }
      Util::sleep(500); // wait a bit before retrying
    }
    FAIL_MSG("Could not retrieve %s", link.getUrl().c_str());
    return false;
  }
  
  bool Downloader::post(const HTTP::URL &link, const std::string &payload, bool sync, uint8_t maxRecursiveDepth){
    if (!canRequest(link)){return false;}
    unsigned int loop = retryCount; // max 5 attempts
    while (--loop){// loop while we are unsuccessful
      MEDIUM_MSG("Posting to %s (%lu/%lu)", link.getUrl().c_str(), retryCount-loop+1, retryCount);
      doRequest(link, "POST", payload);
      //Not synced? Ignore the response and immediately return false.
      if (!sync){return false;}
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
        reqTime = Util::bootSecs();
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
    if (sCode == 401 || sCode == 407 || (sCode >= 300 && sCode < 400)){
      return true;
    }
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
      FAIL_MSG("Authenticating...");
      return true;
    }
    if (getStatusCode() == 407){
      // retry with authentication
      if (H.hasHeader("Proxy-Authenticate")){
        proxyAuthStr = H.GetHeader("Proxy-Authenticate");
      }
      if (!proxyAuthStr.size()){
        FAIL_MSG("Proxy authentication required but no Proxy-Authenticate header present");
        return false;
      }
      if (!proxyUrl.user.size() && !proxyUrl.pass.size()){
        FAIL_MSG("Proxy authentication required but not included in URL");
        return false;
      }
      FAIL_MSG("Authenticating proxy...");
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

