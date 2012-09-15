/// \file http_parser.cpp
/// Holds all code for the HTTP namespace.

#include "http_parser.h"

/// This constructor creates an empty HTTP::Parser, ready for use for either reading or writing.
/// All this constructor does is call HTTP::Parser::Clean().
HTTP::Parser::Parser(){Clean();}

/// Completely re-initializes the HTTP::Parser, leaving it ready for either reading or writing usage.
void HTTP::Parser::Clean(){
  seenHeaders = false;
  seenReq = false;
  method = "GET";
  url = "/";
  protocol = "HTTP/1.1";
  body.clear();
  length = 0;
  headers.clear();
  vars.clear();
}

/// Returns a string containing a valid HTTP 1.0 or 1.1 request, ready for sending.
/// The request is build from internal variables set before this call is made.
/// To be precise, method, url, protocol, headers and body are used.
/// \return A string containing a valid HTTP 1.0 or 1.1 request, ready for sending.
std::string & HTTP::Parser::BuildRequest(){
  /// \todo Include GET/POST variable parsing?
  std::map<std::string, std::string>::iterator it;
  if (protocol.size() < 5 || protocol.substr(0, 4) != "HTTP"){protocol = "HTTP/1.0";}
  builder = method+" "+url+" "+protocol+"\n";
  for (it=headers.begin(); it != headers.end(); it++){
    if ((*it).first != "" && (*it).second != ""){
      builder += (*it).first + ": " + (*it).second + "\n";
    }
  }
  builder += "\n" + body;
  return builder;
}

/// Returns a string containing a valid HTTP 1.0 or 1.1 response, ready for sending.
/// The response is partly build from internal variables set before this call is made.
/// To be precise, protocol, headers and body are used.
/// \param code The HTTP response code. Usually you want 200.
/// \param message The HTTP response message. Usually you want "OK".
/// \return A string containing a valid HTTP 1.0 or 1.1 response, ready for sending.
std::string & HTTP::Parser::BuildResponse(std::string code, std::string message){
  /// \todo Include GET/POST variable parsing?
  std::map<std::string, std::string>::iterator it;
  if (protocol.size() < 5 || protocol.substr(0, 4) != "HTTP"){protocol = "HTTP/1.0";}
  builder = protocol+" "+code+" "+message+"\n";
  for (it=headers.begin(); it != headers.end(); it++){
    if ((*it).first != "" && (*it).second != ""){
      if ((*it).first != "Content-Length" || (*it).second != "0"){
        builder += (*it).first + ": " + (*it).second + "\n";
      }
    }
  }
  builder += "\n";
  builder += body;
  return builder;
}

/// Trims any whitespace at the front or back of the string.
/// Used when getting/setting headers.
/// \param s The string to trim. The string itself will be changed, not returned.
void HTTP::Parser::Trim(std::string & s){
  size_t startpos = s.find_first_not_of(" \t");
  size_t endpos = s.find_last_not_of(" \t");
  if ((std::string::npos == startpos) || (std::string::npos == endpos)){s = "";}else{s = s.substr(startpos, endpos-startpos+1);}
}

/// Function that sets the body of a response or request, along with the correct Content-Length header.
/// \param s The string to set the body to.
void HTTP::Parser::SetBody(std::string s){
  body = s;
  SetHeader("Content-Length", s.length());
}

/// Function that sets the body of a response or request, along with the correct Content-Length header.
/// \param buffer The buffer data to set the body to.
/// \param len Length of the buffer data.
void HTTP::Parser::SetBody(char * buffer, int len){
  body = "";
  body.append(buffer, len);
  SetHeader("Content-Length", len);
}

/// Returns header i, if set.
std::string HTTP::Parser::getUrl(){
  if (url.find('?') != std::string::npos){
    return url.substr(0, url.find('?'));
  }else{
    return url;
  }
}

/// Returns header i, if set.
std::string HTTP::Parser::GetHeader(std::string i){return headers[i];}
/// Returns POST variable i, if set.
std::string HTTP::Parser::GetVar(std::string i){return vars[i];}

/// Sets header i to string value v.
void HTTP::Parser::SetHeader(std::string i, std::string v){
  Trim(i);
  Trim(v);
  headers[i] = v;
}

/// Sets header i to integer value v.
void HTTP::Parser::SetHeader(std::string i, int v){
  Trim(i);
  char val[128];
  sprintf(val, "%i", v);
  headers[i] = val;
}

/// Sets POST variable i to string value v.
void HTTP::Parser::SetVar(std::string i, std::string v){
  Trim(i);
  Trim(v);
  //only set if there is actually a key
  if(!i.empty()){
    vars[i] = v;
  }
}

/// Attempt to read a whole HTTP request or response from a std::string buffer.
/// If a whole request could be read, it is removed from the front of the given buffer.
/// \param strbuf The buffer to read from.
/// \return True if a whole request or response was read, false otherwise.
bool HTTP::Parser::Read(std::string & strbuf){
  return parse(strbuf);
}//HTTPReader::Read

