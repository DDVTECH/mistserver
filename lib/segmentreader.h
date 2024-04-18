#include <http_parser.h>
#include <urireader.h>
#include <dtsc.h>
#include <ts_stream.h>
#include <mp4_stream.h>

namespace Mist{

  enum streamType {STRM_UNKN, STRM_TS, STRM_MP4};

  class SegmentReader: public Util::DataCallback{
  public:
    SegmentReader();
    void onProgress(bool (*callback)(uint8_t));
    operator bool() const {return isOpen;}

    char *packetPtr;
    bool load(const std::string &path, uint64_t startAt, uint64_t stopAt, const char * ivec, const char * keyAES, Util::ResizeablePointer * bufPtr);
    bool readNext(DTSC::Packet & thisPacket, uint64_t bytePos);
    void setInit(const std::string & initData);
    void reset();
    void close();
    void initializeMetadata(DTSC::Meta &meta, size_t tid, size_t mappingId);

    virtual void dataCallback(const char *ptr, size_t size);
    virtual size_t getDataCallbackPos() const;

  private:
    HTTP::URIReader segDL; ///< If non-buffered, reader for the data
    Util::ResizeablePointer * currBuf; ///< Storage for all (non)buffered segment content
    uint64_t startAtByte; ///< Start position in bytes
    uint64_t stopAtByte; ///< Stop position in bytes
    bool encrypted; ///< True if segment must be decrypted before parsing
    bool buffered; ///< True if segment is fully buffered in memory
    bool isOpen; ///< True if a segment has been successfully opened
    bool (*progressCallback)(uint8_t);
    
    bool readTo(size_t offset);
    size_t offset;

    // Parser related
    streamType parser;
    TS::Stream tsStream;
    std::deque<MP4::TrackHeader> mp4Headers;


#ifdef SSL
    //Encryption-related
    Util::ResizeablePointer decBuffer; ///< Buffer for pre-decryption data - max 16 bytes
    unsigned char tmpIvec[16];
    mbedtls_aes_context aes; ///< Decryption context
#endif
  };

} // namespace Mist
