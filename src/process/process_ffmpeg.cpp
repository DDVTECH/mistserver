#include "process_ffmpeg.h"
#include "process.hpp"
#include <algorithm> //for std::find
#include <mist/defines.h>
#include <mist/procs.h>
#include <mist/stream.h>
#include <mist/tinythread.h>
#include <mist/util.h>
#include <ostream>
#include <sys/stat.h>  //for stat
#include <sys/types.h> //for stat
#include <unistd.h>    //for stat

int ofin = -1, ofout = 1, oferr = 2;
int ifin = -1, ifout = -1, iferr = 2;
int pipein[2], pipeout[2];

Util::Config co;
Util::Config conf;

// Complete config file loaded in JSON
JSON::Value opt;

uint64_t packetTimeDiff;
uint64_t sendPacketTime;
bool getFirst = false;
bool sendFirst = false;

uint32_t res_x = 0;
uint32_t res_y = 0;
Mist::OutENC Enc;


//Stat related stuff
JSON::Value pStat;
JSON::Value & pData = pStat["proc_status_update"]["status"];
tthread::mutex statsMutex;
uint64_t statSinkMs = 0;
uint64_t statSourceMs = 0;
int64_t bootMsOffset = 0;

namespace Mist{
  class ProcessSink : public InputEBML{
  public:
    ProcessSink(Util::Config *cfg) : InputEBML(cfg){
      capa["name"] = "FFMPEG";
    };
    void getNext(size_t idx = INVALID_TRACK_ID){
      {
        tthread::lock_guard<tthread::mutex> guard(statsMutex);
        if (pData["sink_tracks"].size() != userSelect.size()){
          pData["sink_tracks"].null();
          for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end(); it++){
            pData["sink_tracks"].append((uint64_t)it->first);
          }
        }
      }
      static bool recurse = false;
      if (recurse){return InputEBML::getNext(idx);}
      recurse = true;
      InputEBML::getNext(idx);
      recurse = false;
      uint64_t pTime = thisPacket.getTime();
      if (thisPacket){
        if (!getFirst){
          packetTimeDiff = sendPacketTime - pTime;
          getFirst = true;
        }
        pTime += packetTimeDiff;
        // change packettime
        char *data = thisPacket.getData();
        Bit::htobll(data + 12, pTime);
        if (pTime >= statSinkMs){statSinkMs = pTime;}
        if (meta && meta.getBootMsOffset() != bootMsOffset){meta.setBootMsOffset(bootMsOffset);}
      }
    }
    void setInFile(int stdin_val){
      inFile.open(stdin_val);
      streamName = opt["sink"].asString();
      if (!streamName.size()){streamName = opt["source"].asString();}
      Util::streamVariables(streamName, opt["source"].asString());
      Util::setStreamName(opt["source"].asString() + "→" + streamName);
      {
        tthread::lock_guard<tthread::mutex> guard(statsMutex);
        pStat["proc_status_update"]["sink"] = streamName;
        pStat["proc_status_update"]["source"] = opt["source"];
      }
      if (opt.isMember("target_mask") && !opt["target_mask"].isNull() && opt["target_mask"].asString() != ""){
        DTSC::trackValidDefault = opt["target_mask"].asInt();
      }
    }
    bool needsLock(){return false;}
    bool isSingular(){return false;}
    void connStats(Comms::Connections &statComm){
      for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end(); it++){
        if (it->second){it->second.setStatus(COMM_STATUS_DONOTTRACK | it->second.getStatus());}
      }
      InputEBML::connStats(statComm);
    }
  };

  class ProcessSource : public OutEBML{
  public:
    bool isRecording(){return false;}
    ProcessSource(Socket::Connection &c) : OutEBML(c){
      capa["name"] = "FFMPEG";
      targetParams["keeptimes"] = true;
      realTime = 0;
    };
    virtual bool onFinish(){
      if (opt.isMember("exit_unmask") && opt["exit_unmask"].asBool()){
        if (userSelect.size()){
          for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end(); it++){
            INFO_MSG("Unmasking source track %zu" PRIu64, it->first);
            meta.validateTrack(it->first, TRACK_VALID_ALL);
          }
        }
      }
      return OutEBML::onFinish();
    }
    virtual void dropTrack(size_t trackId, const std::string &reason, bool probablyBad = true){
      if (opt.isMember("exit_unmask") && opt["exit_unmask"].asBool()){
        INFO_MSG("Unmasking source track %zu" PRIu64, trackId);
        meta.validateTrack(trackId, TRACK_VALID_ALL);
      }
      OutEBML::dropTrack(trackId, reason, probablyBad);
    }
    void sendHeader(){
      if (opt["source_mask"].asBool()){
        for (std::map<size_t, Comms::Users>::iterator ti = userSelect.begin(); ti != userSelect.end(); ++ti){
          if (ti->first == INVALID_TRACK_ID){continue;}
          INFO_MSG("Masking source track %zu", ti->first);
          meta.validateTrack(ti->first, meta.trackValid(ti->first) & ~(TRACK_VALID_EXT_HUMAN | TRACK_VALID_EXT_PUSH));
        }
      }
      realTime = 0;
      OutEBML::sendHeader();
    };
    void connStats(uint64_t now, Comms::Connections &statComm){
      for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end(); it++){
        if (it->second){it->second.setStatus(COMM_STATUS_DONOTTRACK | it->second.getStatus());}
      }
      OutEBML::connStats(now, statComm);
    }
    void sendNext(){
      {
        tthread::lock_guard<tthread::mutex> guard(statsMutex);
        if (pData["source_tracks"].size() != userSelect.size()){
          pData["source_tracks"].null();
          for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end(); it++){
            pData["source_tracks"].append((uint64_t)it->first);
          }
        }
      }
      if (thisTime > statSourceMs){statSourceMs = thisTime;}
      needsLookAhead = 0;
      maxSkipAhead = 0;
      if (!sendFirst){
        sendPacketTime = thisPacket.getTime();
        bootMsOffset = M.getBootMsOffset();
        sendFirst = true;
      }
      OutEBML::sendNext();
    }
  };

}



void sinkThread(void *){
  Mist::ProcessSink in(&co);
  co.getOption("output", true).append("-");
  co.activate();

  MEDIUM_MSG("Running sink thread...");

  in.setInFile(pipeout[0]);
  co.is_active = true;
  in.run();

  conf.is_active = false;
}

