#include "bitfields.h"
#include "defines.h"
#include "encode.h"
#include "timing.h"
#include "websocket.h"
#include "downloader.h"
#ifdef SSL
#include "mbedtls/sha1.h"
#endif

// Takes the data from a Sec-WebSocket-Key header, and returns the corresponding data for a Sec-WebSocket-Accept header
static std::string calculateKeyAccept(std::string client_key){
  client_key += "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  mbedtls_sha1_context ctx;
  unsigned char outdata[20];
  mbedtls_sha1_starts(&ctx);
  mbedtls_sha1_update(&ctx, (const unsigned char *)client_key.data(), client_key.size());
  mbedtls_sha1_finish(&ctx, outdata);
  return Encodings::Base64::encode(std::string((const char *)outdata, 20));
}

namespace HTTP{

  /// Uses the referenced Socket::Connection to make use of an already connected Websocket.
  Websocket::Websocket(Socket::Connection &c, bool client) : C(c){
    maskOut = client;
  }

  /// Uses the referenced Socket::Connection to make a new Websocket by connecting to the given URL.
  Websocket::Websocket(Socket::Connection &c, const HTTP::URL & url, std::map<std::string, std::string> * headers) : C(c){
    HTTP::Downloader d;

    //Ensure our passed socket gets used by the downloader class
    d.setSocket(&C);
    
    //Generate a random nonce based on the current process ID
    //Note: This is not cryptographically secure, nor intended to be.
    //It does make it possible to trace which stream came from which PID, if needed.
    char nonce[16];
    unsigned int state = getpid();
    for (size_t i = 0; i < 16; ++i){nonce[i] = rand_r(&state) % 255;}
    std::string handshakeKey = Encodings::Base64::encode(std::string(nonce, 16));

    //Prepare the headers
    d.setHeader("Connection", "Upgrade");
    d.setHeader("Upgrade", "websocket");
    d.setHeader("Sec-WebSocket-Version", "13");
    d.setHeader("Sec-WebSocket-Key", handshakeKey);
    if (headers && headers->size()){
      for (std::map<std::string, std::string>::iterator it = headers->begin(); it != headers->end(); ++it){
        d.setHeader(it->first, it->second);
      }
    }
    if (!d.get(url) || d.getStatusCode() != 101 || !d.getHeader("Sec-WebSocket-Accept").size()){
      FAIL_MSG("Could not connect websocket to %s", url.getUrl().c_str());
      d.getSocket().close();
      C = d.getSocket();
      return;
    }

#ifdef SSL
    std::string handshakeAccept = calculateKeyAccept(handshakeKey);
    if (d.getHeader("Sec-WebSocket-Accept") != handshakeAccept){
      FAIL_MSG("WebSocket handshake failure: expected accept parameter %s but received %s", handshakeAccept.c_str(), d.getHeader("Sec-WebSocket-Accept").c_str());
      d.getSocket().close();
      C = d.getSocket();
      return;
    }
#endif
   
    MEDIUM_MSG("Connected to websocket %s", url.getUrl().c_str());
    maskOut = true;
  }

  /// Takes an incoming HTTP::Parser request for a Websocket, and turns it into one.
  Websocket::Websocket(Socket::Connection &c, const HTTP::Parser &req, HTTP::Parser &resp) : C(c){
    frameType = 0;
    maskOut = false;
    std::string connHeader = req.GetHeader("Connection");
    Util::stringToLower(connHeader);
    if (connHeader.find("upgrade") == std::string::npos){
      FAIL_MSG("Could not negotiate websocket, connection header incorrect (%s).", connHeader.c_str());
      C.close();
      return;
    }
    std::string upgradeHeader = req.GetHeader("Upgrade");
    Util::stringToLower(upgradeHeader);
    if (upgradeHeader != "websocket"){
      FAIL_MSG("Could not negotiate websocket, upgrade header incorrect (%s).", upgradeHeader.c_str());
      C.close();
      return;
    }
    if (req.GetHeader("Sec-WebSocket-Version") != "13"){
      FAIL_MSG("Could not negotiate websocket, version incorrect (%s).",
               req.GetHeader("Sec-WebSocket-Version").c_str());
      C.close();
      return;
    }
#ifdef SSL
    std::string client_key = req.GetHeader("Sec-WebSocket-Key");
    if (!client_key.size()){
      FAIL_MSG("Could not negotiate websocket, missing key!");
      C.close();
      return;
    }
#endif

    resp.SetHeader("Upgrade", "websocket");
    resp.SetHeader("Connection", "Upgrade");
#ifdef SSL
    resp.SetHeader("Sec-WebSocket-Accept", calculateKeyAccept(client_key));
#endif
    // H.SetHeader("Sec-WebSocket-Protocol", "json");
    resp.SendResponse("101", "Websocket away!", C);
  }

