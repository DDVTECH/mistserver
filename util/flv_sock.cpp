SWBaseSocket::SWBaseError SWBerr;
char * FLVbuffer;
int FLV_len;
int FLVbs = 0;
bool HeaderDone = false;
static char FLVheader[13];

void FLV_Readheader(SWUnixSocket & ss){
}//FLV_Readheader

void FLV_Dump(){FLV_len = 0;}

bool FLV_GetPacket(SWUnixSocket & ss){
  if (!HeaderDone){
    if (ss.frecv(FLVheader, 13, &SWBerr) == 13){HeaderDone = true;}
    return false;
  }

  
  if (FLVbs < 15){FLVbuffer = (char*)realloc(FLVbuffer, 15); FLVbs = 15;}
  //if received a whole header, receive a whole packet
  //if not, retry header next pass
  if (FLV_len == 0){
    if (ss.frecv(FLVbuffer, 11, &SWBerr) == 11){
      FLV_len = FLVbuffer[3] + 15;
      FLV_len += (FLVbuffer[2] << 8);
      FLV_len += (FLVbuffer[1] << 16);
      if (FLVbs < FLV_len){FLVbuffer = (char*)realloc(FLVbuffer, FLV_len);FLVbs = FLV_len;}
    }
  }else{
    if (ss.frecv(FLVbuffer+11, FLV_len-11, &SWBerr) == FLV_len-11){return true;}
  }
  return false;
}//FLV_GetPacket
