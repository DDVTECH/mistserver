#include "../lib/urireader.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <iostream>
#include <unistd.h>

/// Regression test: a URIReader must release its file descriptor and memory
/// mapping when it goes out of scope. Rolling-playlist consumers construct a
/// local reader for every playlist refresh (every few seconds, forever); before
/// this fix each refresh leaked the fd+mmap of the just-replaced playlist
/// inode, pinning hundreds of deleted files in memory and on disk per hour.

static size_t countFds(){
#ifdef __APPLE__
  const char *fdDir = "/dev/fd";
#else
  const char *fdDir = "/proc/self/fd";
#endif
  size_t count = 0;
  DIR *d = opendir(fdDir);
  if (!d){
    std::cerr << "cannot open " << fdDir << std::endl;
    std::exit(1);
  }
  while (readdir(d)){++count;}
  closedir(d);
  return count;
}

int main(){
  // Create a small temporary file to read
  char path[] = "/tmp/uritest.XXXXXX";
  int fd = mkstemp(path);
  if (fd < 0){
    std::cerr << "mkstemp failed" << std::endl;
    return 1;
  }
  const char payload[] = "#EXTM3U\ntest payload for the URI reader\n";
  if (write(fd, payload, sizeof(payload) - 1) != (ssize_t)(sizeof(payload) - 1)){
    std::cerr << "write failed" << std::endl;
    return 1;
  }
  ::close(fd);

  const size_t before = countFds();
  for (int i = 0; i < 10; ++i){
    HTTP::URIReader reader((std::string("file://") + path));
    if (!reader){
      std::cerr << "URIReader failed to open " << path << std::endl;
      unlink(path);
      return 1;
    }
    // Intentionally no close(): destruction alone must release everything
  }
  const size_t after = countFds();
  unlink(path);

  if (after != before){
    std::cerr << "URIReader leaked " << (after - before)
              << " file descriptor(s) across 10 scoped opens (before=" << before
              << ", after=" << after << ")" << std::endl;
    return 1;
  }
  return 0;
}
