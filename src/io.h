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

    bool isBuffered(size_t idx, uint32_t keyNum, DTSC::Meta & aMeta);
    uint32_t bufferedOnPage(size_t idx, uint32_t keyNum, DTSC::Meta & aMeta);

    size_t getMainSelectedTrack();

    bool bufferStart(size_t idx, uint32_t pageNumber, IPC::sharedPage & page, DTSC::Meta & aMeta);
    void bufferFinalize(size_t idx, IPC::sharedPage & page);
    void liveFinalize(size_t idx);
    bool isCurrentLivePage(size_t idx, uint32_t pageNumber);
    void bufferRemove(size_t idx, uint32_t pageNumber);
    void bufferLivePacket(const DTSC::Packet &packet);

    void bufferNext(uint64_t packTime, int64_t packOffset, uint32_t packTrack, const char *packData,
                    size_t packDataSize, uint64_t packBytePos, bool isKeyframe, IPC::sharedPage & page, DTSC::Meta & aMeta);
    void bufferNext(uint64_t packTime, int64_t packOffset, uint32_t packTrack, const char *packData,
                    size_t packDataSize, uint64_t packBytePos, bool isKeyframe, IPC::sharedPage & page);
    void bufferLivePacket(uint64_t packTime, int64_t packOffset, uint32_t packTrack, const char *packData,
                          size_t packDataSize, uint64_t packBytePos, bool isKeyframe);
    void bufferLivePacket(uint64_t packTime, int64_t packOffset, uint32_t packTrack, const char *packData,
                          size_t packDataSize, uint64_t packBytePos, bool isKeyframe, DTSC::Meta & aMeta);
    const std::string & getStreamName() const{return streamName;}

  protected:
    void updateTrackFromKeyframe(uint32_t packTrack, const char *packData, size_t packDataSize, DTSC::Meta & aMeta);
    bool standAlone;

    DTSC::Packet thisPacket; // The current packet that is being parsed
    size_t thisIdx; //Track index of current packet
    uint64_t thisTime; //Time of current packet

    std::string streamName;

    DTSC::Meta meta;
    const DTSC::Meta &M;

    std::map<size_t, Comms::Users> userSelect;

  private:
    std::map<uint32_t, IPC::sharedPage> livePage;
    std::map<uint32_t, size_t> curPageNum;
  };
}// namespace Mist
