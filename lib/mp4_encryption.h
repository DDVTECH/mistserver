#include "mp4.h"
#include "mp4_ms.h"

namespace MP4 {

  struct UUID_SampleEncryption_Sample_Entry{
    uint32_t BytesClear;
    uint32_t BytesEncrypted;
  };

  struct UUID_SampleEncryption_Sample{
    std::string InitializationVector;
    uint32_t NumberOfEntries;
    std::vector<UUID_SampleEncryption_Sample_Entry> Entries;
  };

  class UUID_SampleEncryption: public UUID{
    public:
      UUID_SampleEncryption();
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

  class UUID_TrackEncryption: public UUID{
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

  class UUID_ProtectionSystemSpecificHeader: public UUID{
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

  class SINF: public Box{
    public:
      SINF();
      void setEntry(Box & newEntry, uint32_t no);
      Box & getEntry(uint32_t no);
      std::string toPrettyString(uint32_t indent = 0);
  };

  class FRMA: public Box{
    public:
      FRMA();
      void setOriginalFormat(std::string newFormat);
      std::string getOriginalFormat();
      std::string toPrettyString(uint32_t indent = 0);
  };

  class SCHM: public fullBox{
    public:
      SCHM();
      void setSchemeType(uint32_t newType);
      uint32_t getSchemeType();
      void setSchemeVersion(uint32_t newVersion);
      uint32_t getSchemeVersion();
      void setSchemeURI(std::string newURI);
      std::string getSchemeURI();
      std::string toPrettyString(uint32_t indent = 0);
  };

  class SCHI: public Box{
    public:
      SCHI();
      void setContent(Box & newContent);
      Box & getContent();
      std::string toPrettyString(uint32_t indent = 0);
  };

}
