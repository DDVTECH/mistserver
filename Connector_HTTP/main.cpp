//debugging level 0 = nothing
//debugging level 1 = critical errors
//debugging level 2 = errors
//debugging level 3 = status information
//debugging level 4 = extremely verbose status information
#define DEBUG 4

#include <iostream>
#include <queue>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/epoll.h>
#include <getopt.h>
#include <ctime>

enum {HANDLER_NONE, HANDLER_PROGRESSIVE, HANDLER_FLASH, HANDLER_APPLE, HANDLER_MICRO};

#define DEFAULT_PORT 8080
#include "../util/server_setup.cpp"
#include "../util/http_parser.cpp"
#include "../util/MP4/interface.cpp"
#include "amf.cpp"

static const std::string base64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
std::string base64_encode(std::string const input) {
  std::string ret;
  unsigned int in_len = input.size();
  char quad[4], triple[3];
  unsigned int i, x, n = 3;
  for (x = 0; x < in_len; x = x + 3){
    if ((in_len - x) / 3 == 0){n = (in_len - x) % 3;}
    for (i=0; i < 3; i++){triple[i] = '0';}
    for (i=0; i < n; i++){triple[i] = input[x + i];}
    quad[0] = base64_chars[(triple[0] & 0xFC) >> 2]; // FC = 11111100
    quad[1] = base64_chars[((triple[0] & 0x03) << 4) | ((triple[1] & 0xF0) >> 4)]; // 03 = 11
    quad[2] = base64_chars[((triple[1] & 0x0F) << 2) | ((triple[2] & 0xC0) >> 6)]; // 0F = 1111, C0=11110
    quad[3] = base64_chars[triple[2] & 0x3F]; // 3F = 111111
    if (n < 3){quad[3] = '=';}
    if (n < 2){quad[2] = '=';}
    for(i=0; i < 4; i++){ret += quad[i];}
  }
  return ret;
}//base64_encode

int FlvToFragNum( FLV_Pack * tag ) {
  int Timestamp = (tag->data[7] << 24 ) + (tag->data[4] << 16 ) + (tag->data[5] << 8 ) + (tag->data[6] );
  return (Timestamp / 10000) + 1;
}

int FlvGetTimestamp( FLV_Pack * tag ) {
  return ( (tag->data[7] << 24 ) + (tag->data[4] << 16 ) + (tag->data[5] << 8 ) + (tag->data[6] ) );
}

std::string GetMetaData( ) {
  AMFType amfreply("container", (unsigned char)AMF0_DDV_CONTAINER);
  amfreply.addContent(AMFType("onMetaData",(unsigned char)AMF0_STRING));
  amfreply.addContent(AMFType("",(unsigned char)AMF0_ECMA_ARRAY));
  amfreply.getContentP(1)->addContent(AMFType("trackinfo", (unsigned char)AMF0_STRICT_ARRAY));
  amfreply.getContentP(1)->getContentP(0)->addContent(AMFType("arrVal"));
//  amfreply.getContentP(1)->getContentP(0)->getContentP(0)->addContent(AMFType("timescale",(double)1000));
//  amfreply.getContentP(1)->getContentP(0)->getContentP(0)->addContent(AMFType("length",(double)59641700));
  amfreply.getContentP(1)->getContentP(0)->getContentP(0)->addContent(AMFType("language","eng"));
  amfreply.getContentP(1)->getContentP(0)->getContentP(0)->addContent(AMFType("sampledescription", (unsigned char)AMF0_STRICT_ARRAY));
  amfreply.getContentP(1)->getContentP(0)->getContentP(0)->getContentP(1)->addContent(AMFType("arrVal"));
  amfreply.getContentP(1)->getContentP(0)->getContentP(0)->getContentP(1)->getContentP(0)->addContent(AMFType("sampletype","avc1"));
  amfreply.getContentP(1)->getContentP(0)->addContent(AMFType("arrVal"));
//  amfreply.getContentP(1)->getContentP(0)->getContentP(1)->addContent(AMFType("timescale",(double)44100));
//  amfreply.getContentP(1)->getContentP(0)->getContentP(1)->addContent(AMFType("length",(double)28630000));
  amfreply.getContentP(1)->getContentP(0)->getContentP(1)->addContent(AMFType("language","eng"));
  amfreply.getContentP(1)->getContentP(0)->getContentP(1)->addContent(AMFType("sampledescription", (unsigned char)AMF0_STRICT_ARRAY));
  amfreply.getContentP(1)->getContentP(0)->getContentP(1)->getContentP(1)->addContent(AMFType("arrVal"));
  amfreply.getContentP(1)->getContentP(0)->getContentP(1)->getContentP(1)->getContentP(0)->addContent(AMFType("sampletype","mp4a"));
  amfreply.getContentP(1)->addContent(AMFType("audiochannels",(double)2));
  amfreply.getContentP(1)->addContent(AMFType("audiosamplerate",(double)44100));
  amfreply.getContentP(1)->addContent(AMFType("videoframerate",(double)25));
  amfreply.getContentP(1)->addContent(AMFType("aacaot",(double)2));
  amfreply.getContentP(1)->addContent(AMFType("avclevel",(double)12));
  amfreply.getContentP(1)->addContent(AMFType("avcprofile",(double)77));
  amfreply.getContentP(1)->addContent(AMFType("audiocodecid","mp4a"));
  amfreply.getContentP(1)->addContent(AMFType("videocodecid","avc1"));
  amfreply.getContentP(1)->addContent(AMFType("width",(double)1280));
  amfreply.getContentP(1)->addContent(AMFType("height",(double)720));
  amfreply.getContentP(1)->addContent(AMFType("frameWidth",(double)1280));
  amfreply.getContentP(1)->addContent(AMFType("frameHeight",(double)720));
  amfreply.getContentP(1)->addContent(AMFType("displayWidth",(double)1280));
  amfreply.getContentP(1)->addContent(AMFType("displayHeight",(double)720));
//  amfreply.getContentP(1)->addContent(AMFType("moovposition",(double)35506700));
//  amfreply.getContentP(1)->addContent(AMFType("duration",(double)596.458));
  return amfreply.Pack( );
}