  /// Loops calling readFrame until the connection is closed, sleeping in between reads if needed.
  bool Websocket::readLoop(){
    while (C){
      if (readFrame()){return true;}
      Util::sleep(500);
    }
    return false;
  }

  /// Loops reading from the socket until either there is no more data ready or a whole frame was read.
  bool Websocket::readFrame(){
    while (true){
      // Check if we can receive the minimum frame size (2 header bytes, 0 payload)
      if (!C.Received().available(2)){
        if (C.spool(true)){continue;}
        return false;
      }
      std::string head = C.Received().copy(2);
      // Read masked bit and payload length
      bool masked = head[1] & 0x80;
      uint64_t payLen = head[1] & 0x7F;
      uint32_t headSize = 2 + (masked ? 4 : 0) + (payLen == 126 ? 2 : 0) + (payLen == 127 ? 8 : 0);
      if (headSize > 2){
        // Check if we can receive the whole header
        if (!C.Received().available(headSize)){
          if (C.spool(true)){continue;}
          return false;
        }
        // Read entire header, re-read real payload length
        head = C.Received().copy(headSize);
        if (payLen == 126){
          payLen = Bit::btohs(head.data() + 2);
        }else if (payLen == 127){
          payLen = Bit::btohll(head.data() + 2);
        }
      }
      // Check if we can receive the whole frame (header + payload)
      if (!C.Received().available(headSize + payLen)){
        if (C.spool(true)){continue;}
        return false;
      }
      C.Received().remove(headSize); // delete the header
      if ((head[0] & 0xF)){
        // Non-continuation
        frameType = (head[0] & 0xF);
        data.truncate(0);
      }
      size_t preSize = data.size();
      C.Received().remove(data, payLen);
      if (masked){
        // If masked, apply the mask to the payload
        const char *mask = head.data() + headSize - 4; // mask is last 4 bytes of header
        for (uint32_t i = 0; i < payLen; ++i){data[i+preSize] ^= mask[i % 4];}
      }
      if (head[0] & 0x80){
        // FIN
        switch (frameType){
        case 0x0: // Continuation, should not happen
          WARN_MSG("Received unknown websocket frame - ignoring");
          return false;
          break;
        case 0x8: // Connection close
          HIGH_MSG("Websocket close received");
          C.close();
          return false;
          break;
        case 0x9: // Ping
          HIGH_MSG("Websocket ping received");
          sendFrame(data, data.size(), 0xA); // send pong
          return false;
          break;
        case 0xA: // Pong
          HIGH_MSG("Websocket pong received");
          return false;
          break;
        }
        return true;
      }
    }
  }

  void Websocket::sendFrameHead(unsigned int len, unsigned int frameType){
    header[0] = 0x80 + frameType; // FIN + frameType
    headLen = 2;
    if (len < 126){
      header[1] = len;
    }else{
      if (len <= 0xFFFF){
        header[1] = 126;
        Bit::htobs(header + 2, len);
        headLen = 4;
      }else{
        header[1] = 127;
        Bit::htobll(header + 2, len);
        headLen = 10;
      }
    }
    if (maskOut){
      header[1] |= 128;
      header[headLen++] = 0;
      header[headLen++] = 0;
      header[headLen++] = 0;
      header[headLen++] = 0;
    }
    C.SendNow(header, headLen);
    dataCtr = 0;
  }

  void Websocket::sendFrameData(const char *data, unsigned int len){
    C.SendNow(data, len);
    dataCtr += len;
  }

  void Websocket::sendFrame(const char *data, unsigned int len, unsigned int frameType){
    sendFrameHead(len, frameType);
    sendFrameData(data, len);
  }

  void Websocket::sendFrame(const std::string &data){
    sendFrameHead(data.size());
    sendFrameData(data.data(), data.size());
  }

  Websocket::operator bool() const{return C;}

}// namespace HTTP
