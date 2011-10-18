#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <vector>
#include <map>
#include <set>
#include <cstdlib>
#include <queue>
#include <cmath>
#include <ctime>
#include <cstdio>
#include <climits>
#include <cstring>
#include <unistd.h>
#include <getopt.h>
#include <set>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <signal.h>
#include "../util/socket.h"
#include "../util/http_parser.h"
#include "../util/md5.h"
#include "../util/json/json.h"
#include "../util/util.h"

#include <openssl/rsa.h>
#include <openssl/x509.h>

#define UPLINK_INTERVAL 30

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

Socket::Server API_Socket; ///< Main connection socket.

/// Basic signal handler. Disconnects the server_socket if it receives
/// a SIGINT, SIGHUP or SIGTERM signal, but does nothing for SIGPIPE.
/// Disconnecting the server_socket will terminate the main listening loop
/// and cleanly shut down the process.
void signal_handler (int signum){
  switch (signum){
    case SIGINT:
      #if DEBUG >= 1
      fprintf(stderr, "Received SIGINT - closing server socket.\n");
      #endif
      break;
    case SIGHUP:
      #if DEBUG >= 1
      fprintf(stderr, "Received SIGHUP - closing server socket.\n");
      #endif
      break;
    case SIGTERM:
      #if DEBUG >= 1
      fprintf(stderr, "Received SIGTERM - closing server socket.\n");
      #endif
      break;
    default: return; break;
  }
  API_Socket.close();
}//signal_handler


/// Needed for base64_encode function
static const std::string base64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/// Helper for base64_decode function
static inline bool is_base64(unsigned char c) {
  return (isalnum(c) || (c == '+') || (c == '/'));
}

/// Used to base64 encode data. Input is the plaintext as std::string, output is the encoded data as std::string.
/// \param input Plaintext data to encode.
/// \returns Base64 encoded data.
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

/// Used to base64 decode data. Input is the encoded data as std::string, output is the plaintext data as std::string.
/// \param input Base64 encoded data to decode.
/// \returns Plaintext decoded data.
std::string base64_decode(std::string const& encoded_string) {
  int in_len = encoded_string.size();
  int i = 0;
  int j = 0;
  int in_ = 0;
  unsigned char char_array_4[4], char_array_3[3];
  std::string ret;
  while (in_len-- && ( encoded_string[in_] != '=') && is_base64(encoded_string[in_])) {
    char_array_4[i++] = encoded_string[in_]; in_++;
    if (i ==4) {
      for (i = 0; i <4; i++){char_array_4[i] = base64_chars.find(char_array_4[i]);}
      char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
      char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
      char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];
      for (i = 0; (i < 3); i++){ret += char_array_3[i];}
      i = 0;
    }
  }
  if (i) {
    for (j = i; j <4; j++){char_array_4[j] = 0;}
    for (j = 0; j <4; j++){char_array_4[j] = base64_chars.find(char_array_4[j]);}
    char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
    char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
    char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];
    for (j = 0; (j < i - 1); j++) ret += char_array_3[j];
  }
  return ret;
}

