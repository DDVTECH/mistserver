/// \file player.cpp
/// Holds all code for the MistPlayer application used for VoD streams.

#if DEBUG >= 4
#include <iostream>//for std::cerr
#endif

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

  //send the header
  {
    in_out.Send("DTSC");
    unsigned int size = htonl(meta_str.size());
    in_out.Send((char*)&size, 4);
    in_out.Send(meta_str);
  }

  JSON::Value meta = JSON::fromDTMI(meta_str);
  JSON::Value last_pack;

  bool meta_sent = false;
  long long now, timeDiff = 0, lastTime = 0;
  Stats sts;

  while (in_out.connected()){
    if (in_out.spool()){
      while (in_out.Received().find('\n') != std::string::npos){
        std::string cmd = in_out.Received().substr(0, in_out.Received().find('\n'));
        in_out.Received().erase(0, in_out.Received().find('\n')+1);
        if (cmd != ""){
          switch (cmd[0]){
            case 'P':{ //Push
              #if DEBUG >= 4
              std::cerr << "Received push - ignoring (" << cmd << ")" << std::endl;
              #endif
              in_out.close();//pushing to VoD makes no sense
            } break;
            case 'S':{ //Stats
              if (!StatsSocket.connected()){
                StatsSocket = Socket::Connection("/tmp/mist/statistics", true);
              }
              if (StatsSocket.connected()){
                sts = Stats(cmd.substr(2));
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
              #if DEBUG >= 4
              std::cerr << "Received ms-seek (" << cmd << ")" << std::endl;
              #endif
              int ms = JSON::Value(cmd.substr(2)).asInt();
              bool ret = source.seek_time(ms);
              #if DEBUG >= 4
              std::cerr << "Second-seek completed (time " << ms << "ms) " << ret << std::endl;
              #endif
            } break;
            case 'f':{ //frame-seek
              #if DEBUG >= 4
              std::cerr << "Received frame-seek (" << cmd << ")" << std::endl;
              #endif
              bool ret = source.seek_frame(JSON::Value(cmd.substr(2)).asInt());
              #if DEBUG >= 4
              std::cerr << "Frame-seek completed " << ret << std::endl;
              #endif
            } break;
            case 'p':{ //play
              #if DEBUG >= 4
              std::cerr << "Received play" << std::endl;
              #endif
              playing = -1;
              in_out.setBlocking(false);
            } break;
            case 'o':{ //once-play
              #if DEBUG >= 4
              std::cerr << "Received once-play" << std::endl;
              #endif
              if (playing <= 0){playing = 1;}
              ++playing;
              in_out.setBlocking(false);
            } break;
            case 'q':{ //quit-playing
              #if DEBUG >= 4
              std::cerr << "Received quit-playing" << std::endl;
              #endif
              playing = 0;
              in_out.setBlocking(true);
            } break;
          }
        }
      }
    }
    if (playing != 0){
      now = getNowMS();
      if (playing > 0 || now - timeDiff >= lastTime || lastTime - (now - timeDiff) > 15000) {
        source.seekNext();
        lastTime = source.getJSON()["time"].asInt();
        if ((now - timeDiff - lastTime) > 5000 || (now - timeDiff - lastTime < -5000)){
          timeDiff = now - lastTime;
        }
        if (source.getJSON().isMember("keyframe")){
          if (playing > 0){--playing;}
          if (playing == 0){
            #if DEBUG >= 4
            std::cerr << "Sending pause_marker" << std::endl;
            #endif
            pausemark["time"] = (long long int)now;
            pausemark.toPacked();
            in_out.Send(pausemark.toNetPacked());
            in_out.flush();
            in_out.setBlocking(true);
          }
        }
        if (playing != 0){
          //insert proper header for this type of data
          in_out.Send("DTPD");
          //insert the packet length
          unsigned int size = htonl(source.getPacket().size());
          in_out.Send((char*)&size, 4);
          in_out.Send(source.getPacket());
        }
      } else {
        usleep(std::min(10000LL, lastTime - (now - timeDiff)) * 1000);
      }
    }
    usleep(10000);//sleep 10ms
  }

  StatsSocket.close();
  return 0;
}
