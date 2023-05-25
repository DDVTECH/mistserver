#include "cmaf.h"

#include "bitfields.h"
#include "mp4_dash.h"
#include "mp4_generic.h"
#include "stream.h"
#include "timing.h"

#include <sstream>

static uint64_t unixBootDiff = Util::unixMS();

namespace CMAF{
  /// Function to determine the payload size of a CMAF fragment.
  size_t payloadSize(const DTSC::Meta &M, size_t track, uint64_t startTime, uint64_t endTime){
    DTSC::Parts parts(M.parts(track));
    size_t firstPart = M.getPartIndex(startTime, track);
    size_t endPart = M.getPartIndex(endTime, track);
    size_t payloadSize = 0;
    for (size_t i = firstPart; i < endPart; i++){payloadSize += parts.getSize(i);}
    return payloadSize;
  }

  std::string trackHeader(const DTSC::Meta &M, size_t track, bool simplifyTrackIds){
    std::string tType = M.getType(track);

    std::stringstream header;

    MP4::FTYP ftypBox;
    ftypBox.setMajorBrand("isom");
    ftypBox.setCompatibleBrands("cmfc", 0);
    ftypBox.setCompatibleBrands("isom", 1);
    ftypBox.setCompatibleBrands("dash", 2);
    ftypBox.setCompatibleBrands("iso9", 3);
    header.write(ftypBox.asBox(), ftypBox.boxedSize());

    MP4::MOOV moovBox;

    MP4::MVHD mvhdBox(0);
    mvhdBox.setTrackID(0xFFFFFFFF); // This value needs to point to an unused trackid
    moovBox.setContent(mvhdBox, 0);

    MP4::TRAK trakBox;

    MP4::TKHD tkhdBox(M, track);
    tkhdBox.setDuration(0);
    trakBox.setContent(tkhdBox, 0);

    MP4::MDIA mdiaBox;

    MP4::MDHD mdhdBox(0, M.getLang(track));
    mdiaBox.setContent(mdhdBox, 0);

    MP4::HDLR hdlrBox(tType, M.getType(track));
    mdiaBox.setContent(hdlrBox, 1);

    MP4::MINF minfBox;

    if (tType == "video"){
      MP4::VMHD vmhdBox;
      vmhdBox.setFlags(1);
      minfBox.setContent(vmhdBox, 0);
    }else if (tType == "audio"){
      MP4::SMHD smhdBox;
      minfBox.setContent(smhdBox, 0);
    }else{
      MP4::NMHD nmhdBox;
      minfBox.setContent(nmhdBox, 0);
    }

    MP4::DINF dinfBox;
    MP4::DREF drefBox;
    dinfBox.setContent(drefBox, 0);
    minfBox.setContent(dinfBox, 1);

    MP4::STBL stblBox;

    // Add STSD box
    MP4::STSD stsdBox(0);
    if (tType == "video"){
      MP4::VisualSampleEntry sampleEntry(M, track);
      MP4::BTRT btrtBox;
      btrtBox.setDecodingBufferSize(0xFFFFFFFFull);
      btrtBox.setAverageBitrate(M.getBps(track));
      btrtBox.setMaxBitrate(M.getMaxBps(track));

      sampleEntry.setBoxEntry(sampleEntry.getBoxEntryCount(), btrtBox);
      stsdBox.setEntry(sampleEntry, 0);
    }else if (tType == "audio"){
      MP4::AudioSampleEntry sampleEntry(M, track);
      MP4::BTRT btrtBox;
      btrtBox.setDecodingBufferSize(0xFFFFFFFFull);
      btrtBox.setAverageBitrate(M.getBps(track));
      btrtBox.setMaxBitrate(M.getMaxBps(track));

      sampleEntry.setBoxEntry(sampleEntry.getBoxEntryCount(), btrtBox);
      stsdBox.setEntry(sampleEntry, 0);
    }else if (tType == "meta"){
      MP4::TextSampleEntry sampleEntry(M, track);

      MP4::FontTableBox ftab;
      sampleEntry.setFontTableBox(ftab);
      stsdBox.setEntry(sampleEntry, 0);
    }

    stblBox.setContent(stsdBox, 0);

    MP4::STTS sttsBox(0);
    stblBox.setContent(sttsBox, 1);
    MP4::STSC stscBox(0);
    stblBox.setContent(stscBox, 2);
    MP4::STSZ stszBox(0);
    stblBox.setContent(stszBox, 3);
    MP4::STCO stcoBox(0);
    stblBox.setContent(stcoBox, 4);

    minfBox.setContent(stblBox, 2);
    mdiaBox.setContent(minfBox, 2);
    trakBox.setContent(mdiaBox, 1);
    moovBox.setContent(trakBox, 1);

    MP4::MVEX mvexBox;

    if (M.getVod()){
      MP4::MEHD mehdBox;
      mehdBox.setFragmentDuration(M.getDuration(track));
      mvexBox.setContent(mehdBox, 0);
    }

    MP4::TREX trexBox(track + 1);
    trexBox.setDefaultSampleDuration(1000);
    mvexBox.setContent(trexBox, M.getVod() ? 1 : 0);

    moovBox.setContent(mvexBox, 2);
    header.write(moovBox.asBox(), moovBox.boxedSize());

    if (M.getVod()){
      DTSC::Fragments fragments(M.fragments(track));
      DTSC::Keys keys(M.keys(track));
      DTSC::Parts parts(M.parts(track));

      MP4::SIDX sidxBox;
      sidxBox.setReferenceID(track + 1);
      sidxBox.setTimescale(1000);
      sidxBox.setEarliestPresentationTime(keys.getTime(0) + parts.getOffset(0) -
                                          M.getFirstms(track));

      for (size_t i = 0; i < fragments.getEndValid(); i++){
        size_t firstKey = fragments.getFirstKey(i);
        size_t endKey =
            ((i + 1 < fragments.getEndValid()) ? fragments.getFirstKey(i + 1) : keys.getEndValid());

        MP4::sidxReference refItem;
        refItem.referencedSize =
            payloadSize(M, track, keys.getTime(firstKey), keys.getTime(endKey)) +
            keyHeaderSize(M, track, i) + 8;
        refItem.subSegmentDuration =
            (endKey == keys.getEndValid() ? M.getLastms(track) : keys.getTime(endKey)) -
            keys.getTime(firstKey);
        refItem.sapStart = true;
        refItem.sapType = 16;
        refItem.sapDeltaTime = 0;
        refItem.referenceType = 0;

        sidxBox.setReference(refItem, i);
      }
      header.write(sidxBox.asBox(), sidxBox.boxedSize());
    }

    return header.str();
  }

