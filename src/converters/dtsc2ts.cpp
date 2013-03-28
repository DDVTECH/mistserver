#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstdlib>
#include <cstdio>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <mist/ts_packet.h> //TS support
#include <mist/dtsc.h> //DTSC support
#include <mist/mp4.h> //For initdata conversion
#include <mist/config.h>

///\brief Holds everything unique to converters.
namespace Converters {
  ///\brief Converts DTSC from stdin to TS on stdout.
  ///\return The return code for the converter.
  int DTSC2TS(){
    char charBuffer[1024 * 10];
    unsigned int charCount;
    std::string StrData;
    TS::Packet PackData;
    DTSC::Stream Strm;
    int PacketNumber = 0;
    long long unsigned int TimeStamp = 0;
    int ThisNaluSize;
    char VideoCounter = 0;
    char AudioCounter = 0;
    bool WritePesHeader;
    bool IsKeyFrame;
    MP4::AVCC avccbox;
    bool haveAvcc = false;
    std::stringstream TSBuf;


    while (std::cin.good()){
      if (Strm.parsePacket(StrData)){
        if (Strm.lastType() == DTSC::PAUSEMARK){
          TSBuf.flush();
          if (TSBuf.str().size()){
            std::cout << TSBuf.str();
            TSBuf.str("");
            PacketNumber = 0;
          }
          TSBuf.str("");
        }
        if ( !haveAvcc){
          avccbox.setPayload(Strm.metadata["video"]["init"].asString());
          haveAvcc = true;
        }
        if (Strm.lastType() == DTSC::VIDEO || Strm.lastType() == DTSC::AUDIO){
          Socket::Buffer ToPack;
          //write PAT and PMT TS packets
          if (PacketNumber == 0){
            PackData.DefaultPAT();
            TSBuf.write(PackData.ToString(), 188);
            PackData.DefaultPMT();
            TSBuf.write(PackData.ToString(), 188);
            PacketNumber += 2;
          }

          int PIDno = 0;
          char * ContCounter = 0;
          if (Strm.lastType() == DTSC::VIDEO){
            IsKeyFrame = Strm.getPacket(0).isMember("keyframe");
            if (IsKeyFrame){
              TimeStamp = (Strm.getPacket(0)["time"].asInt() * 27000);
            }
            ToPack.append(avccbox.asAnnexB());
            while (Strm.lastData().size()){
              ThisNaluSize = (Strm.lastData()[0] << 24) + (Strm.lastData()[1] << 16) + (Strm.lastData()[2] << 8) + Strm.lastData()[3];
              Strm.lastData().replace(0, 4, TS::NalHeader, 4);
              if (ThisNaluSize + 4 == Strm.lastData().size()){
                ToPack.append(Strm.lastData());
                break;
              }else{
                ToPack.append(Strm.lastData().c_str(), ThisNaluSize + 4);
                Strm.lastData().erase(0, ThisNaluSize + 4);
              }
            }
            ToPack.prepend(TS::Packet::getPESVideoLeadIn(0ul, Strm.getPacket(0)["time"].asInt() * 90));
            PIDno = 0x100;
            ContCounter = &VideoCounter;
          }else if (Strm.lastType() == DTSC::AUDIO){
            ToPack.append(TS::GetAudioHeader(Strm.lastData().size(), Strm.metadata["audio"]["init"].asString()));
            ToPack.append(Strm.lastData());
            ToPack.prepend(TS::Packet::getPESAudioLeadIn(ToPack.bytes(1073741824ul), Strm.getPacket(0)["time"].asInt() * 90));
            PIDno = 0x101;
            ContCounter = &AudioCounter;
          }

          //initial packet
          PackData.Clear();
          PackData.PID(PIDno);
          PackData.ContinuityCounter(( *ContCounter)++);
          PackData.UnitStart(1);
          if (IsKeyFrame){
            PackData.RandomAccess(1);
            PackData.PCR(TimeStamp);
          }
          unsigned int toSend = PackData.AddStuffing(ToPack.bytes(184));
          std::string gonnaSend = ToPack.remove(toSend);
          PackData.FillFree(gonnaSend);
          TSBuf.write(PackData.ToString(), 188);
          PacketNumber++;

          //rest of packets
          while (ToPack.size()){
            PackData.Clear();
            PackData.PID(PIDno);
            PackData.ContinuityCounter(( *ContCounter)++);
            toSend = PackData.AddStuffing(ToPack.bytes(184));
            gonnaSend = ToPack.remove(toSend);
            PackData.FillFree(gonnaSend);
            TSBuf.write(PackData.ToString(), 188);
            PacketNumber++;
          }

        }
      }else{
        std::cin.read(charBuffer, 1024 * 10);
        charCount = std::cin.gcount();
        StrData.append(charBuffer, charCount);
      }
    }
    return 0;
  }
}

///\brief Entry point for DTSC2TS, simply calls Converters::DTSC2TS().
int main(int argc, char* argv[]){
  Util::Config conf = Util::Config(argv[0], PACKAGE_VERSION);
  conf.parseArgs(argc, argv);
  return Converters::DTSC2TS();
}
