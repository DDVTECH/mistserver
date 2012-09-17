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

/// Copy of stats from buffer_user.cpp
class Stats{
  public:
    unsigned int up;
    unsigned int down;
    std::string host;
    std::string connector;
    unsigned int conntime;
    Stats(){
      up = 0; down = 0; conntime = 0;
    };
    /// Reads a stats string and parses it to the internal representation.
    Stats(std::string s){
      size_t f = s.find(' ');
      if (f != std::string::npos){
        host = s.substr(0, f);
        s.erase(0, f+1);
      }
      f = s.find(' ');
      if (f != std::string::npos){
        connector = s.substr(0, f);
        s.erase(0, f+1);
      }
      f = s.find(' ');
      if (f != std::string::npos){
        conntime = atoi(s.substr(0, f).c_str());
        s.erase(0, f+1);
      }
      f = s.find(' ');
      if (f != std::string::npos){
        up = atoi(s.substr(0, f).c_str());
        s.erase(0, f+1);
        down = atoi(s.c_str());
      }
    };
};


/// Gets the current system time in milliseconds.
long long int getNowMS(){
  timeval t;
  gettimeofday(&t, 0);
  return t.tv_sec * 1000 + t.tv_usec/1000;
}//getNowMS

int main(int argc, char** argv){
  Util::Config conf(argv[0], PACKAGE_VERSION);
  conf.addOption("filename", JSON::fromString("{\"arg_num\":1, \"help\":\"Name of the file to write to stdout.\"}"));
  conf.parseArgs(argc, argv);
  conf.activate();
  int playing = 0;

  DTSC::File source = DTSC::File(conf.getString("filename"));
  Socket::Connection in_out = Socket::Connection(fileno(stdout), fileno(stdin));
  std::string meta_str = source.getHeader();
  JSON::Value pausemark;
  pausemark["datatype"] = "pause_marker";
  pausemark["time"] = (long long int)0;

  Socket::Connection StatsSocket = Socket::Connection("/tmp/mist/statistics", true);
  int lasttime = time(0);//time last packet was sent

  //send the header
  {
    in_out.Send("DTSC");
    unsigned int size = htonl(meta_str.size());
    in_out.Send((char*)&size, 4);
    in_out.Send(meta_str);
  }

  JSON::Value meta = JSON::fromDTMI(meta_str);
  if (meta["video"]["keyms"].asInt() < 11){
    meta["video"]["keyms"] = (long long int)1000;
  }
  JSON::Value last_pack;

  bool meta_sent = false;
  long long now, lastTime = 0;//for timing of sending packets
  long long bench = 0;//for benchmarking
  Stats sts;

  while (in_out.connected() && std::cin.good() && std::cout.good() && (time(0) - lasttime < 60)){
    if (in_out.spool()){
      while (in_out.Received().size()){
        //delete anything that doesn't end with a newline
        if (*(in_out.Received().get().rbegin()) != '\n'){
          in_out.Received().get().clear();
          continue;
        }
        in_out.Received().get().resize(in_out.Received().get().size() - 1);
        if (!in_out.Received().get().empty()){
          switch (in_out.Received().get()[0]){
            case 'P':{ //Push
              #if DEBUG >= 4
              std::cerr << "Received push - ignoring (" << in_out.Received().get() << ")" << std::endl;
              #endif
              in_out.close();//pushing to VoD makes no sense
            } break;
            case 'S':{ //Stats
              if (!StatsSocket.connected()){
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
                json_sts["vod"]["now"] = (long long int)time(0);
                json_sts["vod"]["start"] = (long long int)(time(0) - sts.conntime);
                if (!meta_sent){
                  json_sts["vod"]["meta"] = meta;
                  meta_sent = true;
                }
                StatsSocket.Send(json_sts.toString().c_str());
                StatsSocket.Send("\n\n");
                StatsSocket.flush();
              }
            } break;
            case 's':{ //second-seek
              int ms = JSON::Value(in_out.Received().get().substr(2)).asInt();
              bool ret = source.seek_time(ms);
            } break;
            case 'f':{ //frame-seek
              bool ret = source.seek_frame(JSON::Value(in_out.Received().get().substr(2)).asInt());
            } break;
            case 'p':{ //play
              playing = -1;
              in_out.setBlocking(false);
            } break;
            case 'o':{ //once-play
              if (playing <= 0){playing = 1;}
              ++playing;
              in_out.setBlocking(false);
              #if DEBUG >= 4
              std::cerr << "Playing one keyframe" << std::endl;
              #endif
              bench = getNowMS();
            } break;
            case 'q':{ //quit-playing
              playing = 0;
              in_out.setBlocking(true);
            } break;
          }
          in_out.Received().get().clear();
        }
      }
    }
    if (playing != 0){
      now = getNowMS();
      if (playing > 0 || meta["video"]["keyms"].asInt() <= now-lastTime) {
        source.seekNext();
        if (source.getJSON().isMember("keyframe")){
          lastTime = now;
          if (playing > 0){--playing;}
          if (playing == 0){
            #if DEBUG >= 4
            std::cerr << "Sending pause_marker (" << (getNowMS() - bench) << "ms)" << std::endl;
            #endif
            pausemark["time"] = (long long int)now;
            pausemark.toPacked();
            in_out.SendNow(pausemark.toNetPacked());
            in_out.setBlocking(true);
          }
        }
        if (playing != 0){
          lasttime = time(0);
          //insert proper header for this type of data
          in_out.Send("DTPD");
          //insert the packet length
          unsigned int size = htonl(source.getPacket().size());
          in_out.Send((char*)&size, 4);
          in_out.SendNow(source.getPacket());
        }
      } else {
        usleep((meta["video"]["keyms"].asInt()-(now-lastTime))*1000);
      }
    }else{
      usleep(10000);//sleep 10ms
    }
  }

  StatsSocket.close();
  in_out.close();
  return 0;
}
