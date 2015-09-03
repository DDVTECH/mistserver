#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h> 
#include <sys/wait.h>
#include <unistd.h>
#include <semaphore.h>

#include INPUTTYPE 
#include <mist/config.h>
#include <mist/defines.h>
#include <mist/stream.h>

int main(int argc, char * argv[]) {
  Util::Config conf(argv[0]);
  mistIn conv(&conf);
  if (conf.parseArgs(argc, argv)) {
    if (conf.getBool("json")) {
      conv.run();
      return 0;
    }
    
    std::string strm = conf.getString("streamname");
    if (strm.find_first_of("+ ") == std::string::npos){
      FAIL_MSG("Folder input requires a + or space in the stream name.");
      return 1;
    }
    
    std::string folder = conf.getString("input");
    if (folder[folder.size() - 1] != '/'){
      FAIL_MSG("Input path must end in a forward slash.");
      return 1;
    }
    std::string folder_noslash = folder.substr(0, folder.size() - 1);
    struct stat fileCheck;
    if (stat(folder_noslash.c_str(), &fileCheck) != 0 || !S_ISDIR(fileCheck.st_mode)){
      FAIL_MSG("Folder input requires a folder as input.");
      return 1;
    }
    
    std::string path = folder + strm.substr(strm.find_first_of("+ ")+1);
    if (stat(path.c_str(), &fileCheck) != 0 || S_ISDIR(fileCheck.st_mode)){
      FAIL_MSG("File not found: %s", path.c_str());
      return 1;
    }
    
    Util::startInput(strm, path, false);
    return 1;
  }
  return 1;
}


