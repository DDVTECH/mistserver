
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

bool ReadUntil(char * buffer, unsigned int count, unsigned int & sofar, int sock){
  if (sofar >= count){return true;}
  bool r = DDV_read(buffer + sofar,count-sofar,sock);
  sofar = count;
  fprintf(stderr, "Reading %i/%i\n", sofar, count);
  if (!r){
    All_Hell_Broke_Loose = true;
    fprintf(stderr, "ReadUntil fail: %s. All Hell Broke Loose!\n", strerror(errno));
  }
  return r;
}

//gets a packet, storing in given FLV_Pack pointer.
//will assign pointer if null
//resizes FLV_Pack data field bigger if data doesn't fit
// (does not auto-shrink for speed!)
bool FLV_GetPacket(FLV_Pack *& p, int sock){
  static bool done = true;
  static unsigned int sofar = 0;
  if (!p){p = (FLV_Pack*)calloc(1, sizeof(FLV_Pack));}
  if (p->buf < 15){p->data = (char*)realloc(p->data, 5000); p->buf = 5000;}

  if (done){
    //read a header
    if (ReadUntil(p->data, 11, sofar, sock)){
      //if its a correct FLV header, throw away and read tag header
      if (FLV_Isheader(p->data)){
        if (ReadUntil(p->data, 13, sofar, sock)){
          if (FLV_Checkheader(p->data)){
            sofar = 0;
            memcpy(FLVHeader, p->data, 13);
          }else{
            All_Hell_Broke_Loose = true;
            fprintf(stderr, "Invalid FLV header. All Hell Broke Loose!\n");
          }
        }
      }else{
        //if a tag header, calculate length and read tag body
        p->len = p->data[3] + 15;
        p->len += (p->data[2] << 8);
        p->len += (p->data[1] << 16);
        //fprintf(stderr, "Tag of len %i\n", p->len);
        if (p->buf < p->len){p->data = (char*)realloc(p->data, p->len);p->buf = p->len;}
        done = false;
      }
    }
  }else{
    //read tag body
    if (ReadUntil(p->data, p->len, sofar, sock)){
      //calculate keyframeness, next time read header again, return true
      p->isKeyframe = false;
      if ((p->data[0] == 0x09) && (((p->data[11] & 0xf0) >> 4) == 1)){p->isKeyframe = true;}
      int testlen = p->data[p->len-1] + 4;
      testlen += (p->data[p->len-2] << 8);
      testlen += (p->data[p->len-3] << 16);
      testlen += (p->data[p->len-4] << 24);
      if (p->len == testlen){
        fprintf(stderr, "Correct length tag...\n");
      }else{
        fprintf(stderr, "Len: %i, testlen: %i\n", p->len, testlen);
        All_Hell_Broke_Loose = true;
        fprintf(stderr, "ReadUntil fail: > 500kb tag? All Hell Broke Loose!\n", strerror(errno));
        return false;
      }
      done = true;
      sofar = 0;
      return true;
    }
  }
  return false;
}//FLV_GetPacket

