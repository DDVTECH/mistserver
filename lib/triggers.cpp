/// \page triggers Triggers
/// \brief Listing of all available triggers and their payloads.
/// MistServer reports certain occurances as configurable triggers to a URL or executable. This page
/// describes the triggers system in full.
///
/// Triggers are the preferred way of responding to server events. Each trigger has a name and a
/// payload, and may be stream-specific or global.
///
/// Triggers may be handled by a URL or an executable. If the handler contains ://, a HTTP URL is
/// assumed. Otherwise, an executable is assumed. If handled by an URL, a POST request is sent to
/// the URL with an extra X-Trigger header containing the trigger name and the payload as the POST
/// body. If handled by an executable, it's started with the trigger name as its only argument, and
/// the payload is piped into the executable over standard input.
///
/// Currently, all triggers are handled asynchronously and responses (if any) are completely
/// ignored. In the future this may change.
///

#include "bitfields.h"  //for strToBool
#include "defines.h"    //for FAIL_MSG and INFO_MSG
#include "downloader.h" //for sending http request
#include "procs.h"      //for StartPiped
#include "shared_memory.h"
#include "timing.h"
#include "triggers.h"
#include "util.h"
#include "json.h"
#include "stream.h"
#include <string.h> //for strncmp

namespace Triggers{

  static void submitTriggerStat(const std::string trigger, uint64_t millis, bool ok){
    JSON::Value j;
    j["trigger_stat"]["name"] = trigger;
    j["trigger_stat"]["ms"] = Util::bootMS() - millis;
    j["trigger_stat"]["ok"] = ok;
    Util::sendUDPApi(j);
  }