  bool header(Util::ResizeablePointer & headOut, const DTSC::Meta & M, const std::map<size_t, Comms::Users> & userSelect) {
    // MP4 Files always start with an FTYP box. Constructor sets default values
    MP4::FTYP ftypBox;
    ftypBox.setMajorBrand("isom");
    ftypBox.setCompatibleBrands("cmfc", 0);
    ftypBox.setCompatibleBrands("isom", 1);
    ftypBox.setCompatibleBrands("dash", 2);
    ftypBox.setCompatibleBrands("iso9", 3);
    headOut.append(ftypBox.asBox(), ftypBox.boxedSize());

    // Start building the moov box. This is the metadata box for an mp4 file, and will contain all
    // metadata.
    MP4::MOOV moovBox;
    // Keep track of the current index within the moovBox
    unsigned int moovOffset = 0;

    // Construct with duration of -1, as this is the default for fragmented
    MP4::MVHD mvhdBox(0);
    // Set the trackid for the first "empty" track within the file.
    mvhdBox.setTrackID(userSelect.size() + 1);
    moovBox.setContent(mvhdBox, moovOffset++);

    for (std::map<size_t, Comms::Users>::const_iterator it = userSelect.begin(); it != userSelect.end(); it++) {
      DTSC::Parts parts(M.parts(it->first));
      DTSC::Keys keys = M.getKeys(it->first);
      std::string tType = M.getType(it->first);

      MP4::TRAK trakBox;
      // Keep track of the current index within the moovBox
      size_t trakOffset = 0;

      MP4::TKHD tkhdBox(M, it->first);
      tkhdBox.setDuration(0);
      trakBox.setContent(tkhdBox, trakOffset++);

      MP4::MDIA mdiaBox;
      size_t mdiaOffset = 0;

      // Add the mandatory MDHD and HDLR boxes to the MDIA
      MP4::MDHD mdhdBox(0);
      mdhdBox.setLanguage(M.getLang(it->first));
      mdiaBox.setContent(mdhdBox, mdiaOffset++);
      MP4::HDLR hdlrBox(tType, M.getTrackIdentifier(it->first));
      mdiaBox.setContent(hdlrBox, mdiaOffset++);

      MP4::MINF minfBox;
      size_t minfOffset = 0;

      // Add a track-type specific box to the MINF box
      if (tType == "video") {
        MP4::VMHD vmhdBox(0, 1);
        minfBox.setContent(vmhdBox, minfOffset++);
      } else if (tType == "audio") {
        MP4::SMHD smhdBox;
        minfBox.setContent(smhdBox, minfOffset++);
      } else {
        // create nmhd box
        MP4::NMHD nmhdBox;
        minfBox.setContent(nmhdBox, minfOffset++);
      }

      // Add the mandatory DREF (dataReference) box
      MP4::DINF dinfBox;
      MP4::DREF drefBox;
      dinfBox.setContent(drefBox, 0);
      minfBox.setContent(dinfBox, minfOffset++);

      // Add STSD box
      MP4::STSD stsdBox(0);
      if (tType == "video") {
        MP4::VisualSampleEntry sampleEntry(M, it->first);
        stsdBox.setEntry(sampleEntry, 0);
      } else if (tType == "audio") {
        MP4::AudioSampleEntry sampleEntry(M, it->first);
        stsdBox.setEntry(sampleEntry, 0);
      } else if (tType == "meta") {
        MP4::TextSampleEntry sampleEntry(M, it->first);

        MP4::FontTableBox ftab;
        sampleEntry.setFontTableBox(ftab);
        stsdBox.setEntry(sampleEntry, 0);
      }

      MP4::STBL stblBox;
      size_t stblOffset = 0;
      stblBox.setContent(stsdBox, stblOffset++);

      // Add STTS Box
      // note: STTS is empty when fragmented
      MP4::STTS sttsBox(0);
      // Add STSZ Box
      // note: STSZ is empty when fragmented
      MP4::STSZ stszBox(0);
      stblBox.setContent(sttsBox, stblOffset++);
      stblBox.setContent(stszBox, stblOffset++);

      // Add STSC Box
      // note: STSC is empty when fragmented
      MP4::STSC stscBox(0);
      stblBox.setContent(stscBox, stblOffset++);

      // Create STCO Box (either stco or co64)
      // note: 64bit boxes will never be used in fragmented
      // note: Inserting empty values on purpose here, will be fixed later.
      MP4::STCO stcoBox(0);
      stcoBox.setEntryCount(0);
      stblBox.setContent(stcoBox, stblOffset++);

      minfBox.setContent(stblBox, minfOffset++);

      mdiaBox.setContent(minfBox, mdiaOffset++);

      trakBox.setContent(mdiaBox, trakOffset++);

      moovBox.setContent(trakBox, moovOffset++);
    }

    MP4::MVEX mvexBox;
    size_t curBox = 0;
    MP4::MEHD mehdBox;
    mehdBox.setFragmentDuration(-1);

    mvexBox.setContent(mehdBox, curBox++);
    for (std::map<size_t, Comms::Users>::const_iterator it = userSelect.begin(); it != userSelect.end(); it++) {
      MP4::TREX trexBox(it->first + 1);
      trexBox.setDefaultSampleDuration(1000);
      mvexBox.setContent(trexBox, curBox++);
    }
    moovBox.setContent(mvexBox, moovOffset++);
    headOut.append(moovBox.asBox(), moovBox.boxedSize());
    return true;
  }

