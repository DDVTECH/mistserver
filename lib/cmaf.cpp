#include "cmaf.h"

#include "bitfields.h"
#include "mp4_dash.h"
#include "mp4_generic.h"
#include "stream.h"
#include "timing.h"

#include <algorithm>
#include <sstream>

static uint64_t unixBootDiff = Util::unixMS();

namespace CMAF{
  /// Function to determine the payload size of a CMAF fragment.
  size_t payloadSize(const DTSC::Meta &M, size_t track, uint64_t startTime, uint64_t endTime){
    DTSC::Parts parts(M.parts(track));
    size_t firstValidPart = parts.getFirstValid();
    size_t endValidPart = parts.getEndValid();
    if (firstValidPart >= endValidPart) { return 0; }
    if (startTime < M.getPartTime(firstValidPart, track)) { return 0; }

    size_t firstPart = M.getPartIndex(startTime, track);
    size_t endPart = M.getPartIndex(endTime, track);
    if (firstPart < firstValidPart || firstPart >= endPart || firstPart >= endValidPart || endPart > endValidPart) {
      return 0;
    }
    size_t payloadSize = 0;
    for (size_t i = firstPart; i < endPart; i++){payloadSize += parts.getSize(i);}
    return payloadSize;
  }

  size_t payloadSize(const DTSC::Meta & M, const std::map<size_t, Comms::Users> & tracks, uint64_t startTime, uint64_t endTime) {
    size_t result = 0;
    for (const auto & track : tracks) { result += payloadSize(M, track.first, startTime, endTime); }
    return result;
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

        MP4::BTRT btrtBox;
        btrtBox.setDecodingBufferSize(0xFFFFFFFFull);
        btrtBox.setAverageBitrate(M.getBps(it->first));
        btrtBox.setMaxBitrate(M.getMaxBps(it->first));
        sampleEntry.setBoxEntry(sampleEntry.getBoxEntryCount(), btrtBox);

        stsdBox.setEntry(sampleEntry, 0);
      } else if (tType == "audio") {
        MP4::AudioSampleEntry sampleEntry(M, it->first);

        MP4::BTRT btrtBox;
        btrtBox.setDecodingBufferSize(0xFFFFFFFFull);
        btrtBox.setAverageBitrate(M.getBps(it->first));
        btrtBox.setMaxBitrate(M.getMaxBps(it->first));
        sampleEntry.setBoxEntry(sampleEntry.getBoxEntryCount(), btrtBox);

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
    if (M.getVod()) {
      uint64_t duration = 0;
      for (const auto & it : userSelect) { duration = std::max(duration, M.getDuration(it.first)); }
      MP4::MEHD mehdBox;
      mehdBox.setFragmentDuration(duration);
      mvexBox.setContent(mehdBox, curBox++);
    }
    for (const auto & it : userSelect) {
      MP4::TREX trexBox(it.first + 1);
      trexBox.setDefaultSampleDuration(1000);
      mvexBox.setContent(trexBox, curBox++);
    }
    moovBox.setContent(mvexBox, moovOffset++);
    headOut.append(moovBox.asBox(), moovBox.boxedSize());

    if (M.getVod()) {
      for (const auto & it : userSelect) {
        DTSC::Fragments fragments(M.fragments(it.first));
        DTSC::Keys keys(M.keys(it.first));
        DTSC::Parts parts(M.parts(it.first));

        MP4::SIDX sidxBox;
        sidxBox.setReferenceID(it.first + 1);
        sidxBox.setTimescale(1000);
        sidxBox.setEarliestPresentationTime(keys.getTime(0) + parts.getOffset(0) - M.getFirstms(it.first));

        for (size_t i = 0; i < fragments.getEndValid(); i++) {
          size_t firstKey = fragments.getFirstKey(i);
          size_t endKey = ((i + 1 < fragments.getEndValid()) ? fragments.getFirstKey(i + 1) : keys.getEndValid());
          uint64_t endTime = (endKey == keys.getEndValid() ? M.getLastms(it.first) : keys.getTime(endKey));

          MP4::sidxReference refItem;
          refItem.referencedSize = payloadSize(M, it.first, keys.getTime(firstKey), endTime) + keyHeaderSize(M, it.first, i) + 8;
          refItem.subSegmentDuration = endTime - keys.getTime(firstKey);
          refItem.sapStart = true;
          refItem.sapType = 16;
          refItem.sapDeltaTime = 0;
          refItem.referenceType = 0;

          sidxBox.setReference(refItem, i);
        }
        headOut.append(sidxBox.asBox(), sidxBox.boxedSize());
      }
    }

    return true;
  }

