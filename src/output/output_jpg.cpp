#include "output_jpg.h"
#include <mist/bitfields.h>
#include <mist/mp4_generic.h>
#include <mist/procs.h>
#include <sys/stat.h>  //for stat
#include <sys/types.h> //for stat
#include <unistd.h>    //for stat

namespace Mist{
  OutJPG::OutJPG(Socket::Connection &conn) : HTTPOutput(conn){
    motion = false;
    if (isRecording()){
      motion = (config->getString("target").find(".mj") != std::string::npos);
    }
  }

  void OutJPG::respondHTTP(const HTTP::Parser & req, bool headersOnly){
    // Set global defaults
    HTTPOutput::respondHTTP(req, headersOnly);

    motion = (req.url.find(".mj") != std::string::npos);
    if (motion){
      boundary = Util::getRandomAlphanumeric(24);
      H.SetHeader("Content-Type", "multipart/x-mixed-replace;boundary="+boundary);
      H.SetHeader("Connection", "close");
    }

    H.CleanPreserveHeaders();
    H.SendResponse("200", "OK", myConn);
    if (headersOnly){return;}
    if (motion){
      myConn.SendNow("\r\n--" + boundary + "\r\nContent-Type: image/jpeg\r\n\r\n");
    }
    parseData = true;
    wantRequest = false;
  }

  void OutJPG::sendNext(){
    char *dataPointer = 0;
    size_t len = 0;
    thisPacket.getString("data", dataPointer, len);
    myConn.SendNow(dataPointer, len);
    if (!motion){
      Util::logExitReason(ER_CLEAN_EOF, "end of single JPG frame");
      myConn.close();
    }else{
      myConn.SendNow("\r\n--" + boundary + "\r\nContent-Type: image/jpeg\r\n\r\n");
    }
  }

  /// Pretends the stream is always ready to play - we don't care about waiting times or whatever
  bool OutJPG::isReadyForPlay(){return true;}

  void OutJPG::init(Util::Config *cfg){
    HTTPOutput::init(cfg);
    capa["name"] = "JPG";
    capa["desc"] = "Support both single-frame JPEG and motion JPEG (e.g. MJPEG) over HTTP";
    capa["url_rel"].append("/$.jpg");
    capa["url_rel"].append("/$.mjpg");
    capa["url_match"].append("/$.jpg");
    capa["url_match"].append("/$.jpeg");
    capa["url_match"].append("/$.mjpg");
    capa["url_match"].append("/$.mjpeg");
    capa["codecs"][0u][0u].append("JPEG");
    capa["methods"][0u]["handler"] = "http";
    capa["methods"][0u]["type"] = "html5/image/jpeg";
    capa["methods"][0u]["hrn"] = "JPEG image";
    capa["methods"][0u]["url_rel"] = "/$.jpg";
    capa["methods"][0u]["priority"] = 1;
    capa["methods"][1u]["handler"] = "http";
    capa["methods"][1u]["type"] = "html5/image/jpeg";
    capa["methods"][1u]["hrn"] = "JPEG stream";
    capa["methods"][1u]["url_rel"] = "/$.mjpg";
    capa["methods"][1u]["priority"] = 2;
    config->addStandardPushCapabilities(capa);
    capa["push_urls"].append("/*.jpg");
    capa["push_urls"].append("/*.jpeg");
    capa["push_urls"].append("/*.mjpg");
    capa["push_urls"].append("/*.mjpeg");

    cfg->addOptionsFromCapabilities(capa);

    JSON::Value opt;
    opt["arg"] = "string";
    opt["default"] = "";
    opt["arg_num"] = 1;
    opt["help"] = "Target filename to store JPG file as, or - for stdout.";
    cfg->addOption("target", opt);
  }

}// namespace Mist
