// This line will make ftello/fseeko work with 64 bits numbers
#define _FILE_OFFSET_BITS 64

#include "bitfields.h"
#include "defines.h"
#include "dtsc.h"
#include "procs.h"
#include "timing.h"
#include "util.h"
#include <errno.h> // errno, ENOENT, EEXIST
#include <iomanip>
#include <iostream>
#include <stdio.h>
#include <sys/stat.h> // stat
#if defined(_WIN32)
#include <direct.h> // _mkdir
#endif
#include <stdlib.h>
#include <sys/resource.h>
#include <mist/triggers.h>

#define RAXHDR_FIELDOFFSET p[1]
#define RAX_REQDFIELDS_LEN 36

// Converts the given record number into an offset of records after getOffset()'s offset.
// Does no bounds checking whatsoever, allowing access to not-yet-created or already-deleted
// records.
// This access method is stable with changing start/end positions and present record counts,
// because it only
// depends on the record count, which may not change for ring buffers.
#define RECORD_POINTER p + *hdrOffset + (((*hdrRecordCnt)?(recordNo % *hdrRecordCnt) : recordNo) * *hdrRecordSize) + fd.offset

namespace Util{
  Util::DataCallback defaultDataCallback;

  /// Helper function that cross-platform checks if a given directory exists.
  bool isDirectory(const std::string &path){
#if defined(_WIN32)
    struct _stat info;
    if (_stat(path.c_str(), &info) != 0){return false;}
    return (info.st_mode & _S_IFDIR) != 0;
#else
    struct stat info;
    if (stat(path.c_str(), &info) != 0){return false;}
    return (info.st_mode & S_IFDIR) != 0;
#endif
  }

  bool createPathFor(const std::string &file){
    int pos = file.find_last_of('/');
#if defined(_WIN32)
    // Windows also supports backslashes as directory separator
    if (pos == std::string::npos){pos = file.find_last_of('\\');}
#endif
    if (pos == std::string::npos){
      return true; // There is no parent
    }
    // Fail if we cannot create a parent
    return createPath(file.substr(0, pos));
  }

  /// Helper function that will attempt to create the given path if it not yet exists.
  /// Returns true if path exists or was successfully created, false otherwise.
  bool createPath(const std::string &path){
#if defined(_WIN32)
    int ret = _mkdir(path.c_str());
#else
    mode_t mode = 0755;
    int ret = mkdir(path.c_str(), mode);
#endif
    if (ret == 0){// Success!
      INFO_MSG("Created directory: %s", path.c_str());
      return true;
    }

    switch (errno){
    case ENOENT:{// Parent does not exist
      int pos = path.find_last_of('/');
#if defined(_WIN32)
      // Windows also supports backslashes as directory separator
      if (pos == std::string::npos){pos = path.find_last_of('\\');}
#endif
      if (pos == std::string::npos){
        // fail if there is no parent
        // Theoretically cannot happen, but who knows
        FAIL_MSG("Could not create %s: %s", path.c_str(), strerror(errno));
        return false;
      }
      // Fail if we cannot create a parent
      if (!createPath(path.substr(0, pos))) return false;
#if defined(_WIN32)
      ret = _mkdir(path.c_str());
#else
      ret = mkdir(path.c_str(), mode);
#endif
      if (ret){FAIL_MSG("Could not create %s: %s", path.c_str(), strerror(errno));}
      return (ret == 0);
    }
    case EEXIST: // Is a file or directory
      if (isDirectory(path)){
        return true; // All good, already exists
      }else{
        FAIL_MSG("Not a directory: %s", path.c_str());
        return false;
      }

    default: // Generic failure
      FAIL_MSG("Could not create %s: %s", path.c_str(), strerror(errno));
      return false;
    }
  }

  bool stringScan(const std::string &src, const std::string &pattern, std::deque<std::string> &result){
    result.clear();
    std::deque<size_t> positions;
    size_t pos = pattern.find("%", 0);
    while (pos != std::string::npos){
      positions.push_back(pos);
      pos = pattern.find("%", pos + 1);
    }
    if (positions.size() == 0){return false;}
    size_t sourcePos = 0;
    size_t patternPos = 0;
    std::deque<size_t>::iterator posIter = positions.begin();
    while (sourcePos != std::string::npos){
      // Match first part of the string
      if (pattern.substr(patternPos, *posIter - patternPos) != src.substr(sourcePos, *posIter - patternPos)){
        break;
      }
      sourcePos += *posIter - patternPos;
      std::deque<size_t>::iterator nxtIter = posIter + 1;
      if (nxtIter != positions.end()){
        patternPos = *posIter + 2;
        size_t tmpPos = src.find(pattern.substr(*posIter + 2, *nxtIter - patternPos), sourcePos);
        result.push_back(src.substr(sourcePos, tmpPos - sourcePos));
        sourcePos = tmpPos;
      }else{
        result.push_back(src.substr(sourcePos));
        sourcePos = std::string::npos;
      }
      posIter++;
    }
    return result.size() == positions.size();
  }

  void stringToLower(std::string &val){
    int i = 0;
    while (val[i]){
      val.at(i) = tolower(val.at(i));
      i++;
    }
  }

  std::string generateRandomString(const int len)
  {
    static const char alphanum[] = "0123456789"
                                   "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                   "abcdefghijklmnopqrstuvwxyz";

    std::string s;
    for (int i = 0; i < len; ++i)
    {
      s.push_back(alphanum[rand() % (sizeof(alphanum) - 1)]);
    }

    return s;
  }

