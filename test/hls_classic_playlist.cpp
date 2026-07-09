#include <mist/hls_support.h>

#include <cstdlib>
#include <deque>
#include <iostream>
#include <string>

static void expect(bool ok, const std::string &msg){
  if (!ok){
    std::cerr << msg << std::endl;
    std::exit(1);
  }
}

static HLS::ClassicSegment seg(uint32_t idx, uint64_t durMs, const std::string &line,
                               bool discontinuity = false, uint64_t discontinuitySequence = 0){
  HLS::ClassicSegment ret;
  ret.fragmentIndex = idx;
  ret.durationMs = durMs;
  ret.line = line;
  ret.discontinuity = discontinuity;
  ret.discontinuitySequence = discontinuitySequence;
  return ret;
}

int main(){
  {
    std::deque<HLS::ClassicSegment> segments;
    segments.push_back(seg(47, 2000, "#EXTINF:2.000,\r\n4707158_4709156.ts\r\n"));
    segments.push_back(seg(48, 2000, "#EXTINF:2.000,\r\n4709156_4711156.ts\r\n"));

    HLS::ClassicPlaylistWindow window = HLS::buildClassicPlaylistWindow(segments, false, 0);

    expect(window.mediaSequence == 47, "shifted playlist media sequence must be first emitted fragment");
    expect(window.targetDuration == 2, "target duration must be based on emitted segments");
    expect(window.segments.size() == 2, "VOD-like shifted playlist should keep both supplied segments");
  }

  {
    std::deque<HLS::ClassicSegment> segments;
    segments.push_back(seg(10, 2000, "#EXTINF:2.000,\r\n10_12.ts\r\n"));
    segments.push_back(seg(11, 2000, "#EXTINF:2.000,\r\n12_14.ts\r\n", true, 1));
    segments.push_back(seg(12, 2000, "#EXTINF:2.000,\r\n14_16.ts\r\n"));

    HLS::ClassicPlaylistWindow window = HLS::buildClassicPlaylistWindow(segments, false, 0);

    expect(!window.hasDiscontinuitySequence,
           "full playlist starting before first discontinuity needs no base discontinuity sequence");
    expect(window.body().find("#EXT-X-DISCONTINUITY\r\n#EXTINF:2.000,\r\n12_14.ts") != std::string::npos,
           "playlist must emit discontinuity tag before the marked segment");
  }

  {
    std::deque<HLS::ClassicSegment> segments;
    segments.push_back(seg(12, 2000, "#EXTINF:2.000,\r\n14_16.ts\r\n", false, 1));

    HLS::ClassicPlaylistWindow window = HLS::buildClassicPlaylistWindow(segments, false, 0);

    expect(window.hasDiscontinuitySequence,
           "window starting after a discontinuity must advertise base discontinuity sequence");
    expect(window.discontinuitySequence == 1,
           "base discontinuity sequence must match first emitted segment");
    expect(window.body().find("#EXT-X-DISCONTINUITY") == std::string::npos,
           "no discontinuity tag is needed when the visible window starts after the boundary");
  }

  {
    const std::string playlist =
        "#EXTM3U\n"
        "#EXT-X-MEDIA-SEQUENCE:337\n"
        "#EXTINF:2.000,\n"
        "a.ts\n"
        "#EXTINF:2.000,\n"
        "b.ts\n"
        "#EXTINF:2.000,\n"
        "c.ts\n";

    HLS::MediaPlaylistState state = HLS::inspectMediaPlaylist(playlist);

    expect(state.mediaSequence == 337, "existing media sequence must be parsed");
    expect(state.visibleSegments == 3, "visible EXTINF segment count must be parsed");
    expect(state.nextSegmentCounter() == 340, "next segment counter must continue after visible window");
  }

  {
    std::deque<HLS::ClassicSegment> segments;
    segments.push_back(seg(100, 2000, "#EXTINF:2.000,\r\n100_102.ts\r\n"));
    segments.push_back(seg(101, 2000, "#EXTINF:2.000,\r\n102_104.ts\r\n"));
    segments.push_back(seg(102, 2000, "#EXTINF:2.000,\r\n104_106.ts\r\n"));
    segments.push_back(seg(103, 2000, "#EXTINF:2.000,\r\n106_108.ts\r\n"));
    segments.push_back(seg(104, 2000, "#EXTINF:2.000,\r\n108_110.ts\r\n"));
    segments.push_back(seg(105, 2000, "#EXTINF:2.000,\r\n110_112.ts\r\n"));

    HLS::ClassicPlaylistWindow window = HLS::buildClassicPlaylistWindow(segments, true, 3, true);

    expect(window.segments.size() == 3,
           "catchup live playlist should keep at most listlimit segments from the delayed start");
    expect(window.mediaSequence == 100, "catchup live playlist should begin at the delayed start");
    expect(window.body().find("100_102.ts") != std::string::npos,
           "catchup live playlist should include the first delayed segment");
    expect(window.body().find("106_108.ts") == std::string::npos,
           "catchup live playlist should trim newer segments from the back");
  }

  {
    std::deque<HLS::ClassicSegment> segments;
    segments.push_back(seg(200, 2000, "#EXTINF:2.000,\r\n200_202.ts\r\n"));
    segments.push_back(seg(201, 2000, "#EXTINF:2.000,\r\n202_204.ts\r\n"));
    segments.push_back(seg(202, 2000, "#EXTINF:2.000,\r\n204_206.ts\r\n"));
    segments.push_back(seg(203, 2000, "#EXTINF:2.000,\r\n206_208.ts\r\n"));
    segments.push_back(seg(204, 2000, "#EXTINF:2.000,\r\n208_210.ts\r\n"));
    segments.push_back(seg(205, 2000, "#EXTINF:2.000,\r\n210_212.ts\r\n"));
    segments.push_back(seg(206, 2000, "#EXTINF:2.000,\r\n212_214.ts\r\n"));
    segments.push_back(seg(207, 2000, "#EXTINF:2.000,\r\n214_216.ts\r\n"));

    HLS::ClassicPlaylistWindow window = HLS::buildClassicPlaylistWindow(segments, true, 3);

    expect(window.mediaSequence == 202,
           "normal live playlist should continue trimming old segments from the front");
    expect(window.body().find("200_202.ts") == std::string::npos,
           "normal live playlist should not keep the oldest segment when catchup mode is disabled");
  }

  return 0;
}
