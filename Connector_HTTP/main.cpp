//debugging level 0 = nothing
//debugging level 1 = critical errors
//debugging level 2 = errors
//debugging level 3 = status information
//debugging level 4 = extremely verbose status information
#define DEBUG 3

#include <iostream>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/epoll.h>
#include <getopt.h>

enum {HANDLER_PROGRESSIVE, HANDLER_FLASH, HANDLER_APPLE, HANDLER_MICRO};

#define DEFAULT_PORT 80
#include "../util/server_setup.cpp"
#include "../util/http_parser.cpp"
#include "../MP4/interface.h"

int FlvToFragNum( FLV_Pack * tag ) {
  int Timestamp = (tag->data[7] << 24 ) + (tag->data[4] << 16 ) + (tag->data[5] << 8 ) + (tag->data[6] );
  return (Timestamp / 10000) + 1;
}

std::string BuildManifest( std::string MetaData, std::string MovieId, int CurrentMediaTime ) {
  Interface * temp = new Interface;
  std::string Result="<manifest>\n";
  Result += "  <mimeType>video/mp4</mimeType>\n";
  Result += "  <streamType>live</streamType>\n";
  Result += "  <deliveryType>streaming</deliveryType>\n";
  Result += "  <bootstrapInfo profile=\"named\" id=\"bootstrap1\">\n";
  //JARON: Encode BASE64
  Result += temp->GenerateLiveBootstrap( CurrentMediaTime );
  Result += "  </bootstrapInfo>\n";
  Result += "  <media streamId=\"1\" bootstrapInfoId=\"bootstrap1\" url=\"";
  Result += MovieId;
  Result += "/\">";
  Result += "    <metadata>\n";
  //JARON: Encode BASE64
  Result += MetaData;
  Result += "    </metadata>\n";
  Result += "  </media>\n";
  Result += "</manifest>\n";
  delete temp;
  return Result;
}

