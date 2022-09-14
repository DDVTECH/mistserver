#include "controller_external_writers.h"
#include "controller_statistics.h"
#include "controller_storage.h"
#include <mist/downloader.h>
#include <mist/bitfields.h>
#include <mist/config.h>
#include <mist/json.h>
#include <mist/stream.h>
#include <mist/triggers.h>
#include <string>

namespace Controller{
  // Size of the shared memory page
  static uint64_t pageSize = EXTWRITERS_INITSIZE;

  /// \brief Writes external writers from the server config to shared memory
  void externalWritersToShm(){
    uint64_t writerCount = Controller::Storage["extwriters"].size();
    IPC::sharedPage writersPage(EXTWRITERS, pageSize, false, false);
    // If we have an existing page, set the reload flag
    if (writersPage.mapped){
      writersPage.master = true;
      Util::RelAccX binAccx = Util::RelAccX(writersPage.mapped, false);
      // Check if we need a bigger page
      uint64_t sizeRequired = binAccx.getOffset() + binAccx.getRSize() * writerCount;
      if (pageSize < sizeRequired){pageSize = sizeRequired;}
      binAccx.setReload();
    }
    // Close & unlink any existing page and create a new one
    writersPage.close();
    writersPage.init(EXTWRITERS, pageSize, true, false);
    Util::RelAccX exwriAccx = Util::RelAccX(writersPage.mapped, false);
    exwriAccx = Util::RelAccX(writersPage.mapped, false);
    exwriAccx.addField("name", RAX_32STRING);
    exwriAccx.addField("cmdline", RAX_256STRING);
    exwriAccx.addField("protocols", RAX_NESTED, RAX_64STRING * writerCount * 8);
    // Set amount of records that can fit and how many will be used
    uint64_t reqCount = (pageSize - exwriAccx.getOffset()) / exwriAccx.getRSize();
    exwriAccx.setRCount(reqCount);
    exwriAccx.setPresent(reqCount);
    exwriAccx.setEndPos(writerCount);
    // Do the same for the nested protocol field
    uint64_t index = 0;
    jsonForEach(Controller::Storage["extwriters"], it){
      std::string name = (*it)[0u].asString();
      std::string cmdline = (*it)[1u].asString();
      exwriAccx.setString("name", name, index);
      exwriAccx.setString("cmdline", cmdline, index);
      // Create nested field for source match
      uint8_t protocolCount = (*it)[2u].size();
      Util::RelAccX protocolAccx = Util::RelAccX(exwriAccx.getPointer("protocols", index), false);
      protocolAccx.addField("protocol", RAX_64STRING);
      protocolAccx.setRCount(protocolCount);
      protocolAccx.setPresent(protocolCount);
      protocolAccx.setEndPos(protocolCount);
      uint8_t binIt = 0;
      jsonForEach((*it)[2u], protIt){
        std::string thisProtocol = (*protIt).asString();
        protocolAccx.setString("protocol", thisProtocol, binIt);
        binIt++;
      }
      index++;
      protocolAccx.setReady();
    }
    exwriAccx.setReady();
    // Leave the page in memory after returning
    writersPage.master = false;
  }

  /// \brief Adds a new generic writer binary to the server config
  /// The request should contain:
  /// - name: name given to this binary in order to edit/remove it's entry in Mists config
  /// - cmdline: command line including arguments
  /// - supported URL protocols: used to identify for what targets we need to run the executable
  void addExternalWriter(JSON::Value &request){
    std::string name;
    std::string cmdline;
    JSON::Value protocols;
    bool isNew = true;
    if (request.isArray()){
      if(request.size() == 4){
        name = request[0u].asString();
        cmdline = request[1u].asString();
        protocols = request[2u];
      }else{
        ERROR_MSG("Cannot add external writer, as the request contained %u variables. Required variables are: name, cmdline and protocols", request.size());
        return;
      }
    }else{
      name = request["name"].asString();
      cmdline = request["cmdline"].asString();
      protocols = request["protocols"];
    }
    //convert protocols from string to array if needed
    if (protocols.isString()){protocols.append(protocols.asString());}
    if (!name.size()){
      ERROR_MSG("Blank or missing name in request");
      return;
    }
    if (!cmdline.size()){
      ERROR_MSG("Blank or missing cmdline in request");
      return;
    }
    if (!protocols.size()){
      ERROR_MSG("Missing protocols in request");
      return;
    }
    if (name.size() > 31){
      name = name.substr(0, 31);
      WARN_MSG("Maximum name length is 31 characters, truncating name to '%s'", name.c_str());
    }
    if (cmdline.size() > 255){
      cmdline.erase(255);
      WARN_MSG("Maximum cmdline length is 255 characters, truncating cmdline to '%s'", cmdline.c_str());
    }
    jsonForEach(protocols, protIt){
      if ((*protIt).size() > 63){
        (*protIt) = (*protIt).asString().substr(0, 63);
        WARN_MSG("Maximum protocol length is 63 characters, truncating protocol to '%s'", (*protIt).asStringRef().c_str());
      }
    }

    // Check if we have an existing variable with the same name to modify
    jsonForEach(Controller::Storage["extwriters"], it){
      if ((*it)[0u].asString() == name){
        INFO_MSG("Modifying existing external writer '%s'", name.c_str());
        (*it)[1u] = cmdline;
        (*it)[2u] = protocols;
        isNew = false;
        break;
      }
    }
    // Else push a new custom variable to the list
    if (isNew){
      INFO_MSG("Adding new external writer '%s'", name.c_str());
      JSON::Value thisVar;
      thisVar.append(name);
      thisVar.append(cmdline);
      thisVar.append(protocols);
      Controller::Storage["extwriters"].append(thisVar);
    }
    // Modify shm
    externalWritersToShm();
  }

  /// \brief Fills output with all defined external writers
  void listExternalWriters(JSON::Value &output){
    output = Controller::Storage["extwriters"];
  }

  /// \brief Removes the external writer name contained in the request from shm and the sever config
  void removeExternalWriter(const JSON::Value &request){
    std::string name;
    if (request.isString()){
      name = request.asStringRef();
    }else if (request.isArray()){
      name = request[0u].asStringRef();
    }else if (request.isMember("name")){
      name = request["name"].asStringRef();
    }
    if (!name.size()){
      WARN_MSG("Aborting request to remove an external writer, as no name was given");
      return;
    }
    // Modify config
    jsonForEach(Controller::Storage["extwriters"], it){
      if ((*it)[0u].asString() == name){it.remove();}
    }
    // Modify shm
    externalWritersToShm();
  }
}// namespace Controller

