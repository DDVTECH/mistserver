#include "websocket.h"
#include "defines.h"
#include "encode.h"
#include "bitfields.h"
#include "timing.h"
#ifdef SSL
#include "mbedtls/sha1.h"
#endif

namespace HTTP{

  Websocket::Websocket(Socket::Connection &c, HTTP::Parser &h) : C(c), H(h){
    frameType = 0;
    if (H.GetHeader("Connection").find("Upgrade") == std::string::npos){
      FAIL_MSG("Could not negotiate websocket, connection header incorrect (%s).",
               H.GetHeader("Connection").c_str());
      C.close();
      return;
    }
    if (H.GetHeader("Upgrade") != "websocket"){
      FAIL_MSG("Could not negotiate websocket, upgrade header incorrect (%s).",
               H.GetHeader("Upgrade").c_str());
      C.close();
      return;
    }
    if (H.GetHeader("Sec-WebSocket-Version") != "13"){
      FAIL_MSG("Could not negotiate websocket, version incorrect (%s).",
               H.GetHeader("Sec-WebSocket-Version").c_str());
      C.close();
      return;
    }
    std::string client_key = H.GetHeader("Sec-WebSocket-Key");
    if (!client_key.size()){
      FAIL_MSG("Could not negotiate websocket, missing key!");
      C.close();
      return;
    }
    client_key += "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

    H.Clean();
    H.setCORSHeaders();
    H.SetHeader("Upgrade", "websocket");
    H.SetHeader("Connection", "Upgrade");
#ifdef SSL
    mbedtls_sha1_context ctx;
    unsigned char outdata[20];
    mbedtls_sha1_starts(&ctx);
    mbedtls_sha1_update(&ctx, (const unsigned char*)client_key.data(), client_key.size());
    mbedtls_sha1_finish(&ctx, outdata);
    H.SetHeader("Sec-WebSocket-Accept", Encodings::Base64::encode(std::string((const char*)outdata, 20)));
#endif
    //H.SetHeader("Sec-WebSocket-Protocol", "json");
    H.SendResponse("101", "Websocket away!", C);
  }

  /// Loops calling readFrame until the connection is closed, sleeping in between reads if needed.
  bool Websocket::readLoop(){
    while (C){
      if (readFrame()){
        return true;
      }
      Util::sleep(500);
    }
    return false;
  }

  /// Loops reading from the socket until either there is no more data ready or a whole frame was read.
  bool Websocket::readFrame(){
    while(true){ 
      //Check if we can receive the minimum frame size (2 header bytes, 0 payload)
      if (!C.Received().available(2)){
        if (C.spool()){continue;}
        return false;
      }
      std::string head = C.Received().copy(2);
      //Read masked bit and payload length
      bool masked = head[1] & 0x80;
      uint64_t payLen = head[1] & 0x7F;
      uint32_t headSize = 2 + (masked?4:0) + (payLen==126?2:0) + (payLen==127?8:0);
      if (headSize > 2){
        //Check if we can receive the whole header
        if (!C.Received().available(headSize)){
          if (C.spool()){continue;}
          return false;
        }
        //Read entire header, re-read real payload length
        head = C.Received().copy(headSize);
        if (payLen == 126){
          payLen = Bit::btohs(head.data()+2);
        }else if (payLen == 127){
          payLen = Bit::btohll(head.data()+2);
        }
      }
      //Check if we can receive the whole frame (header + payload)
      if (!C.Received().available(headSize + payLen)){
        if (C.spool()){continue;}
        return false;
      }
      C.Received().remove(headSize);//delete the header
      std::string pl = C.Received().remove(payLen);
      if (masked){
        //If masked, apply the mask to the payload
        const char * mask = head.data() + headSize - 4;//mask is last 4 bytes of header
        for (uint32_t i = 0; i < payLen; ++i){
          pl[i] ^= mask[i % 4];
        }
      }
      if ((head[0] & 0xF)){
        //Non-continuation
        frameType = (head[0] & 0xF);
        data.assign(pl.data(), pl.size());
      }else{
        //Continuation
        data.append(pl.data(), pl.size());
      }
      if (head[0] & 0x80){
        //FIN
        switch (frameType){
          case 0x0://Continuation, should not happen
            WARN_MSG("Received unknown websocket frame - ignoring");
            break;
          case 0x8://Connection close
            HIGH_MSG("Websocket close received");
            C.close();
            break;
          case 0x9://Ping
            HIGH_MSG("Websocket ping received");
            sendFrame(data, data.size(), 0xA);//send pong
            break;
          case 0xA://Pong
            HIGH_MSG("Websocket pong received");
            break;
        }
        return true;
      }
    }
  }
  
  void Websocket::sendFrame(const char * data, unsigned int len, unsigned int frameType){
    char header[10];
    header[0] = 0x80 + frameType;//FIN + frameType
    if (len < 126){
      header[1] = len;
      C.SendNow(header, 2);
    }else{
      if (len <= 0xFFFF){
        header[1] = 126;
        Bit::htobs(header+2, len);
        C.SendNow(header, 4);
      }else{
        header[1] = 127;
        Bit::htobll(header+2, len);
        C.SendNow(header, 10);
      }
    }
    C.SendNow(data, len);
  }
  
  void Websocket::sendFrame(const std::string & data){
    sendFrame(data.data(), data.size());
  }

  Websocket::operator bool() const{return C;}

}// namespace HTTP