unsigned char __gbv2keypub_der[] = {
  0x30, 0x82, 0x01, 0x22, 0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86,
  0xf7, 0x0d, 0x01, 0x01, 0x01, 0x05, 0x00, 0x03, 0x82, 0x01, 0x0f, 0x00,
  0x30, 0x82, 0x01, 0x0a, 0x02, 0x82, 0x01, 0x01, 0x00, 0xe5, 0xd7, 0x9c,
  0x7d, 0x73, 0xc6, 0xe6, 0xfb, 0x35, 0x7e, 0xd7, 0x57, 0x99, 0x07, 0xdb,
  0x99, 0x70, 0xc9, 0xd0, 0x3e, 0x53, 0x57, 0x3c, 0x1e, 0x55, 0xda, 0x0f,
  0x69, 0xbf, 0x26, 0x79, 0xc7, 0xb6, 0xdd, 0x8e, 0x83, 0x32, 0x65, 0x74,
  0x0d, 0x74, 0x48, 0x42, 0x49, 0x22, 0x52, 0x58, 0x56, 0xc3, 0xe4, 0x49,
  0x5d, 0xac, 0x6a, 0x94, 0xb1, 0x64, 0x14, 0xbf, 0x4d, 0xd5, 0xd7, 0x3a,
  0xca, 0x5c, 0x1e, 0x6f, 0x42, 0x30, 0xac, 0x29, 0xaa, 0xa0, 0x85, 0xd2,
  0x16, 0xa2, 0x8e, 0x89, 0x12, 0xc4, 0x92, 0x06, 0xea, 0xed, 0x48, 0xf6,
  0xdb, 0xed, 0x4f, 0x62, 0x6c, 0xfa, 0xcf, 0xc2, 0xb9, 0x8d, 0x04, 0xb2,
  0xba, 0x63, 0xc9, 0xcc, 0xee, 0x23, 0x64, 0x46, 0x14, 0x12, 0xc8, 0x38,
  0x67, 0x69, 0x6b, 0xaf, 0xd1, 0x7c, 0xb1, 0xb5, 0x79, 0xe4, 0x4e, 0x3a,
  0xa7, 0xe8, 0x28, 0x89, 0x25, 0xc0, 0xd0, 0xd8, 0xc7, 0xd2, 0x26, 0xaa,
  0xf5, 0xbf, 0x36, 0x55, 0x01, 0x89, 0x58, 0x1f, 0x1e, 0xf5, 0xa5, 0x42,
  0x8f, 0x60, 0x2e, 0xc2, 0xd8, 0x21, 0x0b, 0x6c, 0x8d, 0xbb, 0x72, 0xf2,
  0x19, 0x30, 0xe3, 0x4c, 0x3e, 0x80, 0xe7, 0xf2, 0xe3, 0x89, 0x4f, 0xd4,
  0xee, 0x96, 0x3e, 0x4a, 0x9b, 0xe5, 0x16, 0x01, 0xf1, 0x98, 0xc9, 0x0b,
  0xd6, 0xdf, 0x8a, 0x64, 0x47, 0xc4, 0x44, 0xcc, 0x92, 0x69, 0x28, 0xee,
  0x7d, 0xac, 0xdc, 0x30, 0x56, 0x3a, 0xe7, 0xbc, 0xba, 0x45, 0x16, 0x2c,
  0x4c, 0x46, 0x6b, 0x2b, 0x20, 0xfb, 0x3d, 0x20, 0x35, 0xbb, 0x48, 0x49,
  0x13, 0x65, 0xc9, 0x9a, 0x38, 0x10, 0x84, 0x1a, 0x8c, 0xc9, 0xd7, 0xde,
  0x07, 0x10, 0x5a, 0xfb, 0xb4, 0x95, 0xae, 0x18, 0xf2, 0xe3, 0x15, 0xe8,
  0xad, 0x7e, 0xe5, 0x3c, 0xa8, 0x47, 0x85, 0xd6, 0x1f, 0x54, 0xb5, 0xa3,
  0x79, 0x02, 0x03, 0x01, 0x00, 0x01
}; ///< The GBv2 public key file.
unsigned int __gbv2keypub_der_len = 294; ///< Length of GBv2 public key data

RSA * pubkey = 0; ///< Holds the public key.
/// Attempts to load the GBv2 public key.
void RSA_Load(){
  const unsigned char * key = __gbv2keypub_der;
  pubkey = d2i_RSAPublicKey(0, &key, __gbv2keypub_der_len);
}