  //Returns the time to wait in milliseconds for exponential back-off waiting.
  //If currIter > maxIter, always returns 5ms to prevent tight eternal loops when mistakes are made
  //Otherwise, exponentially increases wait time for a total of maxWait milliseconds after maxIter calls.
  //Wildly inaccurate for very short max durations and/or very large maxIter values, but otherwise works as expected.
  int64_t expBackoffMs(const size_t currIter, const size_t maxIter, const int64_t maxWait){
    if (currIter > maxIter){return 5;}
    int64_t w = maxWait >> 1;
    for (size_t i = maxIter; i > currIter; --i){
      w >>= 1;
      if (w < 2){w = 2;}
    }
    DONTEVEN_MSG("Waiting %" PRId64 " ms out of %" PRId64 " for iteration %zu/%zu", w, maxWait, currIter, maxIter);
    return w;
  }

  /// 64-bits version of ftell
  uint64_t ftell(FILE *stream){
    /// \TODO Windows implementation (e.g. _ftelli64 ?)
    return ftello(stream);
  }

  /// 64-bits version of fseek
  uint64_t fseek(FILE *stream, uint64_t offset, int whence){
    /// \TODO Windows implementation (e.g. _fseeki64 ?)
    clearerr(stream);
    return fseeko(stream, offset, whence);
  }

  ResizeablePointer::ResizeablePointer(){
    currSize = 0;
    ptr = 0;
    maxSize = 0;
  }

  ResizeablePointer::~ResizeablePointer(){
    if (ptr){free(ptr);}
    currSize = 0;
    ptr = 0;
    maxSize = 0;
  }

  void ResizeablePointer::shift(size_t byteCount){
    //Shifting the entire buffer is easy, we do nothing and set size to zero
    if (byteCount >= currSize){
      currSize = 0;
      return;
    }
    //Shifting partial needs a memmove and size change
    memmove(ptr, ((char*)ptr)+byteCount, currSize-byteCount);
    currSize -= byteCount;
  }

  bool ResizeablePointer::assign(const void *p, uint32_t l){
    if (!allocate(l)){return false;}
    memcpy(ptr, p, l);
    currSize = l;
    return true;
  }

  bool ResizeablePointer::assign(const std::string &str){
    return assign(str.data(), str.length());
  }

  bool ResizeablePointer::append(const void *p, uint32_t l){
    // We're writing from null pointer - assume outside write (e.g. fread or socket operation) and update the size
    if (!p){
      if (currSize + l > maxSize){
        FAIL_MSG("Pointer write went beyond allocated size! Memory corruption likely.");
        BACKTRACE;
        return false;
      }
      currSize += l;
      return true;
    }
    if (!allocate(l + currSize)){return false;}
    memcpy(((char *)ptr) + currSize, p, l);
    currSize += l;
    return true;
  }

  bool ResizeablePointer::append(const std::string &str){
    return append(str.data(), str.length());
  }

  bool ResizeablePointer::allocate(uint32_t l){
    if (l > maxSize){
      void *tmp = realloc(ptr, l);
      if (!tmp){
        FAIL_MSG("Could not allocate %" PRIu32 " bytes of memory", l);
        return false;
      }
      ptr = tmp;
      maxSize = l;
    }
    return true;
  }

  /// Returns amount of space currently reserved for this pointer
  uint32_t ResizeablePointer::rsize(){return maxSize;}

  void ResizeablePointer::truncate(const size_t newLen){
    if (currSize > newLen){currSize = newLen;}
  }

  /// Redirects stderr to log parser, writes log parser to the old stderr.
  /// Does nothing if the MIST_CONTROL environment variable is set.
  void redirectLogsIfNeeded(){
    // The controller sets this environment variable.
    // We don't do anything if set, since the controller wants the messages raw.
    if (getenv("MIST_CONTROL")){return;}
    setenv("MIST_CONTROL", "1", 1);
    // Okay, we're stand-alone, lets do some parsing!
    int true_stderr = dup(STDERR_FILENO);
    int pipeErr[2];
    if (pipe(pipeErr) >= 0){
      // Start reading log messages from the unnamed pipe
      Util::Procs::fork_prepare();
      pid_t pid = fork();
      if (pid == 0){// child
        Util::Procs::fork_complete();
        close(pipeErr[1]); // close the unneeded pipe file descriptor
        // Close all sockets in the socketList
        for (std::set<int>::iterator it = Util::Procs::socketList.begin();
             it != Util::Procs::socketList.end(); ++it){
          close(*it);
        }
        close(2);
        struct sigaction new_action;
        new_action.sa_handler = SIG_IGN;
        sigemptyset(&new_action.sa_mask);
        new_action.sa_flags = 0;
        sigaction(SIGINT, &new_action, NULL);
        sigaction(SIGHUP, &new_action, NULL);
        sigaction(SIGTERM, &new_action, NULL);
        sigaction(SIGPIPE, &new_action, NULL);
        Util::logParser(pipeErr[0], true_stderr, isatty(true_stderr));
        exit(0);
      }
      Util::Procs::fork_complete();
      if (pid == -1){
        FAIL_MSG("Failed to fork child process for log handling!");
      }else{
        dup2(pipeErr[1], STDERR_FILENO); // cause stderr to write to the pipe
      }
      close(pipeErr[1]); // close the unneeded pipe file descriptor
      close(pipeErr[0]);
      close(true_stderr);
    }
  }

