#pragma once

#include <deque>
#include <map>
#include <mist/comms.h>
#include <mist/defines.h>
#include <mist/dtsc.h>
#include <mist/shared_memory.h>

#include <mist/encryption.h> //LTS
namespace Mist{
  ///\brief Class containing all basic input and output functions.
  class InOutBase{
  public:
    InOutBase();

    bool isBuffered(size_t idx, uint32_t keyNum);
    size_t bufferedOnPage(size_t idx, size_t keyNum);

    size_t getMainSelectedTrack();

    bool bufferStart(size_t idx, size_t pageNumber);
    void bufferFinalize(size_t idx);
    void bufferRemove(size_t idx, size_t pageNumber);
    void bufferLivePacket(const DTSC::Packet &packet);

    void bufferNext(uint64_t packTime, int64_t packOffset, uint32_t packTrack, const char *packData,
                    size_t packDataSize, uint64_t packBytePos, bool isKeyframe);
    void bufferLivePacket(uint64_t packTime, int64_t packOffset, uint32_t packTrack, const char *packData,
                          size_t packDataSize, uint64_t packBytePos, bool isKeyframe);

  protected:
    void updateTrackFromKeyframe(uint32_t packTrack, const char *packData, size_t packDataSize);
    bool standAlone;

    DTSC::Packet thisPacket; // The current packet that is being parsed

    std::string streamName;

    DTSC::Meta meta;
    const DTSC::Meta &M;

    std::map<size_t, Comms::Users> userSelect;

    std::map<size_t, size_t> curPageNum; ///< For each track, holds the number page that is currently being written.
    std::map<size_t, IPC::sharedPage> curPage; ///< For each track, holds the page that is currently being written.
  };
}// namespace Mist
