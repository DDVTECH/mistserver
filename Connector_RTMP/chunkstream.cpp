#include <map>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include <arpa/inet.h>

unsigned int chunk_rec_max = 128;
unsigned int chunk_snd_max = 128;
unsigned int rec_window_size = 1024*500;
unsigned int snd_window_size = 1024*500;
unsigned int rec_window_at = 0;
unsigned int snd_window_at = 0;
unsigned int rec_cnt = 0;
unsigned int snd_cnt = 0;

struct chunkinfo {
  unsigned int timestamp;
  unsigned int len;
  unsigned int real_len;
  unsigned int len_left;
  unsigned char msg_type_id;
  unsigned int msg_stream_id;
};//chunkinfo

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

//clean a chunk so that it may be re-used without memory leaks
void scrubChunk(struct chunkpack c){
  if (c.data){free(c.data);}
  c.data = 0;
  c.real_len = 0;
}//scrubChunk

//sends the chunk over the network
void SendChunk(chunkpack ch){
  unsigned char tmp;
  unsigned int tmpi;
  if (ch.cs_id <= 63){
    tmp = ch.cs_id; fwrite(&tmp, 1, 1, stdout);
  }else{
    if (ch.cs_id <= 255+64){
      tmp = 0; fwrite(&tmp, 1, 1, stdout);
      tmp = ch.cs_id - 64; fwrite(&tmp, 1, 1, stdout);
    }else{
      tmp = 1; fwrite(&tmp, 1, 1, stdout);
      tmpi = ch.cs_id - 64;
      tmp = tmpi % 256; fwrite(&tmp, 1, 1, stdout);
      tmp = tmpi / 256; fwrite(&tmp, 1, 1, stdout);
    }
  }
  //timestamp
  //TODO: support for > 0x00ffffff timestamps!
  tmpi = ch.timestamp;
  tmp = tmpi / (256*256); fwrite(&tmp, 1, 1, stdout);
  tmp = tmpi / 256; fwrite(&tmp, 1, 1, stdout);
  tmp = tmpi % 256; fwrite(&tmp, 1, 1, stdout);
  //len
  tmpi = ch.len;
  tmp = tmpi / (256*256); fwrite(&tmp, 1, 1, stdout);
  tmp = tmpi / 256; fwrite(&tmp, 1, 1, stdout);
  tmp = tmpi % 256; fwrite(&tmp, 1, 1, stdout);
  //msg type id
  tmp = ch.msg_type_id; fwrite(&tmp, 1, 1, stdout);
  //msg stream id
  tmp = ch.msg_stream_id / (256*256*256); fwrite(&tmp, 1, 1, stdout);
  tmp = ch.msg_stream_id / (256*256); fwrite(&tmp, 1, 1, stdout);
  tmp = ch.msg_stream_id / 256; fwrite(&tmp, 1, 1, stdout);
  tmp = ch.msg_stream_id % 256; fwrite(&tmp, 1, 1, stdout);
  ch.len_left = 0;
  while (ch.len_left < ch.len){
    tmpi = ch.len - ch.len_left;
    if (tmpi > chunk_snd_max){tmpi = chunk_snd_max;}
    fwrite((ch.data + ch.len_left), 1, tmpi, stdout);
    ch.len_left += tmpi;
    if (ch.len_left < ch.len){
      if (ch.cs_id <= 63){
        tmp = 0xC + ch.cs_id; fwrite(&tmp, 1, 1, stdout);
      }else{
        if (ch.cs_id <= 255+64){
          tmp = 0xC0; fwrite(&tmp, 1, 1, stdout);
          tmp = ch.cs_id - 64; fwrite(&tmp, 1, 1, stdout);
        }else{
          tmp = 0xC1; fwrite(&tmp, 1, 1, stdout);
          tmpi = ch.cs_id - 64;
          tmp = tmpi % 256; fwrite(&tmp, 1, 1, stdout);
          tmp = tmpi / 256; fwrite(&tmp, 1, 1, stdout);
        }
      }
    }
  }
  fflush(stdout);
}//SendChunk

//sends a chunk
void SendChunk(unsigned int cs_id, unsigned char msg_type_id, unsigned int msg_stream_id, std::string data){
  chunkpack ch;
  timeval t;
  gettimeofday(&t, 0);
  ch.cs_id = cs_id;
  ch.timestamp = t.tv_sec * 10 + t.tv_usec / 1000000;
  ch.len = data.size();
  ch.real_len = data.size();
  ch.len_left = 0;
  ch.msg_type_id = msg_type_id;
  ch.msg_stream_id = msg_stream_id;
  ch.data = (unsigned char*)malloc(data.size());
  memcpy(ch.data, data.c_str(), data.size());
  SendChunk(ch);
  free(ch.data);
}//SendChunk