  void convertLogs(const char *progName, std::string triggerPayload){
    int pipeErr[2];
    int pipeOut[2];
    pid_t thisPid = getpid();
    if (pipe(pipeErr) >= 0 && pipe(pipeOut) >= 0){
      // Start reading log messages from the unnamed pipe
      Util::Procs::fork_prepare();
      pid_t pid = fork();
      if (pid == 0){// child
        Util::Procs::fork_complete();
        close(pipeErr[1]); // close the unneeded pipe file descriptor
        close(pipeOut[1]);
        // Close all sockets in the socketList
        for (std::set<int>::iterator it = Util::Procs::socketList.begin();
             it != Util::Procs::socketList.end(); ++it){
          close(*it);
        }
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        struct sigaction new_action;
        new_action.sa_handler = SIG_IGN;
        sigemptyset(&new_action.sa_mask);
        new_action.sa_flags = 0;
        sigaction(SIGINT, &new_action, NULL);
        sigaction(SIGHUP, &new_action, NULL);
        sigaction(SIGTERM, &new_action, NULL);
        sigaction(SIGPIPE, &new_action, NULL);
        Util::logConverter(pipeErr[0], pipeOut[0], STDERR_FILENO, progName, thisPid, triggerPayload);
        exit(0);
      }
      Util::Procs::fork_complete();
      if (pid == -1){
        FAIL_MSG("Failed to fork child process for log handling!");
      }else{
        dup2(pipeErr[1], STDERR_FILENO); // cause stderr to write to the pipe
        dup2(pipeOut[1], STDOUT_FILENO); // cause stdout to write to the pipe
      }
      close(pipeErr[1]); // close the unneeded pipe file descriptor
      close(pipeErr[0]);
      close(pipeOut[1]);
      close(pipeOut[0]);
    }
  }

  void logConverter(int inErr, int inOut, int out, const char *progName, pid_t pid, std::string triggerPayload){
    Socket::Connection errStream(-1, inErr);
    Socket::Connection outStream(-1, inOut);
    errStream.setBlocking(false);
    outStream.setBlocking(false);
    while (errStream || outStream){
      if (errStream.spool() || errStream.Received().size()){
        while (errStream.Received().size()){
          std::string &line = errStream.Received().get();
          while (line.find('\r') != std::string::npos){line.erase(line.find('\r'));}
          while (line.find('\n') != std::string::npos){line.erase(line.find('\n'));}
          dprintf(out, "INFO|%s|%d||%s|%s\n", progName, pid, Util::streamName, line.c_str());
          line.clear();
        }
      }else if (outStream.spool() || outStream.Received().size()){
        while (outStream.Received().size()){
          std::string &line = outStream.Received().get();
          while (line.find('\r') != std::string::npos){line.erase(line.find('\r'));}
          while (line.find('\n') != std::string::npos){line.erase(line.find('\n'));}
          dprintf(out, "INFO|%s|%d||%s|%s\n", progName, pid, Util::streamName, line.c_str());
          if (!strcmp(progName, "livepeer-catalyst-uploader") && !triggerPayload.empty()) {
            // output is one-line JSON with URL
            const JSON::Value &value = JSON::fromString(line);
            // insert received IPFS CID
            unsigned long cid_pos = triggerPayload.find("IPFS_CID");
            if (cid_pos != std::string::npos) {
              triggerPayload.replace(cid_pos, 8, value["uri"].asString());
              // invoke RECORDING_END trigger
              Triggers::doTrigger("RECORDING_END", triggerPayload, streamName);
            }
          }
          line.clear();
        }      
      }else{Util::sleep(25);}
    }
    errStream.close();
    outStream.close();
  }

