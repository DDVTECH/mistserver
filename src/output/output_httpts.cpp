#include "output_httpts.h"
#include "lib/defines.h"
#include <mist/defines.h>
#include <mist/http_parser.h>
#include <mist/procs.h>
#include <mist/stream.h>
#include <mist/ts_packet.h>
#include <mist/ts_stream.h>
#include <mist/url.h>
#include <dirent.h>
#include <unistd.h>

namespace Mist{
  OutHTTPTS::OutHTTPTS(Socket::Connection &conn) : TSOutput(conn){
    sendRepeatingHeaders = 500; // PAT/PMT every 500ms (DVB spec)
    removeOldPlaylistFiles = true;
    previousStartTime = 0xFFFFFFFFFFFFFFFFull;
    prevTsFile = "";
    durationSum = 0;
    addFinalHeader = false;
    isUrlTarget = false;
    forceVodPlaylist = false;
    writeFilenameOnly = false;
    playlistBuffer = "";
    HTTP::URL target(config->getString("target"));

    if (targetParams["overwrite"].size()){
      std::string paramValue = targetParams["overwrite"];
      if (paramValue == "0" || paramValue == "false"){
        removeOldPlaylistFiles = false;
      }
    }

    if (target.protocol == "srt"){
      std::string tgt = config->getString("target");
      HTTP::URL srtUrl(tgt);
      config->getOption("target", true).append("ts-exec:srt-live-transmit file://con " + srtUrl.getUrl());
      INFO_MSG("Rewriting SRT target '%s' to '%s'", tgt.c_str(), config->getString("target").c_str());
    } else if (config->getString("target").substr(0, 8) == "ts-exec:"){
      std::string input = config->getString("target").substr(8);
      char *args[128];
      uint8_t argCnt = 0;
      char *startCh = 0;
      for (char *i = (char *)input.c_str(); i <= input.data() + input.size(); ++i){
        if (!*i){
          if (startCh){args[argCnt++] = startCh;}
          break;
        }
        if (*i == ' '){
          if (startCh){
            args[argCnt++] = startCh;
            startCh = 0;
            *i = 0;
          }
        }else{
          if (!startCh){startCh = i;}
        }
      }
      args[argCnt] = 0;

      int fin = -1;
      Util::Procs::StartPiped(args, &fin, 0, 0);
      myConn.open(fin, -1);

      wantRequest = false;
      parseData = true;
    } else if (target.protocol == "s3+http" || target.protocol == "s3+https" || target.protocol == "s3"){
      addFinalHeader = true;
      isUrlTarget = true;
      forceVodPlaylist = true;
      writeFilenameOnly = true;
      prepend = "";
      playlistLocation = target.getUrl();
      tsLocation = target.link("./0.ts").getUrl();
      INFO_MSG("Uploading to S3 using dms-uploader with URL '%s'", playlistLocation.c_str());
      setenv("MST_ORIG_TARGET", tsLocation.c_str(), 1);
      INFO_MSG("Playlist location will be '%s'. TS location will be '%s'", playlistLocation.c_str(), tsLocation.c_str());
      config->getOption("target", true).append(tsLocation);
      std::stringstream ss;
      ss << config->getInteger("targetSegmentLength");
      targetParams["split"] = ss.str();
    } else if (config->getString("target").size()){
      addFinalHeader = true;
      HTTP::URL target(config->getString("target"));
      // If writing to a playlist file, set target strings and remember playlist location
      if(target.getExt() == "m3u" || target.getExt() == "m3u8"){
        // Location to .m3u(8) file we will keep updated
        playlistLocation = target.getFilePath();
        // Subfolder name which gets prepended to each entry in the playlist file
        prepend = "./segments_" + target.path.substr(target.path.rfind("/") + 1, target.path.size() - target.getExt().size() - target.path.rfind("/") - 2) + "/";
        HTTP::URL tsFolderPath(target.link(prepend + "/0.ts").getFilePath());
        tsLocation = tsFolderPath.getFilePath();
        INFO_MSG("Playlist location will be '%s'. TS filename will be in the form of '%s'", playlistLocation.c_str(), tsLocation.c_str());
        // Remember target name including the $datetime variable
        setenv("MST_ORIG_TARGET", tsLocation.c_str(), 1);
        // If the playlist exists, first remove existing TS files
        if (removeOldPlaylistFiles){
          DIR *dir = opendir(tsFolderPath.getFilePath().c_str());
          if (dir){
            INFO_MSG("Removing TS files in %s", tsFolderPath.getFilePath().c_str());
            struct dirent *dp;
            do{
              errno = 0;
              if ((dp = readdir(dir))){
                HTTP::URL filePath = tsFolderPath.link(dp->d_name);
                if (filePath.getExt() == "ts"){
                  MEDIUM_MSG("Removing TS file '%s'", filePath.getFilePath().c_str());
                  remove(filePath.getFilePath().c_str());
                }
              }
            }while (dp != NULL);
            closedir(dir);
          }
          // Also remove the playlist file itself. SendHeader handles (re)creation of the playlist file
          if (!remove(playlistLocation.c_str())){
            HIGH_MSG("Removed existing playlist file '%s'", playlistLocation.c_str());
          }
        }else{
          // Else we want to add the #EXT-X-DISCONTINUITY tag
          std::ofstream outPlsFile;
          outPlsFile.open(playlistLocation.c_str(), std::ofstream::app);
          outPlsFile << "#EXT-X-DISCONTINUITY" << "\n";
          outPlsFile.close();
        }
        config->getOption("target", true).append(tsLocation);
        // Finally set split time in seconds
        std::stringstream ss;
        ss << config->getInteger("targetSegmentLength");
        targetParams["split"] = ss.str();
      }
    }
  }