  class sortPart{
  public:
    uint64_t time;
    size_t partIndex;
    size_t bytePos;
    bool operator<(const sortPart &rhs) const{return time < rhs.time;}
  };

  size_t keyHeaderSize(const DTSC::Meta &M, size_t track, size_t fragment){
    uint64_t tmpRes = 8 + 16 + 32 + 20;

    DTSC::Fragments fragments(M.fragments(track));
    DTSC::Keys keys(M.keys(track));
    DTSC::Parts parts(M.parts(track));

    size_t firstKey = fragments.getFirstKey(fragment);
    size_t firstPart = keys.getFirstPart(firstKey);
    size_t endPart = parts.getEndValid();
    if (fragment + 1 < fragments.getEndValid()){
      endPart = keys.getFirstPart(fragments.getFirstKey(fragment + 1));
    }

    tmpRes += 24 + ((endPart - firstPart) * 12);
    return tmpRes;
  }

  /// Calculates the full size of a 'moof' box for a DTSC::Key based fragment.
  /// Used when building the 'moof' box to calculate the relative data offsets.
  size_t keyHeaderSize(const DTSC::Meta &M, size_t track, uint64_t startTime, uint64_t endTime){
    uint64_t tmpRes = 8 + 16 + 32 + 20;
    size_t firstPart = M.getPartIndex(startTime, track);
    size_t endPart = M.getPartIndex(endTime, track);
    tmpRes += 24 + ((endPart - firstPart) * 12);
    return tmpRes;
  }