std::string BuildManifest( std::string MetaData, std::string MovieId, int CurrentMediaTime ) {
  Interface * temp = new Interface;
  std::string Result="<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<manifest xmlns=\"http://ns.adobe.com/f4m/1.0\">\n";
  Result += "<id>";
  Result += MovieId;
  Result += "</id>\n<mimeType>video/mp4</mimeType>\n";
  Result += "<streamType>live</streamType>\n";
  Result += "<deliveryType>streaming</deliveryType>\n";
  Result += "<bootstrapInfo profile=\"named\" id=\"bootstrap1\">";
  Result += base64_encode(temp->GenerateLiveBootstrap(1));
  Result += "</bootstrapInfo>\n";
  Result += "<media streamId=\"1\" bootstrapInfoId=\"bootstrap1\" url=\"";
  Result += MovieId;
  Result += "/\">\n";
  Result += "<metadata>";
  Result += base64_encode(GetMetaData());
  Result += "</metadata>\n";
  Result += "</media>\n";
  Result += "</manifest>\n";
  delete temp;
  return Result;
}

int mainHandler(int CONN_fd){
//  GetMetaData( ); return 0;
  int handler = HANDLER_PROGRESSIVE;
  bool ready4data = false;//set to true when streaming starts
  bool inited = false;
  bool progressive_has_sent_header = false;
  int ss;
  std::string streamname;
  std::string FlashBuf;
  std::string FlashMeta;
  bool Flash_ManifestSent = false;
  int Flash_RequestPending = 0;
  std::queue<std::string> Flash_FragBuffer;
  FLV_Pack * tag = 0;
  char * Video_Init_Data = 0;
  int Video_Init_Len = 0;
  char * Audio_Init_Data = 0;
  int Audio_Init_Len = 0;
  HTTPReader HTTP_R, HTTP_S;//HTTP Receiver en HTTP Sender.

  int retval;
  int poller = epoll_create(1);
  int sspoller = epoll_create(1);
  struct epoll_event ev;
  ev.events = EPOLLIN;
  ev.data.fd = CONN_fd;
  epoll_ctl(poller, EPOLL_CTL_ADD, CONN_fd, &ev);
  struct epoll_event events[1];

  std::string Movie = "";
  std::string Quality = "";
  int Segment = -1;
  int ReqFragment = -1;
  int temp;
  //int CurrentFragment = -1; later herbruiken?

  while (!socketError && !All_Hell_Broke_Loose){
    //only parse input if available or not yet init'ed
    if (HTTP_R.ReadSocket(CONN_fd)){
      handler = HANDLER_PROGRESSIVE;
      if ((HTTP_R.url.find("Seg") != std::string::npos) && (HTTP_R.url.find("Frag") != std::string::npos)){handler = HANDLER_FLASH;}
      if (HTTP_R.url.find("f4m") != std::string::npos){handler = HANDLER_FLASH;}
      if (HTTP_R.url == "/crossdomain.xml"){
        handler = HANDLER_NONE;
        HTTP_S.Clean();
        HTTP_S.SetHeader("Content-Type", "text/xml");
        HTTP_S.SetBody("<?xml version=\"1.0\"?><!DOCTYPE cross-domain-policy SYSTEM \"http://www.adobe.com/xml/dtds/cross-domain-policy.dtd\"><cross-domain-policy><allow-access-from domain=\"*\" /><site-control permitted-cross-domain-policies=\"all\"/></cross-domain-policy>");
        HTTP_S.SendResponse(CONN_fd, "200", "OK");//geen SetBody = unknown length! Dat willen we hier.
        #if DEBUG >= 3
        printf("Sending crossdomain.xml file\n");
        #endif
      }
      if(handler == HANDLER_FLASH){
        if (HTTP_R.url.find("f4m") == std::string::npos){
          Movie = HTTP_R.url.substr(1);
          Movie = Movie.substr(0,Movie.find("/"));
          Quality = HTTP_R.url.substr( HTTP_R.url.find("/",1)+1 );
          Quality = Quality.substr(0, Quality.find("Seg"));
          temp = HTTP_R.url.find("Seg") + 3;
          Segment = atoi( HTTP_R.url.substr(temp,HTTP_R.url.find("-",temp)-temp).c_str());
          temp = HTTP_R.url.find("Frag") + 4;
          ReqFragment = atoi( HTTP_R.url.substr(temp).c_str() );
          #if DEBUG >= 4
          printf( "URL: %s\n", HTTP_R.url.c_str());
          printf( "Movie: %s, Quality: %s, Seg %d Frag %d\n", Movie.c_str(), Quality.c_str(), Segment, ReqFragment);
          #endif
          Flash_RequestPending++;
        }else{
          Movie = HTTP_R.url.substr(1);
          Movie = Movie.substr(0,Movie.find("/"));
        }
        streamname = "/tmp/shared_socket_";
        for (std::string::iterator i=Movie.end()-1; i>=Movie.begin(); --i){
          if (!isalpha(*i) && !isdigit(*i)){
            Movie.erase(i);
          }else{
            *i=tolower(*i);
          }//strip nonalphanumeric
        }
        streamname += Movie;
        ready4data = true;
      }//FLASH handler
      if (handler == HANDLER_PROGRESSIVE){
        //in het geval progressive nemen we aan dat de URL de streamname is, met .flv erachter
        streamname = HTTP_R.url.substr(0, HTTP_R.url.size()-4);//strip de .flv
        for (std::string::iterator i=streamname.end()-1; i>=streamname.begin(); --i){
          if (!isalpha(*i) && !isdigit(*i)){streamname.erase(i);}else{*i=tolower(*i);}//strip nonalphanumeric
        }
        streamname = "/tmp/shared_socket_" + streamname;//dit is dan onze shared_socket
        //normaal zouden we ook een position uitlezen uit de URL, maar bij LIVE streams is dat zinloos
        printf("Streamname: %s\n", streamname.c_str());
        ready4data = true;
      }//PROGRESSIVE handler
      HTTP_R.CleanForNext(); //maak schoon na verwerken voor eventuele volgende requests...
    }
    if (ready4data){
      if (!inited){
        //we are ready, connect the socket!
        ss = DDV_OpenUnix(streamname);
        if (ss <= 0){
          #if DEBUG >= 1
          fprintf(stderr, "Could not connect to server!\n");
          #endif
          socketError = 1;
          break;
        }
        ev.events = EPOLLIN;
        ev.data.fd = ss;
        epoll_ctl(sspoller, EPOLL_CTL_ADD, ss, &ev);
        #if DEBUG >= 3
        fprintf(stderr, "Everything connected, starting to send video data...\n");
        #endif
        inited = true;
      }
      if ((Flash_RequestPending > 0) && !Flash_FragBuffer.empty()){
        HTTP_S.Clean();
        HTTP_S.SetHeader("Content-Type","video/mp4");
        HTTP_S.SetBody(Interface::mdatFold(Flash_FragBuffer.front()));
        Flash_FragBuffer.pop();
        HTTP_S.SendResponse(CONN_fd, "200", "OK");//schrijf de HTTP response header
        Flash_RequestPending--;
        #if DEBUG >= 3
        fprintf(stderr, "Sending a video fragment. %i left in buffer, %i requested\n", (int)Flash_FragBuffer.size(), Flash_RequestPending);
        #endif
      }
      retval = epoll_wait(sspoller, events, 1, 1);
      switch (DDV_ready(ss)){
        case 0:
          socketError = true;
          #if DEBUG >= 1
          fprintf(stderr, "Source socket is disconnected.\n");
          #endif
          break;
        case -1: break;//not ready yet
        default:
          if (FLV_GetPacket(tag, ss)){//able to read a full packet?f
            if (handler == HANDLER_FLASH){
              if(tag->data[0] != 0x12 ) {
                if ((tag->isKeyframe) && (Video_Init_Data == 0)){
                  if (((tag->data[11] & 0x0f) == 7) && (tag->data[12] == 0)){
                    tag->data[4] = 0;//timestamp to zero
                    tag->data[5] = 0;//timestamp to zero
                    tag->data[6] = 0;//timestamp to zero
                    Video_Init_Data = (char*)malloc(tag->len);
                    Video_Init_Len = tag->len;
                    memcpy(Video_Init_Data, tag->data, tag->len);
                  }
                }
                if ((tag->data[0] == 0x08) && (Audio_Init_Data == 0)){
                  if (((tag->data[11] & 0xf0) >> 4) == 10){//aac packet
                    tag->data[4] = 0;//timestamp to zero
                    tag->data[5] = 0;//timestamp to zero
                    tag->data[6] = 0;//timestamp to zero
                    Audio_Init_Data = (char*)malloc(tag->len);
                    Audio_Init_Len = tag->len;
                    memcpy(Audio_Init_Data, tag->data, tag->len);
                  }
                }
                if (tag->isKeyframe){
                  if (FlashBuf != ""){
                    Flash_FragBuffer.push(FlashBuf);
                    #if DEBUG >= 4
                    fprintf(stderr, "Received a fragment. Now %i in buffer.\n", (int)Flash_FragBuffer.size());
                    #endif
                  }
                  FlashBuf.clear();
                  if (Video_Init_Len > 0) FlashBuf.append(Video_Init_Data, Video_Init_Len);
                  if (Audio_Init_Len > 0) FlashBuf.append(Audio_Init_Data, Audio_Init_Len);
                }
                FlashBuf.append(tag->data,tag->len);
              } else {
                FlashMeta = "";
                FlashMeta.append(tag->data+11,tag->len-15);
                if( !Flash_ManifestSent ) {
                  HTTP_S.Clean();
                  HTTP_S.SetHeader("Content-Type","text/xml");
                  HTTP_S.SetHeader("Cache-Control","no-cache");
                  HTTP_S.SetBody(BuildManifest(FlashMeta, Movie, FlvGetTimestamp(tag)));
                  HTTP_S.SendResponse(CONN_fd, "200", "OK");
                }
              }
            }
            if (handler == HANDLER_PROGRESSIVE){
              if (!progressive_has_sent_header){
                HTTP_S.Clean();//troep opruimen die misschien aanwezig is...
                HTTP_S.SetHeader("Content-Type", "video/x-flv");//FLV files hebben altijd dit content-type.
                //HTTP_S.SetHeader("Transfer-Encoding", "chunked");
                HTTP_S.protocol = "HTTP/1.0";
                HTTP_S.SendResponse(CONN_fd, "200", "OK");//geen SetBody = unknown length! Dat willen we hier.
                //HTTP_S.SendBodyPart(CONN_fd, FLVHeader, 13);//schrijf de FLV header
                DDV_write(FLVHeader, 13, CONN_fd);
                progressive_has_sent_header = true;
              }
              //HTTP_S.SendBodyPart(CONN_fd, tag->data, tag->len);//schrijf deze FLV tag onbewerkt weg
              DDV_write(tag->data, tag->len, CONN_fd);
            }//PROGRESSIVE handler
          }
          break;
      }
    }
  }
  close(CONN_fd);
  if (Video_Init_Data){free(Video_Init_Data);}
  if (Audio_Init_Data){free(Audio_Init_Data);}
  if (inited) close(ss);
  #if DEBUG >= 1
  if (All_Hell_Broke_Loose){fprintf(stderr, "All Hell Broke Loose\n");}
  fprintf(stderr, "User %i disconnected.\n", CONN_fd);
  if (inited){
    fprintf(stderr, "Status was: inited\n");
  }else{
    if (ready4data){
      fprintf(stderr, "Status was: ready4data\n");
    }else{
      fprintf(stderr, "Status was: connected\n");
    }
  }
  #endif
  return 0;
}

