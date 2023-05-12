#pragma once
#include "input.h"

#include <mist/ebml.h>
#include <mist/ebml_socketglue.h>
#include <mist/urireader.h>
#include <mist/util.h>

namespace Mist{

  class InputEBML : public Input, public Util::DataCallback{
  public:
    InputEBML(Util::Config *cfg);
    bool needsLock();
    virtual bool isSingular(){return standAlone && !config->getBool("realtime");}
    virtual void dataCallback(const char *ptr, size_t size);
    virtual size_t getDataCallbackPos() const;

  protected:

    HTTP::URIReader inFile;
    Util::ResizeablePointer readBuffer;
    uint64_t readBufferOffset;
    uint64_t readPos;
    bool firstRead;
    bool readingMinimal;
    uint64_t lastClusterBPos;
    EBML::toDTSC parser;
    size_t totalBytes;

    /// For live streams: to update the stats with correct values.
    virtual size_t streamByteCount() { return totalBytes; }

    bool checkArguments();
    bool preRun();
    bool readHeader();
    void postHeader();
    bool readElement();
    void readTrack(const EBML::Element & E);
    void getNext(size_t idx = INVALID_TRACK_ID);
    void seek(uint64_t seekTime, size_t idx = INVALID_TRACK_ID);
    bool readExistingHeader();
    void parseStreamHeader(){readHeader();}
    bool openStreamSource(){return true;}
    bool needHeader() { return (config->getBool("realtime") || needsLock()) && !readExistingHeader(); }
  };
}// namespace Mist

typedef Mist::InputEBML mistIn;
