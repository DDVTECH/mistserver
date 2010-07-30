#include <unistd.h> //for read()

struct FLV_Pack {
  int len;
  int buf;
  char * data;
};//FLV_Pack

char FLVHeader[13];

//reads full length from a file descriptor
void Magic_Read(char * buf, int len, int file){
  int i = 0;
  while (i < len) i += read(file, buf, len-i);
}


//reads a FLV header and checks for correctness
//returns true if everything is alright, false otherwise
bool FLV_Readheader(){
  fread(FLVHeader,1,13,stdin);
  if (FLVHeader[0] != 'F') return false;
  if (FLVHeader[1] != 'L') return false;
  if (FLVHeader[2] != 'V') return false;
  if (FLVHeader[8] != 0x09) return false;
  if (FLVHeader[9] != 0) return false;
  if (FLVHeader[10] != 0) return false;
  if (FLVHeader[11] != 0) return false;
  if (FLVHeader[12] != 0) return false;
  return true;
}//FLV_Readheader

//gets a packet, storing in given FLV_Pack pointer.
//will assign pointer if null
//resizes FLV_Pack data field bigger if data doesn't fit
// (does not auto-shrink for speed!)
void FLV_GetPacket(FLV_Pack *& p){
  if (!p){p = (FLV_Pack*)calloc(1, sizeof(FLV_Pack));}
  if (p->buf < 15){p->data = (char*)realloc(p->data, 15); p->buf = 15;}
  fread(p->data,1,11,stdin);
  p->len = p->data[3] + 15;
  p->len += (p->data[2] << 8);
  p->len += (p->data[1] << 16);
  if (p->buf < p->len){p->data = (char*)realloc(p->data, p->len);p->buf = p->len;}
  fread(p->data+11,1,p->len-11,stdin);
}//FLV_GetPacket