/// Attempts to verify RSA signature using the public key loaded with RSA_Load().
/// Assumes basesign argument is base64 encoded RSA signature for data.
/// Returns true if the data could be verified, false otherwise.
bool RSA_check(std::string & data, std::string basesign){
  std::string sign = base64_decode(basesign);
  return (RSA_verify(NID_md5, (unsigned char*)data.c_str(), data.size(), (unsigned char*)sign.c_str(), sign.size(), pubkey) == 1);
}

Json::Value Storage = Json::Value(Json::objectValue); ///< Global storage of data.

void WriteFile( std::string Filename, std::string contents ) {
  std::ofstream File;
  File.open( Filename.c_str( ) );
  File << contents << std::endl;
  File.close( );
}

std::string ReadFile( std::string Filename ) {
  std::string Result;
  std::ifstream File;
  File.open( Filename.c_str( ) );
  while( File.good( ) ) { Result += File.get( ); }
  File.close( );
  return Result;
}

class ConnectedUser{
  public:
    std::string writebuffer;
    Socket::Connection C;
    HTTP::Parser H;
    bool Authorized;
    bool clientMode;
    int logins;
    std::string Username;
    ConnectedUser(Socket::Connection c){
      C = c;
      H.Clean();
      logins = 0;
      Authorized = false;
      clientMode = false;
    }
};

void Log(std::string kind, std::string message){
  Json::Value m;
  m.append((Json::Value::UInt)time(0));
  m.append(kind);
  m.append(message);
  Storage["log"].append(m);
  std::cout << "[" << kind << "] " << message << std::endl;
}

void Authorize( Json::Value & Request, Json::Value & Response, ConnectedUser & conn ) {
  time_t Time = time(0);
  tm * TimeInfo = localtime(&Time);
  std::stringstream Date;
  std::string retval;
  Date << TimeInfo->tm_mday << "-" << TimeInfo->tm_mon << "-" << TimeInfo->tm_year + 1900;
  std::string Challenge = md5( Date.str().c_str() + conn.C.getHost() );
  if( Request.isMember( "authorize" ) ) {
    std::string UserID = Request["authorize"]["username"].asString();
    if (Storage["account"].isMember(UserID)){
      if( md5( Storage["account"][UserID]["password"].asString() + Challenge ) == Request["authorize"]["password"].asString() ) {
        Response["authorize"]["status"] = "OK";
        conn.Username = UserID;
        conn.Authorized = true;
        return;
      }
    }
    if (Storage["authorize"].isMember("key")){
      UserID = "gearbox";
      if (RSA_check(Challenge, Storage["authorize"]["key"].asString())){
        Response["authorize"]["status"] = "OK";
        conn.Username = UserID;
        conn.Authorized = true;
        return;
      }
    }
    if (UserID != ""){
      Log("AUTH", "Failed login attempt "+UserID+" @ "+conn.C.getHost());
    }
    conn.logins++;
  }
  conn.Username = "";
  conn.Authorized = false;
  Response["authorize"]["status"] = "CHALL";
  Response["authorize"]["challenge"] = Challenge;
  return;
}

void CheckProtocols(Json::Value & p){
  static std::map<std::string, std::string> connports;
  bool seenHTTP = false;
  bool seenRTMP = false;
  std::string tmp;
  Util::Procs::Stop("RTMP");
  for (Json::ValueIterator jit = p.begin(); jit != p.end(); jit++){
    if (jit.memberName() == std::string("HTTP")){
      tmp = p[jit.memberName()]["port"].asString();
      seenHTTP = true;
      if (connports["HTTP"] != tmp){Util::Procs::Stop("HTTP");}
      connports["HTTP"] = tmp;
      if (!Util::Procs::isActive("HTTP")){
        Util::Procs::Start("HTTP", std::string("DDV_Conn_HTTP -p ")+tmp);
      }
    }
    if (jit.memberName() == std::string("RTMP")){
      tmp = p[jit.memberName()]["port"].asString();
      seenRTMP = true;
      if (connports["RTMP"] != tmp){Util::Procs::Stop("RTMP");}
      connports["RTMP"] = tmp;
      if (!Util::Procs::isActive("RTMP")){
        Util::Procs::Start("RTMP", std::string("DDV_Conn_RTMP -p ")+tmp);
      }
    }
  }
  if (!seenHTTP){Util::Procs::Stop("HTTP");}
  if (!seenRTMP){Util::Procs::Stop("RTMP");}
}