void sourceThread(void *){
  Mist::ProcessSource::init(&conf);
  conf.getOption("streamname", true).append(opt["source"].c_str());

  if (Enc.isAudio){
    conf.getOption("target", true).append("-?audio=" + opt["source_track"].asString() + "&video=-1");
  }else if (Enc.isVideo){
    conf.getOption("target", true).append("-?video=" + opt["source_track"].asString() + "&audio=-1");
  }else{
    FAIL_MSG("Cannot set target option parameters");
    return;
  }

  conf.is_active = true;

  Socket::Connection c(pipein[1], 0);
  Mist::ProcessSource out(c);

  MEDIUM_MSG("Running source thread...");
  out.run();

  co.is_active = false;
}

int main(int argc, char *argv[]){
  DTSC::trackValidMask = TRACK_VALID_INT_PROCESS;
  Util::Config config(argv[0]);
  Util::Config::binaryType = Util::PROCESS;
  JSON::Value capa;

  {
    JSON::Value opt;
    opt["arg"] = "string";
    opt["default"] = "-";
    opt["arg_num"] = 1;
    opt["help"] = "JSON configuration, or - (default) to read from stdin";
    config.addOption("configuration", opt);

    JSON::Value option;
    option["long"] = "json";
    option["short"] = "j";
    option["help"] = "Output connector info in JSON format, then exit.";
    option["value"].append(0);
    config.addOption("json", option);
  }

  if (!(config.parseArgs(argc, argv))){return 1;}
  if (config.getBool("json")){

    capa["name"] = "FFMPEG";                                             // internal name of process
    capa["hrn"] = "Encoder: FFMPEG";                                     // human readable name
    capa["desc"] = "Use a local FFMPEG installed binary to do encoding"; // description
    capa["sort"] = "sort"; // sort the parameters by this key
    addGenericProcessOptions(capa);

    capa["optional"]["source_mask"]["name"] = "Source track mask";
    capa["optional"]["source_mask"]["help"] = "What internal processes should have access to the source track(s)";
    capa["optional"]["source_mask"]["type"] = "select";
    capa["optional"]["source_mask"]["select"][0u][0u] = "";
    capa["optional"]["source_mask"]["select"][0u][1u] = "Keep original value";
    capa["optional"]["source_mask"]["select"][1u][0u] = 255;
    capa["optional"]["source_mask"]["select"][1u][1u] = "Everything";
    capa["optional"]["source_mask"]["select"][2u][0u] = 4;
    capa["optional"]["source_mask"]["select"][2u][1u] = "Processing tasks (not viewers, not pushes)";
    capa["optional"]["source_mask"]["select"][3u][0u] = 6;
    capa["optional"]["source_mask"]["select"][3u][1u] = "Processing and pushing tasks (not viewers)";
    capa["optional"]["source_mask"]["select"][4u][0u] = 5;
    capa["optional"]["source_mask"]["select"][4u][1u] = "Processing and viewer tasks (not pushes)";
    capa["optional"]["source_mask"]["default"] = "";
    capa["optional"]["source_mask"]["sort"] = "dba";

    capa["optional"]["target_mask"]["name"] = "Output track mask";
    capa["optional"]["target_mask"]["help"] = "What internal processes should have access to the output track(s)";
    capa["optional"]["target_mask"]["type"] = "select";
    capa["optional"]["target_mask"]["select"][0u][0u] = "";
    capa["optional"]["target_mask"]["select"][0u][1u] = "Keep original value";
    capa["optional"]["target_mask"]["select"][1u][0u] = 255;
    capa["optional"]["target_mask"]["select"][1u][1u] = "Everything";
    capa["optional"]["target_mask"]["select"][2u][0u] = 1;
    capa["optional"]["target_mask"]["select"][2u][1u] = "Viewer tasks (not processing, not pushes)";
    capa["optional"]["target_mask"]["select"][3u][0u] = 2;
    capa["optional"]["target_mask"]["select"][3u][1u] = "Pushing tasks (not processing, not viewers)";
    capa["optional"]["target_mask"]["select"][4u][0u] = 4;
    capa["optional"]["target_mask"]["select"][4u][1u] = "Processing tasks (not pushes, not viewers)";
    capa["optional"]["target_mask"]["select"][5u][0u] = 3;
    capa["optional"]["target_mask"]["select"][5u][1u] = "Viewer and pushing tasks (not processing)";
    capa["optional"]["target_mask"]["select"][6u][0u] = 5;
    capa["optional"]["target_mask"]["select"][6u][1u] = "Viewer and processing tasks (not pushes)";
    capa["optional"]["target_mask"]["select"][7u][0u] = 6;
    capa["optional"]["target_mask"]["select"][7u][1u] = "Pushing and processing tasks (not viewers)";
    capa["optional"]["target_mask"]["select"][8u][0u] = 0;
    capa["optional"]["target_mask"]["select"][8u][1u] = "Nothing";
    capa["optional"]["target_mask"]["default"] = "";
    capa["optional"]["target_mask"]["sort"] = "dca";

    capa["optional"]["exit_unmask"]["name"] = "Undo masks on process exit/fail";
    capa["optional"]["exit_unmask"]["help"] = "If/when the process exits or fails, the masks for input tracks will be reset to defaults. (NOT to previous value, but to defaults!)";
    capa["optional"]["exit_unmask"]["default"] = false;
    capa["optional"]["exit_unmask"]["sort"] = "dda";

    capa["optional"]["inconsequential"]["name"] = "Inconsequential process";
    capa["optional"]["inconsequential"]["help"] = "If set, this process need not be running for a stream to be considered fully active.";
    capa["optional"]["inconsequential"]["default"] = false;

    capa["required"]["x-LSP-kind"]["name"] = "Input type"; // human readable name of option
    capa["required"]["x-LSP-kind"]["help"] = "The type of input to use"; // extra information
    capa["required"]["x-LSP-kind"]["type"] = "select";          // type of input field to use
    capa["required"]["x-LSP-kind"]["select"][0u][0u] = "video"; // value of first select field
    capa["required"]["x-LSP-kind"]["select"][0u][1u] = "Video"; // label of first select field
    capa["required"]["x-LSP-kind"]["select"][1u][0u] = "audio";
    capa["required"]["x-LSP-kind"]["select"][1u][1u] = "Audio";
    capa["required"]["x-LSP-kind"]["sort"] = "aaaa"; // sorting index
    capa["required"]["x-LSP-kind"]["influences"].append("codec");
    capa["required"]["x-LSP-kind"]["influences"].append("resolution");
    capa["required"]["x-LSP-kind"]["influences"].append("sources");
    capa["required"]["x-LSP-kind"]["influences"].append("x-LSP-rate_or_crf");
    capa["required"]["x-LSP-kind"]["influences"].append("keys");
    capa["required"]["x-LSP-kind"]["influences"].append("keyfrms");
    capa["required"]["x-LSP-kind"]["influences"].append("keysecs");
    capa["required"]["x-LSP-kind"]["value"] = "video"; // preselect this value

    capa["optional"]["source_track"]["name"] = "Input selection";
    capa["optional"]["source_track"]["help"] =
        "Track ID, codec or language of the source stream to encode.";
    capa["optional"]["source_track"]["type"] = "string";
    capa["optional"]["source_track"]["sort"] = "aaa";
    capa["optional"]["source_track"]["default"] = "automatic";
    capa["optional"]["source_track"]["validate"][0u] = "track_selector_parameter";

    // use an array for this parameter, because there are two input field variations
    capa["required"]["codec"][0u]["name"] = "Target codec";
    capa["required"]["codec"][0u]["help"] = "Which codec to encode to";
    capa["required"]["codec"][0u]["type"] = "select";
    capa["required"]["codec"][0u]["select"][0u] = "H264";
    capa["required"]["codec"][0u]["select"][1u] = "VP9";
    capa["required"]["codec"][0u]["influences"][0u] = "crf";
    capa["required"]["codec"][0u]["sort"] = "aaab";
    capa["required"]["codec"][0u]["dependent"]["x-LSP-kind"] =
        "video"; // this field is only shown if x-LSP-kind is set to "video"

    capa["required"]["codec"][1u]["name"] = "Target codec";
    capa["required"]["codec"][1u]["help"] = "Which codec to encode to";
    capa["required"]["codec"][1u]["type"] = "select";
    capa["required"]["codec"][1u]["select"][0u] = "AAC";
    capa["required"]["codec"][1u]["select"][1u] = "MP3";
    capa["required"]["codec"][1u]["select"][2u][0u] = "opus";
    capa["required"]["codec"][1u]["select"][2u][1u] = "Opus";
    capa["required"]["codec"][1u]["influences"][0u] = "x-LSP-rate_or_crf";
    capa["required"]["codec"][1u]["sort"] = "aaab";
    capa["required"]["codec"][1u]["dependent"]["x-LSP-kind"] = "audio";

    capa["optional"]["sink"]["name"] = "Target stream";
    capa["optional"]["sink"]["help"] =
        "What stream the encoded track should be added to. Defaults to source stream.";
    capa["optional"]["sink"]["placeholder"] = "source stream";
    capa["optional"]["sink"]["type"] = "str";
    capa["optional"]["sink"]["validate"][0u] = "streamname_with_wildcard_and_variables";
    capa["optional"]["sink"]["sort"] = "daa";

    capa["optional"]["resolution"]["name"] = "resolution";
    capa["optional"]["resolution"]["help"] = "Resolution of the output stream, e.g. 1920x1080";
    capa["optional"]["resolution"]["type"] = "str";
    capa["optional"]["resolution"]["default"] = "keep source resolution";
    capa["optional"]["resolution"]["sort"] = "aca";
    capa["optional"]["resolution"]["dependent"]["x-LSP-kind"] = "video";

    capa["optional"]["x-LSP-rate_or_crf"][0u]["name"] = "Quality";
    capa["optional"]["x-LSP-rate_or_crf"][0u]["type"] = "select";
    capa["optional"]["x-LSP-rate_or_crf"][0u]["select"][0u][0u] = "";
    capa["optional"]["x-LSP-rate_or_crf"][0u]["select"][0u][1u] = "automatic";
    capa["optional"]["x-LSP-rate_or_crf"][0u]["select"][1u][0u] = "rate";
    capa["optional"]["x-LSP-rate_or_crf"][0u]["select"][1u][1u] = "Target bitrate";
    capa["optional"]["x-LSP-rate_or_crf"][0u]["select"][2u][0u] = "crf";
    capa["optional"]["x-LSP-rate_or_crf"][0u]["select"][2u][1u] = "Target constant rate factor";
    capa["optional"]["x-LSP-rate_or_crf"][0u]["sort"] = "caa";
    capa["optional"]["x-LSP-rate_or_crf"][0u]["influences"][0u] = "crf";
    capa["optional"]["x-LSP-rate_or_crf"][0u]["influences"][1u] = "rate";
    capa["optional"]["x-LSP-rate_or_crf"][0u]["dependent"]["x-LSP-kind"] = "video";

    capa["optional"]["x-LSP-rate_or_crf"][1u]["name"] = "Quality";
    capa["optional"]["x-LSP-rate_or_crf"][1u]["type"] = "select";
    capa["optional"]["x-LSP-rate_or_crf"][1u]["select"][0u][0u] = "";
    capa["optional"]["x-LSP-rate_or_crf"][1u]["select"][0u][1u] = "automatic";
    capa["optional"]["x-LSP-rate_or_crf"][1u]["select"][1u][0u] = "rate";
    capa["optional"]["x-LSP-rate_or_crf"][1u]["select"][1u][1u] = "Target bitrate";
    capa["optional"]["x-LSP-rate_or_crf"][0u]["sort"] = "caa";
    capa["optional"]["x-LSP-rate_or_crf"][1u]["influences"][0u] = "rate";
    capa["optional"]["x-LSP-rate_or_crf"][1u]["dependent"]["x-LSP-kind"] = "audio";

    capa["optional"]["crf"][0u]["help"] = "Video quality, ranging from 0 (best) to 51 (worst). This value automatically scales with resolution. Around 17 is 'visually lossless', and we find 25 to be a reasonable trade off between quality and bit rate but your mileage may vary.";
    capa["optional"]["crf"][0u]["min"] = "0";
    capa["optional"]["crf"][0u]["max"] = "51";
    capa["optional"]["crf"][0u]["type"] = "int";
    capa["optional"]["crf"][0u]["dependent"]["codec"] = "H264";
    capa["optional"]["crf"][0u]["dependent"]["x-LSP-rate_or_crf"] = "crf";
    capa["optional"]["crf"][0u]["sort"] = "cba";

    capa["optional"]["crf"][1u]["help"] = "Video quality, ranging from 0 (best) to 63 (worst). Higher resolution requires a better quality to match, and HD (720p/1080p) generally looks good around 31, but your mileage may vary.";
    capa["optional"]["crf"][1u]["min"] = "0";
    capa["optional"]["crf"][1u]["max"] = "63";
    capa["optional"]["crf"][1u]["type"] = "int";
    capa["optional"]["crf"][1u]["dependent"]["codec"] = "VP9";
    capa["optional"]["crf"][1u]["dependent"]["x-LSP-rate_or_crf"] = "crf";
    capa["optional"]["crf"][1u]["sort"] = "cba";

    capa["optional"]["rate"]["name"] = "Bitrate";
    capa["optional"]["rate"]["help"] = "Bitrate of the encoding";
    capa["optional"]["rate"]["type"] = "str";
    capa["optional"]["rate"]["dependent"]["x-LSP-rate_or_crf"] = "rate";
    capa["optional"]["rate"]["sort"] = "cba";

    capa["optional"]["min_rate"]["name"] = "Minimum bitrate";
    capa["optional"]["min_rate"]["help"] = "Minimum bitrate of the encoding";
    capa["optional"]["min_rate"]["type"] = "str";
    capa["optional"]["min_rate"]["dependent"]["x-LSP-rate_or_crf"] = "rate";
    capa["optional"]["min_rate"]["sort"] = "cbb";

    capa["optional"]["max_rate"]["name"] = "Maximum bitrate";
    capa["optional"]["max_rate"]["help"] = "Maximum bitrate of the encoding";
    capa["optional"]["max_rate"]["type"] = "str";
    capa["optional"]["max_rate"]["dependent"]["x-LSP-rate_or_crf"] = "rate";
    capa["optional"]["max_rate"]["sort"] = "cbc";

    capa["optional"]["profile"]["name"] = "Transcode profile";
    capa["optional"]["profile"]["help"] = "Limits the output to a specific H.264 profile";
    capa["optional"]["profile"]["type"] = "select";
    capa["optional"]["profile"]["select"][0u][0u] = "";
    capa["optional"]["profile"]["select"][0u][1u] = "automatic";
    capa["optional"]["profile"]["select"][1u][0u] = "baseline";
    capa["optional"]["profile"]["select"][1u][1u] = "baseline";
    capa["optional"]["profile"]["select"][2u][0u] = "main";
    capa["optional"]["profile"]["select"][2u][1u] = "main";
    capa["optional"]["profile"]["select"][3u][0u] = "high";
    capa["optional"]["profile"]["select"][3u][1u] = "high";
    capa["optional"]["profile"]["select"][4u][0u] = "high10";
    capa["optional"]["profile"]["select"][4u][1u] = "high10";
    capa["optional"]["profile"]["select"][5u][0u] = "high422";
    capa["optional"]["profile"]["select"][5u][1u] = "high422";
    capa["optional"]["profile"]["select"][6u][0u] = "high444";
    capa["optional"]["profile"]["select"][6u][1u] = "high444";
    capa["optional"]["profile"]["default"] = "";
    capa["optional"]["profile"]["sort"] = "cca";

    capa["optional"]["preset"]["name"] = "Transcode preset";
    capa["optional"]["preset"]["help"] = "Preset for encoding speed and compression ratio";
    capa["optional"]["preset"]["type"] = "select";
    capa["optional"]["preset"]["select"][0u][0u] = "ultrafast";
    capa["optional"]["preset"]["select"][0u][1u] = "ultrafast";
    capa["optional"]["preset"]["select"][1u][0u] = "superfast";
    capa["optional"]["preset"]["select"][1u][1u] = "superfast";
    capa["optional"]["preset"]["select"][2u][0u] = "veryfast";
    capa["optional"]["preset"]["select"][2u][1u] = "veryfast";
    capa["optional"]["preset"]["select"][3u][0u] = "faster";
    capa["optional"]["preset"]["select"][3u][1u] = "faster";
    capa["optional"]["preset"]["select"][4u][0u] = "fast";
    capa["optional"]["preset"]["select"][4u][1u] = "fast";
    capa["optional"]["preset"]["select"][5u][0u] = "medium";
    capa["optional"]["preset"]["select"][5u][1u] = "medium";
    capa["optional"]["preset"]["select"][6u][0u] = "slow";
    capa["optional"]["preset"]["select"][6u][1u] = "slow";
    capa["optional"]["preset"]["select"][7u][0u] = "slower";
    capa["optional"]["preset"]["select"][7u][1u] = "slower";
    capa["optional"]["preset"]["select"][8u][0u] = "veryslow";
    capa["optional"]["preset"]["select"][8u][1u] = "veryslow";
    capa["optional"]["preset"]["default"] = "medium";
    capa["optional"]["preset"]["sort"] = "ccb";

    capa["optional"]["keys"]["name"] = "Keyframes";
    capa["optional"]["keys"]["help"] = "What to do with keyframes";
    capa["optional"]["keys"]["type"] = "select";
    capa["optional"]["keys"]["select"][0u][0u] = "";
    capa["optional"]["keys"]["select"][0u][1u] = "Match input keyframes";
    capa["optional"]["keys"]["select"][1u][0u] = "frames";
    capa["optional"]["keys"]["select"][1u][1u] = "Every X frames";
    capa["optional"]["keys"]["select"][2u][0u] = "secs";
    capa["optional"]["keys"]["select"][2u][1u] = "Every X seconds";
    capa["optional"]["keys"]["default"] = "";
    capa["optional"]["keys"]["sort"] = "cda";
    capa["optional"]["keys"]["influences"][0u] = "keyfrms";
    capa["optional"]["keys"]["influences"][0u] = "keysecs";
    capa["optional"]["keys"]["dependent"]["X-LSP-kind"] = "video";

    capa["optional"]["keyfrms"]["name"] = "Key interval";
    capa["optional"]["keyfrms"]["type"] = "int";
    capa["optional"]["keyfrms"]["help"] = "Key interval in frames";
    capa["optional"]["keyfrms"]["unit"] = "frames";
    capa["optional"]["keyfrms"]["dependent"]["X-LSP-kind"] = "video";
    capa["optional"]["keyfrms"]["dependent"]["keys"] = "frames";
    capa["optional"]["keyfrms"]["sort"] = "cdb";

    capa["optional"]["keysecs"]["name"] = "Key interval";
    capa["optional"]["keysecs"]["type"] = "float";
    capa["optional"]["keysecs"]["help"] = "Key interval in seconds";
    capa["optional"]["keysecs"]["unit"] = "seconds";
    capa["optional"]["keysecs"]["dependent"]["X-LSP-kind"] = "video";
    capa["optional"]["keysecs"]["dependent"]["keys"] = "secs";
    capa["optional"]["keysecs"]["sort"] = "cdb";

    capa["optional"]["flags"]["name"] = "Flags";
    capa["optional"]["flags"]["help"] = "Extra flags to add to the end of the transcode command";
    capa["optional"]["flags"]["type"] = "str";
    capa["optional"]["flags"]["sort"] = "cea";

    capa["optional"]["sources"]["name"] = "Layers";
    capa["optional"]["sources"]["type"] = "sublist";
    capa["optional"]["sources"]["itemLabel"] = "layer";
    capa["optional"]["sources"]["help"] =
        "List of sources to overlay on top of each other, in order. If left empty, simply uses the "
        "input track without modifications and nothing else.";
    capa["optional"]["sources"]["sort"] = "baa";
    capa["optional"]["sources"]["dependent"]["x-LSP-kind"] = "video";

    capa["codecs"][0u][0u].append("H264");
    capa["codecs"][0u][0u].append("HEVC");
    capa["codecs"][0u][0u].append("VP8");
    capa["codecs"][0u][0u].append("VP9");
    capa["codecs"][0u][0u].append("theora");
    capa["codecs"][0u][0u].append("MPEG2");
    capa["codecs"][0u][0u].append("AV1");
    capa["codecs"][0u][0u].append("YUYV");
    capa["codecs"][0u][0u].append("UYVY");
    capa["codecs"][0u][0u].append("JPEG");
    capa["codecs"][0u][1u].append("AAC");
    capa["codecs"][0u][1u].append("vorbis");
    capa["codecs"][0u][1u].append("opus");
    capa["codecs"][0u][1u].append("PCM");
    capa["codecs"][0u][1u].append("ALAW");
    capa["codecs"][0u][1u].append("ULAW");
    capa["codecs"][0u][1u].append("MP2");
    capa["codecs"][0u][1u].append("MP3");
    capa["codecs"][0u][1u].append("FLOAT");
    capa["codecs"][0u][1u].append("AC3");
    capa["codecs"][0u][1u].append("DTS");
    capa["codecs"][0u][2u].append("+JSON");

    JSON::Value &grp = capa["optional"]["sources"]["optional"];
    grp["src"]["name"] = "Source";
    grp["src"]["help"] =
        "Source image/video file or URL to overlay. Leave empty to apply original source.";
    grp["src"]["type"] = "str";
    grp["src"]["default"] = "-";
    grp["src"]["n"] = 0;
    grp["anchor"]["name"] = "Origin corner";
    grp["anchor"]["help"] =
        "What corner to use as origin for the X/Y coordinate system for placement.";
    grp["anchor"]["type"] = "select";
    grp["anchor"]["default"] = "topleft";
    grp["anchor"]["select"].append("topleft");
    grp["anchor"]["select"].append("topright");
    grp["anchor"]["select"].append("bottomleft");
    grp["anchor"]["select"].append("bottomright");
    grp["anchor"]["select"].append("center");
    grp["anchor"]["n"] = 3;
    grp["x"]["name"] = "X position";
    grp["x"]["help"] = "Horizontal distance of this layer to the origin corner.";
    grp["x"]["unit"] = "pixels";
    grp["x"]["type"] = "int";
    grp["x"]["default"] = 0;
    grp["x"]["n"] = 4;
    grp["y"]["name"] = "Y position";
    grp["y"]["help"] = "Vertical distance of this layer to the origin corner.";
    grp["y"]["unit"] = "pixels";
    grp["y"]["type"] = "int";
    grp["y"]["default"] = 0;
    grp["y"]["n"] = 5;
    grp["width"]["name"] = "Width";
    grp["width"]["help"] =
        "Width to scale the layer to, use -1 to keep aspect ratio if only height is known.";
    grp["width"]["unit"] = "pixels";
    grp["width"]["type"] = "int";
    grp["width"]["default"] = "original width";
    grp["width"]["n"] = 1;
    grp["height"]["name"] = "Height";
    grp["height"]["help"] =
        "Height to scale the layer to, use -1 to keep aspect ratio if only width is known.";
    grp["height"]["unit"] = "pixels";
    grp["height"]["type"] = "int";
    grp["height"]["default"] = "original height";
    grp["height"]["n"] = 2;

    std::cout << capa.toString() << std::endl;
    return -1;
  }

  Util::redirectLogsIfNeeded();

  // read configuration
  if (config.getString("configuration") != "-"){
    opt = JSON::fromString(config.getString("configuration"));
  }else{
    std::string json, line;
    INFO_MSG("Reading configuration from standard input");
    while (std::getline(std::cin, line)){json.append(line);}
    opt = JSON::fromString(json.c_str());
  }

  Enc.SetConfig(opt);
  if (!Enc.CheckConfig()){
    FAIL_MSG("Error config syntax error!");
    return 1;
  }


  const std::string & srcStrm = opt["source"].asStringRef();
  //connect to source metadata
  DTSC::Meta M(srcStrm, false);

  //find source video track
  std::map<std::string, std::string> targetParams;
  targetParams["video"] = "maxbps";
  JSON::Value sourceCapa;
  sourceCapa["name"] = "FFMPEG";
  sourceCapa["codecs"][0u][0u].append("H264");
  sourceCapa["codecs"][0u][0u].append("HEVC");
  sourceCapa["codecs"][0u][0u].append("VP8");
  sourceCapa["codecs"][0u][0u].append("VP9");
  sourceCapa["codecs"][0u][0u].append("theora");
  sourceCapa["codecs"][0u][0u].append("MPEG2");
  sourceCapa["codecs"][0u][0u].append("AV1");
  sourceCapa["codecs"][0u][0u].append("JPEG");
  sourceCapa["codecs"][0u][0u].append("YUYV");
  sourceCapa["codecs"][0u][0u].append("UYVY");
  sourceCapa["codecs"][0u][0u].append("NV12");
  sourceCapa["codecs"][0u][1u].append("AAC");
  sourceCapa["codecs"][0u][1u].append("FLAC");
  sourceCapa["codecs"][0u][1u].append("vorbis");
  sourceCapa["codecs"][0u][1u].append("opus");
  sourceCapa["codecs"][0u][1u].append("PCM");
  sourceCapa["codecs"][0u][1u].append("ALAW");
  sourceCapa["codecs"][0u][1u].append("ULAW");
  sourceCapa["codecs"][0u][1u].append("MP2");
  sourceCapa["codecs"][0u][1u].append("MP3");
  sourceCapa["codecs"][0u][1u].append("FLOAT");
  sourceCapa["codecs"][0u][1u].append("AC3");
  sourceCapa["codecs"][0u][1u].append("DTS");
  if (Enc.isVideo){
    if (opt.isMember("source_track") && opt["source_track"].isString() && opt["source_track"]){
      targetParams["video"] = opt["source_track"].asStringRef();
    }else{
      targetParams["video"] = "";
    }
  }else{
    targetParams["video"] = "none";
  }
  if (Enc.isAudio){
    if (opt.isMember("source_track") && opt["source_track"].isString() && opt["source_track"]){
      targetParams["audio"] = opt["source_track"].asStringRef();
    }else{
      targetParams["audio"] = "";
    }
  }else{
    targetParams["audio"] = "none";
  }
  size_t sourceIdx = INVALID_TRACK_ID;
  size_t sleeps = 0;
  while (++sleeps < 60 && (sourceIdx == INVALID_TRACK_ID || (Enc.isVideo && (!M.getWidth(sourceIdx) || !M.getHeight(sourceIdx))))){
    M.reloadReplacedPagesIfNeeded();
    std::set<size_t> vidTrack = Util::wouldSelect(M, targetParams, sourceCapa);
    sourceIdx = vidTrack.size() ? (*(vidTrack.begin())) : INVALID_TRACK_ID;
    if (sourceIdx == INVALID_TRACK_ID || (Enc.isVideo && (!M.getWidth(sourceIdx) || !M.getHeight(sourceIdx)))){
      Util::sleep(250);
    }
  }
  if (sourceIdx == INVALID_TRACK_ID || (Enc.isVideo && (!M.getWidth(sourceIdx) || !M.getHeight(sourceIdx)))){
    FAIL_MSG("No valid source track!");
    return 1;
  }
  if (Enc.isVideo){
    Enc.setResolution(M.getWidth(sourceIdx), M.getHeight(sourceIdx));
  }

  // create pipe pair before thread
  if (pipe(pipein) || pipe(pipeout)){
    FAIL_MSG("Could not create pipes for process!");
    return 1;
  }
  Util::Procs::socketList.insert(pipeout[0]);
  Util::Procs::socketList.insert(pipeout[1]);
  Util::Procs::socketList.insert(pipein[0]);
  Util::Procs::socketList.insert(pipein[1]);

  // stream which connects to input
  tthread::thread source(sourceThread, 0);

  // needs to pass through encoder to outputEBML
  tthread::thread sink(sinkThread, 0);

  co.is_active = true;

  // run ffmpeg
  Enc.Run();

  MEDIUM_MSG("closing encoding");

  co.is_active = false;
  conf.is_active = false;

  // close pipes
  close(pipein[0]);
  close(pipeout[0]);
  close(pipein[1]);
  close(pipeout[1]);

  sink.join();
  HIGH_MSG("sink thread joined")

  source.join();
  HIGH_MSG("source thread joined");

  return 0;
}

