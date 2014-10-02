#include <sys/stat.h>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <mist/timing.h>
#include <mist/shared_memory.h>
#include "controller_storage.h"
#include "controller_capabilities.h"

///\brief Holds everything unique to the controller.
namespace Controller {

  Util::Config conf;
  JSON::Value Storage; ///< Global storage of data.
  tthread::mutex configMutex;
  tthread::mutex logMutex;
  ///\brief Store and print a log message.
  ///\param kind The type of message.
  ///\param message The message to be logged.
  void Log(std::string kind, std::string message){
    tthread::lock_guard<tthread::mutex> guard(logMutex);
    JSON::Value m;
    m.append(Util::epoch());
    m.append(kind);
    m.append(message);
    Storage["log"].append(m);
    Storage["log"].shrink(100); //limit to 100 log messages
    time_t rawtime;
    struct tm * timeinfo;
    char buffer [100];
    time (&rawtime);
    timeinfo = localtime (&rawtime);
    strftime (buffer,100,"%b %d %Y -- %H:%M",timeinfo);
    std::cout << "(" << buffer << ") [" << kind << "] " << message << std::endl;
  }

  ///\brief Write contents to Filename
  ///\param Filename The full path of the file to write to.
  ///\param contents The data to be written to the file.
  bool WriteFile(std::string Filename, std::string contents){
    std::ofstream File;
    File.open(Filename.c_str());
    File << contents << std::endl;
    File.close();
    return File.good();
  }
  
  /// Handles output of a Mist application, detecting and catching debug messages.
  /// Debug messages are automatically converted into Log messages.
  /// Closes the file descriptor on read error.
  /// \param err File descriptor of the stderr output of the process to monitor.
  void handleMsg(void * err){
    char buf[1024];
    FILE * output = fdopen((long long int)err, "r");
    while (fgets(buf, 1024, output)){
      unsigned int i = 0;
      while (i < 9 && buf[i] != '|' && buf[i] != 0){
        ++i;
      }
      unsigned int j = i;
      while (j < 1024 && buf[j] != '\n' && buf[j] != 0){
        ++j;
      }
      buf[j] = 0;
      if(i < 9){
        buf[i] = 0;
        Log(buf,buf+i+1);
      }else{
        printf("%s", buf);
      }
    }
    fclose(output);
    close((long long int)err);
  }
  
  /// Writes the current config to shared memory to be used in other processes
  void writeConfig(){
    JSON::Value writeConf;
    writeConf["config"] = Storage["config"];
    writeConf["streams"] = Storage["streams"];
    writeConf["capabilities"] = capabilities;

    static IPC::sharedPage mistConfOut("!mistConfig", 4*1024*1024, true);
    IPC::semaphore configLock("!mistConfLock", O_CREAT | O_RDWR, ACCESSPERMS, 1);
    //lock semaphore
    configLock.wait();
    //write config
    std::string temp = writeConf.toPacked();
    memcpy(mistConfOut.mapped, temp.data(), std::min(temp.size(), (unsigned long)mistConfOut.len));
    //unlock semaphore
    configLock.post();
  }
  
}
