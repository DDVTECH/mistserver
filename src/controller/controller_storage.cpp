#include <sys/stat.h>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <mist/timing.h>
#include <mist/shared_memory.h>
#include <mist/defines.h>
#include "controller_storage.h"
#include "controller_capabilities.h"
#include <mist/triggers.h>//LTS

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
    strftime(buffer,100,"%F %H:%M:%S",timeinfo);
    std::cout << "[" << buffer << "] " << kind << ": " << message << std::endl;
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
  /// \triggers 
  /// The `"SYSTEM_START"` trigger is global, and is ran as soon as the server configuration is first stable. It has no payload. If cancelled, the system immediately shuts down again.
  /// \n
  /// The `"SYSTEM_CONFIG"` trigger is global, and is ran every time the server configuration is updated. Its payload is the new configuration in JSON format. This trigger cannot be cancelled.
  void writeConfig(){
    static JSON::Value writeConf;
    bool changed = false;
    if (writeConf["config"] != Storage["config"]){
      writeConf["config"] = Storage["config"];
      VERYHIGH_MSG("Saving new config because of edit in server config structure");
      changed = true;
    }
    if (writeConf["streams"] != Storage["streams"]){
      writeConf["streams"] = Storage["streams"];
      VERYHIGH_MSG("Saving new config because of edit in streams");
      changed = true;
    }
    if (writeConf["capabilities"] != capabilities){
      writeConf["capabilities"] = capabilities;
      VERYHIGH_MSG("Saving new config because of edit in capabilities");
      changed = true;
    }
    if (!changed){return;}//cancel further processing if no changes

    static IPC::sharedPage mistConfOut("!mistConfig", DEFAULT_CONF_PAGE_SIZE, true);
    IPC::semaphore configLock("!mistConfLock", O_CREAT | O_RDWR, ACCESSPERMS, 1);
    //lock semaphore
    configLock.wait();
    //write config
    std::string temp = writeConf.toPacked();
    memcpy(mistConfOut.mapped, temp.data(), std::min(temp.size(), (size_t)mistConfOut.len));
    //unlock semaphore
    configLock.post();

    /*LTS-START*/
    static std::map<std::string,IPC::sharedPage> pageForType;     //should contain one page for every trigger type    
    char tmpBuf[NAME_BUFFER_SIZE];
    
    //for all shm pages that hold triggers  
    pageForType.clear();
    
    if( writeConf["config"]["triggers"].size() ){//if triggers are defined...
      jsonForEach(writeConf["config"]["triggers"], it){//for all types defined in config        
        snprintf(tmpBuf,NAME_BUFFER_SIZE,SHM_TRIGGER,(it.key()).c_str());   //create page
        pageForType[it.key()].init(tmpBuf, 8*1024, true, false);//  todo: should this be false/why??                          
        char * bytePos=pageForType[it.key()].mapped;
        
        //write data to page
        jsonForEach(*it, triggIt){ //for all defined
          unsigned int tmpUrlSize=(*triggIt)[(unsigned int) 0].asStringRef().size();
          unsigned int tmpStreamNames=0;// (*triggIt)[2ul].packedSize();
          std::string namesArray="";
          
          if( (triggIt->size() >= 3) && (*triggIt)[2ul].size()){
            jsonForEach((*triggIt)[2ul], shIt){
              unsigned int tmpLen=shIt->asString().size();
              tmpStreamNames+= 4+tmpLen;
              //INFO_MSG("adding string: %s len: %d",  shIt->asString().c_str() , tmpLen  );              
              ((unsigned int*)tmpBuf)[0] = tmpLen;          //NOTE: namesArray may be replaced by writing directly to tmpBuf.
              namesArray.append(tmpBuf,4);
              namesArray.append(shIt->asString());
            }
          }
          unsigned int totalLen=9+tmpUrlSize+tmpStreamNames;     //4Btotal len, 4Burl len ,XB tmpurl, 1B sync , XB tmpstreamnames
          
          if(totalLen > (pageForType[it.key()].len-(bytePos-pageForType[it.key()].mapped)) ){ //check if totalLen fits on the page            
            ERROR_MSG("trigger does not fit on page. size: %d bytes left on page:  %d skipping.",totalLen,(pageForType[it.key()].len-(bytePos-pageForType[it.key()].mapped))); //doesnt fit
            continue;
          }
          
          ((unsigned int*)bytePos)[0] = totalLen;
          bytePos+=4;
          ((unsigned int*)bytePos)[0] = tmpUrlSize;          
          bytePos+=4;
          memcpy(bytePos, (*triggIt)[(unsigned int) 0].asStringRef().data(), (*triggIt)[(unsigned int) 0].asStringRef().size());
          bytePos+=(*triggIt)[(unsigned int) 0].asStringRef().size();
          (bytePos++)[0] = (*triggIt)[1ul].asBool() ? '\001' : '\000';          
          if(tmpStreamNames){
            memcpy(bytePos,namesArray.data(),tmpStreamNames); //contains a string of 4Blen,XBstring pairs
            bytePos+=tmpStreamNames;
          }
        }
      }      
    }
  
  static bool serverStartTriggered;  
  if(!serverStartTriggered){    
    if (!Triggers::doTrigger("SYSTEM_START")){
      conf.is_active = false;
    }
    serverStartTriggered++;
  }
  if (Triggers::shouldTrigger("SYSTEM_CONFIG")){
    std::string payload = writeConf.toString();
    Triggers::doTrigger("SYSTEM_CONFIG", payload);
  }
  /*LTS-END*/
 
  }
}


/*NOTES:
4B size (total size of entry 9B+XB(URL)+ 0..XB(nameArrayLen)) (if 0x00, stop reading)
4B url_len
XB url
1B async 
for(number of strings)    
  4B stringLen
  XB string
)
*/