  class sortPart{
  public:
    uint64_t time;
    size_t partIndex;
    size_t bytePos;
    bool operator<(const sortPart & rhs) const {
      if (time < rhs.time) { return true; }
      if (time > rhs.time) { return false; }
      return partIndex < rhs.partIndex;
    }
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
    DTSC::Parts parts(M.parts(track));
    DTSC::Keys keys(M.keys(track));
    uint64_t firstSampleTime = startTime;
    if (firstPart < parts.getEndValid()) { firstSampleTime = M.getPartTime(firstPart, track); }

    // We use keyHeaderSize here to determine the relative offsets of the data in the 'mdat' box.
    uint64_t relativeOffset = keyHeaderSize(M, track, startTime, endTime) + 8;

    sortPart temp;
    temp.time = firstSampleTime;
    temp.partIndex = firstPart;
    temp.bytePos = relativeOffset;

    for (size_t p = firstPart; p < endPart; p++){
      trunOrder.insert(temp);
      temp.time += parts.getDuration(p);
      temp.partIndex++;
      temp.bytePos += parts.getSize(p);
    }

    MEDIUM_MSG("CMAF header track=%zu start=%" PRIu64 " mediaStart=%" PRIu64 " end=%" PRIu64
               " firstPart=%zu endPart=%zu samples=%zu payload=%zu",
               track, startTime, firstSampleTime, endTime, firstPart, endPart, trunOrder.size(),
               payloadSize(M, track, startTime, endTime));

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
      tfdtBox.setBaseMediaDecodeTime(firstSampleTime - M.getFirstms(track));
    }else{
      tfdtBox.setBaseMediaDecodeTime(UTCTime ? firstSampleTime + M.getBootMsOffset() + unixBootDiff : firstSampleTime);
    }
    trafBox.setContent(tfdtBox, 1);

    MP4::TRUN trunBox;
    trunBox.setFlags(MP4::trundataOffset | MP4::trunfirstSampleFlags | MP4::trunsampleSize |
                     MP4::trunsampleDuration | MP4::trunsampleOffsets);

    trunBox.setDataOffset(trunOrder.size() ? trunOrder.begin()->bytePos : relativeOffset);

    bool firstSampleIsKey = M.getType(track) != "video";
    if (!firstSampleIsKey && trunOrder.size()) {
      size_t keyIdx = M.getKeyIndexForTime(track, firstSampleTime);
      firstSampleIsKey = keyIdx < keys.getEndValid() && keys.getTime(keyIdx) == firstSampleTime;
    }
    trunBox.setFirstSampleFlags(firstSampleIsKey ? (MP4::isIPicture | MP4::isKeySample) : (MP4::noIPicture | MP4::noKeySample));

    size_t trunOffset = 0;

