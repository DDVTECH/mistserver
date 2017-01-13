/// \file stats_analyser.cpp
/// Will emulate a given amount of clients in the statistics.

#include <stdlib.h>

#include <mist/dtsc.h>
#include <mist/json.h>
#include <mist/shared_memory.h>
#include <mist/config.h>
#include <mist/defines.h>
#include <mist/socket.h>


void printStatistics(char * data, size_t len, unsigned int id){
  std::cout << "#" << id << ": (state: " << (unsigned int)(data[-1] & 0x7F) << ")" << std::endl;
  IPC::statExchange tmpEx(data);
  std::cout << "  Now:        " << tmpEx.now() << std::endl;
  std::cout << "  Time:       " << tmpEx.time() << std::endl;
  std::cout << "  lastSec:    " << tmpEx.lastSecond() << std::endl;
  std::cout << "  down:       " << tmpEx.down() << std::endl;
  std::cout << "  up:         " << tmpEx.up() << std::endl;
  std::string h;
  Socket::hostBytesToStr(tmpEx.host().data(), tmpEx.host().size(), h);
  std::cout << "  host:       " << h << std::endl;
  std::cout << "  streamName: " << tmpEx.streamName() << std::endl;
  std::cout << "  connector:  " << tmpEx.connector() << std::endl;
  std::cout << "  CRC32:      " << tmpEx.crc() << std::endl;
  std::cout << "  sync:       " << (int)tmpEx.getSync() << std::endl;
  std::cout << "  PID:        " << tmpEx.getPID() << std::endl;
}


/// Will emulate a given amount of clients in the statistics.
int main(int argc, char ** argv){
  Util::Config conf = Util::Config(argv[0]);
  conf.addOption("clients", JSON::fromString("{\"arg\":\"num\", \"short\":\"c\", \"long\":\"clients\", \"default\":1000, \"help\":\"Amount of clients to emulate.\"}"));
  conf.addOption("stream", JSON::fromString("{\"arg\":\"string\", \"short\":\"s\", \"long\":\"stream\", \"default\":\"test\", \"help\":\"Streamname to pretend to request.\"}"));
  conf.addOption("up", JSON::fromString("{\"arg\":\"string\", \"short\":\"u\", \"long\":\"up\", \"default\":131072, \"help\":\"Bytes per second upstream.\"}"));
  conf.addOption("down", JSON::fromString("{\"arg\":\"string\", \"short\":\"d\", \"long\":\"down\", \"default\":13000, \"help\":\"Bytes per second downstream.\"}"));
  conf.addOption("sine", JSON::fromString("{\"arg\":\"string\", \"short\":\"S\", \"long\":\"sine\", \"default\":0, \"help\":\"Bytes per second variance in a sine pattern.\"}"));
  conf.addOption("userscale", JSON::fromString("{\"arg\":\"string\", \"short\":\"U\", \"long\":\"userscale\", \"default\":0, \"help\":\"If != 0, scales users from 0% to 100% bandwidth.\"}"));
  conf.addOption("read", JSON::fromString("{\"arg\":\"num\", \"short\":\"r\", \"long\":\"read\", \"default\":0, \"help\":\"If != 0, does not simulate, just reads existing data.\"}"));
  conf.parseArgs(argc, argv);
  
  std::string streamName = conf.getString("stream");
  long long clientCount = conf.getInteger("clients");
  long long up = conf.getInteger("up");
  long long down = conf.getInteger("down");
  long long sine = conf.getInteger("sine");
  long long scale = conf.getInteger("userscale");
  long long currsine = sine;
  long long goingUp = 0;
 

  if (conf.getInteger("read")){
    IPC::sharedServer statServer(SHM_STATISTICS, STAT_EX_SIZE, true);
    statServer.abandon();
    statServer.parseEach(printStatistics);
    return 0;
  }


  IPC::sharedClient ** clients = (IPC::sharedClient **)malloc(sizeof(IPC::sharedClient *)*clientCount);
  for (long long i = 0; i < clientCount; i++){
    clients[i] = new IPC::sharedClient(SHM_STATISTICS, STAT_EX_SIZE, true);
  }
  
  unsigned long long int counter = 0;
  conf.activate();
  
  while (conf.is_active){
    unsigned long long int now = Util::epoch();
    counter++;
    if (sine){
      currsine += goingUp;
      if (currsine < -down || currsine < -up){
        currsine = std::max(-down, -up);
      }
      if (currsine > 0){
        goingUp -= sine/100 + 1;
      }else{
        goingUp += sine/100 + 1;
      }
    }
    for (long long i = 0; i < clientCount; i++){
      if (clients[i]->getData()){
        IPC::statExchange tmpEx(clients[i]->getData());
        tmpEx.now(now);
        tmpEx.host("::42");
        tmpEx.crc(i);
        tmpEx.streamName(streamName);
        tmpEx.connector("TEST");
        if (scale){
          tmpEx.up(tmpEx.up() + (up+currsine)*i/clientCount);
          tmpEx.down(tmpEx.down() + (down+currsine)*i/clientCount);
        }else{
          tmpEx.up(tmpEx.up()+up+currsine);
          tmpEx.down(tmpEx.down()+down+currsine);
        }
        tmpEx.time(counter);
        tmpEx.lastSecond(counter * 1000);
        clients[i]->keepAlive();
      }
    }
    Util::wait(1000);
  }
  
  for (long long i = 0; i < clientCount; i++){
    clients[i]->finish();
    delete clients[i];
  }
  
  free(clients);
  return 0;
}
