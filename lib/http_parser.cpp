/// \file http_parser.cpp
/// Holds all code for the HTTP namespace.

#include "http_parser.h"
#include "encode.h"
#include "timing.h"
#include "defines.h"

/// Helper function to check if the given c-string is numeric or not
static bool is_numeric(const char * str){
  while (str != 0){
    if (str[0] < 48 || str[0] > 57){return false;}
    ++str;
  }
  return true;
}

///Constructor that does the actual parsing
HTTP::URL::URL(const std::string & url){
  //first detect protocol at the start, if any
  size_t proto_sep = url.find("://");
  if (proto_sep != std::string::npos){
    protocol = url.substr(0, proto_sep);
    proto_sep += 3;
  }else{
    proto_sep = 0;
  }
  //proto_sep now points to the start of the host, guaranteed
  //continue by finding the path, if any
  size_t first_slash = url.find_first_of("/?#", proto_sep);
  if (first_slash != std::string::npos){
    if (url[first_slash] == '/'){
      path = url.substr(first_slash+1);
    }else{
      path = url.substr(first_slash);
    }
    size_t hmark = path.find('#');
    if (hmark != std::string::npos){
      frag = path.substr(hmark+1);
      path.erase(hmark);
    }
    size_t qmark = path.find('?');
    if (qmark != std::string::npos){
      args = path.substr(qmark+1);
      path.erase(qmark);
    }
    if (path.size()){
      size_t dots = path.find("/./");
      while (dots != std::string::npos){
        path.erase(dots, 2);
        dots = path.find("/./");
      }
      dots = path.find("/../");
      while (dots != std::string::npos){
        size_t prevslash = path.rfind('/', dots-1);
        if (prevslash == std::string::npos){
          path.erase(0, dots+4);
        }else{
          path.erase(prevslash+1, dots-prevslash+3);
        }
        dots = path.find("/../");
      }
      if (path.substr(0, 2) == "./"){
        path.erase(0, 2);
      }
      if (path.substr(0, 3) == "../"){
        path.erase(0, 3);
      }
    }
  }
  //host and port are now definitely between proto_sep and first_slash
  //we check for [ at the start because we may have an IPv6 address as host
  if (url[proto_sep] == '['){
    //IPv6 address - find matching brace
    size_t closing_brace = url.find(']', proto_sep);
    //check if it exists at all
    if (closing_brace == std::string::npos || closing_brace > first_slash){
      //assume host ends at first slash if there is no closing brace before it
      closing_brace = first_slash;
    }
    host = url.substr(proto_sep+1, closing_brace-(proto_sep+1));
    //continue by finding port, if any
    size_t colon = url.rfind(':', first_slash);
    if (colon == std::string::npos || colon <= closing_brace){
      //no port. Assume 80
      port = "80";
    }else{
      //we have a port number, read it
      port = url.substr(colon+1, first_slash-(colon+1));
    }
  }else{
    //"normal" host - first find port, if any
    size_t colon = url.rfind(':', first_slash);
    if (colon == std::string::npos || colon < proto_sep){
      //no port. Assume default
      port = "";
      host = url.substr(proto_sep, first_slash-proto_sep);
    }else{
      //we have a port number, read it
      port = url.substr(colon+1, first_slash-(colon+1));
      host = url.substr(proto_sep, colon-proto_sep);
    }
  }
  //if the host is numeric, assume it is a port, instead
  if (is_numeric(host.c_str())){
    port = host;
    host = "";
  }
  EXTREME_MSG("URL host: %s", host.c_str());
  EXTREME_MSG("URL protocol: %s", protocol.c_str());
  EXTREME_MSG("URL port: %s", port.c_str());
  EXTREME_MSG("URL path: %s", path.c_str());
  EXTREME_MSG("URL args: %s", args.c_str());
}

///Returns the port in numeric format
uint32_t HTTP::URL::getPort() const{
  if (!port.size()){return getDefaultPort();}
  return atoi(port.c_str());
}

///Returns the default port for the protocol in numeric format
uint32_t HTTP::URL::getDefaultPort() const{
  if (protocol == "https"){return 443;}
  if (protocol == "rtmp"){return 1935;}
  if (protocol == "dtsc"){return 4200;}
  return 80;
}