  OutHTTPTS::~OutHTTPTS(){}

  void OutHTTPTS::initialSeek(){
    // Adds passthrough support to the regular initialSeek function
    if (targetParams.count("passthrough")){selectAllTracks();}
    Output::initialSeek();
  }

  void OutHTTPTS::init(Util::Config *cfg){
    HTTPOutput::init(cfg);
    capa["name"] = "HTTPTS";
    capa["friendly"] = "TS over HTTP";
    capa["desc"] = "Pseudostreaming in MPEG2/TS format over HTTP";
    capa["url_rel"] = "/$.ts";
    capa["url_match"] = "/$.ts";
    capa["socket"] = "http_ts";
    capa["codecs"][0u][0u].append("+H264");
    capa["codecs"][0u][0u].append("+HEVC");
    capa["codecs"][0u][0u].append("+MPEG2");
    capa["codecs"][0u][1u].append("+AAC");
    capa["codecs"][0u][1u].append("+MP3");
    capa["codecs"][0u][1u].append("+AC3");
    capa["codecs"][0u][1u].append("+EAC3");
    capa["codecs"][0u][1u].append("+MP2");
    capa["codecs"][0u][1u].append("+opus");
    capa["codecs"][1u][0u].append("rawts");
    capa["methods"][0u]["handler"] = "http";
    capa["methods"][0u]["type"] = "html5/video/mpeg";
    capa["methods"][0u]["hrn"] = "TS HTTP progressive";
    capa["methods"][0u]["priority"] = 1;
    capa["push_urls"].append("/*.ts");
    capa["push_urls"].append("ts-exec:*");
    capa["push_urls"].append("*.m3u");
    capa["push_urls"].append("*.m3u8");

#ifndef WITH_SRT
    {
      pid_t srt_tx = -1;
      const char *args[] ={"srt-live-transmit", 0};
      srt_tx = Util::Procs::StartPiped(args, 0, 0, 0);
      if (srt_tx > 1){
        capa["push_urls"].append("srt://*");
        capa["desc"] = capa["desc"].asStringRef() +
                       ". Non-native SRT push output support (srt://*) is installed and available.";
      }else{
        capa["desc"] =
            capa["desc"].asStringRef() +
            ". To enable non-native SRT push output support, please install the srt-live-transmit binary.";
      }
    }
#endif

    JSON::Value opt;
    opt["arg"] = "string";
    opt["default"] = "";
    opt["arg_num"] = 1;
    opt["help"] = "Target filename to store TS file as, '*.m3u8' or '*.m3u' for writing to a playlist, or - for stdout.";
    cfg->addOption("target", opt);

    opt.null();
    opt["arg"] = "integer";
    opt["long"] = "targetSegmentLength";
    opt["short"] = "l";
    opt["help"] = "Target time duration in seconds for TS files, when outputting to disk.";
    opt["value"].append(5);
    config->addOption("targetSegmentLength", opt);
    capa["optional"]["targetSegmentLength"]["name"] = "Length of TS files (ms)";
    capa["optional"]["targetSegmentLength"]["help"] = "Target time duration in milliseconds for TS files, when outputting to disk.";
    capa["optional"]["targetSegmentLength"]["option"] = "--targetLength";
    capa["optional"]["targetSegmentLength"]["type"] = "uint";
    capa["optional"]["targetSegmentLength"]["default"] = 5;
  }

  bool OutHTTPTS::isRecording(){return config->getString("target").size();}

  bool OutHTTPTS::onFinish(){
    if(addFinalHeader){
      addFinalHeader = false;
      sendHeader();
    }
    if (plsConn){
      if (forceVodPlaylist || !M.getLive()){
        playlistBuffer += "#EXT-X-ENDLIST";
        plsConn.SendNow(playlistBuffer);
      }
      plsConn.close();
    }
    return false;
  }

