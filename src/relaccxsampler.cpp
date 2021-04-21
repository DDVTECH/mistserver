#include <mist/defines.h>
#include <mist/util.h>

#include <iostream>

int main(){
  char storage[5000000]; // 5mb
  Util::RelAccX tmp(storage, false);

  tmp.addField("vod", RAX_UINT);
  tmp.addField("live", RAX_UINT);
  tmp.addField("source", RAX_STRING, 512);
  tmp.addField("bufferwindow", RAX_64UINT);
  tmp.addField("bootmsoffset", RAX_64UINT);
  tmp.setRCount(1);
  tmp.setReady();

  std::cout << tmp.toPrettyString() << std::endl;

  return 0;
}
