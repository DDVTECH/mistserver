/// \file ts_erase_track.cpp
/// A track whose thread ended with frames pending must not keep reporting a packet.
///
/// TS::Stream::eraseTrack() used to clear a track's buffers but not seenUnitStart[tid]. With two PES starts
/// pending at that moment, the next continuation packet for the PID made add() create an empty buffer and
/// point psCache at it; hasPacket(tid) then stayed true while parse(tid) found nothing, getPacket() logged
/// "PES without valid packets?" and changed nothing, and input_ts.cpp's `while (hasPacket(tid)) getPacket(tid)`
/// never ended. On a source that had gone quiet nothing ever moved the state on: one MistInTS logged the error
/// ~30 000 times a minute for days until it was killed.
#include <mist/defines.h>
#include <mist/dtsc.h>
#include <mist/ts_packet.h>
#include <mist/ts_stream.h>
#include <mist/util.h>
#include <cstdio>
#include <string>

static const int PMT_PID = 0x1000;

static void fin(std::string &p) { p.resize(188, (char)0xFF); }

static std::string pat(int cc) {
  std::string p("\x47\x40\x00", 3); p += (char)(0x10 | (cc & 15));
  const unsigned char s[] = {0x00, 0x00, 0xB0, 0x0D, 0x00, 0x01, 0xC1, 0x00, 0x00,
                             0x00, 0x01, 0xF0, 0x00, 0x2A, 0xB1, 0x04, 0xB2};
  p.append((const char *)s, sizeof s); fin(p); return p;
}

static std::string pmt(int cc) {
  std::string p("\x47\x50\x00", 3); p += (char)(0x10 | (cc & 15));
  const unsigned char s[] = {0x00, 0x02, 0xB0, 0x12, 0x00, 0x01, 0xC1, 0x00, 0x00,
                             0xE1, 0x01, 0xF0, 0x00,
                             0x0F, 0xE1, 0x01, 0xF0, 0x00,     // AAC on PID 257
                             0x00, 0x00, 0x00, 0x00};
  p.append((const char *)s, sizeof s); fin(p); return p;
}

// One AAC access unit per PES: an ADTS header (48 kHz, stereo, LC) and silence.
static std::string pesStart(int cc, uint64_t pts90) {
  std::string p("\x47\x41\x01", 3); p += (char)(0x10 | (cc & 15));
  const int frameLen = 7 + 160;
  unsigned char h[] = {0x00, 0x00, 0x01, 0xC0, 0x00, 0x00, 0x80, 0x80, 0x05,
                       (unsigned char)(0x21 | ((pts90 >> 29) & 0x0E)), (unsigned char)(pts90 >> 22),
                       (unsigned char)(0x01 | ((pts90 >> 14) & 0xFE)), (unsigned char)(pts90 >> 7),
                       (unsigned char)(0x01 | ((pts90 << 1) & 0xFE)),
                       0xFF, 0xF1, 0x4C, 0x80, (unsigned char)((frameLen >> 3) & 0xFF),
                       (unsigned char)(((frameLen & 7) << 5) | 0x1F), 0xFC};
  p.append((const char *)h, sizeof h); fin(p); return p;
}

static std::string pesCont(int cc) {
  std::string p("\x47\x01\x01", 3); p += (char)(0x10 | (cc & 15));
  fin(p); for (size_t i = 4; i < 188; ++i) p[i] = 0; return p;
}

static void add(TS::Stream &s, const std::string &raw){
  TS::Packet p;
  p.FromPointer(raw.data());
  s.add(p);
}

int main(){
  Util::printDebugLevel = 0;
  TS::Stream s;
  add(s, pat(0));
  s.parse(0);
  add(s, pmt(0));
  s.parse(PMT_PID); // PID 257 = AAC
  add(s, pesStart(0, 0));
  add(s, pesStart(1, 1920)); // two frames pending
  s.eraseTrack(257);         // the track's thread ends
  add(s, pesCont(2));        // one more packet arrives, then nothing
  int turns = 0;             // the input thread's inner loop, bounded here
  while (s.hasPacket(257) && turns < 1000){
    DTSC::Packet pk;
    s.getPacket(257, pk);
    if (pk){break;}
    ++turns;
  }
  if (turns >= 1000){
    fprintf(stderr, "hasPacket(257) still true after 1000 getPacket() calls that returned nothing\n");
    return 1;
  }
  return 0;
}
