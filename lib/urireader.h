#pragma once
#include "downloader.h"
#include "util.h"
#include <fstream>
namespace HTTP{

  enum URIType{Closed = 0, File, Stream, HTTP};

  /// Opens a generic URI for reading. Supports streams/pipes, HTTP(S) and file access.
  /// Supports seeking, partial and full reads; emulating behaviour where necessary.
  /// Calls progress callback for long-duration operations, if set.
  class URIReader : public Util::DataCallback{
  public:
    // Setters/initers

    /// Sets the internal URI to the current working directory, but does not call open().
    URIReader();
    /// Calls open on the given uri during construction
    URIReader(const HTTP::URL &uri);
    /// Calls open on the given relative uri during construction
    /// URI is resolved relative to the current working directory
    URIReader(const std::string &reluri);
    /// Sets the internal URI to file://- and opens the given file descriptor in stream mode.
    bool open(const int fd);
    /// Sets the internal URI to the given URI and opens it, whatever that may mean for the given URI type.
    bool open(const HTTP::URL &uri);
    /// Links the internal URI to the given relative URI and opens it, whatever that may mean for the current URI type.
    bool open(const std::string &reluri);
    /// Seeks to the given position, relative to fragment's #start=X value or 0 if not set.
    bool seek(const uint64_t pos);
    /// Reads all data from start to end, calling the dataCallback whenever minLen/maxLen require it.
    void readAll(size_t (*dataCallback)(const char *data, size_t len));

    /// Reads all data from start to end, returning it in a single buffer with all data.
    void readAll(char *&dataPtr, size_t &dataLen);
    /// Reads all data from start to end, using callbacks
    void readAll(Util::DataCallback &cb);

    /// Reads wantedLen bytes of data from current position, calling the dataCallback whenever minLen/maxLen require it.
    void readSome(size_t (*dataCallback)(const char *data, size_t len), size_t wantedLen);
    /// Reads wantedLen bytes of data from current position, returning it in a single buffer.
    void readSome(char *&dataPtr, size_t &dataLen, size_t wantedLen);

    void readSome(size_t wantedLen, Util::DataCallback &cb);

    /// Closes the currently open URI. Does not change the internal URI value.
    void close();

    // Configuration setters

    /// Progress callback, called whenever transfer stalls. Not called if unset.
    void onProgress(bool (*progressCallback)(uint8_t));
    /// Sets minimum and maximum buffer size for read calls that use callbacks
    void setBounds(size_t minLen = 0, size_t maxLen = 0);

    // Static getters
    bool isSeekable() const; ///< Returns true if seeking is possible in this URI.
    bool isEOF() const;      ///< Returns true if the end of the URI has been reached.
    operator bool() const{return !isEOF();}///< Returns !isEOF()
    uint64_t getPos();                         ///< Returns the current byte position in the URI.
    const HTTP::URL &getURI() const; ///< Returns the most recently open URI, or the current working directory if not set.
    size_t getSize() const; ///< Returns the size of the currently open URI, if known. Returns std::string::npos if unknown size.

    void (*httpBodyCallback)(const char *ptr, size_t size);
    virtual void dataCallback(const char *ptr, size_t size);
    virtual size_t getDataCallbackPos() const;

    std::string userAgentOverride;

    std::string getHost() const; ///< Gets hostname for connection, or [::] if local.
    std::string getBinHost() const; ///< Gets binary form hostname for connection, or [::] if local.

  private:
    // Internal state variables
    bool (*cbProgress)(uint8_t); ///< The progress callback, if any. Not called if set to a null pointer.
    HTTP::URL myURI; ///< The most recently open URI, or the current working directory if nothing has been opened yet.
    size_t minLen;   ///< Minimum buffer size for dataCallback.
    size_t maxLen;   ///< Maximum buffer size for dataCallback.
    size_t startPos; ///< Start position for byte offsets.
    size_t endPos;   ///< End position for byte offsets.
    size_t totalSize; ///< Total size in bytes of the current URI. May be incomplete before read finished.
    size_t curPos; ///< Current read position in source
    size_t bufPos; ///< Current read position in buffer
    int handle;    ///< Open file handle, if file-based.
    char *mapped;  ///< Memory-map of open file handle, if file-based.
    HTTP::URL originalUrl;
    bool supportRangeRequest;
    Util::ResizeablePointer rPtr;
    Util::ResizeablePointer allData;
    bool clearPointer;
    URIType stateType;       ///< Holds the type of URI this is, for internal processing purposes.
    HTTP::Downloader downer; ///< For HTTP(S)-based URIs, the Downloader instance used for the download.
    void init();
  };

  HTTP::URL localURIResolver();
}// namespace HTTP
