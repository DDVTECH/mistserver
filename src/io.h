#pragma once

#include <map>
#include <deque>
#include <mist/shared_memory.h>
#include <mist/defines.h>
#include <mist/dtsc.h>

#include <mist/encryption.h>//LTS
namespace Mist {
  enum negotiationState {
    FILL_NEW,///< New track, just sent negotiation request
    FILL_NEG,///< Negotiating this track, written metadata
    FILL_DEC,///< Declined Track
    FILL_ACC///< Accepted Track
  };

  struct DTSCPageData {
    DTSCPageData() : pageNum(0), keyNum(0), partNum(0), dataSize(0), curOffset(0), firstTime(0), lastKeyTime(-5000){}
    unsigned long pageNum;///The current page number
    unsigned long keyNum;///<The number of keyframes in this page.
    unsigned long partNum;///<The number of parts in this page.
    unsigned long long int dataSize;///<The full size this page should be.
    unsigned long long int curOffset;///<The current write offset in the page.
    unsigned long long int firstTime;///<The first timestamp of the page.
    unsigned long lastKeyTime;///<The last key time encountered on this track.
  };

  ///\brief Class containing all basic input and output functions.
  class InOutBase {
    public:
      void initiateMeta();
      bool bufferStart(unsigned long tid, unsigned long pageNumber);
      void bufferNext(DTSC::Packet & pack);
      void bufferNext(JSON::Value & pack);
      void bufferFinalize(unsigned long tid);
      void bufferRemove(unsigned long tid, unsigned long pageNumber);
      void bufferLivePacket(JSON::Value & packet);
      void bufferLivePacket(DTSC::Packet & packet);
      bool isBuffered(unsigned long tid, unsigned long keyNum);
      unsigned long bufferedOnPage(unsigned long tid, unsigned long keyNum);
    protected:
      bool standAlone;
      static Util::Config * config;
      void initiateEncryption();//LTS

      void continueNegotiate(unsigned long tid);

      DTSC::Packet thisPacket;//The current packet that is being parsed

      std::string streamName;///< Name of the stream to connect to
      IPC::sharedClient userClient;///< Shared memory used for connection to Mixer process.

      DTSC::Meta myMeta;///< Stores either the input or output metadata

      std::set<unsigned long> selectedTracks;///< Stores the track id's that are either selected for playback or input
      std::map<unsigned long, std::map<unsigned long, DTSCPageData> > pagesByTrack;///<Holds the data for all pages of a track. Based on unmapped tids

      //Negotiation stuff (from unmapped tid's)
      std::map<unsigned long, unsigned long> trackOffset; ///< Offset of data on user page
      std::map<unsigned long, negotiationState> trackState; ///< State of the negotiation for tracks
      std::map<unsigned long, unsigned long> trackMap;///<Determines which input track maps onto which "final" track
      std::map<unsigned long, IPC::sharedPage> metaPages;///< For each track, holds the page that describes which dataPages are mapped
      std::map<unsigned long, unsigned long> curPageNum;///< For each track, holds the number page that is currently being written.
      std::map<unsigned long, IPC::sharedPage> curPage;///< For each track, holds the page that is currently being written.
      std::map<unsigned long, std::deque<DTSC::Packet> > trackBuffer; ///< Buffer to be used during active track negotiation

      bool encrypt;
      Encryption::verimatrixData vmData;
      std::map<int,unsigned long long int> iVecs;
      IPC::sharedPage encryptionPage;
  };
}
