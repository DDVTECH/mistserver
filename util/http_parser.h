/// \file http_parser.h
/// Holds all headers for the HTTP namespace.

#pragma once
#include <map>
#include <string>
#include <stdlib.h>
#include <stdio.h>
#include "ddv_socket.h"

/// Holds all HTTP processing related code.
namespace HTTP{
  /// Simple class for reading and writing HTTP 1.0 and 1.1.
  class Parser{
    public:
      Parser();
      bool Read(DDV::Socket & sock);
      bool Read(FILE * F);
      std::string GetHeader(std::string i);
      std::string GetVar(std::string i);
      void SetHeader(std::string i, std::string v);
      void SetHeader(std::string i, int v);
      void SetVar(std::string i, std::string v);
      void SetBody(std::string s);
      void SetBody(char * buffer, int len);
      std::string BuildRequest();
      std::string BuildResponse(std::string code, std::string message);
      void SendResponse(DDV::Socket & conn, std::string code, std::string message);
      void SendBodyPart(DDV::Socket & conn, char * buffer, int len);
      void SendBodyPart(DDV::Socket & conn, std::string bodypart);
      void Clean();
      bool CleanForNext();
      std::string body;
      std::string method;
      std::string url;
      std::string protocol;
      unsigned int length;
    private:
      bool seenHeaders;
      bool seenReq;
      bool parse();
      std::string HTTPbuffer;
      std::map<std::string, std::string> headers;
      std::map<std::string, std::string> vars;
      void Trim(std::string & s);
  };//HTTP::Parser class
};//HTTP namespace
