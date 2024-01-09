#include <iostream>
#include <mist/shared_memory.h>
#include <mist/util.h>
#include <mist/stream.h>
#include <mist/procs.h>
#include <mist/comms.h>
#include <mist/config.h>

const char * getStateString(uint8_t state){
  switch (state){
  case STRMSTAT_OFF: return "Stream is offline";
  case STRMSTAT_INIT: return "Stream is initializing";
  case STRMSTAT_BOOT: return "Stream is booting";
  case STRMSTAT_WAIT: return "Stream is waiting for data";
  case STRMSTAT_READY: return "Stream is online";
  case STRMSTAT_SHUTDOWN: return "Stream is shutting down";
  case STRMSTAT_INVALID: return "Stream status is invalid?!";
  default: return "Stream status is unknown?!";
  }
}

/// Gets a PID from a shared memory page, if it exists
uint64_t getPidFromPage(const char * pagePattern){
  char pageName[NAME_BUFFER_SIZE];
  snprintf(pageName, NAME_BUFFER_SIZE, pagePattern, Util::streamName);
  IPC::sharedPage pidPage(pageName, 8, false, false);
  if (pidPage){
    return *(uint64_t*)(pidPage.mapped);
  }
  return 0;
}

/// Deletes a shared memory page, if it exists
void nukePage(const char * pagePattern){
  char pageName[NAME_BUFFER_SIZE];
  snprintf(pageName, NAME_BUFFER_SIZE, pagePattern, Util::streamName);
  IPC::sharedPage page(pageName, 0, false, false);
  page.master = true;
}

/// Deletes a semaphore, if it exists
void nukeSem(const char * pagePattern){
  char pageName[NAME_BUFFER_SIZE];
  snprintf(pageName, NAME_BUFFER_SIZE, pagePattern, Util::streamName);
  IPC::semaphore sem(pageName, O_RDWR, ACCESSPERMS, 0, true);
  if (sem){sem.unlink();}
}

int main(int argc, char **argv){
  Util::redirectLogsIfNeeded();
  if (argc < 1){
    FAIL_MSG("Usage: %s STREAM_NAME", argv[0]);
    return 1;
  }
  Util::setStreamName(argv[1]);
  uint8_t state = Util::getStreamStatus(Util::streamName);
  INFO_MSG("Current stream status: %s", getStateString(state));
  size_t loops = 0;
  if (state != STRMSTAT_OFF){INFO_MSG("Attempting clean shutdown...");}
  while (state != STRMSTAT_OFF && loops++ < 40){
    uint64_t pid;
    pid = getPidFromPage(SHM_STREAM_IPID);
    if (pid > 1){Util::Procs::Stop(pid);}
    pid = getPidFromPage(SHM_STREAM_PPID);
    if (pid > 1){Util::Procs::Stop(pid);}
    Util::wait(250);
    uint8_t prevState = state;
    state = Util::getStreamStatus(Util::streamName);
    if (prevState != state){
      INFO_MSG("Current stream status: %s", getStateString(state));
    }
  }
  INFO_MSG("Detecting and cleaning up any leftovers...");
  // Scoping to clear up metadata and track providers
  {
    char pageName[NAME_BUFFER_SIZE];
    snprintf(pageName, NAME_BUFFER_SIZE, SHM_STREAM_META, argv[1]);
    IPC::sharedPage streamPage(pageName, 0, false, false);
    if (streamPage.mapped){
      streamPage.master = true;
      std::set<pid_t> checkPids;
      Util::RelAccX stream(streamPage.mapped, false);
      if (stream.isReady()){
        Util::RelAccX trackList(stream.getPointer("tracks"), false);
        if (trackList.isReady()){
          for (size_t i = 0; i < trackList.getPresent(); i++){
            IPC::sharedPage trackPage(trackList.getPointer("page", i), SHM_STREAM_TRACK_LEN, false, false);
            trackPage.master = true;
            pid_t pid = trackList.getInt("pid", i);
            if (pid > 1){
              Util::Procs::Stop(pid);
              checkPids.insert(pid);
            }
            if (trackPage){
              Util::RelAccX track(trackPage.mapped, false);
              if (track.isReady()){
                Util::RelAccX pages(track.getPointer("pages"), false);
                if (pages.isReady()){
                  for (uint64_t j = pages.getDeleted(); j < pages.getEndPos(); j++){
                    char thisPageName[NAME_BUFFER_SIZE];
                    snprintf(thisPageName, NAME_BUFFER_SIZE, SHM_TRACK_DATA,
                             argv[1], i, (uint32_t)pages.getInt("firstkey", j));
                    IPC::sharedPage p(thisPageName, 0);
                    p.master = true;
                  }
                }
              }
            }
          }
        }
      }
      // Hard-kill any remaining track providers
      if (checkPids.size()){
        //Wait a bit to settle
        Util::sleep(1000);
        while (checkPids.size()){
          Util::Procs::Murder(*checkPids.begin());
          checkPids.erase(*checkPids.begin());
        }
      }
    }
  }
  { // Wipe applications, if any are left over
    uint64_t pid;
    pid = getPidFromPage(SHM_STREAM_IPID);
    if (pid){Util::Procs::Murder(pid);}
    pid = getPidFromPage(SHM_STREAM_PPID);
    if (pid){Util::Procs::Murder(pid);}
  }
  //Wipe relevant pages
  nukePage(SHM_STREAM_STATE);
  nukePage(SHM_STREAM_IPID);
  nukePage(SHM_STREAM_PPID);
  // Scoping to clear up users page
  {
    Comms::Users cleanUsers;
    cleanUsers.reload(Util::streamName, true);
    std::set<pid_t> checkPids;
    for (size_t i = 0; i < cleanUsers.recordCount(); ++i){
      uint8_t status = cleanUsers.getStatus(i);
      cleanUsers.setStatus(COMM_STATUS_INVALID, i);
      if (status != COMM_STATUS_INVALID && !(status & COMM_STATUS_DISCONNECT)){
        pid_t pid = cleanUsers.getPid(i);
        if (pid > 1 && !(cleanUsers.getStatus(i) & COMM_STATUS_NOKILL)){
          Util::Procs::Stop(pid);
          checkPids.insert(pid);
        }
      }
    }
    cleanUsers.setMaster(true);
    // Hard-kill any remaining clients
    if (checkPids.size()){
      //Wait a bit to settle
      Util::sleep(1000);
      while (checkPids.size()){
        Util::Procs::Murder(*checkPids.begin());
        checkPids.erase(*checkPids.begin());
      }
    }
  }
  nukePage(COMMS_USERS);
  nukeSem(SEM_USERS);
  nukeSem(SEM_LIVE);
  nukeSem(SEM_INPUT);
  nukeSem("/MstPull_%s");
  nukeSem(SEM_TRACKLIST);
}