namespace Mist{

  void OutENC::SetConfig(JSON::Value &config){opt = config;}

  bool OutENC::checkAudioConfig(){
    MEDIUM_MSG("no audio configs to check yet!");

    if (opt.isMember("sample_rate") && opt["sample_rate"].isInt()){
      sample_rate = opt["sample_rate"].asInt();
    }

    return true;
  }

  OutENC::OutENC(){
    ffcmd[10239] = 0;
    isAudio = false;
    isVideo = false;
    crf = -1;
    sample_rate = 0;
    supportedVideoCodecs.insert("H264");
    supportedVideoCodecs.insert("H265");
    supportedVideoCodecs.insert("VP9");
    supportedAudioCodecs.insert("AAC");
    supportedAudioCodecs.insert("opus");
    supportedAudioCodecs.insert("MP3");
  }

  bool OutENC::buildAudioCommand(){
    std::string samplerate;
    if (sample_rate){samplerate = "-ar " + JSON::Value(sample_rate).asString();}
    snprintf(ffcmd, 10240,
             "ffmpeg -fflags nobuffer -hide_banner -loglevel warning -i - -acodec %s %s -strict -2 -ac 2 %s %s -f "
             "matroska -live 1 -cluster_time_limit 100 - ",
             codec.c_str(), samplerate.c_str(), getBitrateSetting().c_str(), flags.c_str());

    return true;
  }

