#include <map>
#include <string.h>
#include <stdlib.h>

struct chunkpack {
  unsigned char chunktype;
  unsigned int cs_id;
  unsigned int timestamp;
  unsigned int len;
  unsigned int real_len;
  unsigned int len_left;
  unsigned char msg_type_id;
  unsigned int msg_stream_id;
  unsigned char * data;
};//chunkpack

unsigned int chunk_rec_max = 128;

//clean a chunk so that it may be re-used without memory leaks
void scrubChunk(struct chunkpack c){
  if (c.data){free(c.data);}
  c.data = 0;
  c.real_len = 0;
}//scrubChunk

//get a chunk from standard input
struct chunkpack getChunk(struct chunkpack prev){
  struct chunkpack ret;
  unsigned char temp;
  fread(&(ret.chunktype), 1, 1, stdin);
  //read the chunkstream ID properly
  switch (ret.chunktype & 0x3F){
    case 0:
      fread(&temp, 1, 1, stdin);
      ret.cs_id = temp + 64;
      break;
    case 1:
      fread(&temp, 1, 1, stdin);
      ret.cs_id = temp + 64;
      fread(&temp, 1, 1, stdin);
      ret.cs_id += temp * 256;
      break;
    default:
      ret.cs_id = ret.chunktype & 0x3F;
      break;
  }
  //process the rest of the header, for each chunk type
  switch (ret.chunktype & 0xC0){
    case 0:
      fread(&temp, 1, 1, stdin);
      ret.timestamp = temp*256*256;
      fread(&temp, 1, 1, stdin);
      ret.timestamp += temp*256;
      fread(&temp, 1, 1, stdin);
      ret.timestamp += temp;
      fread(&temp, 1, 1, stdin);
      ret.len = temp*256*256;
      fread(&temp, 1, 1, stdin);
      ret.len += temp*256;
      fread(&temp, 1, 1, stdin);
      ret.len += temp;
      ret.len_left = 0;
      fread(&temp, 1, 1, stdin);
      ret.msg_type_id = temp;
      fread(&temp, 1, 1, stdin);
      ret.msg_stream_id = temp*256*256*256;
      fread(&temp, 1, 1, stdin);
      ret.msg_stream_id += temp*256*256;
      fread(&temp, 1, 1, stdin);
      ret.msg_stream_id += temp*256;
      fread(&temp, 1, 1, stdin);
      ret.msg_stream_id += temp;
      break;
    case 1:
      fread(&temp, 1, 1, stdin);
      ret.timestamp = temp*256*256;
      fread(&temp, 1, 1, stdin);
      ret.timestamp += temp*256;
      fread(&temp, 1, 1, stdin);
      ret.timestamp += temp;
      ret.timestamp += prev.timestamp;
      fread(&temp, 1, 1, stdin);
      ret.len = temp*256*256;
      fread(&temp, 1, 1, stdin);
      ret.len += temp*256;
      fread(&temp, 1, 1, stdin);
      ret.len += temp;
      ret.len_left = 0;
      fread(&temp, 1, 1, stdin);
      ret.msg_type_id = temp;
      ret.msg_stream_id = prev.msg_stream_id;
      break;
    case 2:
      fread(&temp, 1, 1, stdin);
      ret.timestamp = temp*256*256;
      fread(&temp, 1, 1, stdin);
      ret.timestamp += temp*256;
      fread(&temp, 1, 1, stdin);
      ret.timestamp += temp;
      ret.timestamp += prev.timestamp;
      ret.len = prev.len;
      ret.len_left = prev.len_left;
      ret.msg_type_id = prev.msg_type_id;
      ret.msg_stream_id = prev.msg_stream_id;
      break;
    case 3:
      ret.timestamp = prev.timestamp;
      ret.len = prev.len;
      ret.len_left = prev.len_left;
      ret.msg_type_id = prev.msg_type_id;
      ret.msg_stream_id = prev.msg_stream_id;
      break;
  }
  //calculate chunk length, real length, and length left till complete
  if (ret.len_left > 0){
    ret.real_len = ret.len_left;
    ret.len_left -= ret.real_len;
  }else{
    ret.real_len = ret.len;
  }
  if (ret.real_len > chunk_rec_max){
    ret.len_left += ret.real_len - chunk_rec_max;
    ret.real_len = chunk_rec_max;
  }
  //read extended timestamp, if neccesary
  if (ret.timestamp == 0x00ffffff){
    fread(&temp, 1, 1, stdin);
    ret.timestamp = temp*256*256*256;
    fread(&temp, 1, 1, stdin);
    ret.timestamp += temp*256*256;
    fread(&temp, 1, 1, stdin);
    ret.timestamp += temp*256;
    fread(&temp, 1, 1, stdin);
    ret.timestamp += temp;
  }
  //read data if length > 0, and allocate it
  if (ret.real_len > 0){
    ret.data = (unsigned char*)malloc(ret.real_len);
    fread(ret.data, 1, ret.real_len, stdin);
  }else{
    ret.data = 0;
  }
  return ret;
}//getChunk

