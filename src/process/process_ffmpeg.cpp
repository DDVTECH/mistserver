#include "process_ffmpeg.h"
#include <algorithm> //for std::find
#include <fstream>
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

void sinkThread(void *){
  Mist::EncodeInputEBML in(&co);
  co.getOption("output", true).append("-");
  co.activate();

  MEDIUM_MSG("Running sink thread...");

  in.setInFile(pipeout[0]);
  co.is_active = true;
  in.run();

  conf.is_active = false;
}

void sourceThread(void *){
  Mist::EncodeOutputEBML::init(&conf);
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
  Mist::EncodeOutputEBML out(c);

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
    capa["sort"] = "n"; // sort the parameters by this key

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

    capa["optional"]["target_mask"]["name"] = "Output track mask";
    capa["optional"]["target_mask"]["help"] = "What internal processes should have access to the ouput track(s)";
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

    capa["optional"]["exit_unmask"]["name"] = "Undo masks on process exit/fail";
    capa["optional"]["exit_unmask"]["help"] = "If/when the process exits or fails, the masks for input tracks will be reset to defaults. (NOT to previous value, but to defaults!)";
    capa["optional"]["exit_unmask"]["default"] = false;

    capa["required"]["x-LSP-kind"]["name"] = "Input type"; // human readable name of option
    capa["required"]["x-LSP-kind"]["help"] = "The type of input to use"; // extra information
    capa["required"]["x-LSP-kind"]["type"] = "select";          // type of input field to use
    capa["required"]["x-LSP-kind"]["select"][0u][0u] = "video"; // value of first select field
    capa["required"]["x-LSP-kind"]["select"][0u][1u] = "Video"; // label of first select field
    capa["required"]["x-LSP-kind"]["select"][1u][0u] = "audio";
    capa["required"]["x-LSP-kind"]["select"][1u][1u] = "Audio";
    capa["required"]["x-LSP-kind"]["n"] = 0; // sorting index
    capa["required"]["x-LSP-kind"]["influences"][0u] =
        "codec"; // changing this parameter influences the parameters listed here
    capa["required"]["x-LSP-kind"]["influences"][1u] = "resolution";
    capa["required"]["x-LSP-kind"]["influences"][2u] = "sources";
    capa["required"]["x-LSP-kind"]["influences"][3u] = "x-LSP-rate_or_crf";
    capa["required"]["x-LSP-kind"]["value"] = "video"; // preselect this value

    capa["optional"]["source_track"]["name"] = "Input selection";
    capa["optional"]["source_track"]["help"] =
        "Track ID, codec or language of the source stream to encode.";
    capa["optional"]["source_track"]["type"] = "string";
    capa["optional"]["source_track"]["n"] = 1;
    capa["optional"]["source_track"]["default"] = "automatic";
    capa["optional"]["source_track"]["validate"][0u] = "track_selector_parameter";

    // use an array for this parameter, because there are two input field variations
    capa["required"]["codec"][0u]["name"] = "Target codec";
    capa["required"]["codec"][0u]["help"] = "Which codec to encode to";
    capa["required"]["codec"][0u]["type"] = "select";
    capa["required"]["codec"][0u]["select"][0u] = "H264";
    capa["required"]["codec"][0u]["select"][1u] = "VP9";
    capa["required"]["codec"][0u]["influences"][0u] = "crf";
    capa["required"]["codec"][0u]["n"] = 2;
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
    capa["required"]["codec"][1u]["n"] = 2;
    capa["required"]["codec"][1u]["dependent"]["x-LSP-kind"] = "audio";

    capa["optional"]["sink"]["name"] = "Target stream";
    capa["optional"]["sink"]["help"] =
        "What stream the encoded track should be added to. Defaults to source stream.";
    capa["optional"]["sink"]["placeholder"] = "source stream";
    capa["optional"]["sink"]["type"] = "str";
    capa["optional"]["sink"]["validate"][0u] = "streamname_with_wildcard_and_variables";
    capa["optional"]["sink"]["n"] = 3;

    capa["optional"]["resolution"]["name"] = "resolution";
    capa["optional"]["resolution"]["help"] = "Resolution of the output stream";
    capa["optional"]["resolution"]["type"] = "str";
    capa["optional"]["resolution"]["n"] = 4;
    capa["optional"]["resolution"]["dependent"]["x-LSP-kind"] = "video";

    capa["optional"]["x-LSP-rate_or_crf"][0u]["name"] = "Quality";
    capa["optional"]["x-LSP-rate_or_crf"][0u]["type"] = "select";
    capa["optional"]["x-LSP-rate_or_crf"][0u]["select"][0u][0u] = "";
    capa["optional"]["x-LSP-rate_or_crf"][0u]["select"][0u][1u] = "automatic";
    capa["optional"]["x-LSP-rate_or_crf"][0u]["select"][1u][0u] = "rate";
    capa["optional"]["x-LSP-rate_or_crf"][0u]["select"][1u][1u] = "Target bitrate";
    capa["optional"]["x-LSP-rate_or_crf"][0u]["select"][2u][0u] = "crf";
    capa["optional"]["x-LSP-rate_or_crf"][0u]["select"][2u][1u] = "Target constant rate factor";
    capa["optional"]["x-LSP-rate_or_crf"][0u]["n"] = 5;
    capa["optional"]["x-LSP-rate_or_crf"][0u]["influences"][0u] = "crf";
    capa["optional"]["x-LSP-rate_or_crf"][0u]["influences"][1u] = "rate";
    capa["optional"]["x-LSP-rate_or_crf"][0u]["dependent"]["x-LSP-kind"] = "video";

    capa["optional"]["x-LSP-rate_or_crf"][1u]["name"] = "Quality";
    capa["optional"]["x-LSP-rate_or_crf"][1u]["type"] = "select";
    capa["optional"]["x-LSP-rate_or_crf"][1u]["select"][0u][0u] = "";
    capa["optional"]["x-LSP-rate_or_crf"][1u]["select"][0u][1u] = "automatic";
    capa["optional"]["x-LSP-rate_or_crf"][1u]["select"][1u][0u] = "rate";
    capa["optional"]["x-LSP-rate_or_crf"][1u]["select"][1u][1u] = "Target bitrate";
    capa["optional"]["x-LSP-rate_or_crf"][1u]["n"] = 5;
    capa["optional"]["x-LSP-rate_or_crf"][1u]["influences"][0u] = "rate";
    capa["optional"]["x-LSP-rate_or_crf"][1u]["dependent"]["x-LSP-kind"] = "audio";

    capa["optional"]["crf"][0u]["help"] = "Video quality";
    capa["optional"]["crf"][0u]["min"] = "0";
    capa["optional"]["crf"][0u]["max"] = "51";
    capa["optional"]["crf"][0u]["type"] = "int";
    capa["optional"]["crf"][0u]["dependent"]["codec"] = "H264";
    capa["optional"]["crf"][0u]["dependent"]["x-LSP-rate_or_crf"] = "crf";
    capa["optional"]["crf"][0u]["n"] = 6;

    capa["optional"]["crf"][1u]["help"] = "Video quality";
    capa["optional"]["crf"][1u]["min"] = "0";
    capa["optional"]["crf"][1u]["max"] = "63";
    capa["optional"]["crf"][1u]["type"] = "int";
    capa["optional"]["crf"][1u]["dependent"]["codec"] = "VP9";
    capa["optional"]["crf"][1u]["dependent"]["x-LSP-rate_or_crf"] = "crf";
    capa["optional"]["crf"][1u]["n"] = 7;

    capa["optional"]["rate"]["name"] = "rate";
    capa["optional"]["rate"]["help"] = "Bitrate of the encoding";
    capa["optional"]["rate"]["type"] = "str";
    capa["optional"]["rate"]["dependent"]["x-LSP-rate_or_crf"] = "rate";
    capa["optional"]["rate"]["n"] = 8;

    capa["optional"]["sources"]["name"] = "Layers";
    capa["optional"]["sources"]["type"] = "sublist";
    capa["optional"]["sources"]["itemLabel"] = "layer";
    capa["optional"]["sources"]["help"] =
        "List of sources to overlay on top of each other, in order. If left empty, simply uses the "
        "input track without modifications and nothing else.";
    capa["optional"]["sources"]["n"] = 9;
    capa["optional"]["sources"]["sort"] = "n";
    capa["optional"]["sources"]["dependent"]["x-LSP-kind"] = "video";

    capa["optional"]["track_inhibit"]["name"] = "Track inhibitor(s)";
    capa["optional"]["track_inhibit"]["help"] =
        "What tracks to use as inhibitors. If this track selector is able to select a track, the "
        "process does not start. Defaults to none.";
    capa["optional"]["track_inhibit"]["type"] = "string";
    capa["optional"]["track_inhibit"]["validate"][0u] = "track_selector";
    capa["optional"]["track_inhibit"]["default"] = "audio=none&video=none&subtitle=none";

    capa["codecs"][0u][0u].append("H264");
    capa["codecs"][0u][0u].append("HEVC");
    capa["codecs"][0u][0u].append("VP8");
    capa["codecs"][0u][0u].append("VP9");
    capa["codecs"][0u][0u].append("theora");
    capa["codecs"][0u][0u].append("MPEG2");
    capa["codecs"][0u][0u].append("AV1");
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

  // check config for generic options
  if (!Enc.CheckConfig()){
    FAIL_MSG("Error config syntax error!");
    return 1;
  }

  // create pipe pair before thread
  if (pipe(pipein) || pipe(pipeout)){
    FAIL_MSG("Could not create pipes for process!");
    return 1;
  }
  Util::Procs::socketList.insert(pipeout[0]);
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


    bool EncodeOutputEBML::onFinish(){
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
    void EncodeOutputEBML::dropTrack(size_t trackId, const std::string &reason, bool probablyBad){
      if (opt.isMember("exit_unmask") && opt["exit_unmask"].asBool()){
        INFO_MSG("Unmasking source track %zu" PRIu64, trackId);
        meta.validateTrack(trackId, TRACK_VALID_ALL);
      }
      OutEBML::dropTrack(trackId, reason, probablyBad);
    }


  void EncodeInputEBML::getNext(size_t idx){
    static bool recurse = false;

    // getNext is called recursively, only process the first call
    if (recurse){return InputEBML::getNext(idx);}

    recurse = true;
    InputEBML::getNext(idx);

    if (thisPacket){

      if (!getFirst){
        packetTimeDiff = sendPacketTime - thisPacket.getTime();
        getFirst = true;
      }

      uint64_t tmpLong;
      uint64_t packTime = thisPacket.getTime() + packetTimeDiff;

      // change packettime
      char *data = thisPacket.getData();
      tmpLong = htonl((int)(packTime >> 32));
      memcpy(data + 12, (char *)&tmpLong, 4);
      tmpLong = htonl((int)(packTime & 0xFFFFFFFF));
      memcpy(data + 16, (char *)&tmpLong, 4);
    }

    recurse = false;
  }

  void EncodeInputEBML::setInFile(int stdin_val){
    inFile.open(stdin_val);
    streamName = opt["sink"].asString();
    if (!streamName.size()){streamName = opt["source"].asString();}
    Util::streamVariables(streamName, opt["source"].asString());
    Util::setStreamName(opt["source"].asString() + "→" + streamName);
    if (opt.isMember("target_mask") && !opt["target_mask"].isNull() && opt["target_mask"].asString() != ""){
      DTSC::trackValidDefault = opt["target_mask"].asInt();
    }
  }

  std::string EncodeOutputEBML::getTrackType(int tid){return M.getType(tid);}

  void EncodeOutputEBML::setVideoTrack(std::string tid){
    std::set<size_t> tracks = Util::findTracks(M, capa, "video", tid);
    for (std::set<size_t>::iterator it = tracks.begin(); it != tracks.end(); it++){
      userSelect[*it].reload(streamName, *it);
    }
  }

  void EncodeOutputEBML::setAudioTrack(std::string tid){
    std::set<size_t> tracks = Util::findTracks(M, capa, "audio", tid);
    for (std::set<size_t>::iterator it = tracks.begin(); it != tracks.end(); it++){
      userSelect[*it].reload(streamName, *it);
    }
  }

  void EncodeOutputEBML::sendHeader(){
    realTime = 0;
    size_t idx = getMainSelectedTrack();
    if (opt.isMember("source_mask") && !opt["source_mask"].isNull() && opt["source_mask"].asString() != ""){
      uint64_t sourceMask = opt["source_mask"].asInt();
      INFO_MSG("Masking source track %zu to %" PRIu64, idx, sourceMask);
      meta.validateTrack(idx, sourceMask);
    }
    res_x = M.getWidth(idx);
    res_y = M.getHeight(idx);
    Enc.setResolution(res_x, res_y);
    OutEBML::sendHeader();
  }

  void EncodeOutputEBML::sendNext(){
    if (!sendFirst){
      sendPacketTime = thisPacket.getTime();
      sendFirst = true;
    }

    OutEBML::sendNext();
  }

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
    uint64_t t_limiter = Util::bootSecs();
    while (res_x == 0){
      if (Util::bootSecs() < t_limiter + 5){
        Util::sleep(100);
        MEDIUM_MSG("waiting res_x to be set!");
      }else{
        FAIL_MSG("timeout, resolution is not set!");
        return false;
      }

      MEDIUM_MSG("source resolution: %dx%d", res_x, res_y);
    }

    std::string s_input = "";
    std::string s_overlay = "";
    std::string s_scale = "";
    std::string options = "";

    // load all sources and construct overlay code
    if (opt["sources"].isArray()){
      char in[255] = "";
      char ov[255] = "";

      for (JSON::Iter it(opt["sources"]); it; ++it){

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
          INFO_MSG("no src given, asume reading data from stdin");
          MEDIUM_MSG("Loading Input: -");
        }

        s_input += in;

        uint32_t i_width = -1;
        uint32_t i_height = -1;
        int32_t i_x = 0;
        int32_t i_y = 0;
        std::string i_anchor = "topleft";

        if ((*it).isMember("width") && (*it)["width"].asInt()){i_width = (*it)["width"].asInt();}
        if ((*it).isMember("height") && (*it)["height"].asInt()){
          i_height = (*it)["height"].asInt();
        }

        if ((*it).isMember("x")){i_x = (*it)["x"].asInt();}
        if ((*it).isMember("y")){i_y = (*it)["y"].asInt();}

        if ((*it).isMember("anchor") && (*it)["anchor"].isString()){
          i_anchor = (*it)["anchor"].asString();
        }

        char scale[200];
        sprintf(scale, ";[%d:v]scale=%d:%d[s%d]", it.num() + 1, i_width, i_height, it.num());

        s_scale.append(scale);

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

      s_scale = s_scale.substr(1);
      s_overlay = s_scale + s_overlay;

      if (res_x > 0 || res_y > 0){// video scaling
        sprintf(ov, ";[out]scale=%d:%d,setsar=1:1[out]", res_x, res_y);
      }

      s_overlay.append(ov);

      HIGH_MSG("overlay: %s", s_overlay.c_str());
    }

    // video scaling
    if (res_x > 0 || res_y > 0){
      if (s_overlay.size() == 0){
        char ov[100];
        sprintf(ov, " -filter_complex '[0:v]scale=%d:%d,setsar=1:1[out]' -map [out]", res_x, res_y);
        s_overlay.append(ov);
      }else{
        s_overlay = "-filter_complex " + s_overlay + " -map [out]";
      }
    }else{
      if (s_overlay.size() > 0){s_overlay = "-filter_complex '" + s_overlay + "' -map [out]";}
    }

    if (!profile.empty()){options.append(" -profile:v " + profile);}

    if (!preset.empty()){options.append(" -preset " + preset);}

    snprintf(ffcmd, 10240, "ffmpeg -fflags nobuffer -hide_banner -loglevel warning -f lavfi -i color=c=black:s=%dx%d %s %s -c:v %s %s %s %s -an -force_key_frames source -f matroska - ",
             res_x, res_y, s_input.c_str(), s_overlay.c_str(), codec.c_str(), options.c_str(),
             getBitrateSetting().c_str(), flags.c_str());

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

    if (opt.isMember("preset") && opt["preset"].isString()){preset = opt["preset"].asString();}

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

    if (opt.isMember("bitrate") && opt["bitrate"].isString()){
      b_rate = opt["bitrate"].asString();
    }

    if (opt.isMember("min_bitrate") && opt["min_bitrate"].isString()){
      min_rate = opt["min_bitrate"].asString();
    }

    if (opt.isMember("max_bitrate") && opt["max_bitrate"].isString()){
      max_rate = opt["max_bitrate"].asString();
    }

    setBitrate(b_rate, min_rate, max_rate);

    // extra ffmpeg flags
    if (opt.isMember("flags") && opt["flags"].isString()){flags = opt["bitrate"].asString();}

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
    Util::Procs p;
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
    ffout = p.StartPiped(args, &pipein[0], &pipeout[1], &ffer);

    while (conf.is_active && p.isRunning(ffout)){Util::sleep(200);}

    while (p.isRunning(ffout)){
      MEDIUM_MSG("stopping ffmpeg...");
      p.StopAll();
      Util::sleep(200);
    }

    MEDIUM_MSG("ffmpeg process stopped.");
  }
}// namespace Mist
