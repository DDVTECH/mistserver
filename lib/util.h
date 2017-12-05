#include <deque>
#include <map>
#include <stdint.h>
#include <string>

namespace Util{
  bool isDirectory(const std::string &path);
  bool createPathFor(const std::string &file);
  bool createPath(const std::string &path);
  bool stringScan(const std::string &src, const std::string &pattern,
                  std::deque<std::string> &result);
  void stringToLower(std::string &val);

  uint64_t ftell(FILE *stream);
  uint64_t fseek(FILE *stream, uint64_t offset, int whence);

  /// Helper class that maintains a resizeable pointer and will free it upon deletion of the class.
  class ResizeablePointer{
    public:
      ResizeablePointer();
      ~ResizeablePointer();
      inline uint32_t& size(){return currSize;}
      bool assign(void * p, uint32_t l);
      bool append(void * p, uint32_t l);
      bool allocate(uint32_t l);
      inline operator char*(){return (char*)ptr;}
      inline operator void*(){return ptr;}
    private:
      void * ptr;
      uint32_t currSize;
      uint32_t maxSize;

  };

  void logParser(int in, int out, bool colored, void callback(std::string, std::string, bool) = 0);
  void redirectLogsIfNeeded();

  /// Holds type, size and offset for RelAccX class internal data fields.
  class RelAccXFieldData{
  public:
    uint8_t type;
    uint32_t size;
    uint32_t offset;
    RelAccXFieldData(){}
    RelAccXFieldData(uint8_t t, uint32_t s, uint32_t o){
      type = t;
      size = s;
      offset = o;
    }
  };

  #define RAX_NESTED 0x01
  #define RAX_UINT 0x10
  #define RAX_INT 0x20
  #define RAX_16UINT 0x11
  #define RAX_16INT 0x21
  #define RAX_32UINT 0x13
  #define RAX_32INT 0x23
  #define RAX_64UINT 0x17
  #define RAX_64INT 0x27
  #define RAX_STRING 0x30
  #define RAX_32STRING 0x31
  #define RAX_64STRING 0x32
  #define RAX_128STRING 0x33
  #define RAX_256STRING 0x34
  #define RAX_RAW 0x40
  #define RAX_256RAW 0x44
  #define RAX_512RAW 0x45

  /// Reliable Access class.
  /// Provides reliable access to memory data structures, using dynamic static offsets and a status
  /// field.
  /// All internal fields are host byte order (since no out-of-machine accesses happen), except 24
  /// bit fields, which are network byte order.
  /// Data structure:
  ///     1 byte status bit fields (1 = ready, 2 = exit, 4 = reload)
  ///     1 byte field_offset (where the field description starts)
  ///     4 bytes record count - number of records present
  ///     4 bytes record size - bytes per record
  ///     4 bytes record startpos - position of record where ring starts
  ///     8 bytes records deleted - amount of records no longer present
  ///     4 bytes records present - amount of record currently present
  ///     2 bytes record offset
  ///     @field_offset: offset-field_offset bytes fields:
  ///       5 bits field name len (< 32), 3 bits type len (1-5)
  ///       len bytes field name string (< 32 bytes)
  ///       1 byte field type (0x01 = RelAccX, 0x1X = uint, 0x2X = int, 0x3X = string, 0x4X =
  ///       binary)
  ///         if type-len > 1: rest-of-type-len bytes max len
  ///         else, for 0xYX:
  ///           Y=1/2: X+1 bytes maxlen (1-16b)
  ///           Y=3/4: (16 << X) bytes maxlen (16b-256kb)
  ///     record count * record size bytes, in field order:
  ///       0x01: RelAccX record
  ///       0x1X/2X: X+1 bytes (u)int data
  ///       0x3X: max maxlen bytes string data, zero term'd
  ///       0x4X: maxlen bytes binary data
  /// Setting ready means the record size, offset and fields will no longer change. Count may still
  /// go up (not down)
  /// Setting exit means the writer has exited, and readers should exit too.
  /// Setting reload means the writer needed to change fields, and the pointer should be closed and
  /// re-opened through outside means (e.g. closing and re-opening the containing shm page).
  class RelAccX{
  public:
    RelAccX(char *data, bool waitReady = true);
    // Read-only functions:
    uint32_t getRCount() const;
    uint32_t getRSize() const;
    uint16_t getOffset() const;
    uint32_t getStartPos() const;
    uint64_t getDeleted() const;
    uint32_t getPresent() const;
    bool isReady() const;
    bool isExit() const;
    bool isReload() const;
    bool isRecordAvailable(uint64_t recordNo) const;
    uint32_t getRecordPosition(uint64_t recordNo) const;
    uint32_t getSize(const std::string &name, uint64_t recordNo = 0) const;
    char *getPointer(const std::string &name, uint64_t recordNo = 0) const;
    uint64_t getInt(const std::string &name, uint64_t recordNo = 0) const;
    std::string toPrettyString() const;
    // Read-write functions:
    void addField(const std::string &name, uint8_t fType, uint32_t fLen = 0);
    void setRCount(uint32_t count);
    void setStartPos(uint32_t n);
    void setDeleted(uint64_t n);
    void setPresent(uint32_t n);
    void setReady();
    void setExit();
    void setReload();
    void setString(const std::string &name, const std::string &val, uint64_t recordNo = 0);
    void setInt(const std::string &name, uint64_t val, uint64_t recordNo = 0);
    void deleteRecords(uint32_t amount);
    void addRecords(uint32_t amount);

  protected:
    static uint32_t getDefaultSize(uint8_t fType);

  private:
    char *p;
    std::map<std::string, RelAccXFieldData> fields;
  };
}

