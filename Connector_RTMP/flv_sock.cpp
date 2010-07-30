SWBaseSocket::SWBaseError SWBerr;
char * FLVbuffer;
int FLV_len;
int FLVbs = 0;

void FLV_Readheader(SWUnixSocket & ss){
  static char header[13];
  while (ss.frecv(header, 13, &SWBerr) != 13){
    //wait
  }
}//FLV_Readheader

bool FLV_GetPacket(SWUnixSocket & ss){
  if (FLVbs < 15){FLVbuffer = (char*)realloc(FLVbuffer, 15); FLVbs = 15;}
  //if received a whole header, receive a whole packet
  //if not, retry header next pass
  if (ss.frecv(FLVbuffer, 11, &SWBerr) == 11){
    FLV_len = FLVbuffer[3] + 15;
    FLV_len += (FLVbuffer[2] << 8);
    FLV_len += (FLVbuffer[1] << 16);
    if (FLVbs < FLV_len){FLVbuffer = (char*)realloc(FLVbuffer, FLV_len);FLVbs = FLV_len;}
    while (ss.frecv(FLVbuffer+11, FLV_len-11, &SWBerr) != FLV_len-11){
      //wait
    }
    return true;
  }
  return false;
}//FLV_GetPacket
