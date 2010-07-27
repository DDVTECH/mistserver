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
  fprintf(stderr, "Got chunkstream ID %hhi\n", ret.chunktype & 0x3F);
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
  fprintf(stderr, "Got a type %hhi chunk\n", ret.chunktype & 0xC0);
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
      ret.msg_stream_id = temp*256*256;
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
  fprintf(stderr, "Timestamp: %i\n", ret.timestamp);
  fprintf(stderr, "Length: %i\n", ret.len);
  fprintf(stderr, "Message type ID: %hhi\n", ret.msg_type_id);
  fprintf(stderr, "Message stream ID: %i\n", ret.msg_stream_id);
  if (ret.len_left > 0){
    ret.real_len = ret.len_left;
    ret.len_left -= ret.real_len;
  }else{
    ret.real_len = ret.len;
  }
  if (ret.real_len > chunk_rec_max){
    ret.len_left += ret.real_len - chunk_rec_max;
  }
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
  if (ret.real_len > 0){
    ret.data = (unsigned char*)malloc(ret.real_len);
    fread(ret.data, 1, ret.real_len, stdin);
  }else{
    ret.data = 0;
  }
  return ret;
}
