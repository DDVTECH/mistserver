// This line will make ftello/fseeko work with 64 bits numbers
#define _FILE_OFFSET_BITS 64

#include "util.h"
#include "bitfields.h"
#include "defines.h"
#include "timing.h"
#include "procs.h"
#include <errno.h> // errno, ENOENT, EEXIST
#include <iostream>
#include <iomanip>
#include <stdio.h>
#include <sys/stat.h> // stat
#if defined(_WIN32)
#include <direct.h> // _mkdir
#endif
#include <stdlib.h>

#define RECORD_POINTER p + getOffset() + (getRecordPosition(recordNo) * getRSize()) + fd.offset
#define RAXHDR_FIELDOFFSET p[1]
#define RAXHDR_RECORDCNT *(uint32_t *)(p + 2)
#define RAXHDR_RECORDSIZE *(uint32_t *)(p + 6)
#define RAXHDR_STARTPOS *(uint32_t *)(p + 10)
#define RAXHDR_DELETED *(uint64_t *)(p + 14)
#define RAXHDR_PRESENT *(uint32_t *)(p + 22)
#define RAXHDR_OFFSET *(uint16_t *)(p + 26)
#define RAXHDR_ENDPOS *(uint64_t*)(p + 28)
#define RAX_REQDFIELDS_LEN 36

namespace Util{
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

