/// \file http_parser.cpp
/// Holds all code for the HTTP namespace.

#include "defines.h"
#include "encode.h"
#include "url.h"

/// Helper function to check if the given c-string is numeric or not
static bool is_numeric(const char *str){
  while (str[0] != 0){
    if (str[0] < 48 || str[0] > 57){return false;}
    ++str;
  }
  return true;
}

/// Constructor that does the actual parsing
HTTP::URL::URL(const std::string &url){
  IPv6Addr = false;
  // first detect protocol at the start, if any
  size_t proto_sep = url.find("://");
  if (proto_sep != std::string::npos){
    protocol = url.substr(0, proto_sep);
    proto_sep += 3;
  }else{
    proto_sep = 0;
    if (url.substr(0, 2) == "//"){proto_sep = 2;}
  }
  // proto_sep now points to the start of the host, guaranteed
  // continue by finding the path, if any
  size_t first_slash = url.find_first_of("/?#", proto_sep);
  if (first_slash != std::string::npos){
    if (url[first_slash] == '/'){
      path = url.substr(first_slash + 1);
    }else{
      path = url.substr(first_slash);
    }
    if (protocol != "rtsp"){
      size_t hmark = path.find('#');
      if (hmark != std::string::npos){
        frag = Encodings::URL::decode(path.substr(hmark + 1), true);
        path.erase(hmark);
      }
      size_t qmark = path.find('?');
      if (qmark != std::string::npos){
        args = path.substr(qmark + 1);
        path.erase(qmark);
      }
    }
    if (path.size()){
      if (path[0] == '/'){path.erase(0, 1);}
      size_t dots = path.find("/./");
      while (dots != std::string::npos){
        DONTEVEN_MSG("%s (/./ -> /)", path.c_str());
        path.erase(dots, 2);
        dots = path.find("/./");
      }
      dots = path.find("//");
      while (dots != std::string::npos){
        DONTEVEN_MSG("%s (// -> /)", path.c_str());
        path.erase(dots, 1);
        dots = path.find("//");
      }
      if (path[0] == '/'){path.erase(0, 1);}
      dots = path.find("/../");
      while (dots != std::string::npos){
        size_t prevslash = path.rfind('/', dots - 1);
        if (prevslash == std::string::npos || dots == 0){
          path.erase(0, dots + 4);
        }else{
          path.erase(prevslash + 1, dots - prevslash + 3);
        }
        dots = path.find("/../");
      }
      if (path.substr(0, 2) == "./"){path.erase(0, 2);}
      if (path.substr(0, 3) == "../"){path.erase(0, 3);}
      //RFC 2396 sec 5.2: check if URL ends with <name>/.. -> remove iff name != ..
      if (path.length() == 2 && path == "..")
        path = "";
      if (path.length() > 2 && path.substr(path.length() - 2) == ".."){
        // |<name>| == 1, so <name> != '..'
        if (path.length() == 4){
          path.erase(path.length() - 4, path.length());
        }
        else if (path.length() > 4 && path.substr(path.length() - 5) != "../.."){
          size_t prevslash = path.rfind('/', path.length() - 4);
          path.erase(prevslash + 1, path.length());
        }
      }
      path = Encodings::URL::decode(path, true);
    }
  }
  // user, pass, host and port are now definitely between proto_sep and first_slash
  std::string uphp = url.substr(proto_sep, first_slash - proto_sep); // user+pass+host+port
  // Check if we have a user/pass before the host
  size_t at_sign = uphp.find('@');
  if (at_sign != std::string::npos){
    std::string creds = uphp.substr(0, at_sign);
    uphp.erase(0, at_sign + 1);
    size_t colon = creds.find(':');
    if (colon != std::string::npos){
      user = Encodings::URL::decode(creds.substr(0, colon), true);
      pass = Encodings::URL::decode(creds.substr(colon + 1), true);
    }else{
      user = Encodings::URL::decode(creds, true);
    }
  }
  // we check for [ at the start because we may have an IPv6 address as host
  if (uphp[0] == '['){
    // IPv6 address - find matching brace
    IPv6Addr = true;
    size_t closing_brace = uphp.find(']');
    host = uphp.substr(1, closing_brace - 1);
    // continue by finding port, if any
    size_t colon = uphp.find(':', closing_brace);
    if (colon == std::string::npos){
      // no port. Assume default
      port = "";
    }else{
      // we have a port number, read it
      port = uphp.substr(colon + 1);
      if (!is_numeric(port.c_str())){
        host += ":" + port;
        port = "";
      }
    }
  }else{
    //"normal" host - first find port, if any
    size_t colon = uphp.rfind(':');
    if (colon == std::string::npos){
      // no port. Assume default
      port = "";
      host = uphp;
    }else{
      // we have a port number, read it
      port = uphp.substr(colon + 1);
      host = uphp.substr(0, colon);
      if (!is_numeric(port.c_str())){
        IPv6Addr = true;
        host += ":" + port;
        port = "";
      }
    }
  }
  // if the host is numeric, assume it is a port, instead
  if (host.size() && is_numeric(host.c_str())){
    port = host;
    host = "";
  }
  EXTREME_MSG("URL: %s", getUrl().c_str());
}

