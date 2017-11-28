#include "input.h"
#include <mist/nal.h>
#include <mist/dtsc.h>
#include <mist/ts_packet.h>
#include <mist/ts_stream.h>
#include <string>
#include <set>


namespace Mist {
  /// This class contains all functions needed to implement TS Input
  class inputTS : public Input {
    public:
      inputTS(Util::Config * cfg);
      ~inputTS();
      bool needsLock();
    protected:
      //Private Functions
      bool checkArguments(){return true;}
      bool preRun();
      bool readHeader();
      bool needHeader();
      void getNext(bool smart = true);
      void seek(int seekTime);
      void trackSelect(std::string trackSpec);
      void readPMT();
      bool openStreamSource();
      void parseStreamHeader();
      std::string streamMainLoop();
      void finish();
      FILE * inFile;///<The input file with ts data
      TS::Stream tsStream;///<Used for parsing the incoming ts stream
      Socket::UDPConnection udpCon;
      Socket::Connection tcpCon;
      TS::Packet tsBuf;
      pid_t inputProcess;
  };
}

typedef Mist::inputTS mistIn;