///Returns the full URL in string format
std::string HTTP::URL::getUrl() const{
  std::string ret;
  if (protocol.size()){
    ret = protocol + "://" + host;
  }else{
    ret = "//" + host;
  }
  if (port.size() && getPort() != getDefaultPort()){ret += ":" + port;}
  ret += "/";
  if (path.size()){ret += path;}
  if (args.size()){ret += "?" + args;}
  if (frag.size()){ret += "#" + frag;}
  return ret;
}

///Returns the URL in string format without args and frag
std::string HTTP::URL::getBareUrl() const{
  std::string ret;
  if (protocol.size()){
    ret = protocol + "://" + host;
  }else{
    ret = "//" + host;
  }
  if (port.size() && getPort() != getDefaultPort()){ret += ":" + port;}
  ret += "/";
  if (path.size()){ret += path;}
  return ret;
}

///Returns a URL object for the given link, resolved relative to the current URL object.
HTTP::URL HTTP::URL::link(const std::string &l){
  //Full link
  if (l.find("://") < l.find('/') && l.find('/' != std::string::npos)){
    DONTEVEN_MSG("Full link: %s", l.c_str());
    return URL(l);
  }
  //Absolute link
  if (l[0] == '/'){
    DONTEVEN_MSG("Absolute link: %s", l.c_str());
    if (l.size() > 1 && l[1] == '/'){
      //Same-protocol full link
      return URL(protocol+":"+l);
    }else{
      //Same-domain/port absolute link
      URL tmp = *this;
      tmp.args.clear();
      tmp.path = l.substr(1);
      //Abuse the fact that we don't check for arguments in getUrl()
      return URL(tmp.getUrl());
    }
  }
  //Relative link
  std::string tmpUrl = getBareUrl();
  size_t slashPos = tmpUrl.rfind('/');
  if (slashPos == std::string::npos){
    tmpUrl += "/";
  }else{
    tmpUrl.erase(slashPos+1);
  }
  DONTEVEN_MSG("Relative link: %s+%s", tmpUrl.c_str(), l.c_str());
  return URL(tmpUrl+l);
}

/// This constructor creates an empty HTTP::Parser, ready for use for either reading or writing.
/// All this constructor does is call HTTP::Parser::Clean().
HTTP::Parser::Parser() {
  headerOnly = false;
  Clean();
}

/// Completely re-initializes the HTTP::Parser, leaving it ready for either reading or writing usage.
void HTTP::Parser::Clean() {
  CleanPreserveHeaders();
  headers.clear();
}

/// Completely re-initializes the HTTP::Parser, leaving it ready for either reading or writing usage.
void HTTP::Parser::CleanPreserveHeaders() {
  seenHeaders = false;
  seenReq = false;
  getChunks = false;
  doingChunk = 0;
  bufferChunks = false;
  method = "GET";
  url = "/";
  protocol = "HTTP/1.1";
  body.clear();
  length = 0;
  vars.clear();
}

/// Sets the neccesary headers to allow Cross Origin Resource Sharing with all domains.
void HTTP::Parser::setCORSHeaders(){
  SetHeader("Access-Control-Allow-Origin", "*");
  SetHeader("Access-Control-Allow-Credentials", "true");
  SetHeader("Access-Control-Expose-Headers", "*");
  SetHeader("Access-Control-Max-Age", "600");
  SetHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS, HEAD");
  SetHeader("Access-Control-Allow-Headers", "*");
  SetHeader("Access-Control-Request-Method", "GET");
  SetHeader("Access-Control-Request-Headers", "*");  
  SetHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  SetHeader("Pragma", "no-cache");
  SetHeader("Expires", "0");
}


/// Returns a string containing a valid HTTP 1.0 or 1.1 request, ready for sending.
/// The request is build from internal variables set before this call is made.
/// To be precise, method, url, protocol, headers and body are used.
/// \return A string containing a valid HTTP 1.0 or 1.1 request, ready for sending.
std::string & HTTP::Parser::BuildRequest() {
  /// \todo Include GET/POST variable parsing?
  std::map<std::string, std::string>::iterator it;
  if (protocol.size() < 5 || protocol[4] != '/') {
    protocol = "HTTP/1.0";
  }
  builder = method + " " + url + " " + protocol + "\r\n";
  for (it = headers.begin(); it != headers.end(); it++) {
    if ((*it).first != "" && (*it).second != "") {
      builder += (*it).first + ": " + (*it).second + "\r\n";
    }
  }
  builder += "\r\n" + body;
  return builder;
}