/// Returns the port in numeric format
uint16_t HTTP::URL::getPort() const{
  if (!port.size()){return getDefaultPort();}
  return atoi(port.c_str());
}

/// Returns the default port for the protocol in numeric format
uint16_t HTTP::URL::getDefaultPort() const{
  if (protocol == "http"){return 80;}
  if (protocol == "https"){return 443;}
  if (protocol == "ws"){return 80;}
  if (protocol == "wss"){return 443;}
  if (protocol == "rtmp"){return 1935;}
  if (protocol == "rtmps"){return 443;}
  if (protocol == "dtsc"){return 4200;}
  if (protocol == "rtsp"){return 554;}
  if (protocol == "srt"){return 8889;}
  return 0;
}

/// Returns the file extension of the URL, or an empty string if none.
std::string HTTP::URL::getExt() const{
  if (path.rfind('.') == std::string::npos){return "";}
  return path.substr(path.rfind('.') + 1);
}

/// Returns the full URL in string format
std::string HTTP::URL::getUrl() const{
  std::string ret;
  if (protocol.size()){
    ret = protocol + "://";
  }else{
    ret = "//";
  }
  if (user.size() || pass.size()){
    ret += Encodings::URL::encode(user) + ":" + Encodings::URL::encode(pass) + "@";
  }
  if (IPv6Addr){
    ret += "[" + host + "]";
  }else{
    ret += host;
  }
  if (port.size() && getPort() != getDefaultPort()){ret += ":" + port;}
  ret += "/";
  if (protocol == "rtsp"){
    if (path.size()){ret += Encodings::URL::encode(path, "/:=@[]#?&+");}
  }else{
    if (path.size()){ret += Encodings::URL::encode(path, "/:=@[]+");}
  }
  if (args.size()){ret += "?" + args;}
  if (frag.size()){ret += "#" + Encodings::URL::encode(frag, "/:=@[]#?&+");}
  return ret;
}

/// Returns the full file path, in case this is a local file URI
std::string HTTP::URL::getFilePath() const{
  return "/" + path;
}

/// Returns whether the URL is probably pointing to a local file
bool HTTP::URL::isLocalPath() const{
  // If we have no host, protocol or port we can assume it is a local path
  if (host.size() || protocol.size() || port.size()){
    return false;
  }
  return true;
}

/// Returns the URL in string format without auth and frag
std::string HTTP::URL::getProxyUrl() const{
  std::string ret;
  if (protocol.size()){
    ret = protocol + "://";
  }else{
    ret = "//";
  }
  if (IPv6Addr){
    ret += "[" + host + "]";
  }else{
    ret += host;
  }
  if (port.size() && getPort() != getDefaultPort()){ret += ":" + port;}
  ret += "/";
  if (protocol == "rtsp"){
    if (path.size()){ret += Encodings::URL::encode(path, "/:=@[]#?&");}
  }else{
    if (path.size()){ret += Encodings::URL::encode(path, "/:=@[]");}
  }
  if (args.size()){ret += "?" + args;}
  return ret;
}

/// Returns the URL in string format without args and frag
std::string HTTP::URL::getBareUrl() const{
  std::string ret;
  if (protocol.size()){
    ret = protocol + "://";
  }else{
    ret = "//";
  }
  if (user.size() || pass.size()){
    ret += Encodings::URL::encode(user) + ":" + Encodings::URL::encode(pass) + "@";
  }
  if (IPv6Addr){
    ret += "[" + host + "]";
  }else{
    ret += host;
  }
  if (port.size() && getPort() != getDefaultPort()){ret += ":" + port;}
  ret += "/";
  if (protocol == "rtsp"){
    if (path.size()){ret += Encodings::URL::encode(path, "/:=@[]#?&");}
  }else{
    if (path.size()){ret += Encodings::URL::encode(path, "/:=@[]");}
  }
  return ret;
}

/// Returns a URL object for the given link, resolved relative to the current URL object.
HTTP::URL HTTP::URL::link(const std::string &l) const{
  // Full link
  if (l.find("://") < l.find('/') && l.find('/' != std::string::npos)){
    DONTEVEN_MSG("Full link: %s", l.c_str());
    return URL(l);
  }
  // Absolute link
  if (l[0] == '/'){
    DONTEVEN_MSG("Absolute link: %s", l.c_str());
    if (l.size() > 1 && l[1] == '/'){
      // Same-protocol full link
      return URL(protocol + ":" + l);
    }else{
      // Same-domain/port absolute link
      URL tmp = *this;
      tmp.args.clear();
      tmp.path = l.substr(1);
      // Abuse the fact that we don't check for arguments in getUrl()
      return URL(tmp.getUrl());
    }
  }
  // Relative link
  std::string tmpUrl = getBareUrl();
  size_t slashPos = tmpUrl.rfind('/');
  if (slashPos == std::string::npos){
    tmpUrl += "/";
  }else{
    tmpUrl.erase(slashPos + 1);
  }
  DONTEVEN_MSG("Relative link: %s+%s", tmpUrl.c_str(), l.c_str());
  return URL(tmpUrl + l);
}
