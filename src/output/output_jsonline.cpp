#include "output_jsonline.h"
#include <mist/defines.h>
#include <mist/http_parser.h>
#include <mist/url.h>
#include <mist/triggers.h>
#include <mist/stream.h>

namespace Mist{
  OutJSONLine::OutJSONLine(Socket::Connection &conn) : Output(conn){
    trkIdx = INVALID_TRACK_ID;
    streamName = config->getString("streamname");
    wantRequest = true;
    parseData = false;
    if (Triggers::shouldTrigger("PUSH_REWRITE")){
      std::string payload = "jsonline://" + myConn.getBoundAddress() + ":" + config->getOption("port").asString() + "\n" + getConnectedHost() + "\n" + streamName;
      std::string newStream = streamName;
      Triggers::doTrigger("PUSH_REWRITE", payload, "", false, newStream);
      if (!newStream.size()){
        FAIL_MSG("Push from %s to URL %s rejected - PUSH_REWRITE trigger blanked the URL",
                 getConnectedHost().c_str(), reqUrl.c_str());
        config->is_active = false;
        return;
      }else{
        streamName = newStream;
        Util::sanitizeName(streamName);
        Util::setStreamName(streamName);
      }
    }
    if (!allowPush("")){
      FAIL_MSG("Pushing not allowed");
      config->is_active = false;
      return;
    }
    initialize();
    trkIdx = meta.addTrack();
    meta.setType(trkIdx, "meta");
    meta.setCodec(trkIdx, config->getString("codec"));
    meta.setID(trkIdx, 1);
    offset = M.getBootMsOffset();
    myConn.setBlocking(false);
    INFO_MSG("%s track index is %zu", config->getString("codec").c_str(), trkIdx);
  }

  OutJSONLine::~OutJSONLine(){
    if (trkIdx != INVALID_TRACK_ID && M){
      meta.abandonTrack(trkIdx);
    }
  }

  void OutJSONLine::init(Util::Config *cfg){
    Output::init(cfg);
    capa["name"] = "JSONLine";
    capa["friendly"] = "JSON lines over raw TCP";
    capa["desc"] = "Real time JSON line-by-line input over a raw TCP socket or standard input";
    capa["deps"] = "";
    capa["required"]["streamname"]["name"] = "Stream";
    capa["required"]["streamname"]["help"] = "What streamname to serve. For multiple streams, add "
                                             "this protocol multiple times using different ports.";
    capa["required"]["streamname"]["type"] = "str";
    capa["required"]["streamname"]["option"] = "--stream";
    capa["required"]["streamname"]["short"] = "s";

    cfg->addOption("codec",
                   JSON::fromString("{\"arg\":\"string\",\"default\":\"JSON\",\"short\":\"c\",\"long\":"
                                    "\"codec\",\"help\":\"Codec to use for data ingest, JSON by default\"}"));
    capa["optional"]["codec"]["name"] = "Codec";
    capa["optional"]["codec"]["help"] = "What codec to ingest as";
    capa["optional"]["codec"]["default"] = "JSON";
    capa["optional"]["codec"]["type"] = "str";
    capa["optional"]["codec"]["option"] = "--codec";
    capa["optional"]["codec"]["short"] = "c";

    capa["codecs"][0u][0u].append("JSON");
    cfg->addConnectorOptions(3456, capa);
    config = cfg;
  }

  void OutJSONLine::sendNext(){
  }

  bool OutJSONLine::listenMode(){return true;}

  void OutJSONLine::requestHandler(){
    if (myConn.spool()){
      while (myConn.Received().size()){
        dPtr.append(myConn.Received().get());
        myConn.Received().get().clear();
        if (dPtr.size() && dPtr[dPtr.size() - 1] == '\n'){
          thisTime = Util::bootMS() - offset;
          thisIdx = trkIdx;
          thisPacket.genericFill(thisTime, 0, 1, dPtr, dPtr.size(), 0, true);
          bufferLivePacket(thisPacket);
          dPtr.truncate(0);
        }
      }
    }else{
      meta.setNowms(trkIdx, Util::bootMS() - offset);
      Util::sleep(10);
    }
  }

  std::string OutJSONLine::getStatsName(){
    if (!parseData){
      return "INPUT:" + capa["name"].asStringRef();
    }else{
      return Output::getStatsName();
    }
  }

  bool OutJSONLine::isReadyForPlay(){return true;}

}// namespace Mist