//adds newchunk to global list of unfinished chunks, re-assembling them complete
//returns pointer to chunk when a chunk is finished, 0 otherwise
//removes pointed to chunk from internal list if returned, without cleanup
// (cleanup performed in getWholeChunk function)
chunkpack * AddChunkPart(chunkpack newchunk){
  chunkpack * p;
  unsigned char * tmpdata = 0;
  static std::map<unsigned int, chunkpack *> ch_lst;
  std::map<unsigned int, chunkpack *>::iterator it;
  it = ch_lst.find(newchunk.cs_id);
  if (it == ch_lst.end()){
    p = (chunkpack*)malloc(sizeof(chunkpack));
    *p = newchunk;
    p->data = (unsigned char*)malloc(p->real_len);
    memcpy(p->data, newchunk.data, p->real_len);
    if (p->len_left == 0){
      fprintf(stderr, "New chunk of size %i / %i is whole - returning it\n", newchunk.real_len, newchunk.len);
      return p;
    }
    fprintf(stderr, "New chunk of size %i / %i\n", newchunk.real_len, newchunk.len);
    ch_lst[newchunk.cs_id] = p;
  }else{
    p = it->second;
    fprintf(stderr, "Appending chunk of size %i to chunk of size %i / %i...\n", newchunk.real_len, p->real_len, p->len);
    fprintf(stderr, "Reallocating %i bytes\n", p->real_len + newchunk.real_len);
    tmpdata = (unsigned char*)realloc(p->data, p->real_len + newchunk.real_len);
    if (tmpdata == 0){fprintf(stderr, "Error allocating memory!\n");return 0;}
    p->data = tmpdata;
    fprintf(stderr, "Reallocated %i bytes\n", p->real_len + newchunk.real_len);
    memcpy(p->data+p->real_len, newchunk.data, newchunk.real_len);
    fprintf(stderr, "Copied contents over\n");
    p->real_len += newchunk.real_len;
    p->len_left -= newchunk.real_len;
    fprintf(stderr, "New size: %i / %i\n", p->real_len, p->len);
    if (p->len_left <= 0){
      ch_lst.erase(it);
      return p;
    }else{
      ch_lst[newchunk.cs_id] = p;//pointer may have changed
    }
  }
  return 0;
}//AddChunkPart

//grabs chunks until a whole one comes in, then returns that
chunkpack getWholeChunk(){
  static chunkpack gwc_next, gwc_complete, gwc_prev;
  static bool clean = false;
  if (!clean){gwc_prev.data = 0; clean = true;}//prevent brain damage
  chunkpack * ret = 0;
  scrubChunk(gwc_complete);
  while (true){
    gwc_next = getChunk(gwc_prev);
    scrubChunk(gwc_prev);
    gwc_prev = gwc_next;
    fprintf(stderr, "Processing chunk...\n");
    ret = AddChunkPart(gwc_next);
    if (ret){
      gwc_complete = *ret;
      free(ret);//cleanup returned chunk
      return gwc_complete;
    }
  }
}//getWholeChunk