  bool OutENC::buildVideoCommand(){
    if (!res_x){
      FAIL_MSG("Resolution is not set!");
      return false;
    }

    MEDIUM_MSG("source resolution: %dx%d", res_x, res_y);

    // Init variables used to construct the FFMPEG command
    char in[255] = "";
    char ov[255] = "";
    std::string s_base = "ffmpeg -fflags nobuffer -probesize 32 -max_probe_packets 1 -hide_banner -loglevel warning"; //< Base FFMPEG command
    std::string s_input = ""; //< Inputs of the filter graph
    std::string s_filter = ""; //< Filter graph to use
    std::string s_scale = ""; //< Scaling inputs of the filter graph
    std::string s_overlay = ""; //< Positioning inputs of the filter graph
    std::string options = ""; //< Transcode params

    // Init filter graph
    bool requiresOverlay = opt["sources"].size() > 1;
    if (requiresOverlay){
      // Complex filter graph: overlay each source over a black background
      s_base.append(" -f lavfi");
      s_filter = " -filter_complex ";
      char in[50] = "";
      sprintf(in, " -i color=c=black:s=%dx%d", res_x, res_y);
      s_input = in;
    }else{
      // Simple filter graph
      s_filter = " -vf ";
    }

    // Add sources to input, scaling and positioning strings
    for (JSON::Iter it(opt["sources"]); it; ++it){
      // Add source to input string
      if ((*it).isMember("src") && (*it)["src"].isString() && (*it)["src"].asString().size() > 3){
        std::string src = (*it)["src"].asString();
        std::string ext = src.substr(src.length() - 3);
        if (ext == "gif"){// for animated gif files, prepend extra parameter
          sprintf(in, " -ignore_loop 0 -i %s", src.c_str());
        }else{
          sprintf(in, " -i %s", src.c_str());
        }
        MEDIUM_MSG("Loading Input: %s", src.c_str());
      }else{
        sprintf(in, " -i %s", "-");
        INFO_MSG("no src given, assume reading data from stdin");
        MEDIUM_MSG("Loading Input: -");
      }
      s_input += in;

      // No complex scaling and positioning required if there's only one source
      if(!requiresOverlay){ continue; }

      // Init scaling and positioning params
      uint32_t i_width = -1;
      uint32_t i_height = -1;
      int32_t i_x = 0;
      int32_t i_y = 0;
      std::string i_anchor = "topleft";
      if ((*it).isMember("width") && (*it)["width"].asInt()){i_width = (*it)["width"].asInt();}
      if ((*it).isMember("height") && (*it)["height"].asInt()){i_height = (*it)["height"].asInt();}
      if ((*it).isMember("x")){i_x = (*it)["x"].asInt();}
      if ((*it).isMember("y")){i_y = (*it)["y"].asInt();}
      if ((*it).isMember("anchor") && (*it)["anchor"].isString()){
        i_anchor = (*it)["anchor"].asString();
      }

      // Scale input
      char scale[200];
      sprintf(scale, ";[%d:v]scale=%d:%d[s%d]", it.num() + 1, i_width, i_height, it.num());
      s_scale.append(scale);

      // Position input
      char in_chain[16];
      if (it.num() == 0){
        sprintf(in_chain, ";[0:v][s%d]", it.num());
      }else{
        sprintf(in_chain, ";[out][s%d]", it.num());
      }
      if ((*it)["anchor"] == "topright"){
        sprintf(ov, "overlay=W-w-%d:%d[out]", i_x, i_y);
      }else if ((*it)["anchor"] == "bottomleft"){
        sprintf(ov, "overlay=%d:H-h-%d[out]", i_x, i_y);
      }else if ((*it)["anchor"] == "bottomright"){
        sprintf(ov, "overlay=W-w-%d:H-h-%d[out]", i_x, i_y);
      }else if ((*it)["anchor"] == "center"){
        sprintf(ov, "overlay=(W-w)/2:(H-h)/2[out]");
      }else{// topleft default
        sprintf(ov, "overlay=%d:%d[out]", i_x, i_y);
      }
      s_overlay.append(in_chain);
      s_overlay.append(ov);
    }

    // Finish filter graph
    if (requiresOverlay){
      s_scale = s_scale.substr(1); //< Remove `;` char at the start
      sprintf(ov, ";[out]scale=%d:%d,setsar=1:1[out] -map [out]", res_x, res_y);
      s_overlay.append(ov);
    }else{
      sprintf(ov, "scale=%d:%d,setsar=1:1", res_x, res_y);
      s_scale.append(ov);
    }

    // Set transcode parameters
    options = codec;
    if (!profile.empty()){options.append(" -profile:v " + profile);}
    if (!preset.empty()){options.append(" -preset " + preset);}
    std::string bitrateSettings = getBitrateSetting();
    if (!bitrateSettings.empty()){options.append(" " + bitrateSettings);}
    if (!flags.empty()){options.append(" " + flags);}
    if (!opt.isMember("keys") || !opt["keys"].asStringRef().size()){
      options += " -force_key_frames source";
    }else if (opt["keys"].asStringRef() == "frames"){
      options += " -g ";
      options += opt["keyfrms"].asString();
    }else if (opt["keys"].asStringRef() == "secs"){
      options += " -force_key_frames expr:gte(t,n_forced*";
      options += opt["keysecs"].asString();
      options += ")";
    }else{
      options += " -force_key_frames source";
    }

    // Construct final command
    snprintf(ffcmd, 10240, "%s%s%s%s%s -c:v %s -an -f matroska - ",
             s_base.c_str(), s_input.c_str(), s_filter.c_str(), s_scale.c_str(), s_overlay.c_str(), options.c_str());
    INFO_MSG("Constructed FFMPEG video command: %s", ffcmd);
    return true;
  }