/// Creates and sends a valid HTTP 1.0 or 1.1 request.
/// The request is build from internal variables set before this call is made.
/// To be precise, method, url, protocol, headers and body are used.
void HTTP::Parser::SendRequest(Socket::Connection & conn) {
  /// \todo Include GET/POST variable parsing?
  std::map<std::string, std::string>::iterator it;
  if (protocol.size() < 5 || protocol[4] != '/') {
    protocol = "HTTP/1.0";
  }
  builder = method + " " + url + " " + protocol + "\r\n";
  conn.SendNow(builder);
  for (it = headers.begin(); it != headers.end(); it++) {
    if ((*it).first != "" && (*it).second != "") {
      builder = (*it).first + ": " + (*it).second + "\r\n";
      conn.SendNow(builder);
    }
  }
  conn.SendNow("\r\n", 2);
  conn.SendNow(body);
}

/// Returns a string containing a valid HTTP 1.0 or 1.1 response, ready for sending.
/// The response is partly build from internal variables set before this call is made.
/// To be precise, protocol, headers and body are used.
/// \param code The HTTP response code. Usually you want 200.
/// \param message The HTTP response message. Usually you want "OK".
/// \return A string containing a valid HTTP 1.0 or 1.1 response, ready for sending.
std::string & HTTP::Parser::BuildResponse(std::string code, std::string message) {
  /// \todo Include GET/POST variable parsing?
  std::map<std::string, std::string>::iterator it;
  if (protocol.size() < 5 || protocol[4] != '/') {
    protocol = "HTTP/1.0";
  }
  builder = protocol + " " + code + " " + message + "\r\n";
  for (it = headers.begin(); it != headers.end(); it++) {
    if ((*it).first != "" && (*it).second != "") {
      if ((*it).first != "Content-Length" || (*it).second != "0") {
        builder += (*it).first + ": " + (*it).second + "\r\n";
      }
    }
  }
  builder += "\r\n";
  builder += body;
  return builder;
}

/// Returns a string containing a valid HTTP 1.0 or 1.1 response, ready for sending.
/// The response is partly build from internal variables set before this call is made.
/// To be precise, protocol, headers and body are used.
/// \return A string containing a valid HTTP 1.0 or 1.1 response, ready for sending.
/// This function calls this->BuildResponse(this->method,this->url)
std::string & HTTP::Parser::BuildResponse() {
 return BuildResponse(method,url);
}

/// Creates and sends a valid HTTP 1.0 or 1.1 response.
/// The response is partly build from internal variables set before this call is made.
/// To be precise, protocol, headers and body are used.
/// This call will attempt to buffer as little as possible and block until the whole request is sent.
/// \param code The HTTP response code. Usually you want 200.
/// \param message The HTTP response message. Usually you want "OK".
/// \param conn The Socket::Connection to send the response over.
void HTTP::Parser::SendResponse(std::string code, std::string message, Socket::Connection & conn) {
  /// \todo Include GET/POST variable parsing?
  std::map<std::string, std::string>::iterator it;
  if (protocol.size() < 5 || protocol[4] != '/') {
    protocol = "HTTP/1.0";
  }
  builder = protocol + " " + code + " " + message + "\r\n";
  conn.SendNow(builder);
  for (it = headers.begin(); it != headers.end(); it++) {
    if ((*it).first != "" && (*it).second != "") {
      if ((*it).first != "Content-Length" || (*it).second != "0") {
        builder = (*it).first + ": " + (*it).second + "\r\n";
        conn.SendNow(builder);
      }
    }
  }
  conn.SendNow("\r\n", 2);
  conn.SendNow(body);
}