  /// Parses log messages from the given file descriptor in, printing them to out, optionally
  /// calling the given callback for each valid message. Closes the file descriptor on read error
  void logParser(int in, int out, bool colored,
                 void callback(const std::string &, const std::string &, const std::string &, uint64_t, bool)){
    if (getenv("MIST_COLOR")){colored = true;}
    bool sysd_log = getenv("MIST_LOG_SYSTEMD");
    char *color_time, *color_msg, *color_end, *color_strm, *CONF_msg, *FAIL_msg, *ERROR_msg,
        *WARN_msg, *INFO_msg;
    if (colored){
      color_end = (char *)"\033[0m";
      if (getenv("MIST_COLOR_END")){color_end = getenv("MIST_COLOR_END");}
      color_strm = (char *)"\033[0m";
      if (getenv("MIST_COLOR_STREAM")){color_strm = getenv("MIST_COLOR_STREAM");}
      color_time = (char *)"\033[2m";
      if (getenv("MIST_COLOR_TIME")){color_time = getenv("MIST_COLOR_TIME");}
      CONF_msg = (char *)"\033[0;1;37m";
      if (getenv("MIST_COLOR_CONF")){CONF_msg = getenv("MIST_COLOR_CONF");}
      FAIL_msg = (char *)"\033[0;1;31m";
      if (getenv("MIST_COLOR_FAIL")){FAIL_msg = getenv("MIST_COLOR_FAIL");}
      ERROR_msg = (char *)"\033[0;31m";
      if (getenv("MIST_COLOR_ERROR")){ERROR_msg = getenv("MIST_COLOR_ERROR");}
      WARN_msg = (char *)"\033[0;1;33m";
      if (getenv("MIST_COLOR_WARN")){WARN_msg = getenv("MIST_COLOR_WARN");}
      INFO_msg = (char *)"\033[0;36m";
      if (getenv("MIST_COLOR_INFO")){INFO_msg = getenv("MIST_COLOR_INFO");}
    }else{
      color_end = (char *)"";
      color_strm = (char *)"";
      color_time = (char *)"";
      CONF_msg = (char *)"";
      FAIL_msg = (char *)"";
      ERROR_msg = (char *)"";
      WARN_msg = (char *)"";
      INFO_msg = (char *)"";
    }

    Socket::Connection O(-1, in);
    O.setBlocking(true);
    Util::ResizeablePointer buf;
    while (O){
      if (O.spool()){
        while (O.Received().size()){
          std::string & t = O.Received().get();
          buf.append(t);
          if (buf.size() && (buf.size() > 1024 || *t.rbegin() == '\n')){
            unsigned int i = 0;
            char *kind = buf; // type of message, at begin of string
            char *progname = 0;
            char *progpid = 0;
            char *lineno = 0;
            char *strmNm = 0;
            char *message = 0;
            while (i < 9 && buf[i] != '|' && buf[i] != 0 && buf[i] < 128){++i;}
            if (buf[i] != '|'){
              // on parse error, skip to next message
              t.clear();
              buf.truncate(0);
              continue;
            }
            buf[i] = 0;                      // insert null byte
            ++i;
            progname = buf + i; // progname starts here
            while (i < 40 && buf[i] != '|' && buf[i] != 0){++i;}
            if (buf[i] != '|'){
              // on parse error, skip to next message
              t.clear();
              buf.truncate(0);
              continue;
            }
            buf[i] = 0;                      // insert null byte
            ++i;
            progpid = buf + i; // progpid starts here
            while (i < 60 && buf[i] != '|' && buf[i] != 0){++i;}
            if (buf[i] != '|'){
              // on parse error, skip to next message
              t.clear();
              buf.truncate(0);
              continue;
            }
            buf[i] = 0;                      // insert null byte
            ++i;
            lineno = buf + i; // lineno starts here
            while (i < 180 && buf[i] != '|' && buf[i] != 0){++i;}
            if (buf[i] != '|'){
              // on parse error, skip to next message
              t.clear();
              buf.truncate(0);
              continue;
            }
            buf[i] = 0;                      // insert null byte
            ++i;
            strmNm = buf + i; // stream name starts here
            while (i < 380 && buf[i] != '|' && buf[i] != 0){++i;}
            if (buf[i] != '|'){
              // on parse error, skip to next message
              t.clear();
              buf.truncate(0);
              continue;
            }
            buf[i] = 0;                      // insert null byte
            ++i;
            message = buf + i; // message starts here
            // insert null byte for end of line
            buf[buf.size()-1] = 0;
            // print message
            if (callback){callback(kind, message, strmNm, JSON::Value(progpid).asInt(), true);}
            color_msg = color_end;
            if (colored){
              if (!strcmp(kind, "CONF")){color_msg = CONF_msg;}
              if (!strcmp(kind, "FAIL")){color_msg = FAIL_msg;}
              if (!strcmp(kind, "ERROR")){color_msg = ERROR_msg;}
              if (!strcmp(kind, "WARN")){color_msg = WARN_msg;}
              if (!strcmp(kind, "INFO")){color_msg = INFO_msg;}
            }
            if (sysd_log){
              if (!strcmp(kind, "CONF")){dprintf(out, "<5>");}
              if (!strcmp(kind, "FAIL")){dprintf(out, "<0>");}
              if (!strcmp(kind, "ERROR")){dprintf(out, "<1>");}
              if (!strcmp(kind, "WARN")){dprintf(out, "<2>");}
              if (!strcmp(kind, "INFO")){dprintf(out, "<5>");}
              if (!strcmp(kind, "VERYHIGH") || !strcmp(kind, "EXTREME") || !strcmp(kind, "INSANE") || !strcmp(kind, "DONTEVEN")){
                dprintf(out, "<7>");
              }
            }else{
              time_t rawtime;
              struct tm *timeinfo;
              struct tm timetmp;
              char buffer[100];
              time(&rawtime);
              timeinfo = localtime_r(&rawtime, &timetmp);
              strftime(buffer, 100, "%F %H:%M:%S", timeinfo);
              dprintf(out, "%s[%s] ", color_time, buffer);
            }
            if (progname && progpid && strlen(progname) && strlen(progpid)){
              if (strmNm && strlen(strmNm)){
                dprintf(out, "%s:%s%s%s (%s) ", progname, color_strm, strmNm, color_time, progpid);
              }else{
                dprintf(out, "%s (%s) ", progname, progpid);
              }
            }else{
              if (strmNm && strlen(strmNm)){dprintf(out, "%s%s%s ", color_strm, strmNm, color_time);}
            }
            dprintf(out, "%s%s: %s%s", color_msg, kind, message, color_end);
            if (lineno && strlen(lineno)){dprintf(out, " (%s) ", lineno);}
            dprintf(out, "\n");
            buf.truncate(0);
          }
          t.clear();
        }
      }else{
        Util::sleep(50);
      }
    }
    close(out);
    close(in);
  }

  FieldAccX::FieldAccX(RelAccX *_src, RelAccXFieldData _field) : src(_src), field(_field){}

  uint64_t FieldAccX::uint(size_t recordNo) const{return src->getInt(field, recordNo);}

  std::string FieldAccX::string(size_t recordNo) const{
    return std::string(src->getPointer(field, recordNo));
  }

  const char * FieldAccX::ptr(size_t recordNo) const{
    return src->getPointer(field, recordNo);
  }

  void FieldAccX::set(uint64_t val, size_t recordNo){src->setInt(field, val, recordNo);}

  void FieldAccX::set(const std::string &val, size_t recordNo){
    char *place = src->getPointer(field, recordNo);
    memcpy(place, val.data(), std::min((size_t)field.size, val.size()));
    if ((field.type & 0xF0) == RAX_STRING){
      place[std::min((size_t)field.size - 1, val.size())] = 0;
    }
  }

