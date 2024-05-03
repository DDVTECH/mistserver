#include <iostream>
#include <mist/shared_memory.h>
#include <mist/util.h>

int main(int argc, char **argv){
  Util::redirectLogsIfNeeded();
  if (argc < 1){
    FAIL_MSG("Usage: %s MEMORY_PAGE_NAME", argv[0]);
    return 1;
  }
  IPC::sharedPage f(argv[1], 0, false, false);
  if (!f.mapped){
    std::cout << "Could not open " << argv[1] << ": does not exist" << std::endl;
    return 1;
  }
  const Util::RelAccX A(f.mapped, false);
  if (!A.isReady()){
    std::cout << "Memory structure " << argv[1] << " is not ready" << std::endl;
  }
  std::cout << A.toPrettyString() << std::endl;
}