/// Creates and sends a valid HTTP 1.0 or 1.1 response, based on the given request.
/// The headers must be set before this call is made.
/// This call sets up chunked transfer encoding if the request was protocol HTTP/1.1, otherwise uses a zero-content-length HTTP/1.0 response.
/// \param code The HTTP response code. Usually you want 200.
/// \param message The HTTP response message. Usually you want "OK".
/// \param request The HTTP request to respond to.
/// \param conn The connection to send over.
void HTTP::Parser::StartResponse(std::string code, std::string message, HTTP::Parser & request, Socket::Connection & conn, bool bufferAllChunks) {
  std::string prot = request.protocol;
  sendingChunks = (!bufferAllChunks && protocol == "HTTP/1.1" && request.GetHeader("Connection")!="close");
  CleanPreserveHeaders();
  protocol = prot;
  if (sendingChunks){
    SetHeader("Transfer-Encoding", "chunked");
  } else {
    SetHeader("Connection", "close");
  }
  bufferChunks = bufferAllChunks;
  if (!bufferAllChunks){
    SendResponse(code, message, conn);
  }
}

/// Creates and sends a valid HTTP 1.0 or 1.1 response, based on the given request.
/// The headers must be set before this call is made.
/// This call sets up chunked transfer encoding if the request was protocol HTTP/1.1, otherwise uses a zero-content-length HTTP/1.0 response.
/// This call simply calls StartResponse("200", "OK", request, conn)
/// \param request The HTTP request to respond to.
/// \param conn The connection to send over.
void HTTP::Parser::StartResponse(HTTP::Parser & request, Socket::Connection & conn, bool bufferAllChunks) {
  StartResponse("200", "OK", request, conn, bufferAllChunks);
}

/// After receiving a header with this object, and after a call with SendResponse/SendRequest with this object, this function call will:
/// - Retrieve all the body from the 'from' Socket::Connection.
/// - Forward those contents as-is to the 'to' Socket::Connection.
/// It blocks until completed or either of the connections reaches an error state.
void HTTP::Parser::Proxy(Socket::Connection & from, Socket::Connection & to) {
  if (getChunks) {
    unsigned int proxyingChunk = 0;
    while (to.connected() && from.connected()) {
      if ((from.Received().size() && (from.Received().size() > 1 || *(from.Received().get().rbegin()) == '\n')) || from.spool()) {
        if (proxyingChunk) {
          while (proxyingChunk && from.Received().size()) {
            unsigned int toappend = from.Received().get().size();
            if (toappend > proxyingChunk) {
              toappend = proxyingChunk;
              to.SendNow(from.Received().get().c_str(), toappend);
              from.Received().get().erase(0, toappend);
            } else {
              to.SendNow(from.Received().get());
              from.Received().get().clear();
            }
            proxyingChunk -= toappend;
          }
        } else {
          //Make sure the received data ends in a newline (\n).
          if (*(from.Received().get().rbegin()) != '\n') {
            if (from.Received().size() > 1) {
              //make a copy of the first part
              std::string tmp = from.Received().get();
              //clear the first part, wiping it from the partlist
              from.Received().get().clear();
              from.Received().size();
              //take the now first (was second) part, insert the stored part in front of it
              from.Received().get().insert(0, tmp);
            } else {
              Util::sleep(100);
            }
            if (*(from.Received().get().rbegin()) != '\n') {
              continue;
            }
          }
          //forward the size and any empty lines
          to.SendNow(from.Received().get());

          std::string tmpA = from.Received().get().substr(0, from.Received().get().size() - 1);
          while (tmpA.find('\r') != std::string::npos) {
            tmpA.erase(tmpA.find('\r'));
          }
          unsigned int chunkLen = 0;
          if (!tmpA.empty()) {
            for (unsigned int i = 0; i < tmpA.size(); ++i) {
              chunkLen = (chunkLen << 4) | Encodings::Hex::ord(tmpA[i]);
            }
            if (chunkLen == 0) {
              getChunks = false;
              to.SendNow("\r\n", 2);
              return;
            }
            proxyingChunk = chunkLen;
          }
          from.Received().get().clear();
        }
      } else {
        Util::sleep(100);
      }
    }
  } else {
    unsigned int bodyLen = length;
    while (bodyLen > 0 && to.connected() && from.connected()) {
      if (from.Received().size() || from.spool()) {
        if (from.Received().get().size() <= bodyLen) {
          to.SendNow(from.Received().get());
          bodyLen -= from.Received().get().size();
          from.Received().get().clear();
        } else {
          to.SendNow(from.Received().get().c_str(), bodyLen);
          from.Received().get().erase(0, bodyLen);
          bodyLen = 0;
        }
      } else {
        Util::sleep(100);
      }
    }
  }
}