int mainHandler(int CONN_fd){
  int handler = HANDLER_PROGRESSIVE;
  bool ready4data = false;//set to true when streaming starts
  bool inited = false;
  bool progressive_has_sent_header = false;
  int ss;
  std::string streamname;
  std::string FlashBuf;
  std::string FlashMeta;
  bool Flash_ManifestSent = false;
  FLV_Pack * tag = 0;
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
  int CurrentFragment = -1;

  while (!socketError && !All_Hell_Broke_Loose){
    //only parse input if available or not yet init'ed
    retval = epoll_wait(poller, events, 1, 1);
    if ((retval > 0) || !ready4data){
      if (HTTP_R.ReadSocket(CONN_fd)){
        if( (HTTP_R.url.find("Seg") != std::string::npos) &&
            (HTTP_R.url.find("Frag") != std::string::npos) ) {
          handler = HANDLER_FLASH;
        }
        printf( "Handler: %d\n", handler );
        if(handler == HANDLER_FLASH) {
          Movie = HTTP_R.url.substr(1);
          Movie = Movie.substr(0,Movie.find("/"));
          Quality = HTTP_R.url.substr( HTTP_R.url.find("/",1)+1 );
          Quality = Quality.substr(0, Quality.find("Seg"));
          temp = HTTP_R.url.find("Seg") + 3;
          Segment = atoi( HTTP_R.url.substr(temp,HTTP_R.url.find("-",temp)-temp).c_str());
          temp = HTTP_R.url.find("Frag") + 4;
          ReqFragment = atoi( HTTP_R.url.substr(temp).c_str() );

          printf( "URL: %s\n", HTTP_R.url.c_str());
          printf( "Movie Identifier: %s\n", Movie.c_str() );
          printf( "Quality Modifier: %s\n", Quality.c_str() );
          printf( "Segment: %d\n", Segment );
          printf( "Fragment: %d\n", ReqFragment );
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
        //ERIK: we hebben nu een hele HTTP request geparsed - verwerken mag hier, door aanroepen naar
        //ERIK: bijvoorbeeld HTTP_R.GetHeader("headernaam") (voor headers) of HTTP_R.GetVar("varnaam") (voor GET/POST vars)
        //ERIK: of HTTP_R.method of HTTP_R.url of HTTP_R.protocol....
        //ERIK: zie ook ../util/http_parser.cpp - de class definitie bovenaan zou genoeg moeten zijn voor je

        //ERIK: Ik heb een handler variabele gemaakt die moet setten naar bijv HANDLER_FLASH in jouw geval.
        //ERIK: Als de handler niet is geset, is hij by default PROGRESSIVE, en daarvoor heb ik de verwerking al gecode.
        //ERIK: Je eigen handler instellen voorkomt dus dat mijn code hem handled alsof hij progressive is.
        if (handler == HANDLER_PROGRESSIVE){
          //in het geval progressive nemen we aan dat de URL de streamname is, met .flv erachter
          streamname = HTTP_R.url.substr(0, HTTP_R.url.size()-4);//strip de .flv
          for (std::string::iterator i=streamname.end()-1; i>=streamname.begin(); --i){
            if (!isalpha(*i) && !isdigit(*i)){streamname.erase(i);}else{*i=tolower(*i);}//strip nonalphanumeric
          }
          streamname = "/tmp/shared_socket_" + streamname;//dit is dan onze shared_socket
          //normaal zouden we ook een position uitlezen uit de URL, maar bij LIVE streams is dat zinloos
  printf( "Streamname: %s\n", streamname.c_str() );
          ready4data = true;
        }//PROGRESSIVE handler
        HTTP_R.Clean(); //maak schoon na verwerken voor eventuele volgende requests...
      }
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
          if (FLV_GetPacket(tag, ss)){//able to read a full packet?
            //ERIK: "tag" bevat nu een FLV tag (video, audio, of metadata), de header hebben we al weggelezen, np.
            //ERIK: Dit is het punt waarop je eventueel data mag/kan gaan sturen en/of parsen. Leef je uit.
            //ERIK: je kan een HTTP_S gebruiken om je HTTP request op te bouwen (via SetBody, SetHeader, etc)
            //ERIK: en dan met de .BuildResponse("200", "OK"); call een std::string met de hele response maken, klaar voor versturen
            //ERIK: Note: hergebruik echter NIET de HTTP_R die ik al heb gemaakt hierboven, want er kunnen meerdere requests binnenkomen!
            if (handler == HANDLER_FLASH){
              if(tag->data[0] != 0x12 ) {
                FlashBuf.append(tag->data,tag->len);
              } else {
                FlashMeta = "";
                FlashMeta.append(tag->data,tag->len);
                if( !Flash_ManifestSent ) {
                  HTTP_S.Clean();
                  HTTP_S.SetHeader("Content-Type","text/xml");
                  HTTP_S.SetHeader("Cache-Control","no-cache");
//                  HTTP_S.SetBody
                }
              }
            }
            if (handler == HANDLER_PROGRESSIVE){
              if (!progressive_has_sent_header){
                HTTP_S.Clean();//troep opruimen die misschien aanwezig is...
                HTTP_S.SetHeader("Content-Type", "video/x-flv");//FLV files hebben altijd dit content-type.
                std::string tmpresp = HTTP_S.BuildResponse("200", "OK");//geen SetBody = unknown length! Dat willen we hier.
                DDV_write(tmpresp.c_str(), tmpresp.size(), CONN_fd);//schrijf de HTTP response header
                DDV_write(FLVHeader, 13, CONN_fd);//schrijf de FLV header, altijd 13 chars lang
                progressive_has_sent_header = true;
              }
              DDV_write(tag->data, tag->len, CONN_fd);//schrijf deze FLV tag onbewerkt weg
            }//PROGRESSIVE handler
          }
          break;
      }
    }
  }
  close(CONN_fd);
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

