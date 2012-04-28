#include "stats.h"
#include <stdlib.h> //for atoi()

Buffer::Stats::Stats(){
  up = 0;
  down = 0;
  conntime = 0;
}

Buffer::Stats::Stats(std::string s){
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
}