  void OutENC::setCodec(std::string data){
    codec = data;
    transform(codec.begin(), codec.end(), codec.begin(), (int (*)(int))tolower);
  }

  void OutENC::setBitrate(std::string rate, std::string min, std::string max){
    bitrate = rate;
    min_bitrate = min;
    max_bitrate = max;
  }

  std::string OutENC::getBitrateSetting(){
    std::string setting;

    if (!bitrate.empty()){
      // setting = "-b:v " + bitrate;
      setting = bitrate;
    }

    if (!min_bitrate.empty()){setting.append(" -minrate " + min_bitrate);}

    if (!max_bitrate.empty()){setting.append(" -maxrate " + max_bitrate);}

    if (!setting.empty()){
      if (isVideo){setting = "-b:v " + setting;}

      if (isAudio){setting = "-b:a " + setting;}
    }

    if (isVideo){
      if (crf > -1){
        // use crf value instead of bitrate
        setting = "-crf " + JSON::Value(crf).asString();
      }else{
        // use bitrate value set above
      }
    }

    return setting;
  }

  void OutENC::setCRF(int c){
    if (codec == "h264"){
      if (c < 0 || c > 51){
        WARN_MSG("Incorrect CRF value: %d. Need to be in range [0-51] for codec: %s. Ignoring "
                 "wrong value.",
                 c, codec.c_str());
      }else{
        crf = c;
      }
    }else if (codec == "vp9"){

      if (c < 0 || c > 63){
        WARN_MSG("Incorrect CRF value: %d. Need to be in range [0-63] for codec: %s. Ignoring "
                 "wrong value.",
                 c, codec.c_str());
      }else{
        crf = c;
      }
    }else{
      WARN_MSG("Cannot set CRF: %d for incompatible codec: %s", c, codec.c_str());
    }
  }

