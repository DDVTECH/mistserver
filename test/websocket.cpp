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
  option.null();
  option["help"] = "Interval for messages";
  option["long"] = "interval";
  option["short"] = "I";
  option["default"] = 4;
  option["arg"] = "int";
  c.addOption("interval", option);
  if (!(c.parseArgs(argc, argv))){return 1;}

  Util::redirectLogsIfNeeded();
  Socket::Connection C;
  HTTP::URL url(c.getString("url"));
  INFO_MSG("Connect to: %s", url.getUrl().c_str());
  HTTP::Websocket ws(C, url);
  if (!ws){return 1;}
  uint64_t lastChat = Util::bootMS();
  srand(getpid());
  uint64_t interval = c.getInteger("interval") * 1000;
  while (ws){
    if (Util::bootMS() > lastChat + interval){
      JSON::Value msg;
      msg["type"] = "chat";
      std::string rndMsg;
      rndMsg.reserve(100);
      for (size_t i = 0; i < 100; ++i){
        rndMsg += ('a' + (rand() % 26));
      }
      msg["message"] = rndMsg;
      ws.sendFrame(msg.toString());
      lastChat = Util::bootMS();
    }
    if (!ws.readFrame()){
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