  /// Generates the 'moof' box for a DTSC::Key based CMAF fragment.
  std::string keyHeader(const DTSC::Meta &M, size_t track, uint64_t startTime, uint64_t endTime,
                        uint64_t segmentNum, bool simplifyTrackIds, bool UTCTime){

    size_t firstPart = M.getPartIndex(startTime, track);
    size_t endPart = M.getPartIndex(endTime, track);
    std::stringstream header;
    MP4::MOOF moofBox;
    MP4::MFHD mfhdBox(segmentNum);
    moofBox.setContent(mfhdBox, 0);

    std::set<sortPart> trunOrder;

    // We use keyHeaderSize here to determine the relative offsets of the data in the 'mdat' box.
    uint64_t relativeOffset = keyHeaderSize(M, track, startTime, endTime) + 8;

    sortPart temp;
    temp.time = startTime;
    temp.partIndex = firstPart;
    temp.bytePos = relativeOffset;

    DTSC::Parts parts(M.parts(track));
    for (size_t p = firstPart; p < endPart; p++){
      trunOrder.insert(temp);
      temp.time += parts.getDuration(p);
      temp.partIndex++;
      temp.bytePos += parts.getSize(p);
    }

    MP4::TRAF trafBox;
    MP4::TFHD tfhdBox;

    tfhdBox.setFlags(MP4::tfhdSampleFlag | MP4::tfhdBaseIsMoof | MP4::tfhdSampleDesc);
    tfhdBox.setTrackID(track + 1);
    tfhdBox.setDefaultSampleDuration(444);
    tfhdBox.setDefaultSampleSize(444);
    tfhdBox.setDefaultSampleFlags((M.getType(track) == "video")
                                      ? (MP4::noIPicture | MP4::noKeySample)
                                      : (MP4::isIPicture | MP4::isKeySample));
    tfhdBox.setSampleDescriptionIndex(1);
    trafBox.setContent(tfhdBox, 0);

    MP4::TFDT tfdtBox;
    if (M.getVod()){
      tfdtBox.setBaseMediaDecodeTime(startTime - M.getFirstms(track));
    }else{
      tfdtBox.setBaseMediaDecodeTime(
          (UTCTime ? startTime + M.getBootMsOffset() + unixBootDiff : startTime));
    }
    trafBox.setContent(tfdtBox, 1);

    MP4::TRUN trunBox;
    trunBox.setFlags(MP4::trundataOffset | MP4::trunfirstSampleFlags | MP4::trunsampleSize |
                     MP4::trunsampleDuration | MP4::trunsampleOffsets);

    trunBox.setDataOffset(trunOrder.begin()->bytePos);

    trunBox.setFirstSampleFlags(MP4::isIPicture | MP4::isKeySample);

    size_t trunOffset = 0;

    if (trunOrder.size()){
      std::set<sortPart>::iterator lastOne = trunOrder.end();
      lastOne--;
      for (std::set<sortPart>::iterator it = trunOrder.begin(); it != trunOrder.end(); it++){
        MP4::trunSampleInformation sampleInfo;
        sampleInfo.sampleSize = parts.getSize(it->partIndex);
        sampleInfo.sampleDuration = parts.getDuration(it->partIndex);
        if (it == lastOne){sampleInfo.sampleDuration = endTime - it->time;}
        sampleInfo.sampleOffset = parts.getOffset(it->partIndex);
        trunBox.setSampleInformation(sampleInfo, trunOffset++);
      }
    }else{
      WARN_MSG("Empty CMAF header for track %zu: %" PRIu64 "-%" PRIu64
               " contains no packets (first: %" PRIu64 ", last: %" PRIu64
               "), firstPart=%zu, lastPart=%zu",
               track, startTime, endTime, M.getFirstms(track), M.getLastms(track), firstPart,
               endPart);
    }
    trafBox.setContent(trunBox, 2);

    moofBox.setContent(trafBox, 1);

    header.write(moofBox.asBox(), moofBox.boxedSize());

    return header.str();
  }

