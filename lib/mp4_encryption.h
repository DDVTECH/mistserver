#include "mp4.h"
#include "mp4_ms.h"

namespace MP4{

  struct UUID_SampleEncryption_Sample_Entry{
    uint32_t BytesClear;
    uint32_t BytesEncrypted;
  };

  struct UUID_SampleEncryption_Sample{
    std::string InitializationVector;
    uint32_t NumberOfEntries;
    std::vector<UUID_SampleEncryption_Sample_Entry> Entries;
  };

  class PSSH : public fullBox{
  public:
    PSSH();
    std::string getSystemIDHex();
    void setSystemIDHex(const std::string &systemID);
    std::string toPrettyString(uint32_t indent = 0);
    size_t getKIDCount();
    size_t getDataSize();
    char *getData();
    void setData(const std::string &data);
  };

  class TENC : public fullBox{
  public:
    TENC();
    std::string toPrettyString(uint32_t indent = 0);
    size_t getDefaultIsEncrypted();
    void setDefaultIsEncrypted(size_t isEncrypted);
    size_t getDefaultIVSize();
    void setDefaultIVSize(uint8_t ivSize);
    std::string getDefaultKID();
    void setDefaultKID(const std::string &kid);
  };

  class SENC : public fullBox{
  public:
    SENC();
    uint32_t getSampleCount() const;
    void setSample(UUID_SampleEncryption_Sample newSample, size_t index);
    UUID_SampleEncryption_Sample getSample(size_t index) const;
    std::string toPrettyString(uint32_t indent = 0) const;
  };

  class SAIZ : public fullBox{
  public:
    SAIZ(size_t entryCount = 0);
    size_t getDefaultSampleSize();
    size_t getEntryCount();
    size_t getEntrySize(size_t entryNo);
    std::string toPrettyString(uint32_t indent = 0);
  };

  class SAIO : public fullBox{
  public:
    SAIO(size_t offset = 0);
    size_t getEntryCount();
    size_t getEntrySize(size_t entryNo);
    std::string toPrettyString(uint32_t indent = 0);
  };

  class UUID_SampleEncryption : public UUID{
  public:
    UUID_SampleEncryption();
    UUID_SampleEncryption(const SENC &senc);
    void setVersion(uint32_t newVersion);
    uint32_t getVersion();
    void setFlags(uint32_t newFlags);
    uint32_t getFlags();
    void setAlgorithmID(uint32_t newAlgorithmID);
    uint32_t getAlgorithmID();
    void setIVSize(uint32_t newIVSize);
    uint32_t getIVSize();
    void setKID(std::string newKID);
    std::string getKID();
    uint32_t getSampleCount();
    void setSample(UUID_SampleEncryption_Sample newSample, size_t index);
    UUID_SampleEncryption_Sample getSample(size_t index);
    std::string toPrettyString(uint32_t indent = 0);
  };

  class UUID_TrackEncryption : public UUID{
  public:
    UUID_TrackEncryption();
    void setVersion(uint32_t newVersion);
    uint32_t getVersion();
    void setFlags(uint32_t newFlags);
    uint32_t getFlags();
    void setDefaultAlgorithmID(uint32_t newAlgorithmID);
    uint32_t getDefaultAlgorithmID();
    void setDefaultIVSize(uint8_t newIVSize);
    uint8_t getDefaultIVSize();
    void setDefaultKID(std::string newKID);
    std::string getDefaultKID();
    std::string toPrettyString(uint32_t indent = 0);
  };

  class UUID_ProtectionSystemSpecificHeader : public UUID{
  public:
    UUID_ProtectionSystemSpecificHeader();
    void setVersion(uint32_t newVersion);
    uint32_t getVersion();
    void setFlags(uint32_t newFlags);
    uint32_t getFlags();
    void setSystemID(std::string newID);
    std::string getSystemID();
    void setDataSize(uint32_t newDataSize);
    uint32_t getDataSize();
    void setData(std::string newData);
    std::string getData();
    std::string toPrettyString(uint32_t indent = 0);
  };

  class SINF : public Box{
  public:
    SINF();
    void setEntry(Box &newEntry, uint32_t no);
    Box &getEntry(uint32_t no);
    std::string toPrettyString(uint32_t indent = 0);
  };

  class FRMA : public Box{
  public:
    FRMA(const std::string &originalFormat = "");
    void setOriginalFormat(const std::string &newFormat);
    std::string getOriginalFormat();
    std::string toPrettyString(uint32_t indent = 0);
  };

  class SCHM : public fullBox{
  public:
    SCHM(uint32_t schemeType = 0x636E6563, uint32_t schemeVersion = 0x00000100); // CENC defaults
    void setSchemeType(uint32_t newType);
    uint32_t getSchemeType();
    void setSchemeVersion(uint32_t newVersion);
    uint32_t getSchemeVersion();
    void setSchemeURI(std::string newURI);
    std::string getSchemeURI();
    std::string toPrettyString(uint32_t indent = 0);
  };

  class SCHI : public Box{
  public:
    SCHI();
    void setContent(Box &newContent);
    Box &getContent();
    std::string toPrettyString(uint32_t indent = 0);
  };

}// namespace MP4
