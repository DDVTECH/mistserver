#include "cmaf.h"

namespace CMAF{
  size_t payloadSize(const DTSC::Meta &M, size_t track, size_t fragment){
    DTSC::Fragments fragments(M.fragments(track));
    DTSC::Keys keys(M.keys(track));
    DTSC::Parts parts(M.parts(track));

    size_t firstKey = fragments.getFirstKey(fragment);
    size_t endKey = keys.getEndValid();
    if (fragment + 1 < fragments.getEndValid()){endKey = fragments.getFirstKey(fragment + 1);}

    size_t firstPart = keys.getFirstPart(firstKey);
    size_t endPart = parts.getEndValid();
    if (endKey != keys.getEndValid()){endPart = keys.getFirstPart(endKey);}
    size_t payloadSize = 0;
    for (size_t i = firstPart; i < endPart; i++){payloadSize += parts.getSize(i);}
    return payloadSize;
  }

  size_t trackHeaderSize(const DTSC::Meta &M, size_t track){
    // EDTS Box needed? + 36
    size_t res = 36 + 8 + 108 + 8 + 92 + 8 + 32 + 33 + 44 + 8 + 20 + 16 + 16 + 16 + 40;

    res += M.getTrackIdentifier(track).size();

    // Type-specific boxes
    std::string tType = M.getType(track);
    if (tType == "video"){res += 20 + 16 + 86 + 16 + 8 + M.getInit(track).size();}
    if (tType == "audio"){
      res += 16 + 16 + 36 + 35 + (M.getInit(track).size() ? 2 + M.getInit(track).size() : 0);
    }
    if (tType == "meta"){res += 12 + 16 + 64;}

    if (M.getVod()){res += 16;}

    return res;
  }

  std::string trackHeader(const DTSC::Meta &M, size_t track){
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
    mvhdBox.setTrackID(track + 2); // This value needs to point to an unused trackid
    moovBox.setContent(mvhdBox, 0);

    MP4::TRAK trakBox;

    MP4::TKHD tkhdBox(M, track);
    tkhdBox.setDuration(0);
    trakBox.setContent(tkhdBox, 0);

    MP4::MDIA mdiaBox;

    MP4::MDHD mdhdBox(0, M.getLang(track));
    mdiaBox.setContent(mdhdBox, 0);

    MP4::HDLR hdlrBox(tType, M.getTrackIdentifier(track));
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
      stsdBox.setEntry(sampleEntry, 0);
    }else if (tType == "audio"){
      MP4::AudioSampleEntry sampleEntry(M, track);
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
      sidxBox.setEarliestPresentationTime(keys.getTime(0) + parts.getOffset(0) - M.getFirstms(track));

      for (size_t i = 0; i < fragments.getEndValid(); i++){
        size_t firstKey = fragments.getFirstKey(i);
        size_t endKey =
            ((i + 1 < fragments.getEndValid()) ? fragments.getFirstKey(i + 1) : keys.getEndValid());

        MP4::sidxReference refItem;
        refItem.referencedSize = payloadSize(M, track, i) + fragmentHeaderSize(M, track, i) + 8;
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

  std::string fragmentHeader(const DTSC::Meta &M, size_t track, size_t fragment){

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
    tfhdBox.setDefaultSampleDuration(444);
    tfhdBox.setDefaultSampleSize(444);
    tfhdBox.setDefaultSampleFlags((M.getType(track) == "video") ? (MP4::noIPicture | MP4::noKeySample)
                                                                : (MP4::isIPicture | MP4::isKeySample));
    tfhdBox.setSampleDescriptionIndex(0);
    trafBox.setContent(tfhdBox, 0);

    MP4::TFDT tfdtBox;
    if (M.getVod()){
      tfdtBox.setBaseMediaDecodeTime(M.getTimeForFragmentIndex(track, fragment) - M.getFirstms(track));
    }else{
      tfdtBox.setBaseMediaDecodeTime(M.getTimeForFragmentIndex(track, fragment));
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
}// namespace CMAF