  bool OutENC::checkVideoConfig(){
    bool stdinSource = false;
    if (opt.isMember("resolution") && opt["resolution"]){
      if (opt["resolution"].asString().find("x") == std::string::npos){
        FAIL_MSG("Resolution: '%s' not supported!", opt["resolution"].c_str());
        return false;
      }

      res_x = strtol(opt["resolution"].asString().substr(0, opt["resolution"].asString().find("x")).c_str(),
                     NULL, 0);
      res_y = strtol(opt["resolution"].asString().substr(opt["resolution"].asString().find("x") + 1).c_str(),
                     NULL, 0);
    }else{
      INFO_MSG("No resolution set. Grabbing resolution from source stream...");
    }

    if (opt.isMember("profile") && opt["profile"].isString()){
      profile = opt["profile"].asString();
    }

    if (opt.isMember("preset") && opt["preset"].isString()){
      preset = opt["preset"].asString();
    }

    if (opt.isMember("crf") && opt["crf"].isInt()){setCRF(opt["crf"].asInt());}

    if (opt["sources"].isArray()){
      for (JSON::Iter it(opt["sources"]); it; ++it){
        if ((*it).isMember("src") && (*it)["src"].isString()){
          if ((*it)["src"].asString() == "-"){
            stdinSource = true;
            break;
          }
        }else{
          stdinSource = true;
          break;
        }
      }
    }else{
      // sources array missing, create empty object in array
      opt["sources"][0u]["src"] = "-";
      if (opt.isMember("resolution")){
        opt["sources"][0u]["width"] = -1;
        opt["sources"][0u]["height"] = res_y;
        opt["sources"][0u]["anchor"] = "center";
      }
      INFO_MSG("Default source: input stream at preserved-aspect same height");
      stdinSource = true;
    }

    if (!stdinSource){
      // no stdin source item found in sources configuration, add source object at the beginning
      JSON::Value nOpt;
      nOpt["src"] = "-";
      if (opt.isMember("resolution")){
        nOpt["width"] = -1;
        nOpt["height"] = res_y;
        nOpt["anchor"] = "center";
      }
      opt["sources"].prepend(nOpt);
      WARN_MSG("Source is not used: adding source stream at preserved-aspect same height");
    }

    return true;
  }

