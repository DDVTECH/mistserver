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

/// Re-initializes the HTTP::Parser, leaving the internal data buffer alone, then tries to parse a new request or response.
/// Does the same as HTTP::Parser::Clean(), then returns HTTP::Parser::parse().
bool HTTP::Parser::CleanForNext(){
  Clean();
  return parse();
}

/// Returns a string containing a valid HTTP 1.0 or 1.1 request, ready for sending.
/// The request is build from internal variables set before this call is made.
/// To be precise, method, url, protocol, headers and body are used.
/// \return A string containing a valid HTTP 1.0 or 1.1 request, ready for sending.
std::string HTTP::Parser::BuildRequest(){
  /// \todo Include GET/POST variable parsing?
  std::map<std::string, std::string>::iterator it;
  std::string tmp = method+" "+url+" "+protocol+"\n";
  for (it=headers.begin(); it != headers.end(); it++){
    tmp += (*it).first + ": " + (*it).second + "\n";
  }
  tmp += "\n" + body + "\n";
  return tmp;
}

/// Returns a string containing a valid HTTP 1.0 or 1.1 response, ready for sending.
/// The response is partly build from internal variables set before this call is made.
/// To be precise, protocol, headers and body are used.
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
  tmp += body;
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

/// Attempt to read a whole HTTP request or response from Socket::Connection.
/// \param sock The socket to use.
/// \param nonblock When true, will not block even if the socket is blocking.
/// \return True of a whole request or response was read, false otherwise.
bool HTTP::Parser::Read(Socket::Connection & sock, bool nonblock){
  if (nonblock && (sock.ready() < 1)){return parse();}
  sock.read(HTTPbuffer);
  return parse();
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
        if (url.find('?') != std::string::npos){
          std::string queryvars = url.substr(url.find('?')+1);
          parseVars(queryvars); //parse GET variables
        }
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
        if (HTTPbuffer.length() >= length){
          body = HTTPbuffer.substr(0, length);
          parseVars(body); //parse POST variables
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
/// \param conn The Socket::Connection to send the response over.
/// \param code The HTTP response code. Usually you want 200.
/// \param message The HTTP response message. Usually you want "OK".
void HTTP::Parser::SendResponse(Socket::Connection & conn, std::string code, std::string message){
  std::string tmp = BuildResponse(code, message);
  conn.write(tmp);
}

/// Parses GET or POST-style variable data.
/// Saves to internal variable structure using HTTP::Parser::SetVar.
void HTTP::Parser::parseVars(std::string & data){
  std::string varname;
  std::string varval;
  while (data.find('=') != std::string::npos){
    size_t found = data.find('=');
    varname = urlunescape(data.substr(0, found));
    data.erase(0, found+1);
    found = data.find('&');
    varval = urlunescape(data.substr(0, found));
    SetVar(varname, varval);
    if (found == std::string::npos){
      data.clear();
    }else{
      data.erase(0, found+1);
    }
  }
}

/// Sends data as HTTP/1.1 bodypart to conn.
/// HTTP/1.1 chunked encoding is automatically applied if needed.
/// \param conn The Socket::Connection to send the part over.
/// \param buffer The buffer to send.
/// \param len The length of the buffer.
void HTTP::Parser::SendBodyPart(Socket::Connection & conn, char * buffer, int len){
  std::string tmp;
  tmp.append(buffer, len);
  SendBodyPart(conn, tmp);
}

/// Sends data as HTTP/1.1 bodypart to conn.
/// HTTP/1.1 chunked encoding is automatically applied if needed.
/// \param conn The Socket::Connection to send the part over.
/// \param bodypart The data to send.
void HTTP::Parser::SendBodyPart(Socket::Connection & conn, std::string bodypart){
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
