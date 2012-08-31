/// \file player.cpp
/// Holds all code for the MistPlayer application used for VoD streams.

#include <stdio.h> //for fileno
#include <sys/time.h>
#include <mist/dtsc.h>
#include <mist/json.h>
#include <mist/config.h>
#include <mist/socket.h>

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
  Socket::Connection in_out = Socket::Connection(fileno(stdin), fileno(stdout));
  std::string meta_str = source.getHeader();

  //send the header
  {
    in_out.Send("DTSC");
    unsigned int size = htonl(meta_str.size());
    in_out.Send(std::string((char*)&size, (size_t)4));
    in_out.Send(meta_str);
  }

  JSON::Value meta = JSON::fromDTMI(meta_str);
  JSON::Value last_pack;

  long long now, timeDiff = 0, lastTime = 0;

  while (in_out.connected()){
    if (in_out.spool() && in_out.Received().find('\n') != std::string::npos){
      std::string cmd = in_out.Received().substr(0, in_out.Received().find('\n'));
      in_out.Received().erase(0, in_out.Received().find('\n')+1);
      if (cmd != ""){
        switch (cmd[0]){
          case 'P':{ //Push
            in_out.close();//pushing to VoD makes no sense
          } break;
          case 'S':{ //Stats
            /// \todo Parse stats command properly.
            /* Stats(cmd.substr(2)); */
          } break;
          case 's':{ //second-seek
            int second = JSON::Value(cmd.substr(2)).asInt();
            double keyms = meta["video"]["keyms"].asInt();
            if (keyms <= 0){keyms = 2000;}
            source.seek_frame(second / (keyms / 1000.0));
          } break;
          case 'f':{ //frame-seek
            source.seek_frame(JSON::Value(cmd.substr(2)).asInt());
          } break;
          case 'p':{ //play
            playing = -1;
          } break;
          case 'o':{ //once-play
            if (playing < 0){playing = 0;}
            ++playing;
          } break;
          case 'q':{ //quit-playing
            playing = 0;
          } break;
        }
      }
    }
    if (playing != 0){
      now = getNowMS();
      if (now - timeDiff >= lastTime || lastTime - (now - timeDiff) > 15000) {
        std::string packet = source.getPacket();
        last_pack = JSON::fromDTMI(packet);
        lastTime = last_pack["time"].asInt();
        if ((now - timeDiff - lastTime) > 15000 || (now - timeDiff - lastTime < -15000)){
          timeDiff = now - lastTime;
        }
        //insert proper header for this type of data
        in_out.Send("DTPD");
        //insert the packet length
        unsigned int size = htonl(packet.size());
        in_out.Send(std::string((char*)&size, (size_t)4));
        in_out.Send(packet);
      } else {
        usleep(std::min(14999LL, lastTime - (now - timeDiff)) * 1000);
      }
      if (playing > 0){--playing;}
    }
  }
  return 0;
}
