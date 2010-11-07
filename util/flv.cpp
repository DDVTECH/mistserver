#include <unistd.h> //for read()

struct FLV_Pack {
  int len;
  int buf;
  bool isKeyframe;
  char * data;
};//FLV_Pack

char FLVHeader[13];
bool All_Hell_Broke_Loose = false;

//checks FLV Header for correctness
//returns true if everything is alright, false otherwise
bool FLV_Checkheader(char * header){
  if (header[0] != 'F') return false;
  if (header[1] != 'L') return false;
  if (header[2] != 'V') return false;
  if (header[8] != 0x09) return false;
  if (header[9] != 0) return false;
  if (header[10] != 0) return false;
  if (header[11] != 0) return false;
  if (header[12] != 0) return false;
  return true;
}//FLV_Checkheader

//returns true if header is an FLV header
bool FLV_Isheader(char * header){
  if (header[0] != 'F') return false;
  if (header[1] != 'L') return false;
  if (header[2] != 'V') return false;
  return true;
}//FLV_Isheader

bool ReadUntil(char * buffer, unsigned int count, unsigned int & sofar){
  if (sofar >= count){return true;}
  int r = 0;
  r = fread(buffer + sofar,1,count-sofar,stdin);
  if (r < 0){All_Hell_Broke_Loose = true; return false;}
  sofar += r;
  if (sofar >= count){return true;}
  return false;
}

//gets a packet, storing in given FLV_Pack pointer.
//will assign pointer if null
//resizes FLV_Pack data field bigger if data doesn't fit
// (does not auto-shrink for speed!)
bool FLV_GetPacket(FLV_Pack *& p){
  static bool done = true;
  static unsigned int sofar = 0;
  if (!p){p = (FLV_Pack*)calloc(1, sizeof(FLV_Pack));}
  if (p->buf < 15){p->data = (char*)realloc(p->data, 15); p->buf = 15;}

  if (done){
    //read a header
    if (ReadUntil(p->data, 11, sofar)){
      //if its a correct FLV header, throw away and read tag header
      if (FLV_Isheader(p->data)){
        if (ReadUntil(p->data, 13, sofar)){
          if (FLV_Checkheader(p->data)){
            sofar = 0;
            memcpy(FLVHeader, p->data, 13);
          }else{All_Hell_Broke_Loose = true;}
        }
      }else{
        //if a tag header, calculate length and read tag body
        p->len = p->data[3] + 15;
        p->len += (p->data[2] << 8);
        p->len += (p->data[1] << 16);
        if (p->buf < p->len){p->data = (char*)realloc(p->data, p->len);p->buf = p->len;}
        done = false;
      }
    }
  }else{
    //read tag body
    if (ReadUntil(p->data, p->len, sofar)){
      //calculate keyframeness, next time read header again, return true
      p->isKeyframe = false;
      if ((p->data[0] == 0x09) && (((p->data[11] & 0xf0) >> 4) == 1)){p->isKeyframe = true;}
      done = true;
      sofar = 0;
      return true;
    }
  }
  return false;
}//FLV_GetPacket

