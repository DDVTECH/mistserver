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
  body = "";
  length = 0;
  HTTPbuffer = "";
  headers.clear();
  vars.clear();
}

/// Re-initializes the HTTP::Parser, leaving the internal data buffer alone, then tries to parse a new request or response.
/// First does the same as HTTP::Parser::Clean(), but does not clear the internal data buffer.
/// This function then calls the HTTP::Parser::parse() function, and returns that functions return value.
bool HTTP::Parser::CleanForNext(){
  seenHeaders = false;
  seenReq = false;
  method = "GET";
  url = "/";
  protocol = "HTTP/1.1";
  body = "";
  length = 0;
  headers.clear();
  vars.clear();
  return parse();
}

/// Returns a string containing a valid HTTP 1.0 or 1.1 request, ready for sending.
/// The request is build from internal variables set before this call is made.
/// To be precise, method, url, protocol, headers and the internal data buffer are used,
/// where the internal data buffer is used as the body of the request.
/// This means you cannot mix receiving and sending, because the body would get corrupted.
/// \return A string containing a valid HTTP 1.0 or 1.1 request, ready for sending.
std::string HTTP::Parser::BuildRequest(){
  /// \todo Include GET/POST variable parsing?
  std::map<std::string, std::string>::iterator it;
  std::string tmp = method+" "+url+" "+protocol+"\n";
  for (it=headers.begin(); it != headers.end(); it++){
    tmp += (*it).first + ": " + (*it).second + "\n";
  }
  tmp += "\n";
  tmp += HTTPbuffer;
  return tmp;
}

/// Returns a string containing a valid HTTP 1.0 or 1.1 response, ready for sending.
/// The response is partly build from internal variables set before this call is made.
/// To be precise, protocol, headers and the internal data buffer are used,
/// where the internal data buffer is used as the body of the response.
/// This means you cannot mix receiving and sending, because the body would get corrupted.
/// \param code The HTTP response code. Usually you want 200.
/// \param message The HTTP response message. Usually you want "OK".
/// \return A string containing a valid HTTP 1.0 or 1.1 response, ready for sending.
std::string HTTP::Parser::BuildResponse(std::string code, std::string message){
  /// \todo Include GET/POST variable parsing?
  std::map<std::string, std::string>::iterator it;
  std::string tmp = protocol+" "+code+" "+message+"\n";
  for (it=headers.begin(); it != headers.end(); it++){
    tmp += (*it).first + ": " + (*it).second + "\n";
  }
  tmp += "\n";
  tmp += HTTPbuffer;
  return tmp;
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
  HTTPbuffer = s;
  SetHeader("Content-Length", s.length());
}

/// Function that sets the body of a response or request, along with the correct Content-Length header.
/// \param buffer The buffer data to set the body to.
/// \param len Length of the buffer data.
void HTTP::Parser::SetBody(char * buffer, int len){
  HTTPbuffer = "";
  HTTPbuffer.append(buffer, len);
  SetHeader("Content-Length", len);
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
  vars[i] = v;
}

/// Attempt to read a whole HTTP request or response from DDV::Socket sock.
/// \return True of a whole request or response was read, false otherwise.
bool HTTP::Parser::Read(DDV::Socket & sock){
  //returned true als hele http packet gelezen is
  int r = 0;
  int b = 0;
  char buffer[500];
  while (true){
    r = sock.ready();
    if (r < 1){
      if (r == -1){
        #if DEBUG >= 1
        fprintf(stderr, "User socket is disconnected.\n");
        #endif
      }
      return parse();
    }
    b = sock.iread(buffer, 500);
    HTTPbuffer.append(buffer, b);
  }
  return false;
}//HTTPReader::ReadSocket

/// Reads a full set of HTTP responses/requests from file F.
/// \return Always false. Use HTTP::Parser::CleanForNext() to parse the contents of the file.
bool HTTP::Parser::Read(FILE * F){
  //returned true als hele http packet gelezen is
  int b = 1;
  char buffer[500];
  while (b > 0){
    b = fread(buffer, 1, 500, F);
    HTTPbuffer.append(buffer, b);
  }
  return false;
}//HTTPReader::ReadSocket

/// Attempt to read a whole HTTP response or request from the internal data buffer.
/// If succesful, fills its own fields with the proper data and removes the response/request
/// from the internal data buffer.
/// \return True on success, false otherwise.
bool HTTP::Parser::parse(){
  size_t f;
  std::string tmpA, tmpB, tmpC;
  while (HTTPbuffer != ""){
    if (!seenHeaders){
      f = HTTPbuffer.find('\n');
      if (f == std::string::npos) return false;
      tmpA = HTTPbuffer.substr(0, f);
      HTTPbuffer.erase(0, f+1);
      while (tmpA.find('\r') != std::string::npos){tmpA.erase(tmpA.find('\r'));}
      if (!seenReq){
        seenReq = true;
        f = tmpA.find(' ');
        if (f != std::string::npos){method = tmpA.substr(0, f); tmpA.erase(0, f+1);}
        f = tmpA.find(' ');
        if (f != std::string::npos){url = tmpA.substr(0, f); tmpA.erase(0, f+1);}
        f = tmpA.find(' ');
        if (f != std::string::npos){protocol = tmpA.substr(0, f); tmpA.erase(0, f+1);}
        /// \todo Include GET variable parsing?
      }else{
        if (tmpA.size() == 0){
          seenHeaders = true;
          if (GetHeader("Content-Length") != ""){length = atoi(GetHeader("Content-Length").c_str());}
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
        /// \todo Include POST variable parsing?
        if (HTTPbuffer.length() >= length){
          body = HTTPbuffer.substr(0, length);
          HTTPbuffer.erase(0, length);
          return true;
        }else{
          return false;
        }
      }else{
        return true;
      }
    }
  }
  return false; //we should never get here...
}//HTTPReader::parse

/// Sends data as response to conn.
/// The response is automatically first build using HTTP::Parser::BuildResponse().
/// \param conn The DDV::Socket to send the response over.
/// \param code The HTTP response code. Usually you want 200.
/// \param message The HTTP response message. Usually you want "OK".
void HTTP::Parser::SendResponse(DDV::Socket & conn, std::string code, std::string message){
  std::string tmp = BuildResponse(code, message);
  conn.write(tmp);
}

/// Sends data as HTTP/1.1 bodypart to conn.
/// HTTP/1.1 chunked encoding is automatically applied if needed.
/// \param conn The DDV::Socket to send the part over.
/// \param buffer The buffer to send.
/// \param len The length of the buffer.
void HTTP::Parser::SendBodyPart(DDV::Socket & conn, char * buffer, int len){
  std::string tmp;
  tmp.append(buffer, len);
  SendBodyPart(conn, tmp);
}

/// Sends data as HTTP/1.1 bodypart to conn.
/// HTTP/1.1 chunked encoding is automatically applied if needed.
/// \param conn The DDV::Socket to send the part over.
/// \param bodypart The data to send.
void HTTP::Parser::SendBodyPart(DDV::Socket & conn, std::string bodypart){
  if (protocol == "HTTP/1.1"){
    static char len[10];
    int sizelen;
    sizelen = snprintf(len, 10, "%x\r\n", (unsigned int)bodypart.size());
    conn.write(len, sizelen);
    conn.write(bodypart);
    conn.write(len+sizelen-2, 2);
  }else{
    conn.write(bodypart);
  }
}