/// Trims any whitespace at the front or back of the string.
/// Used when getting/setting headers.
/// \param s The string to trim. The string itself will be changed, not returned.
void HTTP::Parser::Trim(std::string & s) {
  size_t startpos = s.find_first_not_of(" \t");
  size_t endpos = s.find_last_not_of(" \t");
  if ((std::string::npos == startpos) || (std::string::npos == endpos)) {
    s = "";
  } else {
    s = s.substr(startpos, endpos - startpos + 1);
  }
}

/// Function that sets the body of a response or request, along with the correct Content-Length header.
/// \param s The string to set the body to.
void HTTP::Parser::SetBody(std::string s) {
  body = s;
  SetHeader("Content-Length", s.length());
}

/// Function that sets the body of a response or request, along with the correct Content-Length header.
/// \param buffer The buffer data to set the body to.
/// \param len Length of the buffer data.
void HTTP::Parser::SetBody(const char * buffer, int len) {
  body = "";
  body.append(buffer, len);
  SetHeader("Content-Length", len);
}

/// Returns header i, if set.
std::string HTTP::Parser::getUrl() {
  if (url.find('?') != std::string::npos) {
    return url.substr(0, url.find('?'));
  } else {
    return url;
  }
}

/// Returns header i, if set.
const std::string & HTTP::Parser::GetHeader(const std::string & i) const {
  if (headers.count(i)){
    return headers.at(i);
  }else{
    static const std::string empty;
    return empty;
  }
}

/// Returns header i, if set.
bool HTTP::Parser::hasHeader(const std::string & i) const {
  return headers.count(i);
}

/// Returns POST variable i, if set.
const std::string & HTTP::Parser::GetVar(const std::string & i) const {
  if (vars.count(i)){
    return vars.at(i);
  }else{
    static const std::string empty;
    return empty;
  }
}

std::string HTTP::Parser::allVars(){
  std::string ret;
  if (!vars.size()){return ret;}
  for (std::map<std::string, std::string>::iterator it = vars.begin(); it != vars.end(); ++it){
    if (ret.size() > 1){
      ret += "&";
    }else{
      ret += "?";
    }
    ret += it->first + "=" + Encodings::URL::encode(it->second);
  }
  return ret;
}

/// Sets header i to string value v.
void HTTP::Parser::SetHeader(std::string i, std::string v) {
  Trim(i);
  Trim(v);
  headers[i] = v;
}

void HTTP::Parser::clearHeader(const std::string & i){
  headers.erase(i);
}

/// Sets header i to integer value v.
void HTTP::Parser::SetHeader(std::string i, long long v) {
  Trim(i);
  char val[23]; //ints are never bigger than 22 chars as decimal
  sprintf(val, "%lld", v);
  headers[i] = val;
}

/// Sets POST variable i to string value v.
void HTTP::Parser::SetVar(std::string i, std::string v) {
  Trim(i);
  Trim(v);
  //only set if there is actually a key
  if (!i.empty()) {
    vars[i] = v;
  }
}

/// Attempt to read a whole HTTP request or response from a Socket::Connection.
/// If a whole request could be read, it is removed from the front of the socket buffer and true returned.
/// If not, as much as can be interpreted is removed and false returned.
/// \param conn The socket to read from.
/// \return True if a whole request or response was read, false otherwise.
bool HTTP::Parser::Read(Socket::Connection & conn) {
  //Make sure the received data ends in a newline (\n).
  while ((!seenHeaders || (getChunks && !doingChunk)) && conn.Received().get().size() && *(conn.Received().get().rbegin()) != '\n') {
    if (conn.Received().size() > 1) {
      //make a copy of the first part
      std::string tmp = conn.Received().get();
      //clear the first part, wiping it from the partlist
      conn.Received().get().clear();
      conn.Received().size();
      //take the now first (was second) part, insert the stored part in front of it
      conn.Received().get().insert(0, tmp);
    } else {
      return false;
    }
  }
  //if a parse succeeds, simply return true
  if (parse(conn.Received().get())) {
    return true;
  }
  //otherwise, if we have parts left, call ourselves recursively
  if (conn.Received().size()) {
    return Read(conn);
  }
  return false;
} //HTTPReader::Read

