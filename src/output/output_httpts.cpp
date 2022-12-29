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

    if (targetParams["overwrite"].size()){
      std::string paramValue = targetParams["overwrite"];
      if (paramValue == "0" || paramValue == "false"){
        removeOldPlaylistFiles = false;
      }
    }

    if (config->getString("target").substr(0, 6) == "srt://"){
      std::string tgt = config->getString("target");
      HTTP::URL srtUrl(tgt);
      config->getOption("target", true).append("ts-exec:srt-live-transmit file://con " + srtUrl.getUrl());
      INFO_MSG("Rewriting SRT target '%s' to '%s'", tgt.c_str(), config->getString("target").c_str());
    } else if (config->getString("target").substr(0, 8) == "ts-exec:"){
      std::string input = config->getString("target").substr(8);
      int fin = -1;
      Util::Procs::StartPipedShell(input.c_str(), &fin, 0, 0);
      myConn.open(fin, -1);

      wantRequest = false;
      parseData = true;
    } else if (config->getString("target").size()){
      HTTP::URL target(config->getString("target"));
      // If writing to a playlist file, set target strings and remember playlist location
      if(target.getExt() == "m3u" || target.getExt() == "m3u8"){
        // Location to .m3u(8) file we will keep updated
        playlistLocation = target.getFilePath();
        // Subfolder name which gets prepended to each entry in the playlist file
        prepend = "./segments_" + target.path.substr(target.path.rfind("/") + 1, target.path.size() - target.getExt().size() - target.path.rfind("/") - 2) + "/";
        HTTP::URL tsFolderPath(target.link(prepend).getFilePath());
        tsFilePath = tsFolderPath.getFilePath() + "$datetime.ts";
        INFO_MSG("Playlist location will be '%s'. TS filename will be in the form of '%s'", playlistLocation.c_str(), tsFilePath.c_str());
        // Remember target name including the $datetime variable
        setenv("MST_ORIG_TARGET", tsFilePath.c_str(), 1);
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
        // Set first target filename
        Util::streamVariables(tsFilePath, streamName);
        if (tsFilePath.rfind('?') != std::string::npos){
          tsFilePath.erase(tsFilePath.rfind('?'));
        }
        config->getOption("target", true).append(tsFilePath);
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
    capa["codecs"][0u][1u].append("+MP2");
    capa["codecs"][0u][1u].append("+opus");
    capa["codecs"][1u][0u].append("rawts");
    capa["methods"][0u]["handler"] = "http";
    capa["methods"][0u]["type"] = "html5/video/mpeg";
    capa["methods"][0u]["hrn"] = "TS HTTP progressive";
    capa["methods"][0u]["priority"] = 1;
    capa["push_urls"].append("/*.ts");
    capa["push_urls"].append("ts-exec:*");
    capa["push_urls"].append("/*.m3u");
    capa["push_urls"].append("/*.m3u8");

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

  /// \brief Goes through all of the packets in a TS file in order to calculate the total duration
  /// \param firstTime: is set to the firstTime of the TS file
  float OutHTTPTS::calculateSegmentDuration(std::string filepath, uint64_t & firstTime){
    firstTime = -1;
    uint64_t lastTime = 0;
    FILE *inFile;
    TS::Packet packet;
    DTSC::Packet headerPack;
    TS::Stream tsStream;

    inFile = fopen(filepath.c_str(), "r");
    while (!feof(inFile)){
      if (!packet.FromFile(inFile)){
        break;
      }
      tsStream.parse(packet, 0);
      while (tsStream.hasPacketOnEachTrack()){
        tsStream.getEarliestPacket(headerPack);
        lastTime = headerPack.getTime();
        if (firstTime > lastTime){
          firstTime = headerPack.getTime();
        }
        DONTEVEN_MSG("Found DTSC packet with timestamp %" PRIu64, lastTime);
      }
    }
    fclose(inFile);
    HIGH_MSG("Duration of TS file at location '%s' is " PRETTY_PRINT_MSTIME " (" PRETTY_PRINT_MSTIME " - " PRETTY_PRINT_MSTIME ")", filepath.c_str(), PRETTY_ARG_MSTIME(lastTime - firstTime), PRETTY_ARG_MSTIME(lastTime), PRETTY_ARG_MSTIME(firstTime));
    return (lastTime - firstTime);
  }

  void OutHTTPTS::sendHeader(){
    bool writeTimestamp = true;
    if (previousFile != ""){
      std::ofstream outPlsFile;
      // Calculate segment duration and round up to the nearest integer
      uint64_t firstTime = 0;
      float segmentDuration = (calculateSegmentDuration(previousFile, firstTime) / 1000);
      if (segmentDuration > config->getInteger("targetSegmentLength")){
        WARN_MSG("Segment duration exceeds target segment duration. This may cause playback stalls or other errors");
      }
      // If the playlist does not exist, init it
      FILE *fileHandle = fopen(playlistLocation.c_str(), "r");
      if (!fileHandle || removeOldPlaylistFiles){
        INFO_MSG("Creating new playlist at '%s'", playlistLocation.c_str());
        removeOldPlaylistFiles = false;
        outPlsFile.open(playlistLocation.c_str(), std::ofstream::trunc);
        outPlsFile << "#EXTM3U\n" << "#EXT-X-VERSION:3\n" << "#EXT-X-PLAYLIST-TYPE:EVENT\n"
          << "#EXT-X-TARGETDURATION:" << config->getInteger("targetSegmentLength") << "\n#EXT-X-MEDIA-SEQUENCE:0\n";
        // Add current livestream timestamp
        if (M.getLive()){
          uint64_t unixMs = M.getBootMsOffset() + (Util::unixMS() - Util::bootMS()) + firstTime;
          outPlsFile << "#EXT-X-PROGRAM-DATE-TIME:" << Util::getUTCStringMillis(unixMs) << std::endl;
          writeTimestamp = false;
        }
      // Otherwise open it in append mode
      } else {
        fclose(fileHandle);
        outPlsFile.open(playlistLocation.c_str(), std::ofstream::app);
      }
      // Add current timestamp
      if (M.getLive() && writeTimestamp){
        uint64_t unixMs = M.getBootMsOffset() + (Util::unixMS() - Util::bootMS()) + firstTime;
        outPlsFile << "#EXT-X-PROGRAM-DATE-TIME:" << Util::getUTCStringMillis(unixMs) << std::endl;
      }
      INFO_MSG("Adding new segment of %.2f seconds to playlist '%s'", segmentDuration, playlistLocation.c_str());
      // Append duration & TS filename to playlist file
      outPlsFile << "#EXTINF:" << segmentDuration << ",\n" << prepend << previousFile.substr(previousFile.rfind("/") + 1) << "\n";
      outPlsFile.close();
    }

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
