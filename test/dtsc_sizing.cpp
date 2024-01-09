#include <mist/dtsc.h>
#include <iostream>
#include <sstream>
#include <string>


class DTSC_Sizer : DTSC::Meta {
  public:
  size_t trkId;
  DTSC::Track * trkPtr;
  DTSC_Sizer() : DTSC::Meta() {
    reInit("", true);
    trkId = addTrack(1, 1, 1, 1, true);
    trkPtr = &(tracks.at(trkId));
  }

  size_t getMetaOffset(){return stream.getOffset();}
  size_t getMetaRecordSize(){
    Util::RelAccXFieldData trks = stream.getFieldData("tracks");
    return stream.getRSize() - trks.size;
  }

  size_t getMetaTrackOffset(){return trackList.getOffset();}
  size_t getMetaTrackRecordSize(){return trackList.getRSize();}

  size_t getTrackOffset(){return trkPtr->track.getOffset();}
  size_t getTrackRecordSize(){return trkPtr->track.getRSize();}
  size_t getTrackFragOffset(){return trkPtr->fragments.getOffset();}
  size_t getTrackFragRecordSize(){return trkPtr->fragments.getRSize();}
  size_t getTrackKeyOffset(){return trkPtr->keys.getOffset();}
  size_t getTrackKeyRecordSize(){return trkPtr->keys.getRSize();}
  size_t getTrackPartOffset(){return trkPtr->parts.getOffset();}
  size_t getTrackPartRecordSize(){return trkPtr->parts.getRSize();}
  size_t getTrackPageOffset(){return trkPtr->pages.getOffset();}
  size_t getTrackPageRecordSize(){return trkPtr->pages.getRSize();}
};

bool comparer(size_t numA, size_t numB, const char* name){
  if (numA == numB){
    std::cerr << name << " is set correctly!" << std::endl;
    return 0;
  }
  std::cerr << name << " is " << numA << ", but should be " << numB << ":" << std::endl;
  std::cout << "#define " << name << " " << numB << std::endl;
  return 1;
}


int main(int argc, char **argv){
  std::cerr << "Verifying metadata sizes..." << std::endl;
  DTSC_Sizer M;
  int failures = 0;

  failures += comparer(META_META_OFFSET, M.getMetaOffset(), "META_META_OFFSET");
  failures += comparer(META_META_RECORDSIZE, M.getMetaRecordSize(), "META_META_RECORDSIZE");
  failures += comparer(META_TRACK_OFFSET, M.getMetaTrackOffset(), "META_TRACK_OFFSET");
  failures += comparer(META_TRACK_RECORDSIZE, M.getMetaTrackRecordSize(), "META_TRACK_RECORDSIZE");

  failures += comparer(TRACK_TRACK_OFFSET, M.getTrackOffset(), "TRACK_TRACK_OFFSET");
  failures += comparer(TRACK_TRACK_RECORDSIZE, M.getTrackRecordSize(), "TRACK_TRACK_RECORDSIZE");
  failures += comparer(TRACK_FRAGMENT_OFFSET, M.getTrackFragOffset(), "TRACK_FRAGMENT_OFFSET");
  failures += comparer(TRACK_FRAGMENT_RECORDSIZE, M.getTrackFragRecordSize(), "TRACK_FRAGMENT_RECORDSIZE");
  failures += comparer(TRACK_KEY_OFFSET, M.getTrackKeyOffset(), "TRACK_KEY_OFFSET");
  failures += comparer(TRACK_KEY_RECORDSIZE, M.getTrackKeyRecordSize(), "TRACK_KEY_RECORDSIZE");
  failures += comparer(TRACK_PART_OFFSET, M.getTrackPartOffset(), "TRACK_PART_OFFSET");
  failures += comparer(TRACK_PART_RECORDSIZE, M.getTrackPartRecordSize(), "TRACK_PART_RECORDSIZE");
  failures += comparer(TRACK_PAGE_OFFSET, M.getTrackPageOffset(), "TRACK_PAGE_OFFSET");
  failures += comparer(TRACK_PAGE_RECORDSIZE, M.getTrackPageRecordSize(), "TRACK_PAGE_RECORDSIZE");
  return failures;
}