  bool fragmentHeader(Util::ResizeablePointer & headOut, const DTSC::Meta & M,
                      const std::map<size_t, Comms::Users> & userSelect, size_t fragmentIndex) {
    MP4::MOOF moofBox;
    MP4::MFHD mfhdBox(fragmentIndex);
    size_t moofCounter = 0;
    moofBox.setContent(mfhdBox, moofCounter++);
    size_t totalData = 0;

    for (std::map<size_t, Comms::Users>::const_iterator it = userSelect.begin(); it != userSelect.end(); it++) {

      DTSC::Keys keys = M.getKeys(it->first);
      if (!keys.getTotalPartCount()) { continue; }

      Util::packetSorter sort;
      size_t totalTruns = 0;
      size_t totalTracks = 0;
      for (std::map<size_t, Comms::Users>::const_iterator it = userSelect.begin(); it != userSelect.end(); it++) {
        DTSC::Keys loopKeys = M.getKeys(it->first);
        if (!loopKeys.getTotalPartCount()) { continue; }
        Util::sortedPageInfo info;
        info.tid = it->first;
        info.offset = 0;
        info.partIndex = loopKeys.getFirstPart(loopKeys.getFirstValid());
        info.time = loopKeys.getTime(loopKeys.getFirstValid());
        sort.insert(info);
        totalTruns += loopKeys.getTotalPartCount();
        ++totalTracks;
      }
      // moof (8) + mfhd (16) + (tfhd (32) + traf (20)) * trackCount + trun(36) * trunCount + mdat (8)
      size_t baseOffset = 8 + 16 + 52 * totalTracks + 36 * totalTruns + 8;

      MP4::TRAF trafBox;
      size_t trafCounter = 0;

      MP4::TFHD tfhdBox;
      tfhdBox.setFlags(MP4::tfhdSampleFlag | MP4::tfhdBaseIsMoof | MP4::tfhdSampleDesc);
      tfhdBox.setTrackID(it->first + 1);
      tfhdBox.setDefaultSampleDuration(444);
      tfhdBox.setDefaultSampleSize(444);
      tfhdBox.setDefaultSampleFlags((M.getType(it->first) == "video") ? (MP4::noIPicture | MP4::noKeySample)
                                                                      : (MP4::isIPicture | MP4::isKeySample));
      tfhdBox.setSampleDescriptionIndex(1);
      trafBox.setContent(tfhdBox, trafCounter++);

      MP4::TFDT tfdtBox;
      tfdtBox.setBaseMediaDecodeTime(keys.getTime(keys.getFirstValid()));
      trafBox.setContent(tfdtBox, trafCounter++);

      while (sort.size()) {
        Util::sortedPageInfo pi = *(sort.begin());
        DTSC::Parts parts(M.parts(pi.tid));

        if (it->first == pi.tid) {
          MP4::TRUN trunBox;
          trunBox.setFlags(MP4::trundataOffset | MP4::trunfirstSampleFlags | MP4::trunsampleSize |
                           MP4::trunsampleDuration | MP4::trunsampleOffsets);
          trunBox.setDataOffset(baseOffset);
          if (pi.time == keys.getTime(keys.getIndexForTime(pi.time))) {
            trunBox.setFirstSampleFlags(MP4::isIPicture | MP4::isKeySample);
          } else {
            trunBox.setFirstSampleFlags(0);
          }
          MP4::trunSampleInformation sampleInfo;
          sampleInfo.sampleSize = parts.getSize(pi.partIndex);
          sampleInfo.sampleDuration = parts.getDuration(pi.partIndex);
          sampleInfo.sampleOffset = parts.getOffset(pi.partIndex);
          trunBox.setSampleInformation(sampleInfo, 0);
          trafBox.setContent(trunBox, trafCounter++);
          totalData += sampleInfo.sampleSize;
        }

        baseOffset += parts.getSize(pi.partIndex);

        if (pi.time + parts.getDuration(pi.partIndex) >= M.getLastms(pi.tid)) {
          sort.dropTrack(pi.tid);
          continue;
        }

        pi.time += parts.getDuration(pi.partIndex);
        pi.partIndex++;
        sort.replaceFirst(pi);
      }

      moofBox.setContent(trafBox, moofCounter++);
    }
    headOut.append(moofBox.asBox(), moofBox.boxedSize());

    char mdatHeader[] = {0x00, 0x00, 0x00, 0x00, 'm', 'd', 'a', 't'};
    Bit::htobl(mdatHeader, totalData + 8);
    headOut.append(mdatHeader, 8);
    return true;
  }

}// namespace CMAF