  void OutHTTPTS::onHTTP(){
    std::string method = H.method;
    initialize();
    H.clearHeader("Range");
    H.clearHeader("Icy-MetaData");
    H.clearHeader("User-Agent");
    H.clearHeader("Host");
    H.clearHeader("Accept-Ranges");
    H.clearHeader("transferMode.dlna.org");
    H.SetHeader("Content-Type", "video/mpeg");
    H.setCORSHeaders();
    if (method == "OPTIONS" || method == "HEAD"){
      H.SendResponse("200", "OK", myConn);
      H.Clean();
      return;
    }
    H.protocol = "HTTP/1.0"; // Force HTTP/1.0 because some devices just don't understand chunked replies
    H.StartResponse(H, myConn);
    parseData = true;
    wantRequest = false;
  }

  void OutHTTPTS::sendHeader(){
    double segmentDuration = double(lastPacketTime - previousStartTime) / 1000;
    if (previousStartTime == 0xFFFFFFFFFFFFFFFFull){ durationSum += atoll(targetParams["split"].c_str()) * 1000; }
    else { durationSum += lastPacketTime - previousStartTime; }

    // Add the previous TS file to the playlist
    if (prevTsFile != ""){
      std::ofstream outPlsFile;
      HIGH_MSG("Duration of TS file at location '%s' is %f ms", prevTsFile.c_str(), segmentDuration);
      if (segmentDuration > config->getInteger("targetSegmentLength")){
        WARN_MSG("Segment duration exceeds target segment duration. This may cause playback stalls or other errors");
      }
      if (!plsConn){
        static int tmpFd = open("/dev/null", O_RDWR);
        plsConn.open(tmpFd);
        Util::Procs::socketList.insert(tmpFd);
        INFO_MSG("Connecting to playlist file '%s'", playlistLocation.c_str());
        genericWriter(playlistLocation, !removeOldPlaylistFiles, &plsConn);
      }

      if (removeOldPlaylistFiles){
        INFO_MSG("Creating new playlist at '%s'", playlistLocation.c_str());
        removeOldPlaylistFiles = false;
        std::string type = "EVENT";
        if (forceVodPlaylist || !M.getLive()){ type = "VOD";}
        playlistBuffer += "#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-PLAYLIST-TYPE:" + type + "\n#EXT-X-TARGETDURATION:" + \
          JSON::Value(config->getInteger("targetSegmentLength")).asString() + "\n#EXT-X-MEDIA-SEQUENCE:0\n";
      }
      // Add current timestamp
      if (M.getLive() && !forceVodPlaylist){
        uint64_t unixMs = M.getBootMsOffset() + (Util::unixMS() - Util::bootMS()) + firstTime;
        playlistBuffer += std::string("#EXT-X-PROGRAM-DATE-TIME:") + Util::getUTCStringMillis(unixMs) + "\n";
      }
      INFO_MSG("Adding new segment of %.2f seconds to playlist '%s'", segmentDuration, playlistLocation.c_str());
      // Strip path to the ts file if necessary
      if (writeFilenameOnly){ prevTsFile = prevTsFile.substr(prevTsFile.rfind("/") + 1); }
      // Append duration & TS filename to playlist file
      playlistBuffer += std::string("#EXTINF:") + JSON::Value(segmentDuration).asString() + ",\n" + prevTsFile + "\n";
      plsConn.SendNow(playlistBuffer);
      playlistBuffer = "";
    }

    // Save the target we were pushing towards as the latest completed segment
    prevTsFile = tsLocation;
    // Set the start time of the current segment. We will know the end time once the next split point is reached
    previousStartTime = lastPacketTime;
    // Set the next filename we will push towards once the split point is reached
    HTTP::URL target(getenv("MST_ORIG_TARGET"));
    if (isUrlTarget){
      tsLocation = target.link(std::string("./") + JSON::Value(durationSum).asString() + ".ts").getUrl();
    }else{
      tsLocation = target.link(std::string("./") + JSON::Value(durationSum).asString() + ".ts").getFilePath();
    }
    INFO_MSG("Setting next target TS file '%s'", tsLocation.c_str());
    setenv("MST_ORIG_TARGET", tsLocation.c_str(), 1);

    TSOutput::sendHeader();
  }

  void OutHTTPTS::sendTS(const char *tsData, size_t len){
    if (isRecording()){
      myConn.SendNow(tsData, len);
      return;
    }
    H.Chunkify(tsData, len, myConn);
    if (targetParams.count("passthrough")){selectAllTracks();}
  }
}// namespace Mist
