/// \file player.cpp
/// Holds all code for the MistPlayer application used for VoD streams.

#include <iostream>//for std::cerr
#include <stdio.h> //for fileno
#include <stdlib.h> //for atoi
#include <sys/time.h>
#include <mist/dtsc.h>
#include <mist/json.h>
#include <mist/config.h>
#include <mist/socket.h>
#include <mist/timing.h>

//under cygwin, recv blocks for ~15ms if no data is available.
//This is a hack to keep performance decent with that bug present.
#ifdef __CYGWIN__
#define CYG_DEFI int cyg_count;
#define CYG_INCR cyg_count++;
#define CYG_LOOP (cyg_count % 20 == 0) &&
#else
#define CYG_DEFI 
#define CYG_INCR 
#define CYG_LOOP 
#endif

/// Copy of stats from buffer_user.cpp
class Stats{
  public:
    unsigned int up;
    unsigned int down;
    std::string host;
    std::string connector;
    unsigned int conntime;
    Stats(){
      up = 0;
      down = 0;
      conntime = 0;
    }
    ;
    /// Reads a stats string and parses it to the internal representation.
    Stats(std::string s){
      size_t f = s.find(' ');
      if (f != std::string::npos){
        host = s.substr(0, f);
        s.erase(0, f + 1);
      }
      f = s.find(' ');
      if (f != std::string::npos){
        connector = s.substr(0, f);
        s.erase(0, f + 1);
      }
      f = s.find(' ');
      if (f != std::string::npos){
        conntime = atoi(s.substr(0, f).c_str());
        s.erase(0, f + 1);
      }
      f = s.find(' ');
      if (f != std::string::npos){
        up = atoi(s.substr(0, f).c_str());
        s.erase(0, f + 1);
        down = atoi(s.c_str());
      }
    }
};

int main(int argc, char** argv){
  Util::Config conf(argv[0], PACKAGE_VERSION);
  conf.addOption("filename", JSON::fromString("{\"arg_num\":1, \"help\":\"Name of the file to write to stdout.\"}"));
  conf.parseArgs(argc, argv);
  conf.activate();
  int playing = 0;

  DTSC::File source = DTSC::File(conf.getString("filename"));
  Socket::Connection in_out = Socket::Connection(fileno(stdout), fileno(stdin));
  JSON::Value meta = source.getMeta();
  JSON::Value pausemark;
  pausemark["datatype"] = "pause_marker";
  pausemark["time"] = (long long int)0;

  Socket::Connection StatsSocket = Socket::Connection("/tmp/mist/statistics", true);
  int lasttime = Util::epoch(); //time last packet was sent

  //send the header
  std::string meta_str = meta.toNetPacked();
  in_out.Send(meta_str);

  if (meta["video"]["keyms"].asInt() < 11){
    meta["video"]["keyms"] = (long long int)1000;
  }
  JSON::Value last_pack;

  bool meta_sent = false;
  long long now, lastTime = 0; //for timing of sending packets
  long long bench = 0; //for benchmarking
  Stats sts;
  CYG_DEFI

  while (in_out.connected() && (Util::epoch() - lasttime < 60)){
    CYG_INCR
    if (CYG_LOOP in_out.spool()){
      while (in_out.Received().size()){
        //delete anything that doesn't end with a newline
        if ( *(in_out.Received().get().rbegin()) != '\n'){
          in_out.Received().get().clear();
          continue;
        }
        in_out.Received().get().resize(in_out.Received().get().size() - 1);
        if ( !in_out.Received().get().empty()){
          switch (in_out.Received().get()[0]){
            case 'P': { //Push
#if DEBUG >= 4
              std::cerr << "Received push - ignoring (" << in_out.Received().get() << ")" << std::endl;
#endif
              in_out.close(); //pushing to VoD makes no sense
            }
              break;
            case 'S': { //Stats
              if ( !StatsSocket.connected()){
                StatsSocket = Socket::Connection("/tmp/mist/statistics", true);
              }
              if (StatsSocket.connected()){
                sts = Stats(in_out.Received().get().substr(2));
                JSON::Value json_sts;
                json_sts["vod"]["down"] = (long long int)sts.down;
                json_sts["vod"]["up"] = (long long int)sts.up;
                json_sts["vod"]["time"] = (long long int)sts.conntime;
                json_sts["vod"]["host"] = sts.host;
                json_sts["vod"]["connector"] = sts.connector;
                json_sts["vod"]["filename"] = conf.getString("filename");
                json_sts["vod"]["now"] = Util::epoch();
                json_sts["vod"]["start"] = Util::epoch() - sts.conntime;
                if ( !meta_sent){
                  json_sts["vod"]["meta"] = meta;
                  json_sts["vod"]["meta"]["audio"].removeMember("init");
                  json_sts["vod"]["meta"]["video"].removeMember("init");
                  json_sts["vod"]["meta"].removeMember("keytime");
                  json_sts["vod"]["meta"].removeMember("keybpos");
                  meta_sent = true;
                }
                StatsSocket.Send(json_sts.toString().c_str());
                StatsSocket.Send("\n\n");
                StatsSocket.flush();
              }
            }
              break;
            case 's': { //second-seek
              int ms = JSON::Value(in_out.Received().get().substr(2)).asInt();
              bool ret = source.seek_time(ms);
              lastTime = 0;
            }
              break;
            case 'f': { //frame-seek
              bool ret = source.seek_frame(JSON::Value(in_out.Received().get().substr(2)).asInt());
              lastTime = 0;
            }
              break;
            case 'p': { //play
              playing = -1;
              lastTime = 0;
              in_out.setBlocking(false);
            }
              break;
            case 'o': { //once-play
              if (playing <= 0){
                playing = 1;
              }
              ++playing;
              in_out.setBlocking(false);
              bench = Util::getMS();
            }
              break;
            case 'q': { //quit-playing
              playing = 0;
              in_out.setBlocking(true);
            }
              break;
          }
          in_out.Received().get().clear();
        }
      }
    }
    if (playing != 0){
      now = Util::getMS();
      source.seekNext();
      if ( !source.getJSON()){
        playing = 0;
      }
      if (source.getJSON().isMember("keyframe")){
        if (playing == -1 && meta["video"]["keyms"].asInt() > now - lastTime){
          Util::sleep(meta["video"]["keyms"].asInt() - (now - lastTime));
        }
        lastTime = now;
        if (playing > 0){
          --playing;
        }
      }
      if (playing == 0){
#if DEBUG >= 4
        std::cerr << "Completed VoD request in MistPlayer (" << (Util::getMS() - bench) << "ms)" << std::endl;
#endif
        pausemark["time"] = source.getJSON()["time"];
        pausemark.toPacked();
        in_out.SendNow(pausemark.toNetPacked());
        in_out.setBlocking(true);
      }else{
        lasttime = Util::epoch();
        //insert proper header for this type of data
        in_out.Send("DTPD");
        //insert the packet length
        unsigned int size = htonl(source.getPacket().size());
        in_out.Send((char*) &size, 4);
        in_out.SendNow(source.getPacket());
      }
    }else{
      Util::sleep(10);
    }
  }
  StatsSocket.close();
  in_out.close();
#if DEBUG >= 5
  if (Util::epoch() - lasttime < 60){
    std::cerr << "MistPlayer exited (disconnect)." << std::endl;
  }else{
    std::cerr << "MistPlayer exited (command timeout)." << std::endl;
  }
#endif
  return 0;
}