/// Attempt to read a whole HTTP request or response from a std::string buffer.
/// If a whole request could be read, it is removed from the front of the given buffer and true returned.
/// If not, as much as can be interpreted is removed and false returned.
/// \param strbuf The buffer to read from.
/// \return True if a whole request or response was read, false otherwise.
bool HTTP::Parser::Read(std::string & strbuf) {
  return parse(strbuf);
} //HTTPReader::Read

/// Attempt to read a whole HTTP response or request from a data buffer.
/// If succesful, fills its own fields with the proper data and removes the response/request
/// from the data buffer.
/// \param HTTPbuffer The data buffer to read from.
/// \return True on success, false otherwise.
bool HTTP::Parser::parse(std::string & HTTPbuffer) {
  size_t f;
  std::string tmpA, tmpB, tmpC;
  /// \todo Make this not resize HTTPbuffer in parts, but read all at once and then remove the entire request, like doxygen claims it does?
  while (!HTTPbuffer.empty()) {
    if (!seenHeaders) {
      f = HTTPbuffer.find('\n');
      if (f == std::string::npos) return false;
      tmpA = HTTPbuffer.substr(0, f);
      if (f + 1 == HTTPbuffer.size()) {
        HTTPbuffer.clear();
      } else {
        HTTPbuffer.erase(0, f + 1);
      }
      while (tmpA.find('\r') != std::string::npos) {
        tmpA.erase(tmpA.find('\r'));
      }
      if (!seenReq) {
        seenReq = true;
        f = tmpA.find(' ');
        if (f != std::string::npos) {
          if (tmpA.substr(0, 4) == "HTTP") {
            protocol = tmpA.substr(0, f);
            tmpA.erase(0, f + 1);
            f = tmpA.find(' ');
            if (f != std::string::npos) {
              url = tmpA.substr(0, f);
              tmpA.erase(0, f + 1);
              method = tmpA;
              if (url.find('?') != std::string::npos) {
                parseVars(url.substr(url.find('?') + 1), vars); //parse GET variables
                url.erase(url.find('?'));
              }
              url = Encodings::URL::decode(url);
            } else {
              seenReq = false;
            }
          } else {
            method = tmpA.substr(0, f);
            tmpA.erase(0, f + 1);
            f = tmpA.find(' ');
            if (f != std::string::npos) {
              url = tmpA.substr(0, f);
              tmpA.erase(0, f + 1);
              protocol = tmpA;
              if (url.find('?') != std::string::npos) {
                parseVars(url.substr(url.find('?') + 1), vars); //parse GET variables
                url.erase(url.find('?'));
              }
              url = Encodings::URL::decode(url);
            } else {
              seenReq = false;
            }
          }
        } else {
          seenReq = false;
        }
      } else {
        if (tmpA.size() == 0) {
          seenHeaders = true;
          body.clear();
          if (GetHeader("Content-Length") != "") {
            length = atoi(GetHeader("Content-Length").c_str());
            if (body.capacity() < length) {
              body.reserve(length);
            }
          }
          if (GetHeader("Transfer-Encoding") == "chunked") {
            getChunks = true;
            doingChunk = 0;
          }
        } else {
          f = tmpA.find(':');
          if (f == std::string::npos) continue;
          tmpB = tmpA.substr(0, f);
          tmpC = tmpA.substr(f + 1);
          SetHeader(tmpB, tmpC);
        }
      }
    }
    if (seenHeaders) {
      if (length > 0) {
        if (headerOnly) {
          return true;
        }
        unsigned int toappend = length - body.length();
        if (toappend > 0) {
          body.append(HTTPbuffer, 0, toappend);
          HTTPbuffer.erase(0, toappend);
        }
        if (length == body.length()) {
          parseVars(body, vars); //parse POST variables
          return true;
        } else {
          return false;
        }
      } else {
        if (getChunks) {
          if (headerOnly) {
            return true;
          }
          if (doingChunk) {
            unsigned int toappend = HTTPbuffer.size();
            if (toappend > doingChunk) {
              toappend = doingChunk;
            }
            body.append(HTTPbuffer, 0, toappend);
            HTTPbuffer.erase(0, toappend);
            doingChunk -= toappend;
          } else {
            f = HTTPbuffer.find('\n');
            if (f == std::string::npos) return false;
            tmpA = HTTPbuffer.substr(0, f);
            while (tmpA.find('\r') != std::string::npos) {
              tmpA.erase(tmpA.find('\r'));
            }
            unsigned int chunkLen = 0;
            if (!tmpA.empty()) {
              for (unsigned int i = 0; i < tmpA.size(); ++i) {
                chunkLen = (chunkLen << 4) | Encodings::Hex::ord(tmpA[i]);
              }
              if (chunkLen == 0) {
                getChunks = false;
                return true;
              }
              doingChunk = chunkLen;
            }
            if (f + 1 == HTTPbuffer.size()) {
              HTTPbuffer.clear();
            } else {
              HTTPbuffer.erase(0, f + 1);
            }
          }
          return false;
        } else {
          return true;
        }
      }
    }
  }
  return false; //empty input
} //HTTPReader::parse