  ///\brief Handles a trigger by sending a payload to a destination.
  ///\param trigger Trigger event type.
  ///\param value Destination. This can be an (HTTP)URL, or an absolute path to a binary/script
  ///\param payload This data will be sent to the destionation URL/program
  ///\param sync If true, handler is executed blocking and uses the response data.
  ///\returns String, false if further processing should be aborted.
  std::string handleTrigger(const std::string &trigger, const std::string &value,
                            const std::string &payload, int sync, const std::string &defaultResponse){
    uint64_t tStartMs = Util::bootMS();
    if (!value.size()){
      WARN_MSG("Trigger requested with empty destination");
      return "true";
    }
    INFO_MSG("Executing %s trigger: %s (%s)", trigger.c_str(), value.c_str(), sync ? "blocking" : "asynchronous");
    if (value.substr(0, 7) == "http://" || value.substr(0, 8) == "https://"){// interpret as url
      HTTP::Downloader DL;
      DL.setHeader("X-Trigger", trigger);
      std::string iid = Util::getGlobalConfig("iid").asString();
      if (iid.size()){
        DL.setHeader("X-Instance", iid);
      }
      std::string hrn = Util::getGlobalConfig("hrn").asString();
      if (hrn.size()){
        DL.setHeader("X-Name", hrn);
      }
      DL.setHeader("X-Trigger-UUID", getenv("MIST_TUUID"));
      DL.setHeader("X-Trigger-UnixMillis", getenv("MIST_TIME"));
      DL.setHeader("Date", getenv("MIST_DATE"));
      DL.setHeader("Content-Type", "text/plain");
      HTTP::URL url(value);
      if (DL.post(url, payload, sync) && (!sync || DL.isOk())){
        submitTriggerStat(trigger, tStartMs, true);
        return DL.data();
      }
      FAIL_MSG("Trigger failed to execute (%s), using default response: %s",
               DL.getStatusText().c_str(), defaultResponse.c_str());
      submitTriggerStat(trigger, tStartMs, false);
      return defaultResponse;
    }else{// send payload to stdin of newly forked process
      int fdIn = -1;
      int fdOut = -1;
      int fdErr = 2;

      char *argv[3];
      argv[0] = (char *)value.c_str();
      argv[1] = (char *)trigger.c_str();
      argv[2] = NULL;
      setenv("MIST_TRIGGER", trigger.c_str(), 1);
      setenv("MIST_TRIG_DEF", defaultResponse.c_str(), 1);
      std::string iid = Util::getGlobalConfig("iid").asString();
      if (iid.size()){
        setenv("MIST_INSTANCE", iid.c_str(), 1);
      }
      std::string hrn = Util::getGlobalConfig("hrn").asString();
      if (hrn.size()){
        setenv("MIST_NAME", hrn.c_str(), 1);
      }
      pid_t myProc = Util::Procs::StartPiped(argv, &fdIn, &fdOut, &fdErr); // start new process and return stdin file desc.
      unsetenv("MIST_TRIGGER");
      unsetenv("MIST_TRIG_DEF");
      unsetenv("MIST_INSTANCE");
      unsetenv("MIST_NAME");
      if (fdIn == -1 || fdOut == -1 || myProc == -1){
        FAIL_MSG("Could not execute trigger executable: %s", strerror(errno));
        submitTriggerStat(trigger, tStartMs, false);
        return defaultResponse;
      }
      write(fdIn, payload.data(), payload.size());
      shutdown(fdIn, SHUT_RDWR);
      close(fdIn);

      if (sync){// if sync!=0 wait for response
        uint32_t counter = 0;
        while (Util::Procs::isActive(myProc) && counter < 150){
          Util::sleep(100);
          ++counter;
          if (counter >= 100){
            if (counter == 100){FAIL_MSG("Trigger taking too long - killing process");}
            if (counter >= 140){
              Util::Procs::Stop(myProc);
            }else{
              Util::Procs::Murder(myProc);
            }
          }
        }
        std::string ret;
        FILE *outFile = fdopen(fdOut, "r");
        char *fileBuf = 0;
        size_t fileBufLen = 0;
        while (!(feof(outFile) || ferror(outFile)) && (getline(&fileBuf, &fileBufLen, outFile) != -1)){
          ret += fileBuf;
        }
        fclose(outFile);
        free(fileBuf);
        close(fdOut);
        if (counter >= 150 && !ret.size()){
          WARN_MSG("Using default trigger response: %s", defaultResponse.c_str());
          submitTriggerStat(trigger, tStartMs, false);
          return defaultResponse;
        }
        submitTriggerStat(trigger, tStartMs, true);
        return ret;
      }
      close(fdOut);
      submitTriggerStat(trigger, tStartMs, true);
      return defaultResponse;
    }
  }

  static std::string usually_empty;

  ///\brief returns true if a trigger of the specified type should be handled for a specified stream
  ///(, or entire server) \param type Trigger event type. \param streamName the stream to be handled
  ///\return returns true if so
  /// calls doTrigger with dryRun set to true
  /// returns true if a trigger of the specified type should be
  /// handled for a specified stream (, or entire server)
  bool shouldTrigger(const std::string &type, const std::string &streamName,
                     bool paramsCB(const char *, const void *), const void *extraParam){
    usually_empty.clear();
    return doTrigger(type, empty, streamName, true, usually_empty, paramsCB, extraParam);
  }

  ///\brief handles triggers for a specific trigger event type, with a payload, for a specified
  /// stream, and/or server-wide \param type Trigger event type. \param payload Trigger
  /// type-specific data \param streamName The name of a stream. \returns Boolean, false if further
  /// processing should be aborted.
  /// calls doTrigger with dryRun set to false
  bool doTrigger(const std::string &type, const std::string &payload, const std::string &streamName){
    usually_empty.clear();
    return doTrigger(type, payload, streamName, false, usually_empty);
  }

