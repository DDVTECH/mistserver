#include "output_http_minimalserver.h"
#include <fstream>

namespace Mist {
  OutHTTPMinimalServer::OutHTTPMinimalServer(Socket::Connection & conn) : HTTPOutput(conn){
    //resolve symlinks etc to a real path
    char * rp = realpath(config->getString("webroot").c_str(), 0);
    if (rp){
      resolved_path = rp;
      resolved_path += "/";
      free(rp);
    }
  }
  OutHTTPMinimalServer::~OutHTTPMinimalServer() {}
  
  void OutHTTPMinimalServer::init(Util::Config * cfg){
    HTTPOutput::init(cfg);
    capa["name"] = "HTTPMinimalServer";
    capa["desc"] = "Serves static files over HTTP from a set folder";
    capa["url_rel"] = "/static/";
    capa["url_prefix"] = "/static/";
    cfg->addOption("webroot", JSON::fromString("{\"arg\":\"string\", \"short\":\"w\",\"long\":\"webroot\",\"help\":\"Root directory for static files to serve.\"}"));
    capa["required"]["webroot"]["name"] = "Web root directory";
    capa["required"]["webroot"]["help"] = "Main directory where files are served from.";
    capa["required"]["webroot"]["type"] = "str";
    capa["required"]["webroot"]["option"] = "--webroot";
  }
  
  void OutHTTPMinimalServer::onHTTP(){
    std::string method = H.method;
    //determine actual file path for the request
    std::string path = resolved_path + H.url.substr(8);
    
    //convert this to a real path with resolved symlinks etc
    char * rp = realpath(path.c_str(), 0);
    if (rp){
      path = rp;
      free(rp);
    }

    if (!rp || path.substr(0, resolved_path.size()) != resolved_path){
      if (!rp){
        WARN_MSG("URL %s does not exist: %s", H.url.c_str(), path.c_str());
      }else{
        WARN_MSG("URL %s is not inside webroot %s: %s", H.url.c_str(), resolved_path.c_str(), path.c_str());
      }
      H.Clean();
      H.SetHeader("Server", "mistserver/" PACKAGE_VERSION);
      H.setCORSHeaders();
      if(method == "OPTIONS" || method == "HEAD"){
        H.SendResponse("200", "OK", myConn);
        return;
      }
      H.SetBody("File not found");
      H.SendResponse("404", "OK", myConn);
      return;
    }


    char buffer[4096];
    std::ifstream inFile(path.c_str());
    inFile.seekg(0, std::ios_base::end);
    unsigned long long filesize = inFile.tellg();
    inFile.seekg(0, std::ios_base::beg);
    H.Clean();
    H.SetHeader("Server", "mistserver/" PACKAGE_VERSION);
    H.SetHeader("Content-Length", filesize);
    H.setCORSHeaders();
    if(method == "OPTIONS" || method == "HEAD"){
      H.SendResponse("200", "OK", myConn);
      H.Clean();
      return;
    }
    H.SendResponse("200", "OK", myConn);
    while (inFile.good()){
      inFile.read(buffer, 4096);
      myConn.SendNow(buffer, inFile.gcount());
    }
  }
}