    if (trunOrder.size()) {
      for (std::set<sortPart>::iterator it = trunOrder.begin(); it != trunOrder.end(); it++){
        MP4::trunSampleInformation sampleInfo;
        sampleInfo.sampleSize = parts.getSize(it->partIndex);
        sampleInfo.sampleDuration = parts.getDuration(it->partIndex);
        sampleInfo.sampleOffset = parts.getOffset(it->partIndex);
        trunBox.setSampleInformation(sampleInfo, trunOffset++);
      }
    } else {
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

  bool fragmentHeader(Util::ResizeablePointer & headOut, const DTSC::Meta & M, const std::map<size_t, Comms::Users> & userSelect,
                      size_t timingTrack, uint64_t startTime, uint64_t endTime, uint64_t sequenceNumber) {
    if (endTime <= startTime || userSelect.empty() || !M.trackValid(timingTrack)) { return false; }

    std::map<size_t, size_t> firstParts;
    std::map<size_t, size_t> endParts;
    // VOD playlist times are relative to the track that defines the segment grid.
    const uint64_t timeOffset = M.getVod() ? M.getFirstms(timingTrack) : 0;
    size_t totalTruns = 0;
    size_t totalTracks = 0;
    for (const auto & it : userSelect) {
      if (!M.trackValid(it.first)) { return false; }
      DTSC::Parts parts(M.parts(it.first));
      const size_t firstPart = M.getPartIndex(startTime, it.first);
      const size_t endPart = M.getPartIndex(endTime, it.first);
      if (firstPart < parts.getFirstValid() || endPart > parts.getEndValid()) { return false; }
      if (firstPart >= endPart) { continue; }
      firstParts[it.first] = firstPart;
      endParts[it.first] = endPart;
      totalTruns += endPart - firstPart;
      ++totalTracks;
    }
    if (!totalTruns) { return false; }

    MP4::MOOF moofBox;
    MP4::MFHD mfhdBox(sequenceNumber);
    size_t moofCounter = 0;
    moofBox.setContent(mfhdBox, moofCounter++);
    uint64_t totalData = 0;
    const uint64_t dataOffset = 8 + 16 + 52 * totalTracks + 36 * totalTruns + 8;

    for (const auto & track : firstParts) {
      const size_t trackId = track.first;
      DTSC::Keys keys(M.getKeys(trackId));

      Util::packetSorter sort;
      for (const auto & source : firstParts) {
        Util::sortedPageInfo info = {};
        info.tid = source.first;
        info.partIndex = source.second;
        info.time = M.getPartTime(info.partIndex, info.tid);
        sort.insert(info);
      }

      MP4::TRAF trafBox;
      size_t trafCounter = 0;
      MP4::TFHD tfhdBox;
      tfhdBox.setFlags(MP4::tfhdSampleFlag | MP4::tfhdBaseIsMoof | MP4::tfhdSampleDesc);
      tfhdBox.setTrackID(trackId + 1);
      tfhdBox.setDefaultSampleDuration(444);
      tfhdBox.setDefaultSampleSize(444);
      tfhdBox.setDefaultSampleFlags(M.getType(trackId) == "video" ? (MP4::noIPicture | MP4::noKeySample)
                                                                  : (MP4::isIPicture | MP4::isKeySample));
      tfhdBox.setSampleDescriptionIndex(1);
      trafBox.setContent(tfhdBox, trafCounter++);

      MP4::TFDT tfdtBox;
      uint64_t decodeTime = M.getPartTime(track.second, trackId);
      if (decodeTime < timeOffset) { return false; }
      decodeTime -= timeOffset;
      tfdtBox.setBaseMediaDecodeTime(decodeTime);
      trafBox.setContent(tfdtBox, trafCounter++);

      uint64_t offset = dataOffset;
      while (sort.size()) {
        Util::sortedPageInfo part = *sort.begin();
        DTSC::Parts parts(M.parts(part.tid));
        const uint32_t partSize = parts.getSize(part.partIndex);

        if (trackId == part.tid) {
          MP4::TRUN trunBox;
          trunBox.setFlags(MP4::trundataOffset | MP4::trunfirstSampleFlags | MP4::trunsampleSize |
                           MP4::trunsampleDuration | MP4::trunsampleOffsets);
          trunBox.setDataOffset(offset);
          const size_t keyIndex = keys.getIndexForTime(part.time);
          const bool isKey =
            M.getType(trackId) != "video" || (keyIndex < keys.getEndValid() && keys.getTime(keyIndex) == part.time);
          trunBox.setFirstSampleFlags(isKey ? (MP4::isIPicture | MP4::isKeySample) : (MP4::noIPicture | MP4::noKeySample));
          MP4::trunSampleInformation sampleInfo = {};
          sampleInfo.sampleSize = partSize;
          sampleInfo.sampleDuration = parts.getDuration(part.partIndex);
          sampleInfo.sampleOffset = parts.getOffset(part.partIndex);
          trunBox.setSampleInformation(sampleInfo, 0);
          trafBox.setContent(trunBox, trafCounter++);
          totalData += partSize;
        }

        offset += partSize;
        if (++part.partIndex >= endParts[part.tid]) {
          sort.dropTrack(part.tid);
        } else {
          part.time += parts.getDuration(part.partIndex - 1);
          sort.replaceFirst(part);
        }
      }
      moofBox.setContent(trafBox, moofCounter++);
    }

    if (dataOffset != moofBox.boxedSize() + 8 || dataOffset + totalData > INT32_MAX || totalData > 0xFFFFFFFFull - 8) {
      return false;
    }
    headOut.append(moofBox.asBox(), moofBox.boxedSize());
    char mdatHeader[] = {0x00, 0x00, 0x00, 0x00, 'm', 'd', 'a', 't'};
    Bit::htobl(mdatHeader, totalData + 8);
    headOut.append(mdatHeader, 8);
    return true;
  }

  bool fragmentHeader(Util::ResizeablePointer & headOut, const DTSC::Meta & M,
                      const std::map<size_t, Comms::Users> & userSelect, size_t fragmentIndex) {
    if (userSelect.empty()) { return false; }
    size_t timingTrack = M.mainTrack();
    if (!userSelect.count(timingTrack)) { timingTrack = userSelect.begin()->first; }
    if (!M.trackValid(timingTrack)) { return false; }
    DTSC::Fragments fragments(M.fragments(timingTrack));
    DTSC::Keys keys(M.getKeys(timingTrack));
    if (fragmentIndex < fragments.getFirstValid() || fragmentIndex >= fragments.getEndValid()) { return false; }
    const uint64_t duration = fragments.getDuration(fragmentIndex);
    if (!duration) { return false; }
    const uint64_t startTime = keys.getTime(fragments.getFirstKey(fragmentIndex));
    return fragmentHeader(headOut, M, userSelect, timingTrack, startTime, startTime + duration, fragmentIndex);
  }

}// namespace CMAF
