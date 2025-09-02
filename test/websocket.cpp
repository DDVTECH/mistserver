#include <iomanip>
#include <iostream>
#include <mist/config.h>
#include <mist/timing.h>
#include <mist/websocket.h>

int main(int argc, char **argv){
  Util::Config c(argv[0]);

  JSON::Value option;
  option["arg_num"] = 1;
  option["arg"] = "string";
  option["help"] = "URL to retrieve";
  c.addOption("url", option);
  if (!(c.parseArgs(argc, argv))){return 1;}

  Util::redirectLogsIfNeeded();
  Socket::Connection C;
  HTTP::Websocket ws(C, HTTP::URL(c.getString("url")));
  if (!ws){return 1;}
  while (ws){
    if (!ws.readFrame(true)){
      Util::sleep(100);
      continue;
    }
    switch (ws.frameType){
    case 1:
      std::cout << "Text frame (" << ws.data.size() << "b):" << std::endl
                << std::string(ws.data, ws.data.size()) << std::endl;
      break;
    case 2:{
      std::cout << "Binary frame (" << ws.data.size() << "b):" << std::endl;
      size_t counter = 0;
      for (size_t i = 0; i < ws.data.size(); ++i){
        std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)(ws.data[i] & 0xff) << " ";
        if ((counter) % 32 == 31){std::cout << std::endl;}
        counter++;
      }
      std::cout << std::endl;
    }break;
    case 8:
      std::cout << "Connection close frame" << std::endl;
      C.close();
      break;
    case 9: std::cout << "Ping frame" << std::endl; break;
    case 10: std::cout << "Pong frame" << std::endl; break;
    default: std::cout << "Unknown frame (" << (int)ws.frameType << ")" << std::endl; break;
    }
  }
  return 0;
}