  /// check source, sink, source_track, codec, bitrate, flags  and process options.
  bool OutENC::CheckConfig(){
    // Check generic configuration variables
    if (!opt.isMember("source") || !opt["source"] || !opt["source"].isString()){
      FAIL_MSG("invalid source in config!");
      return false;
    }

    if (!opt.isMember("sink") || !opt["sink"] || !opt["sink"].isString()){
      INFO_MSG("No sink explicitly set, using source as sink");
    }

    if (supportedVideoCodecs.count(opt["codec"].asString())){isVideo = true;}
    if (supportedAudioCodecs.count(opt["codec"].asString())){isAudio = true;}
    if (!isVideo && !isAudio){
      FAIL_MSG("Codec: '%s' not supported!", opt["codec"].c_str());
      return false;
    }

    setCodec(opt["codec"]);

    std::string b_rate;
    std::string min_rate;
    std::string max_rate;

    if (opt.isMember("rate") && opt["rate"].isString()){
      b_rate = opt["rate"].asString();
    }

    if (opt.isMember("min_rate") && opt["min_rate"].isString()){
      min_rate = opt["min_rate"].asString();
    }

    if (opt.isMember("max_rate") && opt["max_rate"].isString()){
      max_rate = opt["max_rate"].asString();
    }

    setBitrate(b_rate, min_rate, max_rate);

    // extra ffmpeg flags
    if (opt.isMember("flags") && opt["flags"].isString()){flags = opt["flags"].asString();}

    // Check configuration and construct ffmpeg command based on audio or video encoding
    if (isVideo){
      return checkVideoConfig();
    }else if (isAudio){
      return checkAudioConfig();
    }

    return false;
  }