  ///\brief
  ///\param type Trigger event type
  ///\param payload Trigger type-specific data
  ///\param streamName Name of a stream to check for stream-specific triggers
  ///\param dryRun determines the mode of operation for this function
  ///\param response Returns the last received response by reference
  ///\returns Boolean, false if further processing should be aborted
  /// This function attempts to open and parse a shared memory page with the config for a trigger
  /// event type, in order to parse the triggers defined for that trigger event type. The function
  /// can be used for two separate purposes, determined by the value of dryRun
  ///-if this function is called with dryRun==true (for example, from a handleTrigger function), the
  /// return value will be true, if at least one trigger should be handled for the requested
  /// type/stream.
  /// this can be used to make sure a payload is only generated if at least one trigger should be
  /// handled.
  ///-if this function is called with dryRun==false (for example, from one of the overloaded
  /// doTrigger functions), handleTrigger is called for all configured triggers. In that case, the
  /// return value does not matter, it will probably be false in all cases.
  bool doTrigger(const std::string &type, const std::string &payload, const std::string &streamName,
                 bool dryRun, std::string &response, bool paramsCB(const char *, const void *),
                 const void *extraParam){
    // open SHM page for this type:
    char thisPageName[NAME_BUFFER_SIZE];
    snprintf(thisPageName, NAME_BUFFER_SIZE, SHM_TRIGGER, type.c_str());
    IPC::sharedPage typePage(thisPageName, 8 * 1024, false, false);
    if (!typePage.mapped){// page doesn't exist?
      VERYHIGH_MSG("No triggers for %s found", type.c_str());
      return false;
    }

    Util::RelAccX trigs(typePage.mapped, false);
    if (!trigs.isReady()){
      WARN_MSG("No triggers for %s: list not ready", type.c_str());
      return false;
    }

    {
      std::string uuid = Util::generateUUID();
      setenv("MIST_TUUID", uuid.c_str(), 1);
    }

    {
      uint64_t currTime = Util::unixMS();
      std::string time = JSON::Value(currTime).asString();
      setenv("MIST_TIME", time.c_str(), 1);
      std::string date = Util::getDateString(currTime / 1000);
      setenv("MIST_DATE", date.c_str(), 1);
    }

    size_t splitter = streamName.find_first_of("+ ");
    bool retVal = true;

    for (uint32_t i = 0; i < trigs.getRCount(); ++i){
      std::string uri = std::string(trigs.getPointer("url", i));
      uint8_t sync = trigs.getInt("sync", i);

      char *strPtr = trigs.getPointer("streams", i);
      uint32_t pLen = trigs.getSize("streams");
      uint32_t bPos = 0;

      bool isHandled = !streamName.size();
      while (bPos + 4 < pLen){
        uint32_t stringLen = ((unsigned int *)(strPtr + bPos))[0];
        if (bPos + 4 + stringLen > pLen || !stringLen){break;}
        if ((streamName.size() == stringLen || splitter == stringLen) &&
            strncmp(strPtr + bPos + 4, streamName.data(), stringLen) == 0){
          isHandled = true;
          break;
        }
        // Tag-based? Check tags for this stream
        if (strPtr[bPos + 4] == '#'){
          std::set<std::string> tags = Util::streamTags(streamName);
          if (tags.count(std::string(strPtr + bPos + 5, stringLen - 1))){
            isHandled = true;
            break;
          }
        }
        bPos += stringLen + 4;
      }
      // no streams explicitly defined for this trigger, return true for all streams.
      if (bPos <= 4){isHandled = true;}

      if (isHandled && paramsCB){
        isHandled = paramsCB(trigs.getPointer("params", i), extraParam);
      }
      std::string defaultResponse = trigs.getPointer("default", i);
      if (!defaultResponse.size()){defaultResponse = "true";}

      if (isHandled){
        VERYHIGH_MSG("%s trigger handled by %s", type.c_str(), uri.c_str());
        if (dryRun){return true;}
        if (sync){
          response = handleTrigger(type, uri, payload, sync, defaultResponse); // do it.
          retVal &= Util::stringToBool(response);
        }else{
          std::string unused_response = handleTrigger(type, uri, payload, sync, defaultResponse); // do it.
          retVal &= Util::stringToBool(unused_response);
        }
      }
    }

    unsetenv("MIST_TUUID");
    unsetenv("MIST_TIME");
    unsetenv("MIST_DATE");

    if (dryRun){
      return false;
    }else{
      return retVal;
    }
  }
}// namespace Triggers