  /// If waitReady is true (default), waits for isReady() to return true in 50ms sleep increments.
  RelAccX::RelAccX(char *data, bool waitReady){
    if (!data){
      p = 0;
      return;
    }
    p = data;
    hdrRecordCnt = (uint32_t*)(p+2);
    hdrRecordSize = (uint32_t*)(p+6);
    hdrStartPos = (uint32_t*)(p+10);
    hdrDeleted = (uint64_t*)(p+14);
    hdrPresent = (uint32_t*)(p+22);
    hdrOffset = (uint16_t*)(p+26);
    hdrEndPos = (uint64_t*)(p+28);
    if (waitReady){
      uint64_t maxWait = Util::bootMS() + 10000;
      while (!isReady()){
        if (Util::bootMS() > maxWait){
          FAIL_MSG("Waiting for RelAccX structure to be ready timed out, aborting");
          p = 0;
          return;
        }
        Util::sleep(50);
      }
    }
    if (isReady()){
      uint16_t offset = RAXHDR_FIELDOFFSET;
      if (offset < 11 || offset >= getOffset()){
        FAIL_MSG("Invalid field offset: %u", offset);
        p = 0;
        return;
      }
      uint64_t dataOffset = 0;
      while (offset < getOffset()){
        const uint8_t sizeByte = p[offset];
        const uint8_t nameLen = sizeByte >> 3;
        const uint8_t typeLen = sizeByte & 0x7;
        const uint8_t fieldType = p[offset + 1 + nameLen];
        const std::string fieldName(p + offset + 1, nameLen);
        uint32_t size = 0;
        switch (typeLen){
        case 1: // derived from field type
          if ((fieldType & 0xF0) == RAX_UINT || (fieldType & 0xF0) == RAX_INT){
            // Integer types - lower 4 bits +1 are size in bytes
            size = (fieldType & 0x0F) + 1;
          }else{
            if ((fieldType & 0xF0) == RAX_STRING || (fieldType & 0xF0) == RAX_RAW){
              // String types - 8*2^(lower 4 bits) is size in bytes
              size = 16 << (fieldType & 0x0F);
            }else{
              WARN_MSG("Unhandled field type!");
            }
          }
          break;
        // Simple sizes in bytes
        case 2: size = p[offset + 1 + nameLen + 1]; break;
        case 3: size = *(uint16_t *)(p + offset + 1 + nameLen + 1); break;
        case 4: size = Bit::btoh24(p + offset + 1 + nameLen + 1); break;
        case 5: size = *(uint32_t *)(p + offset + 1 + nameLen + 1); break;
        default: WARN_MSG("Unhandled field data size!"); break;
        }
        fields[fieldName] = RelAccXFieldData(fieldType, size, dataOffset);
        DONTEVEN_MSG("Field %s: type %u, size %" PRIu32 ", offset %" PRIu64, fieldName.c_str(),
                     fieldType, size, dataOffset);
        dataOffset += size;
        offset += nameLen + typeLen + 1;
      }
    }
  }

  /// Gets the amount of records present in the structure.
  uint32_t RelAccX::getRCount() const{return *hdrRecordCnt;}

  /// Gets the size in bytes of a single record in the structure.
  uint32_t RelAccX::getRSize() const{return *hdrRecordSize;}

  /// Gets the position in the records where the entries start
  uint32_t RelAccX::getStartPos() const{return *hdrStartPos;}

  /// Gets the number of deleted records
  uint64_t RelAccX::getDeleted() const{return *hdrDeleted;}

  /// Gets the number of records present
  size_t RelAccX::getPresent() const{return *hdrPresent;}

  /// Gets the number of the last valid index
  uint64_t RelAccX::getEndPos() const{return *hdrEndPos;}

  /// Gets the number of fields per recrd
  uint32_t RelAccX::getFieldCount() const{return fields.size();}

  /// Gets the offset from the structure start where records begin.
  uint16_t RelAccX::getOffset() const{return *hdrOffset;}

  /// Returns true if the structure is ready for read operations.
  bool RelAccX::isReady() const{return p && (p[0] & 1);}

  /// Returns true if the structure will no longer be updated.
  bool RelAccX::isExit() const{return !p || (p[0] & 2);}

  /// Returns true if the structure should be reloaded through out of band means.
  bool RelAccX::isReload() const{return p[0] & 4;}

  /// Returns true if the given record number can be accessed.
  bool RelAccX::isRecordAvailable(uint64_t recordNo) const{
    // Check if the record has been deleted
    if (getDeleted() > recordNo){return false;}
    // Check if the record hasn't been created yet
    if (recordNo >= getEndPos()){return false;}
    return true;
  }

  /// Returns the (max) size of the given field.
  /// For string types, returns the exact size excluding terminating null byte.
  /// For other types, returns the maximum size possible.
  /// Returns 0 if the field does not exist.
  uint32_t RelAccX::getSize(const std::string &name, uint64_t recordNo) const{
    if (!isRecordAvailable(recordNo)){return 0;}
    std::map<std::string, RelAccXFieldData>::const_iterator it = fields.find(name);
    if (it == fields.end()){return 0;}
    const RelAccXFieldData &fd = it->second;
    if ((fd.type & 0xF0) == RAX_STRING){return strnlen(RECORD_POINTER, fd.size);}
    return fd.size;
  }

  /// Returns a pointer to the given field in the given record number.
  /// Returns a null pointer if the field does not exist.
  char *RelAccX::getPointer(const std::string &name, uint64_t recordNo) const{
    std::map<std::string, RelAccXFieldData>::const_iterator it = fields.find(name);
    if (it == fields.end()){return 0;}
    return getPointer(it->second, recordNo);
  }

  char *RelAccX::getPointer(const RelAccXFieldData &fd, uint64_t recordNo) const{
    return RECORD_POINTER;
  }

  /// Returns the value of the given integer-type field in the given record, as an uint64_t type.
  /// Returns 0 if the field does not exist or is not an integer type.
  uint64_t RelAccX::getInt(const std::string &name, uint64_t recordNo) const{
    std::map<std::string, RelAccXFieldData>::const_iterator it = fields.find(name);
    if (it == fields.end()){return 0;}
    return getInt(it->second, recordNo);
  }

