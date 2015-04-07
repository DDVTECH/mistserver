namespace h264 {
  unsigned long toAnnexB(const char * data, unsigned long dataSize, char *& result);
  unsigned long fromAnnexB(const char * data, unsigned long dataSize, char *& result);
}