void CheckConfig(Json::Value & in, Json::Value & out){
  if (in.isObject() && (in.size() > 0)){
    for (Json::ValueIterator jit = in.begin(); jit != in.end(); jit++){
      if (out.isObject() && out.isMember(jit.memberName())){
        if (in[jit.memberName()] != out[jit.memberName()]){
          Log("CONF", std::string("Updated configuration value ")+jit.memberName());
        }
      }else{
        Log("CONF", std::string("New configuration value ")+jit.memberName());
      }
    }
    if (out.isObject() && (out.size() > 0)){
      for (Json::ValueIterator jit = out.begin(); jit != out.end(); jit++){
        if (!in.isMember(jit.memberName())){
          Log("CONF", std::string("Deleted configuration value ")+jit.memberName());
        }
      }
    }
  }
  out = in;
  out["version"] = TOSTRING(VERSION);
}

void startStream(std::string name, Json::Value & data){
  Log("BUFF", "(re)starting stream buffer "+name);
  std::string URL = data["channel"]["URL"].asString();
  std::string preset = data["preset"]["cmd"].asString();
  std::string cmd1, cmd2;
  if (URL.substr(0, 4) == "push"){
    std::string pusher = URL.substr(7);
    cmd2 = "DDV_Buffer 500 "+name+" "+pusher;
    Util::Procs::Start(name, cmd2);
  }else{
    cmd1 = "ffmpeg -re -async 2 -i "+URL+" "+preset+" -f flv -";
    cmd2 = "DDV_Buffer 500 "+name;
    Util::Procs::Start(name, cmd1, cmd2);
  }
}

void CheckAllStreams(Json::Value & data){
  for (Json::ValueIterator jit = data.begin(); jit != data.end(); jit++){
    if (!Util::Procs::isActive(jit.memberName())){
      startStream(jit.memberName(), data[jit.memberName()]);
    }
  }
}

void CheckStreams(Json::Value & in, Json::Value & out){
  if (in.isObject() && (in.size() > 0)){
    for (Json::ValueIterator jit = in.begin(); jit != in.end(); jit++){
      if (out.isObject() && out.isMember(jit.memberName())){
        if (in[jit.memberName()] != out[jit.memberName()]){
          Log("STRM", std::string("Updated stream ")+jit.memberName());
          Util::Procs::Stop(jit.memberName());
          startStream(jit.memberName(), in[jit.memberName()]);
        }
      }else{
        Log("STRM", std::string("New stream ")+jit.memberName());
        startStream(jit.memberName(), in[jit.memberName()]);
      }
    }
  }
  if (out.isObject() && (out.size() > 0)){
    for (Json::ValueIterator jit = out.begin(); jit != out.end(); jit++){
      if (!in.isMember(jit.memberName())){
        Log("STRM", std::string("Deleted stream ")+jit.memberName());
        Util::Procs::Stop(jit.memberName());
      }
    }
  }
  out = in;
}