  uint64_t RelAccX::getInt(const RelAccXFieldData &fd, uint64_t recordNo) const{
    char *ptr = RECORD_POINTER;
    if ((fd.type & 0xF0) == RAX_UINT){// unsigned int
      switch (fd.size){
      case 1: return *(uint8_t *)ptr;
      case 2: return *(uint16_t *)ptr;
      case 3: return Bit::btoh24(ptr);
      case 4: return *(uint32_t *)ptr;
      case 8: return *(uint64_t *)ptr;
      default: WARN_MSG("Unimplemented integer");
      }
    }
    if ((fd.type & 0xF0) == RAX_INT){// signed int
      switch (fd.size){
      case 1: return *(int8_t *)ptr;
      case 2: return *(int16_t *)ptr;
      case 3: return Bit::btoh24(ptr);
      case 4: return *(int32_t *)ptr;
      case 8: return *(int64_t *)ptr;
      default: WARN_MSG("Unimplemented integer");
      }
    }
    return 0; // Not an integer type, or not implemented
  }

  std::string RelAccX::toPrettyString(size_t indent) const{
    std::stringstream r;
    uint64_t delled = getDeleted();
    uint64_t max = getEndPos();
    if (max - delled > getRCount()){max = delled + getRCount();}
    if (max == 0){max = getRCount();}
    r << std::string(indent, ' ') << "RelAccX: " << getRCount() << " x " << getRSize() << "b @"
      << getOffset() << " (#" << getDeleted() << " - #" << getEndPos() - 1 << ")" << std::endl;
    for (uint64_t i = delled; i < max; ++i){
      r << std::string(indent + 2, ' ') << "#" << i << ":" << std::endl;
      for (std::map<std::string, RelAccXFieldData>::const_iterator it = fields.begin();
           it != fields.end(); ++it){
        r << std::string(indent + 4, ' ') << it->first << ": ";
        switch (it->second.type & 0xF0){
        case RAX_INT: r << (int64_t)getInt(it->first, i) << std::endl; break;
        case RAX_UINT: r << getInt(it->first, i) << std::endl; break;
        case RAX_STRING: r << getPointer(it->first, i) << std::endl; break;
        case 0:{// RAX_NESTED
          RelAccX n(getPointer(it->first, i), false);
          if (n.isReady()){
            r << "Nested RelAccX:" << std::endl;
            r << (n.getFieldCount() > 6 ? n.toPrettyString(indent + 6) : n.toCompactString(indent + 6));
          }else{
            r << "Nested RelAccX: not ready" << std::endl;
          }
          break;
        }
        case RAX_RAW:{
          char *ptr = getPointer(it->first, i);
          size_t sz = getSize(it->first, i);
          size_t zeroCount = 0;
          for (size_t j = 0; j < sz && j < 100 && zeroCount < 16; ++j){
            r << "0x" << std::hex << std::setw(2) << std::setfill('0') << (int)ptr[j] << std::dec << " ";
            if (ptr[j] == 0x00){
              zeroCount++;
            }else{
              zeroCount = 0;
            }
          }
          r << std::endl;
          break;
        }
        case RAX_DTSC:{
          char *ptr = getPointer(it->first, i);
          size_t sz = getSize(it->first, i);
          r << std::endl;
          r << DTSC::Scan(ptr, sz).toPrettyString(indent + 6) << std::endl;
          break;
        }
        default: r << "[UNIMPLEMENTED]" << std::endl; break;
        }
      }
    }
    return r.str();
  }

  std::string RelAccX::toCompactString(size_t indent) const{
    std::stringstream r;
    uint64_t delled = getDeleted();
    uint64_t max = getEndPos();
    r << std::string(indent, ' ') << "RelAccX: " << getRCount() << " x " << getRSize() << "b @"
      << getOffset() << " (#" << getDeleted() << " - #" << getEndPos() - 1 << ")" << std::endl;
    for (uint64_t i = delled; i < max; ++i){
      r << std::string(indent + 2, ' ') << "#" << i << ": ";
      for (std::map<std::string, RelAccXFieldData>::const_iterator it = fields.begin();
           it != fields.end(); ++it){
        r << it->first << ": ";
        switch (it->second.type & 0xF0){
        case RAX_INT: r << (int64_t)getInt(it->first, i) << ", "; break;
        case RAX_UINT: r << getInt(it->first, i) << ", "; break;
        case RAX_STRING: r << getPointer(it->first, i) << ", "; break;
        case 0:{// RAX_NESTED
          RelAccX n(getPointer(it->first, i), false);
          if (n.isReady()){
            r << (n.getFieldCount() > 6 ? n.toPrettyString(indent + 2) : n.toCompactString(indent + 2));
          }else{
            r << "Nested RelAccX not ready" << std::endl;
          }
          break;
        }
        default: r << "[UNIMPLEMENTED], "; break;
        }
      }
      r << std::endl;
    }
    return r.str();
  }

  /// Returns the default size in bytes of the data component of a field type number.
  /// Returns zero if not implemented, unknown or the type has no default.
  uint32_t RelAccX::getDefaultSize(uint8_t fType){
    if ((fType & 0XF0) == RAX_INT || (fType & 0XF0) == RAX_UINT){
      return (fType & 0x0F) + 1; // Default size is lower 4 bits plus one bytes
    }
    if ((fType & 0XF0) == RAX_STRING || (fType & 0XF0) == RAX_RAW){
      return 16 << (fType & 0x0F); // Default size is 16 << (lower 4 bits) bytes
    }
    return 0;
  }

