/// \file http_parser.h
/// Holds all headers for the HTTP namespace.

#pragma once
#include <map>
#include <string>
#include <stdlib.h>
#include <stdio.h>

/// Holds all HTTP processing related code.
namespace HTTP {
  /// Simple class for reading and writing HTTP 1.0 and 1.1.
  class Parser{
    public:
      Parser();
      bool Read(std::string & strbuf);
      std::string GetHeader(std::string i);
      std::string GetVar(std::string i);
      std::string getUrl();
      void SetHeader(std::string i, std::string v);
      void SetHeader(std::string i, int v);
      void SetVar(std::string i, std::string v);
      void SetBody(std::string s);
      void SetBody(char * buffer, int len);
      std::string & BuildRequest();
      std::string & BuildResponse(std::string code, std::string message);
      void Chunkify(std::string & bodypart);
      void Clean();
      static std::string urlunescape(const std::string & in);
      static std::string urlencode(const std::string & in);
      std::string body;
      std::string method;
      std::string url;
      std::string protocol;
      unsigned int length;
    private:
      bool seenHeaders;
      bool seenReq;
      bool getChunks;
      unsigned int doingChunk;
      bool parse(std::string & HTTPbuffer);
      void parseVars(std::string data);
      std::string builder;
      std::string read_buffer;
      std::map<std::string, std::string> headers;
      std::map<std::string, std::string> vars;
      void Trim(std::string & s);
      static int unhex(char c);
      static std::string hex(char dec);
  };
//HTTP::Parser class

}//HTTP namespace