int main(int argc, char ** argv){
  //setup signal handler
  struct sigaction new_action;
  new_action.sa_handler = signal_handler;
  sigemptyset (&new_action.sa_mask);
  new_action.sa_flags = 0;
  sigaction(SIGINT, &new_action, NULL);
  sigaction(SIGHUP, &new_action, NULL);
  sigaction(SIGTERM, &new_action, NULL);
  sigaction(SIGPIPE, &new_action, NULL);

  RSA_Load(); // Load GearBox public key
  Util::Config C;
  C.confsection = "API";
  C.parseArgs(argc, argv);
  C.parseFile();
  time_t lastuplink = 0;
  time_t processchecker = 0;
  API_Socket = Socket::Server(C.listen_port, C.interface, true);
  Socket::Server Stats_Socket = Socket::Server("/tmp/ddv_statistics", true);
  Util::setUser(C.username);
  if (C.daemon_mode){
    Util::Daemonize();
  }
  Socket::Connection Incoming;
  std::vector< ConnectedUser > users;
  std::vector<Socket::Connection> buffers;
  Json::Value Request = Json::Value(Json::objectValue);
  Json::Value Response = Json::Value(Json::objectValue);
  Json::Reader JsonParse;
  std::string jsonp;
  ConnectedUser * uplink = 0;
  JsonParse.parse(ReadFile("config.json"), Storage, false);
  if (!Storage.isMember("config")){Storage["config"] = Json::Value(Json::objectValue);}
  if (!Storage.isMember("log")){Storage["log"] = Json::Value(Json::arrayValue);}
  if (!Storage.isMember("statistics")){Storage["statistics"] = Json::Value(Json::objectValue);}
  while (API_Socket.connected()){
    usleep(100000); //sleep for 100 ms - prevents 100% CPU time

    if (time(0) - processchecker > 10){
      processchecker = time(0);
      CheckProtocols(Storage["config"]["protocols"]);
      CheckAllStreams(Storage["streams"]);
    }
    
    if (time(0) - lastuplink > UPLINK_INTERVAL){
      lastuplink = time(0);
      bool gotUplink = false;
      if (users.size() > 0){
        for( std::vector< ConnectedUser >::iterator it = users.end() - 1; it >= users.begin(); it--) {
          if (!it->C.connected()){
            it->C.close();
            users.erase(it);
            break;
          }
          if (it->clientMode){uplink = &*it; gotUplink = true;}
        }
      }
      if (!gotUplink){
        Incoming = Socket::Connection("gearbox.ddvtech.com", 4242, true);
        if (Incoming.connected()){
          users.push_back(Incoming);
          users.back().clientMode = true;
          uplink = &users.back();
          gotUplink = true;
        }
      }
      if (gotUplink){
        Response.clear(); //make sure no data leaks from previous requests
        Response["config"] = Storage["config"];
        Response["streams"] = Storage["streams"];
        Response["log"] = Storage["log"];
        Response["statistics"] = Storage["statistics"];
        uplink->H.Clean();
        uplink->H.SetBody("command="+HTTP::Parser::urlencode(Response.toStyledString()));
        uplink->H.BuildRequest();
        uplink->writebuffer += uplink->H.BuildResponse("200", "OK");
        uplink->H.Clean();
        //Log("UPLK", "Sending server data to uplink.");
      }else{
        Log("UPLK", "Could not connect to uplink.");
      }
    }
    
    Incoming = API_Socket.accept();
    if (Incoming.connected()){users.push_back(Incoming);}
    Incoming = Stats_Socket.accept();
    if (Incoming.connected()){buffers.push_back(Incoming);}
    if (buffers.size() > 0){
      for( std::vector< Socket::Connection >::iterator it = buffers.end() - 1; it >= buffers.begin(); it--) {
        if (!it->connected()){
          it->close();
          buffers.erase(it);
          break;
        }
        
      }
    }
    if (users.size() > 0){
      for( std::vector< ConnectedUser >::iterator it = users.end() - 1; it >= users.begin(); it--) {
        if (!it->C.connected() || it->logins > 3){
          it->C.close();
          users.erase(it);
          break;
        }
        if (it->writebuffer != ""){
          it->C.iwrite(it->writebuffer);
        }
        if (it->H.Read(it->C)){
          Response.clear(); //make sure no data leaks from previous requests
          if (it->clientMode){
            // In clientMode, requests are reversed. These are connections we initiated to GearBox.
            // They are assumed to be authorized, but authorization to gearbox is still done.
            // This authorization uses the compiled-in username and password (account).
            if (!JsonParse.parse(it->H.body, Request, false)){
              Log("HTTP", "Failed to parse body JSON: "+it->H.body);
              Response["authorize"]["status"] = "INVALID";
            }else{
              if (Request["authorize"]["status"] != "OK"){
                if (Request["authorize"].isMember("challenge")){
                  it->logins++;
                  if (it->logins > 2){
                    Log("UPLK", "Max login attempts passed - dropping connection to uplink.");
                    it->C.close();
                  }else{
                    Response["config"] = Storage["config"];
                    Response["streams"] = Storage["streams"];
                    Response["log"] = Storage["log"];
                    Response["statistics"] = Storage["statistics"];
                    Response["authorize"]["username"] = TOSTRING(COMPILED_USERNAME);
                    Response["authorize"]["password"] = md5(TOSTRING(COMPILED_PASSWORD) + Request["authorize"]["challenge"].asString());
                    it->H.Clean();
                    it->H.SetBody("command="+HTTP::Parser::urlencode(Response.toStyledString()));
                    it->H.BuildRequest();
                    it->writebuffer += it->H.BuildResponse("200", "OK");
                    it->H.Clean();
                    Log("UPLK", "Attempting login to uplink.");
                  }
                }
              }else{
                if (Request.isMember("config")){CheckConfig(Request["config"], Storage["config"]);}
                if (Request.isMember("streams")){CheckStreams(Request["streams"], Storage["streams"]);}
                if (Request.isMember("clearstatlogs")){
                  Storage["log"].clear();
                  Storage["statistics"].clear();
                }
                //Log("UPLK", "Received data from uplink.");
                //WriteFile("config.json", Storage.toStyledString());
              }
            }
          }else{
            if (!JsonParse.parse(it->H.GetVar("command"), Request, false)){
              Log("HTTP", "Failed to parse command JSON: "+it->H.GetVar("command"));
              Response["authorize"]["status"] = "INVALID";
            }else{
              std::cout << "Request: " << Request.toStyledString() << std::endl;
              Authorize(Request, Response, (*it));
              if (it->Authorized){
                //Parse config and streams from the request.
                if (Request.isMember("config")){CheckConfig(Request["config"], Storage["config"]);}
                if (Request.isMember("streams")){CheckStreams(Request["streams"], Storage["streams"]);}
                //sent current configuration, no matter if it was changed or not
                //Response["streams"] = Storage["streams"];
                Response["config"] = Storage["config"];
                Response["streams"] = Storage["streams"];
                //add required data to the current unix time to the config, for syncing reasons
                Response["config"]["time"] = (Json::Value::UInt)time(0);
                if (!Response["config"].isMember("serverid")){Response["config"]["serverid"] = "";}
                //sent any available logs and statistics
                Response["log"] = Storage["log"];
                Response["statistics"] = Storage["statistics"];
                //clear log and statistics to prevent useless data transfer
                Storage["log"].clear();
                Storage["statistics"].clear();
              }
            }
            jsonp = "";
            if (it->H.GetVar("callback") != ""){jsonp = it->H.GetVar("callback");}
            if (it->H.GetVar("jsonp") != ""){jsonp = it->H.GetVar("jsonp");}
            it->H.Clean();
            it->H.protocol = "HTTP/1.0";
            it->H.SetHeader("Content-Type", "text/javascript");
            if (jsonp == ""){
              it->H.SetBody(Response.toStyledString()+"\n\n");
            }else{
              it->H.SetBody(jsonp+"("+Response.toStyledString()+");\n\n");
            }
            it->writebuffer += it->H.BuildResponse("200", "OK");
            it->H.Clean();
          }
        }
      }
    }
  }
  Util::Procs::StopAll();
  WriteFile("config.json", Storage.toStyledString());
  std::cout << "Killed all processes, wrote config to disk. Exiting." << std::endl;
  return 0;
}