  bool stringScan(const std::string &src, const std::string &pattern,
                  std::deque<std::string> &result){
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
      if (pattern.substr(patternPos, *posIter - patternPos) !=
          src.substr(sourcePos, *posIter - patternPos)){
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

  void stringToLower(std::string & val){
    int i = 0;
    while(val[i]){
      val.at(i) = tolower(val.at(i));
      i++;
    }
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

  bool ResizeablePointer::assign(const void * p, uint32_t l){
    if (!allocate(l)){return false;}
    memcpy(ptr, p, l);
    currSize = l;
    return true;
  }

  bool ResizeablePointer::append(const void * p, uint32_t l){
    if (!allocate(l+currSize)){return false;}
    memcpy(((char*)ptr)+currSize, p, l);
    currSize += l;
    return true;
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


  static int true_stderr = 2;
  static int log_pipe = -1;
  //Local-only helper function.
  //Detects tty on stdin to decide whether or not colours should be used.
  static void handleMsg(void *nul){
    Util::logParser(log_pipe, true_stderr, isatty(true_stderr));
  }

  /// Redirects stderr to log parser, writes log parser to the old stderr.
  /// Does nothing if the MIST_CONTROL environment variable is set.
  void redirectLogsIfNeeded(){
    //The controller sets this environment variable.
    //We don't do anything if set, since the controller wants the messages raw.
    if (getenv("MIST_CONTROL")){return;}
    setenv("MIST_CONTROL", "1", 1);
    //Okay, we're stand-alone, lets do some parsing!
    true_stderr = dup(STDERR_FILENO);
    int pipeErr[2];
    if (pipe(pipeErr) >= 0){
      dup2(pipeErr[1], STDERR_FILENO); // cause stderr to write to the pipe
      close(pipeErr[1]);               // close the unneeded pipe file descriptor
      log_pipe = pipeErr[0];
      //Start reading log messages from the unnamed pipe
      Util::Procs::socketList.insert(log_pipe); //Mark this FD as needing to be closed before forking
      Util::Procs::socketList.insert(true_stderr); //Mark this FD as needing to be closed before forking
      tthread::thread msghandler(handleMsg, (void *)0);
      msghandler.detach();
    }
  }

  /// Parses log messages from the given file descriptor in, printing them to out, optionally calling the given callback for each valid message.
  /// Closes the file descriptor on read error
  void logParser(int in, int out, bool colored, void callback(std::string, std::string, bool)){
    char buf[1024];
    FILE *output = fdopen(in, "r");
    char *color_time, *color_msg, *color_end, *CONF_msg, *FAIL_msg, *ERROR_msg, *WARN_msg, *INFO_msg;
    if (colored){
      color_end = (char*)"\033[0m";
      if (getenv("MIST_COLOR_END")){color_end = getenv("MIST_COLOR_END");}
      color_time = (char*)"\033[2m";
      if (getenv("MIST_COLOR_TIME")){color_time = getenv("MIST_COLOR_TIME");}
      CONF_msg = (char*)"\033[0;1;37m";
      if (getenv("MIST_COLOR_CONF")){CONF_msg = getenv("MIST_COLOR_CONF");}
      FAIL_msg = (char*)"\033[0;1;31m";
      if (getenv("MIST_COLOR_FAIL")){FAIL_msg = getenv("MIST_COLOR_FAIL");}
      ERROR_msg = (char*)"\033[0;31m";
      if (getenv("MIST_COLOR_ERROR")){ERROR_msg = getenv("MIST_COLOR_ERROR");}
      WARN_msg = (char*)"\033[0;1;33m";
      if (getenv("MIST_COLOR_WARN")){WARN_msg = getenv("MIST_COLOR_WARN");}
      INFO_msg = (char*)"\033[0;36m";
      if (getenv("MIST_COLOR_INFO")){INFO_msg = getenv("MIST_COLOR_INFO");}
    }else{
      color_end = (char*)"";
      color_time = (char*)"";
      CONF_msg = (char*)"";
      FAIL_msg = (char*)"";
      ERROR_msg = (char*)"";
      WARN_msg = (char*)"";
      INFO_msg = (char*)"";
    }
    while (fgets(buf, 1024, output)){
      unsigned int i = 0;
      char * kind = buf;//type of message, at begin of string
      char * progname = 0;
      char * progpid = 0;
      char * lineno = 0;
      char * message = 0;
      while (i < 9 && buf[i] != '|' && buf[i] != 0){++i;}
      if (buf[i] == '|'){
        buf[i] = 0;//insert null byte
        ++i;
        progname = buf+i;//progname starts here
      }
      while (i < 30 && buf[i] != '|' && buf[i] != 0){++i;}
      if (buf[i] == '|'){
        buf[i] = 0;//insert null byte
        ++i;
        progpid = buf+i;//progpid starts here
      }
      while (i < 40 && buf[i] != '|' && buf[i] != 0){++i;}
      if (buf[i] == '|'){
        buf[i] = 0;//insert null byte
        ++i;
        lineno = buf+i;//lineno starts here
      }
      while (i < 80 && buf[i] != '|' && buf[i] != 0){++i;}
      if (buf[i] == '|'){
        buf[i] = 0;//insert null byte
        ++i;
        message = buf+i;//message starts here
      }
      //find end of line, insert null byte
      unsigned int j = i;
      while (j < 1023 && buf[j] != '\n' && buf[j] != 0){++j;}
      buf[j] = 0;
      //print message
      if (message){
        if (callback){callback(kind, message, true);}
        color_msg = color_end;
        if (colored){
          if (!strcmp(kind, "CONF")){color_msg = CONF_msg;}
          if (!strcmp(kind, "FAIL")){color_msg = FAIL_msg;}
          if (!strcmp(kind, "ERROR")){color_msg = ERROR_msg;}
          if (!strcmp(kind, "WARN")){color_msg = WARN_msg;}
          if (!strcmp(kind, "INFO")){color_msg = INFO_msg;}
        }
        time_t rawtime;
        struct tm *timeinfo;
        char buffer[100];
        time(&rawtime);
        timeinfo = localtime(&rawtime);
        strftime(buffer, 100, "%F %H:%M:%S", timeinfo);
        dprintf(out, "%s[%s] ", color_time, buffer);
        if (progname && progpid && strlen(progname) && strlen(progpid)){
          dprintf(out, "%s (%s) ", progname, progpid);
        }
        dprintf(out, "%s%s: %s%s", color_msg, kind, message, color_end);
        if (lineno && strlen(lineno)){
          dprintf(out, " (%s) ", lineno);
        }
        dprintf(out, "\n");
      }else{
        //could not be parsed as log string - print the whole thing
        dprintf(out, "%s\n", buf);
      }
    }
    fclose(output);
    close(in);
  }

  FieldAccX::FieldAccX(RelAccX * _src, RelAccXFieldData _field, char * _data) : src(_src), field(_field), data(_data) {}

  uint64_t FieldAccX::uint(size_t recordNo) const {
    return src->getInt(field, recordNo);
  }

  std::string FieldAccX::string(size_t recordNo) const {
    return std::string(src->getPointer(field, recordNo), field.size) ;
  }

  void FieldAccX::set(uint64_t val, size_t recordNo){
    src->setInt(field, val, recordNo);
  }

  void FieldAccX::set(const std::string & val, size_t recordNo){
    char * place = src->getPointer(field, recordNo);
    memcpy(place, val.data(), std::min((size_t)field.size, val.size()));
  }

  /// If waitReady is true (default), waits for isReady() to return true in 50ms sleep increments.
  RelAccX::RelAccX(char * data, bool waitReady){
    if (!data){
      p = 0;
      return;
    }
    p = data;
    if (waitReady){
      while (!isReady()){Util::sleep(50);}
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
        DONTEVEN_MSG("Field %s: type %u, size %" PRIu32 ", offset %" PRIu64, fieldName.c_str(), fieldType, size, dataOffset);
        dataOffset += size;
        offset += nameLen + typeLen + 1;
      }
    }
  }

  /// Gets the amount of records present in the structure.
  uint32_t RelAccX::getRCount() const{return RAXHDR_RECORDCNT;}

  /// Gets the size in bytes of a single record in the structure.
  uint32_t RelAccX::getRSize() const{return RAXHDR_RECORDSIZE;}

  /// Gets the number of deleted records
  uint64_t RelAccX::getDeleted() const{return RAXHDR_DELETED;}

  /// Gets the number of the last valid index
  uint64_t RelAccX::getEndPos() const{return RAXHDR_ENDPOS;}

  ///Gets the number of fields per recrd 
  uint32_t RelAccX::getFieldCount() const{return fields.size();}

  /// Gets the offset from the structure start where records begin.
  uint16_t RelAccX::getOffset() const{return *(uint16_t *)(p + 26);}

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

  /// Converts the given record number into an offset of records after getOffset()'s offset.
  /// Does no bounds checking whatsoever, allowing access to not-yet-created or already-deleted
  /// records.
  /// This access method is stable with changing start/end positions and present record counts,
  /// because it only
  /// depends on the record count, which may not change for ring buffers.
  uint32_t RelAccX::getRecordPosition(uint64_t recordNo) const{
    if (getRCount()){
      return recordNo % getRCount();
    }else{
      return recordNo;
    }
  }

  /// Returns the (max) size of the given field.
  /// For string types, returns the exact size excluding terminating null byte.
  /// For other types, returns the maximum size possible.
  /// Returns 0 if the field does not exist.
  uint32_t RelAccX::getSize(const std::string &name, uint64_t recordNo) const{
    const RelAccXFieldData &fd = fields.at(name);
    if ((fd.type & 0xF0) == RAX_STRING){
      if (!fields.count(name) || !isRecordAvailable(recordNo)){return 0;}
      return strnlen(RECORD_POINTER, fd.size);
    }else{
      return fd.size;
    }
  }

  /// Returns a pointer to the given field in the given record number.
  /// Returns a null pointer if the field does not exist.
  char *RelAccX::getPointer(const std::string &name, uint64_t recordNo) const{
    if (!fields.count(name)){return 0;}
    return getPointer(fields.at(name), recordNo);
  }

  char * RelAccX::getPointer(const RelAccXFieldData & fd, uint64_t recordNo) const{
    return RECORD_POINTER;
  }

  /// Returns the value of the given integer-type field in the given record, as an uint64_t type.
  /// Returns 0 if the field does not exist or is not an integer type.
  uint64_t RelAccX::getInt(const std::string &name, uint64_t recordNo) const{
    if (!fields.count(name)){return 0;}
    return getInt(fields.at(name), recordNo);
  }

  uint64_t RelAccX::getInt(const RelAccXFieldData & fd, uint64_t recordNo) const{
    char * ptr = RECORD_POINTER;
    if ((fd.type & 0xF0) == RAX_UINT){//unsigned int
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
    r << std::string(indent, ' ') << "RelAccX: " << getRCount() << " x " << getRSize() << "b @" << getOffset() << " (#" << getDeleted() << " - #" << getEndPos()-1 << ")" << std::endl;
    for (uint64_t i = delled; i < max; ++i){
      r << std::string(indent + 2, ' ') << "#" << i << ":" << std::endl;
      for (std::map<std::string, RelAccXFieldData>::const_iterator it = fields.begin(); it != fields.end(); ++it){
        r << std::string(indent + 4, ' ') << it->first << ": ";
        switch (it->second.type & 0xF0){
          case RAX_INT: r << (int64_t)getInt(it->first, i) << std::endl; break;
          case RAX_UINT: r << getInt(it->first, i) << std::endl; break;
          case RAX_STRING: r << getPointer(it->first, i) << std::endl; break;
          case 0: {  //RAX_NESTED
            RelAccX n(getPointer(it->first, i), false);
            if (n.isReady()){
              r << "Nested RelAccX:" << std::endl;
              r << (n.getFieldCount() > 6 ? n.toPrettyString(indent + 6) : n.toCompactString(indent + 6));
            }else{
              r << "Nested RelAccX: not ready" << std::endl;
            }
            break;
          }
          case RAX_RAW: {
            char * ptr = getPointer(it->first, i);
            size_t sz = getSize(it->first, i);
            size_t zeroCount = 0;
            for (size_t j = 0; j < sz && j < 100 && zeroCount < 10; ++j){
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
    r << std::string(indent, ' ') << "RelAccX: " << getRCount() << " x " << getRSize() << "b @" << getOffset() << " (#" << getDeleted() << " - #" << getEndPos()-1 << ")" << std::endl;
    for (uint64_t i = delled; i < max; ++i){
      r << std::string(indent + 2, ' ') << "#" << i << ": ";
      for (std::map<std::string, RelAccXFieldData>::const_iterator it = fields.begin(); it != fields.end(); ++it){
        r << it->first << ": ";
        switch (it->second.type & 0xF0){
          case RAX_INT: r << (int64_t)getInt(it->first, i) << ", "; break;
          case RAX_UINT: r << getInt(it->first, i) << ", "; break;
          case RAX_STRING: r << getPointer(it->first, i) << ", "; break;
          case 0: {  //RAX_NESTED
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
    uint16_t &offset = RAXHDR_OFFSET;
    uint32_t &recSize = RAXHDR_RECORDSIZE;
    // The first field initializes the offset and record size.
    if (!fields.size()){
      recSize = 0;                 // Nothing yet, this is the first data field.
      offset = RAX_REQDFIELDS_LEN; // All mandatory fields are first - so we start there.
      RAXHDR_FIELDOFFSET = offset; // store the field_offset
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
    fields[name] = RelAccXFieldData(fType, fLen, recSize);

    // write the data to memory
    p[offset] = (name.size() << 3) | (typeLen & 0x7);
    memcpy(p + offset + 1, name.data(), name.size());
    p[offset + 1 + name.size()] = fType;
    if (typeLen == 2){*(uint8_t *)(p + offset + 2 + name.size()) = fLen;}
    if (typeLen == 3){*(uint16_t *)(p + offset + 2 + name.size()) = fLen;}
    if (typeLen == 5){*(uint32_t *)(p + offset + 2 + name.size()) = fLen;}

    // Calculate new offset and record size
    offset += 1 + name.size() + typeLen;
    recSize += fLen;
  }

  /// Sets the record counter to the given value.
  void RelAccX::setRCount(uint32_t count){RAXHDR_RECORDCNT = count;}

  /// Sets the number of deleted records
  void RelAccX::setDeleted(uint64_t n){RAXHDR_DELETED = n;}

  /// Sets the number of the last valid index
  void RelAccX::setEndPos(uint64_t n){RAXHDR_ENDPOS = n;}

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
    if (!fields.count(name)){
      WARN_MSG("Setting non-existent string %s", name.c_str());
      return;
    }
    const RelAccXFieldData &fd = fields.at(name);
    if ((fd.type & 0xF0) != RAX_STRING){
      WARN_MSG("Setting non-string %s", name.c_str());
      return;
    }
    char *ptr = RECORD_POINTER;
    memcpy(ptr, val.data(), std::min((uint32_t)val.size(), fd.size));
    ptr[std::min((uint32_t)val.size(), fd.size - 1)] = 0;
  }

  /// Writes the given int to the given field in the given record.
  /// Fails if ready is not set or the field is not an integer type.
  void RelAccX::setInt(const std::string &name, uint64_t val, uint64_t recordNo){
    if (!fields.count(name)){
      WARN_MSG("Setting non-existent integer %s", name.c_str());
      return;
    }
    return setInt(fields.at(name), val, recordNo);
  }
  
  void RelAccX::setInt(const RelAccXFieldData & fd, uint64_t val, uint64_t recordNo){
    char * ptr = RECORD_POINTER;
    if ((fd.type & 0xF0) == RAX_UINT){//unsigned int
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

  void RelAccX::setInts(const std::string & name, uint64_t * values, size_t len){
    if (!fields.count(name)){
      WARN_MSG("Setting non-existent integer %s", name.c_str());
      return;
    }
    const RelAccXFieldData & fd = fields.at(name); 
    for (uint64_t recordNo = 0; recordNo < len; recordNo++){
      setInt(fd, values[recordNo], recordNo);
    }
  }

  /// Updates the deleted record counter, the start position and the present record counter,
  /// shifting the ring buffer start position forward without moving the ring buffer end position.
  void RelAccX::deleteRecords(uint32_t amount){
    RAXHDR_DELETED += amount; // update deleted record counter
  }

  /// Updates the present record counter, shifting the ring buffer end position forward without
  /// moving the ring buffer start position.
  void RelAccX::addRecords(uint32_t amount){
    RAXHDR_ENDPOS += amount;
  }

  FieldAccX RelAccX::getFieldAccX(const std::string & fName){
    return FieldAccX(this, fields.at(fName), p + getOffset());
  }
}

