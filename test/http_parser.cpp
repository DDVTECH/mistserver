#include <iostream>
#include <mist/http_parser.h>
#include <mist/timing.h>

int main(int argc, char ** argv){
  bool preMade = false;
  Socket::Connection C(1, 0); // Open stdio by default
  // If there is a T_HTTP environment variable, use that as input instead
  if (getenv("T_HTTP")){
    preMade = true;
    // Keep stdio open, only drop the reference to it
    C.drop();
    // Create a pipe and reconnect the socket to it
    int p[2];
    if (pipe(p)){
      FAIL_MSG("Could not open pipe!");
      return 1;
    }
    C.open(p[1], p[0]);
    // Write the T_HTTP env contents into the pipe
    C.SendNow(getenv("T_HTTP"));
    // Close the write end if we're not lingering
    if (!getenv("T_LINGER")){close(p[1]);}
  }

  HTTP::Parser p;
  int counter = 0;
  C.setBlocking(false);
  uint64_t lastData = Util::bootMS();
  do {
    if (C.spool()){
      lastData = Util::bootMS();
      while (p.Read(C)){
        INFO_MSG("Read a HTTP message: %s %s %s (%zu bytes)", p.method.c_str(), p.url.c_str(), p.protocol.c_str(), p.body.size());
        ++counter;
        p.Clean();
      }
    }else{
      // premade requests will instantly time out, others after 10 seconds
      if (preMade){break;}
      if (Util::bootMS() > lastData + 10000){
        WARN_MSG("Read timeout, aborting");
        break;
      }
      Util::sleep(5);
    }
  }while(C);
  while (p.Read(C)){
    INFO_MSG("Read a HTTP message: %s %s %s (%zu bytes)", p.method.c_str(), p.url.c_str(), p.protocol.c_str(), p.body.size());
    ++counter;
    p.Clean();
  }

  INFO_MSG("Total messages: %d", counter);

  if (getenv("T_COUNT")){
    if (counter != atoi(getenv("T_COUNT"))){
      return 1;
    }
  }

  return 0;
}

