#include "cmaf.h"

static uint64_t unixBootDiff = (Util::unixMS() - Util::bootMS());

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

  size_t trackHeaderSize(const DTSC::Meta &M, size_t track){
    // EDTS Box needed? + 36
    size_t res = 36 + 8 + 108 + 8 + 92 + 8 + 32 + 33 + 44 + 8 + 20 + 16 + 16 + 16 + 40;

    res += M.getType(track).size();

    // Type-specific boxes
    std::string tType = M.getType(track);
    if (tType == "video"){res += 20 + 16 + 86 + 16 + 8 + M.getInit(track).size() + 20;}//20 for btrt box
    if (tType == "audio"){
      res += 16 + 16 + 36 + 35 + (M.getInit(track).size() ? 2 + M.getInit(track).size() : 0) + 20;//20 for btrt box
    }
    if (tType == "meta"){res += 12 + 16 + 64;}

    if (M.getVod()){res += 16;}

    return res;
  }
  
  size_t simplifiedTrackId(const DTSC::Meta & M, size_t idx) { 
    std::string type = M.getType(idx);
    if (type == "video") {return 1;}
    if (type == "audio") {return 2;}
    if (type == "meta") {return 3;}
    return idx;
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
    if (simplifyTrackIds){
      tkhdBox.setTrackID(simplifiedTrackId(M, track));
    }
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

      sampleEntry.setBoxEntry(sampleEntry.getBoxEntryCount(),btrtBox);
      stsdBox.setEntry(sampleEntry, 0);
    }else if (tType == "audio"){
      MP4::AudioSampleEntry sampleEntry(M, track);
      MP4::BTRT btrtBox;
      btrtBox.setDecodingBufferSize(0xFFFFFFFFull);
      btrtBox.setAverageBitrate(M.getBps(track));
      btrtBox.setMaxBitrate(M.getMaxBps(track));

      sampleEntry.setBoxEntry(sampleEntry.getBoxEntryCount(),btrtBox);
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
    if (simplifyTrackIds){
      trexBox.setTrackID(simplifiedTrackId(M, track));
    }
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
      sidxBox.setEarliestPresentationTime(keys.getTime(0) + parts.getOffset(0) - M.getFirstms(track));

      for (size_t i = 0; i < fragments.getEndValid(); i++){
        size_t firstKey = fragments.getFirstKey(i);
        size_t endKey =
            ((i + 1 < fragments.getEndValid()) ? fragments.getFirstKey(i + 1) : keys.getEndValid());

        MP4::sidxReference refItem;
        refItem.referencedSize = payloadSize(M, track, keys.getTime(firstKey), keys.getTime(endKey)) + fragmentHeaderSize(M, track, i) + 8;
        refItem.subSegmentDuration =
            (endKey == keys.getEndValid() ? M.getLastms(track) : keys.getTime(endKey)) - keys.getTime(firstKey);
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

  class sortPart{
  public:
    uint64_t time;
    size_t partIndex;
    size_t bytePos;
    bool operator<(const sortPart &rhs) const{return time < rhs.time;}
  };

  size_t fragmentHeaderSize(const DTSC::Meta &M, size_t track, size_t fragment){
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

  std::string fragmentHeader(const DTSC::Meta &M, size_t track, size_t fragment, bool simplifyTrackIds, bool UTCTime){

    DTSC::Fragments fragments(M.fragments(track));
    DTSC::Keys keys(M.keys(track));
    DTSC::Parts parts(M.parts(track));

    size_t firstKey = fragments.getFirstKey(fragment);
    size_t endKey = keys.getEndValid();
    if (fragment + 1 < fragments.getEndValid()){endKey = fragments.getFirstKey(fragment + 1);}

    std::stringstream header;

    if (M.getLive()){
      MP4::SIDX sidxBox;
      sidxBox.setTimescale(1000);
      sidxBox.setEarliestPresentationTime(keys.getTime(firstKey));

      MP4::sidxReference refItem;
      refItem.referencedSize = 230000;
      refItem.subSegmentDuration = keys.getTime(endKey) - keys.getTime(firstKey);
      refItem.sapStart = true;
      refItem.sapType = 16;
      refItem.sapDeltaTime = 0;

      refItem.referenceType = 0;
      sidxBox.setReference(refItem, 0);
      sidxBox.setReferenceID(1);

      header.write(sidxBox.asBox(), sidxBox.boxedSize());
    }

    MP4::MOOF moofBox;
    MP4::MFHD mfhdBox(fragment + 1);
    moofBox.setContent(mfhdBox, 0);

    size_t firstPart = keys.getFirstPart(firstKey);
    size_t endPart = parts.getEndValid();
    if (fragment + 1 < fragments.getEndValid()){
      endPart = keys.getFirstPart(fragments.getFirstKey(fragment + 1));
    }

    std::set<sortPart> trunOrder;

    uint64_t relativeOffset = fragmentHeaderSize(M, track, fragment) + 8;

    sortPart temp;
    temp.time = keys.getTime(firstKey);
    temp.partIndex = keys.getFirstPart(firstKey);
    temp.bytePos = relativeOffset;

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
    if (simplifyTrackIds){
      tfhdBox.setTrackID(simplifiedTrackId(M, track));
    }
    tfhdBox.setDefaultSampleDuration(444);
    tfhdBox.setDefaultSampleSize(444);
    tfhdBox.setDefaultSampleFlags((M.getType(track) == "video") ? (MP4::noIPicture | MP4::noKeySample)
                                                                : (MP4::isIPicture | MP4::isKeySample));
    tfhdBox.setSampleDescriptionIndex(1);
    trafBox.setContent(tfhdBox, 0);

    MP4::TFDT tfdtBox;
    if (M.getVod()){
      tfdtBox.setBaseMediaDecodeTime(M.getTimeForFragmentIndex(track, fragment) - M.getFirstms(track));
    }else{
      tfdtBox.setBaseMediaDecodeTime((UTCTime ? M.getTimeForFragmentIndex(track, fragment) + M.getBootMsOffset() + unixBootDiff : M.getTimeForFragmentIndex(track, fragment)));
    }
    trafBox.setContent(tfdtBox, 1);

    MP4::TRUN trunBox;
    trunBox.setFlags(MP4::trundataOffset | MP4::trunfirstSampleFlags | MP4::trunsampleSize |
                     MP4::trunsampleDuration | MP4::trunsampleOffsets);

    // The value set here, will be updated afterwards to the correct value
    trunBox.setDataOffset(trunOrder.begin()->bytePos);

    trunBox.setFirstSampleFlags(MP4::isIPicture | MP4::isKeySample);

    size_t trunOffset = 0;

    for (std::set<sortPart>::iterator it = trunOrder.begin(); it != trunOrder.end(); it++){
      MP4::trunSampleInformation sampleInfo;
      sampleInfo.sampleSize = parts.getSize(it->partIndex);
      sampleInfo.sampleDuration = parts.getDuration(it->partIndex);
      sampleInfo.sampleOffset = parts.getOffset(it->partIndex);
      trunBox.setSampleInformation(sampleInfo, trunOffset++);
    }
    trafBox.setContent(trunBox, 2);

    moofBox.setContent(trafBox, 1);

    header.write(moofBox.asBox(), moofBox.boxedSize());

    return header.str();
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
  std::string keyHeader(const DTSC::Meta &M, size_t track, uint64_t startTime, uint64_t endTime, uint64_t segmentNum, bool simplifyTrackIds, bool UTCTime){

    size_t firstPart = M.getPartIndex(startTime, track);
    size_t endPart = M.getPartIndex(endTime, track);
    std::stringstream header;
    MP4::MOOF moofBox;
    MP4::MFHD mfhdBox(segmentNum);
    moofBox.setContent(mfhdBox, 0);


    std::set<sortPart> trunOrder;

    //We use keyHeaderSize here to determine the relative offsets of the data in the 'mdat' box. 
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
    if (simplifyTrackIds){
      tfhdBox.setTrackID(simplifiedTrackId(M, track));
    }
    tfhdBox.setDefaultSampleDuration(444);
    tfhdBox.setDefaultSampleSize(444);
    tfhdBox.setDefaultSampleFlags((M.getType(track) == "video") ? (MP4::noIPicture | MP4::noKeySample)
                                                                : (MP4::isIPicture | MP4::isKeySample));
    tfhdBox.setSampleDescriptionIndex(1);
    trafBox.setContent(tfhdBox, 0);

    MP4::TFDT tfdtBox;
    if (M.getVod()){
      tfdtBox.setBaseMediaDecodeTime(startTime - M.getFirstms(track));
    }else{
      tfdtBox.setBaseMediaDecodeTime((UTCTime ? startTime + M.getBootMsOffset() + unixBootDiff : startTime));
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
        if (it == lastOne){
          sampleInfo.sampleDuration = endTime - it->time;
        }
        sampleInfo.sampleOffset = parts.getOffset(it->partIndex);
        trunBox.setSampleInformation(sampleInfo, trunOffset++);
      }
    }else{
      WARN_MSG("Empty CMAF header for track %zu: %zu-%zu contains no packets (first: %" PRIu64 ", last: %" PRIu64 "), firstPart=%zu, lastPart=%zu", track, startTime, endTime, M.getFirstms(track), M.getLastms(track), firstPart, endPart);
    }
    trafBox.setContent(trunBox, 2);

    moofBox.setContent(trafBox, 1);

    header.write(moofBox.asBox(), moofBox.boxedSize());

    return header.str();
  }
}// namespace CMAF