  /// Adds a new field to the internal list of fields.
  /// Can only be called if not ready, exit or reload.
  /// Changes the offset and record size to match.
  /// Fails if called multiple times with the same field name.
  void RelAccX::addField(const std::string &name, uint8_t fType, uint32_t fLen){
    if (isExit() || isReload() || isReady()){
      WARN_MSG("Attempting to add a field to a non-writeable memory area");
      return;
    }
    if (!name.size() || name.size() > 31){
      WARN_MSG("Attempting to add a field with illegal name: %s (%zu chars)", name.c_str(), name.size());
      return;
    }
    // calculate fLen if missing
    if (!fLen){
      fLen = getDefaultSize(fType);
      if (!fLen){
        WARN_MSG("Attempting to add a mandatory-size field without size");
        return;
      }
    }
    // We now know for sure fLen is set
    // Get current offset and record size
    // The first field initializes the offset and record size.
    if (!fields.size()){
      *hdrRecordSize = 0;                 // Nothing yet, this is the first data field.
      *hdrOffset = RAX_REQDFIELDS_LEN; // All mandatory fields are first - so we start there.
      RAXHDR_FIELDOFFSET = *hdrOffset; // store the field_offset
    }
    uint8_t typeLen = 1;
    // Check if fLen is a non-default value
    if (getDefaultSize(fType) != fLen){
      // Calculate the smallest size integer we can fit this in
      typeLen = 5;                         // 32 bit
      if (fLen < 0x10000){typeLen = 3;}// 16 bit
      if (fLen < 0x100){typeLen = 2;}// 8 bit
    }
    // store the details for internal use
    // recSize is the field offset, since we haven't updated it yet
    fields[name] = RelAccXFieldData(fType, fLen, *hdrRecordSize);

    // write the data to memory
    p[*hdrOffset] = (name.size() << 3) | (typeLen & 0x7);
    memcpy(p + (*hdrOffset) + 1, name.data(), name.size());
    p[(*hdrOffset) + 1 + name.size()] = fType;
    if (typeLen == 2){*(uint8_t *)(p + (*hdrOffset) + 2 + name.size()) = fLen;}
    if (typeLen == 3){*(uint16_t *)(p + (*hdrOffset) + 2 + name.size()) = fLen;}
    if (typeLen == 5){*(uint32_t *)(p + (*hdrOffset) + 2 + name.size()) = fLen;}

    // Calculate new offset and record size
    *hdrOffset += 1 + name.size() + typeLen;
    *hdrRecordSize += fLen;
  }

  /// Sets the record counter to the given value.
  void RelAccX::setRCount(uint32_t count){*hdrRecordCnt = count;}

  /// Sets the position in the records where the entries start
  void RelAccX::setStartPos(uint32_t n){*hdrStartPos = n;}

  /// Sets the number of deleted records
  void RelAccX::setDeleted(uint64_t n){*hdrDeleted = n;}

  /// Sets the number of records present
  /// Defaults to the record count if set to zero.
  void RelAccX::setPresent(uint32_t n){*hdrPresent = n;}

  /// Sets the number of the last valid index
  void RelAccX::setEndPos(uint64_t n){*hdrEndPos = n;}

  /// Sets the ready flag.
  /// After calling this function, addField() may no longer be called.
  /// Fails if exit, reload or ready are (already) set.
  void RelAccX::setReady(){
    if (isExit() || isReload() || isReady()){
      WARN_MSG("Could not set ready on structure with pre-existing state");
      return;
    }
    p[0] |= 1;
  }

  // Sets the exit flag.
  /// After calling this function, addField() may no longer be called.
  void RelAccX::setExit(){p[0] |= 2;}

  // Sets the reload flag.
  /// After calling this function, addField() may no longer be called.
  void RelAccX::setReload(){p[0] |= 4;}

  /// Writes the given string to the given field in the given record.
  /// Fails if ready is not set.
  /// Ensures the last byte is always a zero.
  void RelAccX::setString(const std::string &name, const std::string &val, uint64_t recordNo){
    std::map<std::string, RelAccXFieldData>::const_iterator it = fields.find(name);
    if (it == fields.end()){
      WARN_MSG("Setting non-existent string %s", name.c_str());
      return;
    }
    setString(it->second, val, recordNo);
  }

  void RelAccX::setString(const RelAccXFieldData &fd, const std::string &val, uint64_t recordNo){
    if ((fd.type & 0xF0) != RAX_STRING && (fd.type & 0xF0) != RAX_RAW){
      WARN_MSG("Setting non-string data type to a string value");
      return;
    }
    char *ptr = RECORD_POINTER;
    memcpy(ptr, val.data(), std::min((uint32_t)val.size(), fd.size));
    if ((fd.type & 0xF0) == RAX_STRING){ptr[std::min((uint32_t)val.size(), fd.size - 1)] = 0;}
  }

  /// Writes the given int to the given field in the given record.
  /// Fails if ready is not set or the field is not an integer type.
  void RelAccX::setInt(const std::string &name, uint64_t val, uint64_t recordNo){
    std::map<std::string, RelAccXFieldData>::const_iterator it = fields.find(name);
    if (it == fields.end()){
      WARN_MSG("Setting non-existent integer %s", name.c_str());
      return;
    }
    setInt(it->second, val, recordNo);
  }

  void RelAccX::setInt(const RelAccXFieldData &fd, uint64_t val, uint64_t recordNo){
    char *ptr = RECORD_POINTER;
    if ((fd.type & 0xF0) == RAX_UINT){// unsigned int
      switch (fd.size){
      case 1: *(uint8_t *)ptr = val; return;
      case 2: *(uint16_t *)ptr = val; return;
      case 3: Bit::htob24(ptr, val); return;
      case 4: *(uint32_t *)ptr = val; return;
      case 8: *(uint64_t *)ptr = val; return;
      default: WARN_MSG("Unimplemented integer size %u", fd.size); return;
      }
    }
    if ((fd.type & 0xF0) == RAX_INT){// signed int
      switch (fd.size){
      case 1: *(int8_t *)ptr = (int64_t)val; return;
      case 2: *(int16_t *)ptr = (int64_t)val; return;
      case 3: Bit::htob24(ptr, val); return;
      case 4: *(int32_t *)ptr = (int64_t)val; return;
      case 8: *(int64_t *)ptr = (int64_t)val; return;
      default: WARN_MSG("Unimplemented integer size %u", fd.size); return;
      }
    }
    WARN_MSG("Setting non-integer field (%u) to integer value!", fd.type);
  }

