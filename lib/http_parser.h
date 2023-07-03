/// \file http_parser.h
/// Holds all headers for the HTTP namespace.

#pragma once
#include "socket.h"
#include "util.h"
#include <map>
#include <stdio.h>
#include <stdlib.h>
#include <string>

/// Holds all HTTP processing related code.
namespace HTTP{

  /// HTTP variable parser to std::map<std::string, std::string> structure.
  /// Reads variables from data, decodes and stores them to storage.
  void parseVars(const std::string &data, std::map<std::string, std::string> &storage, const std::string & separator = "&");

  /// Simple class for reading and writing HTTP 1.0 and 1.1.
  class Parser{
  public:
    Parser();
    bool Read(Socket::Connection &conn, Util::DataCallback &cb = Util::defaultDataCallback);
    bool Read(std::string &strbuf);
    const std::string &GetHeader(const std::string &i) const;
    bool hasHeader(const std::string &i) const;
    void clearHeader(const std::string &i);
    uint8_t getPercentage() const;
    const std::string &GetVar(const std::string &i) const;
    std::string getUrl() const;
    std::string allVars() const;
    void SetHeader(std::string i, std::string v);
    void SetHeader(std::string i, long long v);
    void setCORSHeaders();
    void SetVar(std::string i, std::string v);
    void SetBody(std::string s);
    void SetBody(const char *buffer, int len);
    std::string &BuildRequest();
    std::string &BuildResponse();
    std::string &BuildResponse(std::string code, std::string message);
    void SendRequest(Socket::Connection &conn, const std::string &reqbody = "", bool allAtOnce = false);
    void sendRequest(Socket::Connection &conn, const void *body = 0, const size_t bodyLen = 0,
                     bool allAtOnce = false);
    void SendResponse(std::string code, std::string message, Socket::Connection &conn);
    void StartResponse(std::string code, std::string message, const Parser &request,
                       Socket::Connection &conn, bool bufferAllChunks = false);
    void StartResponse(Parser &request, Socket::Connection &conn, bool bufferAllChunks = false);
    void Chunkify(const std::string &bodypart, Socket::Connection &conn);
    void Chunkify(const char *data, unsigned int size, Socket::Connection &conn);
    void Proxy(Socket::Connection &from, Socket::Connection &to);
    void Clean();
    void CleanPreserveHeaders();
    void auth(const std::string &user, const std::string &pass, const std::string &authReq,
              const std::string &headerName = "Authorization");
    std::string body;
    std::string method;
    std::string url;
    std::string protocol;
    unsigned int length;
    bool knownLength;
    unsigned int currentLength;
    bool headerOnly; ///< If true, do not parse body if the length is a known size.
    bool bufferChunks;
    // this bool was private
    bool sendingChunks;
    void (*bodyCallback)(const char *, size_t);

  private:
    std::string cnonce;
    bool seenHeaders;
    bool seenReq;
    bool getChunks;
    bool possiblyComplete;
    unsigned int doingChunk;
    bool parse(std::string &HTTPbuffer, Util::DataCallback &cb = Util::defaultDataCallback);
    std::string builder;
    std::string read_buffer;
    std::map<std::string, std::string> headers;
    std::map<std::string, std::string> vars;
    void Trim(std::string &s);
  };

}// namespace HTTP
