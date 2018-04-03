#include <stdint.h>
#include <string>

namespace EBML{

  class UniInt{
  public:
    static uint8_t readSize(const char *p);
    static uint8_t writeSize(const uint64_t val);
    static uint64_t readInt(const char *p);
    static void writeInt(char *p, const uint64_t val);
    static int64_t readSInt(const char *p);
    static void writeSInt(char *p, const int64_t val);
  };

  enum ElementType{
    ELEM_UNKNOWN,
    ELEM_MASTER,
    ELEM_UINT,
    ELEM_INT,
    ELEM_STRING,
    ELEM_UTF8,
    ELEM_BIN,
    ELEM_FLOAT,
    ELEM_DATE,
    ELEM_BLOCK
  };

  enum ElementID{
    EID_EBML = 0x0A45DFA3,
    EID_EBMLVERSION = 0x286,
    EID_EBMLREADVERSION = 0x2F7,
    EID_EBMLMAXIDLENGTH = 0x2F2,
    EID_EBMLMAXSIZELENGTH = 0x2F3,
    EID_DOCTYPE = 0x282,
    EID_DOCTYPEVERSION = 0x287,
    EID_DOCTYPEREADVERSION = 0x285,
    EID_CODECID = 0x6,
    EID_TRACKTYPE = 0x3,
    EID_DEFAULTDURATION = 0x3E383,
    EID_DURATION = 0x489,
    EID_CHANNELS = 0x1F,
    EID_SAMPLINGFREQUENCY = 0x35,
    EID_TIMECODE = 0x67,
    EID_BITDEPTH = 0x2264,
    EID_TRACKENTRY = 0x2E,
    EID_TRACKUID = 0x33C5,
    EID_PIXELWIDTH = 0x30,
    EID_FLAGLACING = 0x1C,
    EID_PIXELHEIGHT = 0x3A,
    EID_DISPLAYWIDTH = 0x14B0,
    EID_DISPLAYHEIGHT = 0x14BA,
    EID_TRACKNUMBER = 0x57,
    EID_CODECPRIVATE = 0x23A2,
    EID_LANGUAGE = 0x2B59C,
    EID_VIDEO = 0x60,
    EID_AUDIO = 0x61,
    EID_TIMECODESCALE = 0xAD7B1,
    EID_MUXINGAPP = 0xD80,
    EID_WRITINGAPP = 0x1741,
    EID_CLUSTER = 0x0F43B675,
    EID_SEGMENT = 0x08538067,
    EID_INFO = 0x0549A966,
    EID_TRACKS = 0x0654AE6B,
    EID_SIMPLEBLOCK = 0x23,
    EID_SEEKHEAD = 0x014D9B74,
    EID_SEEK = 0xDBB,
    EID_SEEKID = 0x13AB,
    EID_SEEKPOSITION = 0x13AC,
    EID_CUES = 0xC53BB6B,
    EID_CUETRACK = 0x77,
    EID_CUECLUSTERPOSITION = 0x71,
    EID_CUERELATIVEPOSITION = 0x70,
    EID_CUETRACKPOSITIONS = 0x37,
    EID_CUETIME = 0x33,
    EID_CUEPOINT = 0x3B,
    EID_TAGS = 0x254c367,
    EID_CODECDELAY = 0x16AA,
    EID_SEEKPREROLL = 0x16BB,
    EID_UNKNOWN = 0
  };

  class Element{
  public:
    static uint64_t needBytes(const char *p, uint64_t availBytes, bool minimal = false);
    static std::string getIDString(uint32_t id);
    Element(const char *p = 0, bool minimal = false);
    inline operator bool() const{return data;}
    uint32_t getID() const;
    uint64_t getPayloadLen() const;
    uint8_t getHeaderLen() const;
    const char *getPayload() const;
    uint64_t getOuterLen() const;
    ElementType getType() const;
    virtual std::string toPrettyString(const uint8_t indent = 0, const uint8_t detail = 3) const;
    uint64_t getValUInt() const;
    int64_t getValInt() const;
    double getValFloat() const;
    std::string getValString() const;
    const Element findChild(uint32_t id) const;

  private:
    const char *data;
    bool minimalMode; ///<If set, ELEM_MASTER elements will not access payload data when
                      /// pretty-printing.
  };

  class Block : public Element{
  public:
    Block(const char *p = 0) : Element(p){}
    uint64_t getTrackNum() const;
    int16_t getTimecode() const;
    bool isKeyframe() const;
    bool isInvisible() const;
    bool isDiscardable() const;
    uint8_t getLacing() const;
    uint8_t getFrameCount() const;
    uint32_t getFrameSize(uint8_t no) const;
    const char *getFrameData(uint8_t no) const;
    virtual std::string toPrettyString(const uint8_t indent = 0, const uint8_t detail = 3) const;
  };
}

