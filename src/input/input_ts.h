#include "input.h"
#include <mist/dtsc.h>
#include <mist/nal.h>
#include <mist/ts_packet.h>
#include <mist/ts_stream.h>
#include <set>
#include <string>

namespace Mist{
  /// This class contains all functions needed to implement TS Input
  class inputTS : public Input{
  public:
    inputTS(Util::Config *cfg);
    ~inputTS();
    virtual bool needsLock();

    virtual std::string getConnectedBinHost(){
      if (tcpCon){return tcpCon.getBinHost();}
      /// \TODO Handle UDP
      return Input::getConnectedBinHost();
    }
  protected:
    // Private Functions
    bool checkArguments();
    bool preRun();
    bool readHeader();
    virtual bool needHeader();
    virtual void getNext(size_t idx = INVALID_TRACK_ID);
    void seek(uint64_t seekTime, size_t idx = INVALID_TRACK_ID);
    void readPMT();
    bool openStreamSource();
    void parseStreamHeader();
    void streamMainLoop();
    void finish();
    FILE *inFile;        ///< The input file with ts data
    TS::Assembler assembler;
    TS::Stream tsStream; ///< Used for parsing the incoming ts stream
    Socket::UDPConnection udpCon;
    Socket::Connection tcpCon;
    TS::Packet tsBuf;
    pid_t inputProcess;
    size_t tmpIdx;
    bool isFinished;

    bool rawMode;
    Util::ResizeablePointer rawBuffer;
    size_t rawIdx;
    uint64_t lastRawPacket;
  };
}// namespace Mist

typedef Mist::inputTS mistIn;
