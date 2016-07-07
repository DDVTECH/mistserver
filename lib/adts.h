#include <string>

namespace aac {
  class adts {
    public:
      adts();
      adts(char * _data, unsigned long _len);
      adts(const adts & rhs);
      ~adts();
      adts& operator = (const adts & rhs);
      bool sameHeader(const adts & rhs) const;
      unsigned long getAACProfile() const;
      unsigned long getFrequencyIndex() const;
      unsigned long getFrequency() const;
      unsigned long getChannelConfig() const;
      unsigned long getChannelCount() const;
      unsigned long getHeaderSize() const;
      unsigned long getPayloadSize() const;
      unsigned long getSampleCount() const;
      char * getPayload();
      std::string toPrettyString() const;
      operator bool() const;
    private:
      char * data;
      unsigned long len;
  };
}
