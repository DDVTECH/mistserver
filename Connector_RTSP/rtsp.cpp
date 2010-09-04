#include <stdlib.h>
#include <string>
#include <map>

int clientport = 0;
int serverport = 6256;


class RTSPPack{
  public:
    std::map<std::string, std::string> params;
    std::string data;
};//RTSPPack


RTSPPack GetRTSPPacket(){
  std::string tmp, a, b;
  RTSPPack ret;
  bool first = true;
  std::size_t f;
  do {
    std::getline(std::cin, tmp);
    if (first){
      f = tmp.find_first_of(' ');
      if (f != std::string::npos){
        a = tmp.substr(0, f);
        tmp = tmp.substr(f+1);
        f = tmp.find('\r');
        if (f != std::string::npos){tmp.erase(f);}
        ret.params["request"] = a;
        DEBUG("Request type %s\n", a.c_str());
        f = tmp.find_first_of(' ');
        if (f != std::string::npos){
          a = tmp.substr(0, f);
          ret.params["url"] = a;
          DEBUG("URL=%s\n", a.c_str());
        }
      }
      first = false;
    }else{
      f = tmp.find_first_of(':');
      if (f != std::string::npos){
        a = tmp.substr(0, f);
        b = tmp.substr(f+2);
        f = b.find('\r');
        if (f != std::string::npos){b.erase(f);}
        ret.params[a] = b;
        DEBUG("Adding '%s' as '%s'\n", a.c_str(), b.c_str());
      }
    }
  }while(tmp.length() > 2);
  DEBUG("End of request headers\n");
  int len = 0;
  if (ret.params["Content-Length"] != ""){
    len = atoi(ret.params["Content-Length"].c_str());
  }

  if (len > 0){
    DEBUG("There is a length %i request body:\n", len);
    char * data = (char*)malloc(len);
    fread(&data, 1, len, stdin);
    ret.data = data;
    free(data);
    DEBUG("%s\nEnd of body data.\n", ret.data.c_str());
  }else{
    DEBUG("No request body.\n");
  }
  return ret;
}//GetRTSPPacket

void RTSPReply(RTSPPack & q, RTSPPack & a, std::string retval){
  if (q.params["CSeq"] != ""){a.params["CSeq"] = q.params["CSeq"];}
  if (q.params["Require"] != ""){a.params["Unsupported"] = q.params["Require"];}
  if (a.data != ""){
    char * len = (char*)malloc(10);
    sprintf(len, "%i", (int)a.data.length());
    a.params["Content-Length"] = len;
    free(len);
  }

  std::cout << "RTSP/1.0 " << retval << "\r\n";

  std::map<std::string,std::string>::iterator it;
  for (it = a.params.begin(); it != a.params.end(); it++){
    std::cout << (*it).first << ": " << (*it).second << "\r\n";
  }
  std::cout << "\r\n" << a.data;
  std::cout.flush();
}//RTSPReply

void ParseRTSPPacket(){
  RTSPPack q = GetRTSPPacket();
  RTSPPack a;
  
  //parse OPTIONS request
  if (q.params["request"] == "OPTIONS"){
    a.params["Public"] = "SETUP, TEARDOWN, OPTIONS, DESCRIBE, PLAY";
    RTSPReply(q, a, "200 OK");
    DEBUG("Sent OPTIONS reply\n");
  }
  
  //parse DESCRIBE request
  if (q.params["request"] == "DESCRIBE"){
    a.params["Content-Type"] = "application/sdp";
    a.data = "v=0\r\no=- 0 0 IN IP4 ddvtech.com\r\ns=PLSServer\r\nc=IN IP4 0.0.0.0\r\na=recvonly\r\nt=0 0\r\nm=video 0 RTP/AVP 98\r\na=rtpmap:98 H264/90000\r\na=fmtp:98 packetization-mode=1\r\nm=audio 0 RTP/AVP 99\r\na=rtpmap:99 MPEG4-GENERIC/11025/1\r\na=ftmp:99 profile-level-id=1;mode=AAC-hbr;sizelength=13;indexlength=3;indexltalength=3; config=1210\r\n";
    RTSPReply(q, a, "200 OK");
    DEBUG("Sent DESCRIBE reply\n");
  }

  //parse SETUP request
  if (q.params["request"] == "SETUP"){
    size_t f, g;
    std::string s;
    f = q.params["Transport"].rfind('=')+1;
    g = q.params["Transport"].rfind('-');
    clientport = atoi(q.params["Transport"].substr(f, g-f).c_str());
    DEBUG("Requested client base port: %i\n", clientport);
    char * len = (char*)malloc(30);
    sprintf(len, ";server_port=%i-%i", serverport, serverport+1);
    a.params["Transport"] = len;
    a.params["Transport"] = q.params["Transport"] + a.params["Transport"];
    free(len);
    a.params["Session"] = "puddingbroodjes";
    RTSPReply(q, a, "200 OK");
    DEBUG("Sent SETUP reply\n");
  }

  //parse PLAY request
  if (q.params["request"] == "PLAY"){
    RTSPReply(q, a, "200 OK");
    ready4data = true;
  }

  //parse TEARDOWN request
  if (q.params["request"] == "TEARDOWN"){
    RTSPReply(q, a, "200 OK");
    stopparsing = true;
  }
  

}//ParseRTSPPacket
