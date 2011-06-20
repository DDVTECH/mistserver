#pragma once
#include<string>

struct Server {
  int SrvId;
  std::string SrvName;
  std::string SrvAddr;
  int SrvSSH;
  int SrvHTTP;
  int SrvRTMP;

  int SrvLimitBW;
  int SrvLimitUsers;
};//Server struct