///HTTP variable parser to std::map<std::string, std::string> structure.
///Reads variables from data, decodes and stores them to storage.
void HTTP::parseVars(const std::string & data, std::map<std::string, std::string> & storage) {
  std::string varname;
  std::string varval;
  // position where a part starts (e.g. after &)
  size_t pos = 0;
  while (pos < data.length()) {
    size_t nextpos = data.find('&', pos);
    if (nextpos == std::string::npos) {
      nextpos = data.length();
    }
    size_t eq_pos = data.find('=', pos);
    if (eq_pos < nextpos) {
      // there is a key and value
      varname = data.substr(pos, eq_pos - pos);
      varval = data.substr(eq_pos + 1, nextpos - eq_pos - 1);
    } else {
      // no value, only a key
      varname = data.substr(pos, nextpos - pos);
      varval.clear();
    }
    if (varname.size()){
      storage[Encodings::URL::decode(varname)] = Encodings::URL::decode(varval);
    }
    if (nextpos == std::string::npos) {
      // in case the string is gigantic
      break;
    }
    // erase &
    pos = nextpos + 1;
  }
}

/// Sends a string in chunked format if protocol is HTTP/1.1, sends as-is otherwise.
/// \param bodypart The data to send.
/// \param conn The connection to use for sending.
void HTTP::Parser::Chunkify(const std::string & bodypart, Socket::Connection & conn) {
  Chunkify(bodypart.c_str(), bodypart.size(), conn);
}

/// Sends a string in chunked format if protocol is HTTP/1.1, sends as-is otherwise.
/// \param data The data to send.
/// \param size The size of the data to send.
/// \param conn The connection to use for sending.
void HTTP::Parser::Chunkify(const char * data, unsigned int size, Socket::Connection & conn) {
  static char hexa[] = "0123456789abcdef";
  if (bufferChunks){
    if (size){
      body.append(data, size);
    }else{
      SetHeader("Content-Length", body.length());
      SendResponse("200", "OK", conn);
      Clean();
    }
    return;
  }
  if (sendingChunks) {
    //prepend the chunk size and \r\n
    if (!size){
      conn.SendNow("0\r\n\r\n", 5);
    }
    size_t offset = 8;
    unsigned int t_size = size;
    char len[] = "\000\000\000\000\000\000\0000\r\n";
    while (t_size && offset < 9){
      len[--offset] = hexa[t_size & 0xf];
      t_size >>= 4;
    }
    conn.SendNow(len+offset, 10-offset);
    //send the chunk itself
    conn.SendNow(data, size);
    //append \r\n
    conn.SendNow("\r\n", 2);
  } else {
    //just send the chunk itself
    conn.SendNow(data, size);
    //close the connection if this was the end of the file
    if (!size) {
      conn.close();
      Clean();
    }
  }
}