  /// prepare ffmpeg command by splitting the arguments before running
  void OutENC::prepareCommand(){
    // ffmpeg command
    MEDIUM_MSG("ffmpeg command: %s", ffcmd);
    uint8_t argCnt = 0;
    char *startCh = 0;
    for (char *i = ffcmd; i - ffcmd < 10240; ++i){
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
  }

  void OutENC::setResolution(uint32_t x, uint32_t y){
    res_x = x;
    res_y = y;
  }

  void OutENC::Run(){
    int ffer = 2;
    pid_t ffout = -1;

    if (isVideo){
      if (!buildVideoCommand()){
        FAIL_MSG("Video encode command failed");
        Util::sleep(1000); // this delay prevents coredump...
        return;
      }
    }

    if (isAudio){
      if (!buildAudioCommand()){
        FAIL_MSG("Audio encode command failed");
        Util::sleep(1000); // this delay prevents coredump...
        return;
      }
    }

    prepareCommand();
    ffout = Util::Procs::StartPiped(args, &pipein[0], &pipeout[1], &ffer);

    uint64_t lastProcUpdate = Util::bootSecs();
    {
      tthread::lock_guard<tthread::mutex> guard(statsMutex);
      pStat["proc_status_update"]["id"] = getpid();
      pStat["proc_status_update"]["proc"] = "FFMPEG";
      pData["ainfo"]["child_pid"] = ffout;
      //pData["ainfo"]["cmd"] = opt["exec"];
    }
    uint64_t startTime = Util::bootSecs();
    while (conf.is_active && Util::Procs::isRunning(ffout)){
      Util::sleep(200);
      if (lastProcUpdate + 5 <= Util::bootSecs()){
        tthread::lock_guard<tthread::mutex> guard(statsMutex);
        pData["active_seconds"] = (Util::bootSecs() - startTime);
        pData["ainfo"]["sourceTime"] = statSourceMs;
        pData["ainfo"]["sinkTime"] = statSinkMs;
        Util::sendUDPApi(pStat);
        lastProcUpdate = Util::bootSecs();
      }
    }

    while (Util::Procs::isRunning(ffout)){
      INFO_MSG("Stopping process...");
      Util::Procs::StopAll();
      Util::sleep(200);
    }

    MEDIUM_MSG("ffmpeg process stopped.");
  }
}// namespace Mist
