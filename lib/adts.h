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
      unsigned long getAACProfile();
      unsigned long getFrequencyIndex();
      unsigned long getFrequency();
      unsigned long getChannelConfig();
      unsigned long getChannelCount();
      unsigned long getHeaderSize();
      unsigned long getPayloadSize();
      unsigned long getSampleCount();
      char * getPayload();
      std::string toPrettyString();
    private:
      char * data;
      unsigned long len;
  };
}
