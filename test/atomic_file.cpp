#include <mist/socket.h>
#include <mist/util.h>

#include <atomic>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

static bool expect(bool condition, const std::string &message){
  if (condition){return true;}
  std::cerr << "FAIL: " << message << std::endl;
  return false;
}

static std::string readFile(const std::string &path){
  std::ifstream input(path.c_str(), std::ios::in | std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

static bool hasTempFiles(const std::string &directory, const std::string &prefix){
  DIR *dir = opendir(directory.c_str());
  if (!dir){return true;}
  bool found = false;
  while (dirent *entry = readdir(dir)){
    if (std::string(entry->d_name).find(prefix) == 0){
      found = true;
      break;
    }
  }
  closedir(dir);
  return found;
}

int main(){
  char directoryTemplate[] = "/tmp/mist-atomic-file-XXXXXX";
  char *directoryValue = mkdtemp(directoryTemplate);
  if (!expect(directoryValue != 0, "create temporary directory")){return 1;}

  const std::string directory(directoryValue);
  const std::string path = directory + "/rolling.m3u8";
  const std::string lockPath = path + ".lock";
  const std::string tempPrefix = "rolling.m3u8.tmp.";
  const std::string oldData = "#EXTM3U\n#GENERATION:old\n" + std::string(2 * 1024 * 1024, 'O') + "\n#END:old\n";
  const std::string newData = "#EXTM3U\n#GENERATION:new\n#END:new\n";
  bool ok = true;

  ok &= expect(Util::atomicWriteFile(path, oldData), "initial atomic publish succeeds");
  ok &= expect(readFile(path) == oldData, "initial contents are complete");

  int oldFd = open(path.c_str(), O_RDONLY);
  ok &= expect(oldFd >= 0, "open old generation");
  void *oldMap = MAP_FAILED;
  if (oldFd >= 0){oldMap = mmap(0, oldData.size(), PROT_READ, MAP_SHARED, oldFd, 0);}
  ok &= expect(oldMap != MAP_FAILED, "map old generation");
  ok &= expect(Util::atomicWriteFile(path, newData), "replacement publish succeeds");
  ok &= expect(readFile(path) == newData, "new readers see complete replacement");
  if (oldMap != MAP_FAILED){
    const char *mapped = static_cast<const char *>(oldMap);
    ok &= expect(mapped[oldData.size() - 2] == 'd', "existing mapping retains complete old inode");
    munmap(oldMap, oldData.size());
  }
  if (oldFd >= 0){close(oldFd);}

  chmod(path.c_str(), 0640);
  ok &= expect(Util::atomicWriteFile(path, oldData), "mode-preserving replacement succeeds");
  struct stat publishedStats;
  ok &= expect(stat(path.c_str(), &publishedStats) == 0, "stat replaced file");
  ok &= expect((publishedStats.st_mode & 07777) == 0640, "replacement preserves file mode");

  int heldLock = open(lockPath.c_str(), O_RDWR | O_CREAT, 0666);
  ok &= expect(heldLock >= 0 && flock(heldLock, LOCK_EX | LOCK_NB) == 0, "hold stable writer lock");
  ok &= expect(!Util::atomicWriteFile(path, newData), "contending writer fails without truncating target");
  ok &= expect(readFile(path) == oldData, "lock contention preserves previous target");
  if (heldLock >= 0){close(heldLock);}

  const std::string generationA = "#EXTM3U\n#GENERATION:A\n" + std::string(1024 * 1024, 'A') + "\n#END:A\n";
  const std::string generationB = "#EXTM3U\n#GENERATION:B\n" + std::string(256 * 1024, 'B') + "\n#END:B\n";
  ok &= expect(Util::atomicWriteFile(path, generationA), "initialize stress target");
  std::atomic<bool> writing(true);
  std::atomic<bool> readerOk(true);
  std::thread writer([&](){
    for (size_t i = 0; i < 24; ++i){
      if (!Util::atomicWriteFile(path, (i % 2) ? generationA : generationB)){
        readerOk = false;
        break;
      }
    }
    writing = false;
  });
  std::thread reader([&](){
    size_t reads = 0;
    while (writing || reads < 100){
      std::string snapshot = readFile(path);
      if (snapshot != generationA && snapshot != generationB){
        readerOk = false;
        break;
      }
      ++reads;
    }
  });
  writer.join();
  reader.join();
  ok &= expect(readerOk, "concurrent readers observe only complete generations");

  const std::string beforeAppend = readFile(path);
  Socket::Connection appendConnection;
  ok &= expect(Util::externalWriter(path, appendConnection, true), "existing append writer still opens");
  if (appendConnection){
    appendConnection.SendNow("#APPEND\n");
    appendConnection.close();
  }
  ok &= expect(readFile(path) == beforeAppend + "#APPEND\n", "existing append behavior remains unchanged");
  ok &= expect(!hasTempFiles(directory, tempPrefix), "successful writes leave no temporary files");

  unlink(path.c_str());
  unlink(lockPath.c_str());
  rmdir(directory.c_str());
  if (!ok){return 1;}
  std::cout << "PASS: atomic file publication" << std::endl;
  return 0;
}