//sends a control message
void SendCTL(unsigned char type, unsigned int data){
  chunkpack ch;
  timeval t;
  gettimeofday(&t, 0);
  ch.cs_id = 2;
  ch.timestamp = t.tv_sec * 10 + t.tv_usec / 1000000;
  ch.len = 4;
  ch.real_len = 4;
  ch.len_left = 0;
  ch.msg_type_id = type;
  ch.msg_stream_id = 0;
  ch.data = (unsigned char*)malloc(4);
  data = htonl(data);
  memcpy(ch.data, &data, 4);
  SendChunk(ch);
  free(ch.data);
}//SendCTL

//sends a control message
void SendCTL(unsigned char type, unsigned int data, unsigned char data2){
  chunkpack ch;
  timeval t;
  gettimeofday(&t, 0);
  ch.cs_id = 2;
  ch.timestamp = t.tv_sec * 10 + t.tv_usec / 1000000;
  ch.len = 5;
  ch.real_len = 5;
  ch.len_left = 0;
  ch.msg_type_id = type;
  ch.msg_stream_id = 0;
  ch.data = (unsigned char*)malloc(5);
  data = htonl(data);
  memcpy(ch.data, &data, 4);
  ch.data[4] = data2;
  SendChunk(ch);
  free(ch.data);
}//SendCTL

//sends a usr control message
void SendUSR(unsigned char type, unsigned int data){
  chunkpack ch;
  timeval t;
  gettimeofday(&t, 0);
  ch.cs_id = 2;
  ch.timestamp = t.tv_sec * 10 + t.tv_usec / 1000000;
  ch.len = 6;
  ch.real_len = 6;
  ch.len_left = 0;
  ch.msg_type_id = 4;
  ch.msg_stream_id = 0;
  ch.data = (unsigned char*)malloc(6);
  data = htonl(data);
  memcpy(ch.data+2, &data, 4);
  ch.data[0] = 0;
  ch.data[1] = type;
  SendChunk(ch);
  free(ch.data);
}//SendUSR

//sends a usr control message
void SendUSR(unsigned char type, unsigned int data, unsigned int data2){
  chunkpack ch;
  timeval t;
  gettimeofday(&t, 0);
  ch.cs_id = 2;
  ch.timestamp = t.tv_sec * 10 + t.tv_usec / 1000000;
  ch.len = 10;
  ch.real_len = 10;
  ch.len_left = 0;
  ch.msg_type_id = 4;
  ch.msg_stream_id = 0;
  ch.data = (unsigned char*)malloc(10);
  data = htonl(data);
  data2 = htonl(data2);
  memcpy(ch.data+2, &data, 4);
  memcpy(ch.data+6, &data2, 4);
  ch.data[0] = 0;
  ch.data[1] = type;
  SendChunk(ch);
  free(ch.data);
}//SendUSR

//ugly global, but who cares...
std::map<unsigned int, chunkinfo> prevmap;

//return previous packet of this cs_id
chunkinfo GetPrev(unsigned int cs_id){
  return prevmap[cs_id];
}//GetPrev

//store packet information of last packet of this cs_id
void PutPrev(chunkpack prev){
  prevmap[prev.cs_id].timestamp = prev.timestamp;
  prevmap[prev.cs_id].len = prev.len;
  prevmap[prev.cs_id].real_len = prev.real_len;
  prevmap[prev.cs_id].len_left = prev.len_left;
  prevmap[prev.cs_id].msg_type_id = prev.msg_type_id;
  prevmap[prev.cs_id].msg_stream_id = prev.msg_stream_id;
}//PutPrev

//get a chunk from standard input
struct chunkpack getChunk(){
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
  chunkinfo prev = GetPrev(ret.cs_id);
  //process the rest of the header, for each chunk type
  switch (ret.chunktype & 0xC0){
    case 0x00:
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
    case 0x40:
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
    case 0x80:
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
    case 0xC0:
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
  PutPrev(ret);
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
    if (p->len_left == 0){return p;}
    ch_lst[newchunk.cs_id] = p;
  }else{
    p = it->second;
    tmpdata = (unsigned char*)realloc(p->data, p->real_len + newchunk.real_len);
    if (tmpdata == 0){fprintf(stderr, "Error allocating memory!\n");return 0;}
    p->data = tmpdata;
    memcpy(p->data+p->real_len, newchunk.data, newchunk.real_len);
    p->real_len += newchunk.real_len;
    p->len_left -= newchunk.real_len;
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
  static chunkpack gwc_next, gwc_complete;
  static bool clean = false;
  if (!clean){gwc_complete.data = 0; clean = true;}//prevent brain damage
  chunkpack * ret = 0;
  scrubChunk(gwc_complete);
  while (true){
    gwc_next = getChunk();
    ret = AddChunkPart(gwc_next);
    scrubChunk(gwc_next);
    if (ret){
      gwc_complete = *ret;
      free(ret);//cleanup returned chunk
      return gwc_complete;
    }
  }
}//getWholeChunk