#include <iostream>
/// Attempt to read a whole HTTP response or request from a data buffer.
/// If succesful, fills its own fields with the proper data and removes the response/request
/// from the data buffer.
/// \param HTTPbuffer The data buffer to read from.
/// \return True on success, false otherwise.
bool HTTP::Parser::parse(std::string & HTTPbuffer){
  size_t f;
  std::string tmpA, tmpB, tmpC;
  /// \todo Make this not resize HTTPbuffer in parts, but read all at once and then remove the entire request, like doxygen claims it does?
  while (!HTTPbuffer.empty()){
    if (!seenHeaders){
      f = HTTPbuffer.find('\n');
      if (f == std::string::npos) return false;
      tmpA = HTTPbuffer.substr(0, f);
      if (f+1 == HTTPbuffer.size()){
        HTTPbuffer.clear();
      }else{
        HTTPbuffer.erase(0, f+1);
      }
      while (tmpA.find('\r') != std::string::npos){tmpA.erase(tmpA.find('\r'));}
      if (!seenReq){
        seenReq = true;
        f = tmpA.find(' ');
        if (f != std::string::npos){
          method = tmpA.substr(0, f); tmpA.erase(0, f+1);
          f = tmpA.find(' ');
          if (f != std::string::npos){
            url = tmpA.substr(0, f); tmpA.erase(0, f+1);
            protocol = tmpA;
            if (url.find('?') != std::string::npos){
              parseVars(url.substr(url.find('?')+1)); //parse GET variables
            }
          }else{seenReq = false;}
        }else{seenReq = false;}
      }else{
        if (tmpA.size() == 0){
          seenHeaders = true;
          body.clear();
          if (GetHeader("Content-Length") != ""){
            length = atoi(GetHeader("Content-Length").c_str());
            if (body.capacity() < length){body.reserve(length);}
          }
        }else{
          f = tmpA.find(':');
          if (f == std::string::npos) continue;
          tmpB = tmpA.substr(0, f);
          tmpC = tmpA.substr(f+1);
          SetHeader(tmpB, tmpC);
        }
      }
    }
    if (seenHeaders){
      if (length > 0){
        unsigned int toappend = length - body.length();
        if (toappend > 0){
          body.append(HTTPbuffer, 0, toappend);
          HTTPbuffer.erase(0, toappend);
        }
        if (length == body.length()){
          parseVars(body); //parse POST variables
          return true;
        }else{
          return false;
        }
      }else{
        return true;
      }
    }
  }
  return false; //empty input
}//HTTPReader::parse

/// Parses GET or POST-style variable data.
/// Saves to internal variable structure using HTTP::Parser::SetVar.
void HTTP::Parser::parseVars(std::string data){
  std::string varname;
  std::string varval;
  // position where a part start (e.g. after &)
  size_t pos = 0;
  while (pos < data.length()){
    size_t nextpos = data.find('&', pos);
    if (nextpos == std::string::npos){
      nextpos = data.length();
    }
    size_t eq_pos = data.find('=', pos);
    if (eq_pos < nextpos){
      // there is a key and value
      varname = data.substr(pos, eq_pos - pos);
      varval = data.substr(eq_pos + 1, nextpos - eq_pos - 1);
    }else{
      // no value, only a key
      varname = data.substr(pos, nextpos - pos);
      varval.clear();
    }
    SetVar(urlunescape(varname), urlunescape(varval));
    if (nextpos == std::string::npos){
      // in case the string is gigantic
      break;
    }
    // erase &
    pos = nextpos + 1;
  }
}

/// Converts a string to chunked format if protocol is HTTP/1.1 - does nothing otherwise.
/// \param bodypart The data to convert - will be converted in-place.
void HTTP::Parser::Chunkify(std::string & bodypart){
  if (protocol == "HTTP/1.1"){
    static char len[10];
    int sizelen = snprintf(len, 10, "%x\r\n", (unsigned int)bodypart.size());
    //prepend the chunk size and \r\n
    bodypart.insert(0, len, sizelen);
    //append \r\n
    bodypart.append("\r\n", 2);
  }
}

/// Unescapes URLencoded std::string data.
std::string HTTP::Parser::urlunescape(const std::string & in){
  std::string out;
  for (unsigned int i = 0; i < in.length(); ++i){
    if (in[i] == '%'){
      char tmp = 0;
      ++i;
      if (i < in.length()){
        tmp = unhex(in[i]) << 4;
      }
      ++i;
      if (i < in.length()){
        tmp += unhex(in[i]);
      }
      out += tmp;
    } else {
      if (in[i] == '+'){out += ' ';}else{out += in[i];}
    }
  }
  return out;
}

/// Helper function for urlunescape.
/// Takes a single char input and outputs its integer hex value.
int HTTP::Parser::unhex(char c){
  return( c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c - 'a' + 10 );
}

/// URLencodes std::string data.
std::string HTTP::Parser::urlencode(const std::string &c){
  std::string escaped="";
  int max = c.length();
  for(int i=0; i<max; i++){
    if (('0'<=c[i] && c[i]<='9') || ('a'<=c[i] && c[i]<='z') || ('A'<=c[i] && c[i]<='Z') || (c[i]=='~' || c[i]=='!' || c[i]=='*' || c[i]=='(' || c[i]==')' || c[i]=='\'')){
      escaped.append( &c[i], 1);
    }else{
      escaped.append("%");
      escaped.append(hex(c[i]));
    }
  }
  return escaped;
}

/// Helper function for urlescape.
/// Encodes a character as two hex digits.
std::string HTTP::Parser::hex(char dec){
  char dig1 = (dec&0xF0)>>4;
  char dig2 = (dec&0x0F);
  if (dig1<= 9) dig1+=48;
  if (10<= dig1 && dig1<=15) dig1+=97-10;
  if (dig2<= 9) dig2+=48;
  if (10<= dig2 && dig2<=15) dig2+=97-10;
  std::string r;
  r.append(&dig1, 1);
  r.append(&dig2, 1);
  return r;
}
