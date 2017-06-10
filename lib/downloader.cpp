#include "downloader.h"
#include "defines.h"
#include "timing.h"

namespace HTTP{

  /// Returns a reference to the internal HTTP::Parser body element
  std::string &Downloader::data(){return H.body;}

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

  /// Downloads the given URL into 'H', returns true on success.
  /// Makes at most 5 attempts, and will wait no longer than 5 seconds without receiving data.
  bool Downloader::get(const HTTP::URL &link, uint8_t maxRecursiveDepth){
    if (!link.host.size()){return false;}
    if (link.protocol != "http"){
      FAIL_MSG("Protocol not supported: %s", link.protocol.c_str());
      return false;
    }
    INFO_MSG("Retrieving %s", link.getUrl().c_str());
    unsigned int loop = 6; // max 5 attempts

    while (--loop){// loop while we are unsuccessful
      H.Clean();
      // Reconnect if needed
      if (!S || link.host != connectedHost || link.getPort() != connectedPort){
        S.close();
        connectedHost = link.host;
        connectedPort = link.getPort();
        S = Socket::Connection(connectedHost, connectedPort, true);
      }
      H.url = "/" + link.path;
      if (link.args.size()){H.url += "?" + link.args;}
      if (link.port.size()){
        H.SetHeader("Host", link.host + ":" + link.port);
      }else{
        H.SetHeader("Host", link.host);
      }
      H.SetHeader("User-Agent", "MistServer " PACKAGE_VERSION);
      H.SetHeader("X-Version", PACKAGE_VERSION);
      H.SetHeader("Accept", "*/*");
      if (extraHeaders.size()){
        for (std::map<std::string, std::string>::iterator it = extraHeaders.begin();
             it != extraHeaders.end(); ++it){
          H.SetHeader(it->first, it->second);
        }
      }
      H.SendRequest(S);
      H.Clean();
      uint64_t reqTime = Util::bootSecs();
      while (S && Util::bootSecs() < reqTime + 5){
        // No data? Wait for a second or so.
        if (!S.spool()){
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
        if (H.Read(S)){
          if (getStatusCode() >= 300 && getStatusCode() < 400){
            // follow redirect
            std::string location = getHeader("Location");
            if (maxRecursiveDepth == 0){
              FAIL_MSG("Maximum redirect depth reached: %s", location.c_str());
              return false;
            }else{
              FAIL_MSG("Following redirect to %s", location.c_str());
              return get(link.link(location), maxRecursiveDepth--);
            }
          }
          return true; // Success!
        }
        // reset the 5 second timeout
        reqTime = Util::bootSecs();
      }
      if (S){
        FAIL_MSG("Timeout while retrieving %s", link.getUrl().c_str());
        return false;
      }
      Util::sleep(500); // wait a bit before retrying
    }
    FAIL_MSG("Could not retrieve %s", link.getUrl().c_str());
    return false;
  }
}