  /// Writes the given int to the given field in the given record.
  /// Fails if ready is not set or the field is not an integer type.
  void RelAccX::setInts(const std::string &name, uint64_t *values, size_t len){
    std::map<std::string, RelAccXFieldData>::const_iterator it = fields.find(name);
    if (it == fields.end()){
      WARN_MSG("Setting non-existent integer %s", name.c_str());
      return;
    }
    const RelAccXFieldData &fd = it->second;
    for (uint64_t recordNo = 0; recordNo < len; recordNo++){
      setInt(fd, values[recordNo], recordNo);
    }
  }

  /// Updates the deleted record counter, the start position and the present record counter,
  /// shifting the ring buffer start position forward without moving the ring buffer end position.
  void RelAccX::deleteRecords(uint32_t amount){
    *hdrStartPos += amount;    // update start position
    *hdrDeleted += amount; // update deleted record counter
    if (*hdrPresent >= amount){
      *hdrPresent -= amount; // decrease records present
    }else{
      BACKTRACE;
      WARN_MSG("Depleting recordCount!");
      exit(1);
      *hdrPresent = 0;
    }
  }

  /// Updates the present record counter, shifting the ring buffer end position forward without
  /// moving the ring buffer start position.
  void RelAccX::addRecords(uint32_t amount){
    if ((*hdrEndPos) + amount - *hdrDeleted > *hdrRecordCnt){
      BACKTRACE;
      WARN_MSG("Exceeding recordCount (%" PRIu64 " [%" PRIu64 " + %" PRIu32 " - %" PRIu64 "] > %" PRIu32 ")", (*hdrEndPos) + amount - (*hdrDeleted), *hdrEndPos, amount, *hdrDeleted, *hdrRecordCnt);
      *hdrPresent = 0;
    }else{
      *hdrPresent += amount;
    }
    *hdrEndPos += amount;
  }

  void RelAccX::minimalFrom(const RelAccX &src){
    copyFieldsFrom(src, true);

    uint64_t rCount = src.getPresent();
    setRCount(rCount);
    setReady();
    addRecords(rCount);

    flowFrom(src);
  }

  void RelAccX::copyFieldsFrom(const RelAccX &src, bool minimal){
    fields.clear();
    if (!minimal){
      for (std::map<std::string, RelAccXFieldData>::const_iterator it = src.fields.begin();
           it != src.fields.end(); it++){
        addField(it->first, it->second.type, it->second.size);
      }
      return;
    }
    for (std::map<std::string, RelAccXFieldData>::const_iterator it = src.fields.begin();
         it != src.fields.end(); it++){
      switch (it->second.type & 0xF0){
      case 0x00: // nested RelAccX
      {
        uint64_t maxSize = 0;
        for (int i = 0; i < src.getPresent(); i++){
          Util::RelAccX child(src.getPointer(it->first, i), false);
          char *tmpBuf = (char *)malloc(src.getOffset() + (src.getRCount() * src.getRSize()));
          Util::RelAccX minChild(tmpBuf, false);
          minChild.minimalFrom(child);
          uint64_t thisSize = minChild.getOffset() + (minChild.getRSize() * minChild.getPresent());
          maxSize = std::max(thisSize, maxSize);
          free(tmpBuf);
        }
        addField(it->first, it->second.type, maxSize);
      }break;
      default: addField(it->first, it->second.type, it->second.size); break;
      }
    }
  }

  void RelAccX::flowFrom(const RelAccX &src){
    uint64_t rCount = src.getPresent();
    if (getRCount() == 0){setRCount(rCount);}
    if (rCount > getRCount()){
      FAIL_MSG("Abandoning reflow, target does not have enough records available (%" PRIu64
               " records, %d available)",
               rCount, getRCount());
      return;
    }
    addRecords(rCount - getPresent());
    for (int i = 0; i < rCount; i++){
      for (std::map<std::string, RelAccXFieldData>::const_iterator it = src.fields.begin();
           it != src.fields.end(); it++){
        if (!fields.count(it->first)){
          INFO_MSG("Field %s in source but not in target", it->first.c_str());
          continue;
        }
        switch (it->second.type & 0xF0){
        case RAX_RAW:
          memcpy(getPointer(it->first, i), src.getPointer(it->first, i),
                 std::min(it->second.size, fields.at(it->first).size));
          break;
        case RAX_INT:
        case RAX_UINT: setInt(it->first, src.getInt(it->first, i), i); break;
        case RAX_STRING: setString(it->first, src.getPointer(it->first, i), i); break;
        case 0x00: // nested RelAccX
        {
          Util::RelAccX srcChild(src.getPointer(it->first, i), false);
          Util::RelAccX child(getPointer(it->first, i), false);
          child.flowFrom(srcChild);
        }break;
        default: break;
        }
      }
    }
  }

  FieldAccX RelAccX::getFieldAccX(const std::string &fName){
    if (!fields.count(fName)){return FieldAccX();}
    return FieldAccX(this, fields.at(fName));
  }

  RelAccXFieldData RelAccX::getFieldData(const std::string &fName) const{
    if (!fields.count(fName)){return RelAccXFieldData();}
    return fields.at(fName);
  }

  bool sysSetNrOpenFiles(int n){
    struct rlimit limit;
    if (getrlimit(RLIMIT_NOFILE, &limit) != 0) {
      FAIL_MSG("Could not get open file limit: %s", strerror(errno));
      return false;
    }
    int currLimit = limit.rlim_cur;
    if(limit.rlim_cur < n){
      limit.rlim_cur = n;
      if (setrlimit(RLIMIT_NOFILE, &limit) != 0) {
        FAIL_MSG("Could not set open file limit from %d to %d: %s", currLimit, n, strerror(errno));
        return false;
      }
      HIGH_MSG("Open file limit increased from %d to %d", currLimit, n)
    }
    return true;
  }

}// namespace Util
