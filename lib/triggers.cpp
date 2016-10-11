/// \page triggers Triggers
/// \brief Listing of all available triggers and their payloads.
/// MistServer reports certain occurances as configurable triggers to a URL or executable. This page describes the triggers system in full.
///
/// Triggers are the preferred way of responding to server events. Each trigger has a name and a payload, and may be stream-specific or global.
///
/// Triggers may be handled by a URL or an executable. If the handler contains ://, a HTTP URL is assumed. Otherwise, an executable is assumed.
/// If handled by an URL, a POST request is sent to the URL with an extra X-Trigger header containing the trigger name and the payload as the POST body.
/// If handled by an executable, it's started with the trigger name as its only argument, and the payload is piped into the executable over standard input.
/// 
/// Currently, all triggers are handled asynchronously and responses (if any) are completely ignored. In the future this may change.
/// 

#include <string.h>//for strncmp
#include "triggers.h"
#include "http_parser.h"//for sending http request
#include "defines.h"   //for FAIL_MSG and INFO_MSG
#include "procs.h"     //for StartPiped
#include "shared_memory.h"
#include "bitfields.h" //for strToBool

namespace Triggers{

///\brief Handles a trigger by sending a payload to a destination.
///\param trigger Trigger event type.
///\param value Destination. This can be an (HTTP)URL, or an absolute path to a binary/script
///\param payload This data will be sent to the destionation URL/program
///\param sync If true, handler is executed blocking and uses the response data.
///\returns String, false if further processing should be aborted.
std::string handleTrigger(const std::string &trigger, const std::string &value, const std::string &payload, int sync){  
  if(!value.size()){
    WARN_MSG("Trigger requested with empty destination");
    return "true";
  }
  INFO_MSG("Executing %s trigger: %s (%s)", trigger.c_str(), value.c_str(), sync ? "blocking" : "asynchronous");
  if (value.substr(0, 7) == "http://"){ //interpret as url
    std::string url = value.substr(value.find("://") + 3); //contains server+url
    std::string server = url.substr(0, url.find('/'));    
    int port=80;
    if (server.find(':') != std::string::npos){
      port = atoi(server.data() + server.find(':') + 1);
      server.erase(server.find(':'));
    }
    url = url.substr(url.find('/'));
    
    Socket::Connection conn(server,port,false);    
    HTTP::Parser H;
    H.url = url;    
    H.method = "POST";
    H.SetHeader("Host", server + ":" + JSON::Value((long long)port).toString());
    H.SetHeader("Content-Type", "application/x-www-form-urlencoded");
    H.SetHeader("X-Trigger", trigger);
    
    H.SetBody(payload);
    H.SendRequest(conn);
    H.Clean();
    if(sync){ //if sync!=0 wait for response
      while (conn && (!conn.spool() || !H.Read(conn))) {}
      conn.close();
      /// \todo Handle errors! 
      return H.body;
    }else{
      conn.close();
      return "true";
    }
  } else {    //send payload to stdin of newly forked process    
    int fdIn=-1;    
    int fdOut=-1;
    int fdErr=-1;
    
    char * argv[3];
    argv[0]=(char*)value.c_str();
    argv[1]=(char*)trigger.c_str();
    argv[2]=NULL;
    pid_t myProc = Util::Procs::StartPiped(argv, &fdIn,&fdOut,&fdErr); //start new process and return stdin file desc.
    if ( fdIn == -1 || fdOut == -1 || fdErr == -1 ){ //verify fdIn
      FAIL_MSG("StartPiped returned invalid fd");
      return "true";/// \todo Return true/false based on config here.
    }
    write(fdIn, payload.data(), payload.size()); 
    shutdown(fdIn, SHUT_RDWR);
    close(fdIn);
    
    if(sync){ //if sync!=0 wait for response
      while (Util::Procs::isActive(myProc)) {
        Util::sleep(100);
      }
      std::string ret;
      FILE * outFile = fdopen(fdOut, "r");
      char * fileBuf = 0;
      size_t fileBufLen = 0;
      while (!(feof(outFile) || ferror(outFile)) && (getline(&fileBuf, &fileBufLen, outFile) != -1)) {
        ret += fileBuf;
      }
      fclose(outFile);
      free(fileBuf);
      close(fdOut);
      close(fdErr);
      return ret;
    }
    close(fdOut);
    close(fdErr);
    return "true";
  }
}

static std::string empty;

///\brief Checks if one or more triggers are defined that should be handled for all streams (for a trigger event type)
///\param type Trigger event type.
///\return returns true, if so
///calls doTrigger with dryRun set to true
bool shouldTrigger(const std::string type){ //returns true if a trigger of the specified type should be handled for all streams
  static std::string empty;
  empty.clear();
  return doTrigger(type, empty, empty, true, empty);
}

///\brief returns true if a trigger of the specified type should be handled for a specified stream (, or entire server)
///\param type Trigger event type.
///\param streamName the stream to be handled
///\return returns true if so
///calls doTrigger with dryRun set to true
bool shouldTrigger(const std::string type, const std::string &streamName){ //returns true if a trigger of the specified type should be handled for a specified stream (, or entire server)
  empty.clear();
  return doTrigger(type, empty, streamName, true, empty);
}

///\brief handles triggers for a specific trigger event type, without a payload, server-wide
///\param type Trigger event type.
///\returns Boolean, false if further processing should be aborted.
///calls doTrigger with dryRun set to false
bool doTrigger(const std::string type){  
  empty.clear();
  return doTrigger(type, empty, empty, false, empty);
}

///\brief handles triggers for a specific trigger event type, with a payload, server-wide
///\param type Trigger event type.
///\param payload Trigger type-specific data
///\returns Boolean, false if further processing should be aborted.
///calls doTrigger with dryRun set to false
bool doTrigger(const std::string type, const std::string &payload){  
  empty.clear();
  return doTrigger(type, payload, empty, false, empty);
}

///\brief handles triggers for a specific trigger event type, with a payload, for a specified stream, and/or server-wide
///\param type Trigger event type.
///\param payload Trigger type-specific data
///\param streamName The name of a stream.
///\returns Boolean, false if further processing should be aborted.
///calls doTrigger with dryRun set to false
bool doTrigger(const std::string type, const std::string &payload, const std::string &streamName){
  empty.clear();
  return doTrigger(type, payload, streamName, false, empty);
}

///\brief 
///\param type Trigger event type
///\param payload Trigger type-specific data
///\param streamName Name of a stream to check for stream-specific triggers
///\param dryRun determines the mode of operation for this function
///\param response Returns the last received response by reference
///\returns Boolean, false if further processing should be aborted
///This function attempts to open and parse a shared memory page with the config for a trigger event type, in order to parse the triggers defined for that trigger event type.
///The function can be used for two separate purposes, determined by the value of dryRun
///-if this function is called with dryRun==true (for example, from a handleTrigger function), the return value will be true, if at least one trigger should be handled for the requested type/stream.
///this can be used to make sure a payload is only generated if at least one trigger should be handled.
///-if this function is called with dryRun==false (for example, from one of the overloaded doTrigger functions), handleTrigger is called for all configured triggers. In that case, the return value does not matter, it will probably be false in all cases.
bool doTrigger(const std::string type, const std::string &payload, const std::string &streamName, bool dryRun, std::string & response){    
  //open SHM page for this type:
  char thisPageName[NAME_BUFFER_SIZE];
  snprintf(thisPageName, NAME_BUFFER_SIZE, SHM_TRIGGER, type.c_str());
  IPC::sharedPage typePage(thisPageName, 8*1024, false, false);    
  if(!typePage.mapped){ //page doesn't exist?
    HIGH_MSG("No triggers for %s defined: list does not exist", type.c_str());
    return false;
  }
  
  char* bytepos = typePage.mapped; //not checking page size. will probably be fine.  
  char* startBytepos=bytepos;
  unsigned int totalLen =  ((unsigned int *)bytepos)[0];
  bool retVal = true;
  VERYHIGH_MSG("Parsing %lu bytes of triggers for %s, stream: %s", totalLen, type.c_str(), streamName.c_str());
  std::string uri;
  unsigned int sync=0;
  
  while( totalLen != 0 && bytepos < typePage.mapped + typePage.len ){
    unsigned int uriLen =  ((unsigned int *)bytepos)[1];
    bytepos+=4+4;
    uri=std::string(bytepos,uriLen);
    bytepos+=uriLen;
    sync=bytepos[0];
    bytepos++;
    
    bool isHandled = false;
    if(totalLen>((unsigned int)(bytepos-startBytepos))){
      while( totalLen>((unsigned int)(bytepos-startBytepos)) ){
        unsigned int stringLen=((unsigned int *)bytepos)[0];
        bytepos+=4;
        size_t splitter = streamName.find_first_of("+ ");
        if ((streamName.size() == stringLen || splitter == stringLen) && strncmp(bytepos, streamName.c_str(), stringLen) == 0){
          isHandled = true;
        }
        bytepos+=stringLen;
      }
      if (!streamName.size()){
        isHandled = true;
      }
    } else if(totalLen==((unsigned int)(bytepos-startBytepos))){
      //no streams explicitly defined for this trigger, return true for all streams.
      isHandled = true;
    }
   
    if (isHandled){
      VERYHIGH_MSG("%s trigger handled by %s", type.c_str(), uri.c_str());
      if(dryRun){
        return true;
      }
      response = handleTrigger(type,uri,payload,sync); //do it.
      retVal &= Util::stringToBool(response);
    }

    if(totalLen!=((unsigned int)(bytepos-startBytepos))){ //if this is not the case, something bad might have happened.
      ERROR_MSG("Error in %s trigger, totalLen: %d current position from startBytepos: %d", type.c_str(),totalLen, (unsigned int)(bytepos-startBytepos));      
      break; //stop reading hypothetical garbage
    }
    
    startBytepos=startBytepos+totalLen; //init next iteration
    bytepos=startBytepos;
    totalLen = ((unsigned int *)bytepos)[0]; //read next size
  }
  
  if (dryRun){
    return false;
  }else{
    return retVal;
  }
}

} //end namespace Controller

