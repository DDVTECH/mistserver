/// \file socket.cpp
/// A handy Socket wrapper library.
/// Written by Jaron Vietor in 2010 for DDVTech

#include "defines.h"
#include "socket.h"
#include "timing.h"
#include <cstdlib>
#include <ifaddrs.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sstream>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fstream>
#include <sys/select.h>

#define BUFFER_BLOCKSIZE 4096 // set buffer blocksize to 4KiB

#ifdef __CYGWIN__
#define SOCKETSIZE 8092ul
#else
#define SOCKETSIZE 51200ul
#endif

/// Local-scope only helper function that prints address families
static const char *addrFam(int f){
  switch (f){
  case AF_UNSPEC: return "Unspecified";
  case AF_INET: return "IPv4";
  case AF_INET6: return "IPv6";
  case PF_UNIX: return "Unix";
  default: return "???";
  }
}

/// Calls gai_strerror with the given argument, calling regular strerror on the global errno as needed
static const char *gai_strmagic(int errcode){
  if (errcode == EAI_SYSTEM){
    return strerror(errno);
  }else{
    return gai_strerror(errcode);
  }
}

std::string Socket::sockaddrToString(const sockaddr* A){
  char addressBuffer[INET6_ADDRSTRLEN];
  if (inet_ntop(AF_INET, A, addressBuffer, INET6_ADDRSTRLEN)){
    return addressBuffer;
  }
  return "";
}

static std::string getIPv6BinAddr(const struct sockaddr_in6 &remoteaddr){
  char tmpBuffer[17] = "\000\000\000\000\000\000\000\000\000\000\377\377\000\000\000\000";
  switch (remoteaddr.sin6_family){
  case AF_INET:
    memcpy(tmpBuffer + 12, &(reinterpret_cast<const sockaddr_in *>(&remoteaddr)->sin_addr.s_addr), 4);
    break;
  case AF_INET6: memcpy(tmpBuffer, &(remoteaddr.sin6_addr.s6_addr), 16); break;
  default: return ""; break;
  }
  return std::string(tmpBuffer, 16);
}

bool Socket::isLocalhost(const std::string &remotehost){
  std::string tmpInput = remotehost;
  std::string bf = Socket::getBinForms(tmpInput);
  std::string tmpAddr;
  while (bf.size() >= 16){
    Socket::hostBytesToStr(bf.data(), 16, tmpAddr);
    if (isLocal(tmpAddr)){return true;}
    bf.erase(0, 17);
  }
  return false;
}

/// Checks if the given file descriptor is actually socket or not.
bool Socket::checkTrueSocket(int sock){
  struct stat sBuf;
  if (sock != -1 && !fstat(sock, &sBuf)){return S_ISSOCK(sBuf.st_mode);}
  return false;
}

bool Socket::isLocal(const std::string &remotehost){
  struct ifaddrs *ifAddrStruct = NULL;
  struct ifaddrs *ifa = NULL;
  void *tmpAddrPtr = NULL;
  bool ret = false;
  char addressBuffer[INET6_ADDRSTRLEN];

  getifaddrs(&ifAddrStruct);

  for (ifa = ifAddrStruct; ifa != NULL; ifa = ifa->ifa_next){
    if (!ifa->ifa_addr){continue;}
    if (ifa->ifa_addr->sa_family == AF_INET){// check it is IP4
      tmpAddrPtr = &((struct sockaddr_in *)ifa->ifa_addr)->sin_addr;
      inet_ntop(AF_INET, tmpAddrPtr, addressBuffer, INET_ADDRSTRLEN);
      INSANE_MSG("Comparing '%s'  to '%s'", remotehost.c_str(), addressBuffer);
      if (remotehost == addressBuffer){
        ret = true;
        break;
      }
      INSANE_MSG("Comparing '%s'  to '::ffff:%s'", remotehost.c_str(), addressBuffer);
      if (remotehost == std::string("::ffff:") + addressBuffer){
        ret = true;
        break;
      }
    }else if (ifa->ifa_addr->sa_family == AF_INET6){// check it is IP6
      tmpAddrPtr = &((struct sockaddr_in6 *)ifa->ifa_addr)->sin6_addr;
      inet_ntop(AF_INET6, tmpAddrPtr, addressBuffer, INET6_ADDRSTRLEN);
      INSANE_MSG("Comparing '%s'  to '%s'", remotehost.c_str(), addressBuffer);
      if (remotehost == addressBuffer){
        ret = true;
        break;
      }
    }
  }
  if (ifAddrStruct != NULL) freeifaddrs(ifAddrStruct);
  return ret;
}

/// Helper function that matches two binary-format IPv6 addresses with prefix bits of prefix.
bool Socket::matchIPv6Addr(const std::string &A, const std::string &B, uint8_t prefix){
  if (!prefix){prefix = 128;}
  if (Util::printDebugLevel >= DLVL_MEDIUM){
    std::string Astr, Bstr;
    Socket::hostBytesToStr(A.data(), 16, Astr);
    Socket::hostBytesToStr(B.data(), 16, Bstr);
    MEDIUM_MSG("Matching: %s to %s with %u prefix", Astr.c_str(), Bstr.c_str(), prefix);
  }
  if (memcmp(A.data(), B.data(), prefix / 8)){return false;}
  if ((prefix % 8) && ((A.data()[prefix / 8] & (0xFF << (8 - (prefix % 8)))) !=
                       (B.data()[prefix / 8] & (0xFF << (8 - (prefix % 8)))))){
    return false;
  }
  return true;
}

bool Socket::compareAddress(const sockaddr* A, const sockaddr* B){
  if (!A || !B){return false;}
  bool aSix = false, bSix = false;
  char *aPtr = 0, *bPtr = 0;
  uint16_t aPort = 0, bPort = 0;
  if (A->sa_family == AF_INET){
    aPtr = (char*)&((sockaddr_in*)A)->sin_addr;
    aPort = ((sockaddr_in*)A)->sin_port;
  }else if(A->sa_family == AF_INET6){
    aPtr = (char*)&((sockaddr_in6*)A)->sin6_addr;
    aPort = ((sockaddr_in6*)A)->sin6_port;
    if (!memcmp("\000\000\000\000\000\000\000\000\000\000\377\377", aPtr, 12)){
      aPtr += 12;
    }else{
      aSix = true;
    }
  }else{
    return false;
  }
  if (B->sa_family == AF_INET){
    bPtr = (char*)&((sockaddr_in*)B)->sin_addr;
    bPort = ((sockaddr_in*)B)->sin_port;
  }else if(B->sa_family == AF_INET6){
    bPtr = (char*)&((sockaddr_in6*)B)->sin6_addr;
    bPort = ((sockaddr_in6*)B)->sin6_port;
    if (!memcmp("\000\000\000\000\000\000\000\000\000\000\377\377", bPtr, 12)){
      bPtr += 12;
    }else{
      bSix = true;
    }
  }else{
    return false;
  }
  if (aPort != bPort){return false;}
  if (aSix != bSix){return false;}
  return !memcmp(aPtr, bPtr, aSix?16:4);
}

/// Attempts to match the given address with optional subnet to the given binary-form IPv6 address.
/// Returns true if match could be made, false otherwise.
bool Socket::isBinAddress(const std::string &binAddr, std::string addr){
  // Check if we need to do prefix matching
  uint8_t prefixLen = 0;
  if (addr.find('/') != std::string::npos){
    prefixLen = atoi(addr.c_str() + addr.find('/') + 1);
    addr.erase(addr.find('/'), std::string::npos);
  }
  // Loops over all IPs for the given address and matches them in IPv6 form.
  struct addrinfo *result, *rp, hints;
  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_ADDRCONFIG | AI_V4MAPPED;
  hints.ai_protocol = 0;
  hints.ai_canonname = NULL;
  hints.ai_addr = NULL;
  hints.ai_next = NULL;
  int s = getaddrinfo(addr.c_str(), 0, &hints, &result);
  if (s != 0){return false;}

  for (rp = result; rp != NULL; rp = rp->ai_next){
    std::string tBinAddr = getIPv6BinAddr(*((sockaddr_in6 *)rp->ai_addr));
    if (rp->ai_family == AF_INET){
      if (matchIPv6Addr(tBinAddr, binAddr, prefixLen ? prefixLen + 96 : 0)){return true;}
    }else{
      if (matchIPv6Addr(tBinAddr, binAddr, prefixLen)){return true;}
    }
  }
  freeaddrinfo(result);
  return false;
}

/// Converts the given address with optional subnet to binary IPv6 form.
/// Returns 16 bytes of address, followed by 1 byte of subnet bits, zero or more times.
std::string Socket::getBinForms(std::string addr){
  // Check for empty address
  if (!addr.size()){return std::string(17, (char)0);}
  // Check if we need to do prefix matching
  uint8_t prefixLen = 128;
  if (addr.find('/') != std::string::npos){
    prefixLen = atoi(addr.c_str() + addr.find('/') + 1);
    addr.erase(addr.find('/'), std::string::npos);
  }
  // Loops over all IPs for the given address and converts to IPv6 binary form.
  struct addrinfo *result, *rp, hints;
  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_ADDRCONFIG | AI_V4MAPPED | AI_ALL;
  hints.ai_protocol = 0;
  hints.ai_canonname = NULL;
  hints.ai_addr = NULL;
  hints.ai_next = NULL;
  int s = getaddrinfo(addr.c_str(), 0, &hints, &result);
  if (s != 0){return "";}
  std::string ret;
  for (rp = result; rp != NULL; rp = rp->ai_next){
    ret += getIPv6BinAddr(*((sockaddr_in6 *)rp->ai_addr));
    if (rp->ai_family == AF_INET){
      ret += (char)(prefixLen <= 32 ? prefixLen + 96 : prefixLen);
    }else{
      ret += (char)prefixLen;
    }
  }
  freeaddrinfo(result);
  return ret;
}

std::deque<std::string> Socket::getAddrs(std::string addr, uint16_t port, int family){
  std::deque<std::string> ret;
  struct addrinfo *result, *rp, hints;
  if (addr.substr(0, 7) == "::ffff:"){addr = addr.substr(7);}
  std::stringstream ss;
  ss << port;

  memset(&hints, 0, sizeof(struct addrinfo));
  // For unspecified, we do IPv6, then do IPv4 separately after
  hints.ai_family = family==AF_UNSPEC?AF_INET6:family;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_flags = AI_ADDRCONFIG | AI_PASSIVE | AI_V4MAPPED | AI_ALL;
  hints.ai_protocol = IPPROTO_UDP;
  int s = getaddrinfo(addr.c_str(), ss.str().c_str(), &hints, &result);
  if (!s){
    // Store each address in a string and put it in the deque.
    for (rp = result; rp != NULL; rp = rp->ai_next){
      ret.push_back(std::string((char*)rp->ai_addr, rp->ai_addrlen));
    }
    freeaddrinfo(result);
  }

  // If failed or unspecified, (also) try IPv4
  if (s || family==AF_UNSPEC){
    hints.ai_family = AF_INET;
    s = getaddrinfo(addr.c_str(), ss.str().c_str(), &hints, &result);
    if (!s){
      // Store each address in a string and put it in the deque.
      for (rp = result; rp != NULL; rp = rp->ai_next){
        ret.push_back(std::string((char*)rp->ai_addr, rp->ai_addrlen));
      }
      freeaddrinfo(result);
    }
  }

  // Return all we found
  return ret;
}

/// Checks bytes (length len) containing a binary-encoded IPv4 or IPv6 IP address, and writes it in
/// human-readable notation to target. Writes "unknown" if it cannot decode to a sensible value.
void Socket::hostBytesToStr(const char *bytes, size_t len, std::string &target){
  switch (len){
  case 4:
    char tmpstr[16];
    snprintf(tmpstr, 16, "%hhu.%hhu.%hhu.%hhu", bytes[0], bytes[1], bytes[2], bytes[3]);
    target = tmpstr;
    break;
  case 16:
    if (memcmp(bytes, "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000", 15) == 0){
      if (bytes[15] == 0){
        target = "::";
        return;
      }
      char tmpstr[6];
      snprintf(tmpstr, 6, "::%hhu", bytes[15]);
      target = tmpstr;
      return;
    }
    if (memcmp(bytes, "\000\000\000\000\000\000\000\000\000\000\377\377", 12) == 0){
      char tmpstr[16];
      snprintf(tmpstr, 16, "%hhu.%hhu.%hhu.%hhu", bytes[12], bytes[13], bytes[14], bytes[15]);
      target = tmpstr;
    }else{
      char tmpstr[40];
      snprintf(tmpstr, 40, "%.2x%.2x:%.2x%.2x:%.2x%.2x:%.2x%.2x:%.2x%.2x:%.2x%.2x:%.2x%.2x:%.2x%.2x",
               bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7],
               bytes[8], bytes[9], bytes[10], bytes[11], bytes[12], bytes[13], bytes[14], bytes[15]);
      target = tmpstr;
    }
    break;
  default: target = "unknown"; break;
  }
}

/// Resolves a hostname into a human-readable address that is the best guess for external address matching this host.
/// The optional family can force IPv4/IPv6 resolving, while the optional hint will allow forcing resolving to a
/// specific address if it is a match for this host.
/// Returns empty string if no reasonable match could be made.
std::string Socket::resolveHostToBestExternalAddrGuess(const std::string &host, int family,
                                                       const std::string &hint){
  if (!host.size()){return "";}
  struct addrinfo *result, *rp, hints;
  std::string newaddr;

  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = family;
  hints.ai_socktype = 0;
  hints.ai_flags = AI_ADDRCONFIG;
  int s = getaddrinfo(host.c_str(), 0, &hints, &result);
  if (s != 0){
    FAIL_MSG("Could not resolve %s! Error: %s", host.c_str(), gai_strmagic(s));
    return "";
  }

  for (rp = result; rp != NULL; rp = rp->ai_next){
    static char addrconv[INET6_ADDRSTRLEN];
    if (rp->ai_family == AF_INET6){
      newaddr = inet_ntop(rp->ai_family, &((const sockaddr_in6 *)rp->ai_addr)->sin6_addr, addrconv, INET6_ADDRSTRLEN);
    }
    if (rp->ai_family == AF_INET){
      newaddr = inet_ntop(rp->ai_family, &((const sockaddr_in *)rp->ai_addr)->sin_addr, addrconv, INET6_ADDRSTRLEN);
    }
    if (newaddr.substr(0, 7) == "::ffff:"){newaddr = newaddr.substr(7);}
    HIGH_MSG("Resolved to %s addr [%s]", addrFam(rp->ai_family), newaddr.c_str());
    // if not a local address, we can't bind, so don't bother trying it
    if (!isLocal(newaddr)){continue;}
    // we match the hint, done!
    if (newaddr == hint){break;}
  }
  freeaddrinfo(result);
  return newaddr;
}

/// Gets bound host and port for a socket and returns them by reference.
/// Returns true on success and false on failure.
bool Socket::getSocketName(int fd, std::string &host, uint32_t &port){
  struct sockaddr_in6 tmpaddr;
  socklen_t len = sizeof(tmpaddr);
  if (getsockname(fd, (sockaddr *)&tmpaddr, &len)){return false;}
  static char addrconv[INET6_ADDRSTRLEN];
  if (tmpaddr.sin6_family == AF_INET6){
    host = inet_ntop(AF_INET6, &(tmpaddr.sin6_addr), addrconv, INET6_ADDRSTRLEN);
    if (host.substr(0, 7) == "::ffff:"){host = host.substr(7);}
    port = ntohs(tmpaddr.sin6_port);
    HIGH_MSG("Local IPv6 addr [%s:%" PRIu32 "]", host.c_str(), port);
    return true;
  }
  if (tmpaddr.sin6_family == AF_INET){
    host = inet_ntop(AF_INET, &(((sockaddr_in *)&tmpaddr)->sin_addr), addrconv, INET6_ADDRSTRLEN);
    port = ntohs(((sockaddr_in *)&tmpaddr)->sin_port);
    HIGH_MSG("Local IPv4 addr [%s:%" PRIu32 "]", host.c_str(), port);
    return true;
  }
  return false;
}

/// Gets peer host and port for a socket and returns them by reference.
/// Returns true on success and false on failure.
bool Socket::getPeerName(int fd, std::string &host, uint32_t &port, sockaddr * tmpaddr, socklen_t * addrlen){
  if (getpeername(fd, tmpaddr, addrlen)){return false;}
  static char addrconv[INET6_ADDRSTRLEN];
  if (tmpaddr->sa_family == AF_INET6){
    host = inet_ntop(AF_INET6, &(((sockaddr_in6*)tmpaddr)->sin6_addr), addrconv, INET6_ADDRSTRLEN);
    if (host.substr(0, 7) == "::ffff:"){host = host.substr(7);}
    port = ntohs(((sockaddr_in6 *)tmpaddr)->sin6_port);
    HIGH_MSG("Peer IPv6 addr [%s:%" PRIu32 "]", host.c_str(), port);
    return true;
  }
  if (tmpaddr->sa_family == AF_INET){
    host = inet_ntop(AF_INET, &(((sockaddr_in *)tmpaddr)->sin_addr), addrconv, INET6_ADDRSTRLEN);
    port = ntohs(((sockaddr_in *)tmpaddr)->sin_port);
    HIGH_MSG("Peer IPv4 addr [%s:%" PRIu32 "]", host.c_str(), port);
    return true;
  }
  return false;
}

/// Gets peer host and port for a socket and returns them by reference.
/// Returns true on success and false on failure.
bool Socket::getPeerName(int fd, std::string &host, uint32_t &port){
  struct sockaddr_in6 tmpaddr;
  socklen_t addrLen = sizeof(tmpaddr);
  return getPeerName(fd, host, port, (sockaddr*)&tmpaddr, &addrLen);
}

std::string uint2string(unsigned int i){
  std::stringstream st;
  st << i;
  return st.str();
}

Socket::Buffer::Buffer(){
  splitter = "\n";
}

/// Returns the amount of elements in the internal std::deque of std::string objects.
/// The back is popped as long as it is empty, first - this way this function is
/// guaranteed to return 0 if the buffer is empty.
unsigned int Socket::Buffer::size(){
  while (data.size() > 0 && data.back().empty()){data.pop_back();}
  return data.size();
}

/// Returns either the amount of total bytes available in the buffer or max, whichever is smaller.
unsigned int Socket::Buffer::bytes(unsigned int max){
  unsigned int i = 0;
  for (std::deque<std::string>::iterator it = data.begin(); it != data.end(); ++it){
    i += (*it).size();
    if (i >= max){return max;}
  }
  return i;
}

/// Returns how many bytes to read until the next splitter, or 0 if none found.
unsigned int Socket::Buffer::bytesToSplit(){
  unsigned int i = 0;
  for (std::deque<std::string>::reverse_iterator it = data.rbegin(); it != data.rend(); ++it){
    i += (*it).size();
    if ((*it).size() >= splitter.size() && (*it).substr((*it).size() - splitter.size()) == splitter){
      return i;
    }
  }
  return 0;
}

/// Appends this string to the internal std::deque of std::string objects.
/// It is automatically split every BUFFER_BLOCKSIZE bytes and when the splitter string is
/// encountered.
void Socket::Buffer::append(const std::string &newdata){
  append(newdata.data(), newdata.size());
}

/// Helper function that does a short-circuiting string compare
inline bool string_compare(const char *a, const char *b, const size_t len){
  for (size_t i = 0; i < len; ++i){
    if (a[i] != b[i]){return false;}
  }
  return true;
}

/// Appends this data block to the internal std::deque of std::string objects.
/// It is automatically split every BUFFER_BLOCKSIZE bytes and when the splitter string is
/// encountered.
void Socket::Buffer::append(const char *newdata, const unsigned int newdatasize){
  uint32_t i = 0;
  while (i < newdatasize){
    uint32_t j = 0;
    if (!splitter.size()){
      if (newdatasize - i > BUFFER_BLOCKSIZE){
        j = BUFFER_BLOCKSIZE;
      }else{
        j = newdatasize - i;
      }
    }else{
      while (j + i < newdatasize && j < BUFFER_BLOCKSIZE){
        j++;
        if (j >= splitter.size()){
          if (string_compare(newdata + i + j - splitter.size(), splitter.data(), splitter.size())){
            break;
          }
        }
      }
    }
    if (j){
      data.push_front("");
      data.front().assign(newdata + i, (size_t)j);
      i += j;
    }else{
      FAIL_MSG("Appended an empty string to buffer: aborting!");
      break;
    }
  }
  if (data.size() > 5000){
    WARN_MSG("Warning: After %d new bytes, buffer has %d parts containing over %u bytes!",
             newdatasize, (int)data.size(), bytes(9000));
  }
}

/// Prepends this data block to the internal std::deque of std::string objects.
/// It is _not_ automatically split every BUFFER_BLOCKSIZE bytes.
void Socket::Buffer::prepend(const std::string &newdata){
  data.push_back(newdata);
}

/// Prepends this data block to the internal std::deque of std::string objects.
/// It is _not_ automatically split every BUFFER_BLOCKSIZE bytes.
void Socket::Buffer::prepend(const char *newdata, const unsigned int newdatasize){
  data.push_back(std::string(newdata, (size_t)newdatasize));
}

/// Returns true if at least count bytes are available in this buffer.
bool Socket::Buffer::available(unsigned int count){
  size();
  unsigned int i = 0;
  for (std::deque<std::string>::iterator it = data.begin(); it != data.end(); ++it){
    i += (*it).size();
    if (i >= count){return true;}
  }
  return false;
}

/// Returns true if at least count bytes are available in this buffer.
bool Socket::Buffer::available(unsigned int count) const{
  unsigned int i = 0;
  for (std::deque<std::string>::const_iterator it = data.begin(); it != data.end(); ++it){
    i += (*it).size();
    if (i >= count){return true;}
  }
  return false;
}

/// Removes count bytes from the buffer, returning them by value.
/// Returns an empty string if not all count bytes are available.
std::string Socket::Buffer::remove(unsigned int count){
  size();
  if (!available(count)){return "";}
  unsigned int i = 0;
  std::string ret;
  ret.reserve(count);
  for (std::deque<std::string>::reverse_iterator it = data.rbegin(); it != data.rend(); ++it){
    if (i + (*it).size() < count){
      ret.append(*it);
      i += (*it).size();
      (*it).clear();
    }else{
      ret.append(*it, 0, count - i);
      (*it).erase(0, count - i);
      break;
    }
  }
  return ret;
}

/// Removes count bytes from the buffer, appending them to the given ptr.
/// Does nothing if not all count bytes are available.
void Socket::Buffer::remove(Util::ResizeablePointer & ptr, unsigned int count){
  size();
  if (!available(count)){return;}
  unsigned int i = 0;
  for (std::deque<std::string>::reverse_iterator it = data.rbegin(); it != data.rend(); ++it){
    if (i + (*it).size() < count){
      ptr.append(*it);
      i += (*it).size();
      (*it).clear();
    }else{
      ptr.append(it->data(), count - i);
      (*it).erase(0, count - i);
      break;
    }
  }
}

/// Copies count bytes from the buffer, returning them by value.
/// Returns an empty string if not all count bytes are available.
std::string Socket::Buffer::copy(unsigned int count){
  size();
  if (!available(count)){return "";}
  unsigned int i = 0;
  std::string ret;
  ret.reserve(count);
  for (std::deque<std::string>::reverse_iterator it = data.rbegin(); it != data.rend(); ++it){
    if (i + (*it).size() < count){
      ret.append(*it);
      i += (*it).size();
    }else{
      ret.append(*it, 0, count - i);
      break;
    }
  }
  return ret;
}

/// Gets a reference to the back of the internal std::deque of std::string objects.
std::string &Socket::Buffer::get(){
  size();
  static std::string empty;
  if (data.size() > 0){
    return data.back();
  }else{
    return empty;
  }
}

/// Completely empties the buffer
void Socket::Buffer::clear(){
  data.clear();
}

bool Socket::Buffer::compact(){
  size_t preSize = size();
  size_t preBytes = bytes(0xFFFFFFFFull);

  size_t compactSize = 4096; // 4k chunks
  if (preBytes > 4194304){ // more than 4MiB of data
    compactSize = 8192; // 8k chunks
    if (preBytes > 16777216){ // more than 16MiB of data
      compactSize = 16384; // 16k chunks
      if (preBytes > 33554432){ // more than 32MiB of data
        compactSize = 32768; // 32k chunks
        if (preBytes > 268435456){ // more than 256MiB of data
          FAIL_MSG("Disconnecting socket with buffer containing more than 256MiB of data!");
          return false; // Abort!
        }
      }
    }
  }


  bool changed = false;
  for (std::deque<std::string>::reverse_iterator it = data.rbegin(); it != data.rend(); ++it){
    if (it->size() < compactSize){
      it->reserve(compactSize);
      std::deque<std::string>::reverse_iterator currIt = it;
      ++it;
      while (currIt->size() < compactSize && it != data.rend()){
        changed = true;
        currIt->append(*it);
        it->clear();
        if (currIt->size() >= compactSize){break;}
        ++it;
      }
      if (it == data.rend()){break;}
    }
  }
  if (!changed){
    FAIL_MSG("Disconnecting socket due to socket buffer compacting failure");
    return false;
  }
  while (changed){
    changed = false;
    for (std::deque<std::string>::iterator it = data.begin(); it != data.end(); ++it){
      if (!it->size()){
        data.erase(it);
        changed = true;
        break;
      }
    }
  }

  size_t postSize = size();
  WARN_MSG("Compacted a %zub socket buffer of %zu parts into %zu parts", preBytes, preSize, postSize);
  return true;
}

void Socket::Connection::setBoundAddr(){
  boundaddr.clear();
  // If a bound address was set through environment (e.g. HTTPS output), restore it from there.
  char *envbound = getenv("MIST_BOUND_ADDR");
  if (envbound){boundaddr = envbound;}

  // If we can't read the address, don't try
  if (!isTrueSocket){return;}

  // Otherwise, read from socket pointer. Works for both SSL and non-SSL sockets, and real sockets passed as fd's, but not for non-sockets (duh)
  uint32_t boundport = 0;
  getSocketName(getSocket(), boundaddr, boundport);
  socklen_t aLen = sizeof(remoteaddr);
  getPeerName(getSocket(), remotehost, boundport, (sockaddr *)&remoteaddr, &aLen);
}

// Cleans up the socket by dropping the connection.
// Does not call close because it calls shutdown, which would destroy any copies of this socket too.
Socket::Connection::~Connection(){
  drop();
}

/// Create a new base socket. This is a basic constructor for converting any valid socket to a
/// Socket::Connection. \param sockNo Integer representing the socket to convert.
Socket::Connection::Connection(int sockNo){
  clear();
  open(sockNo, sockNo);
}// Socket::Connection basic constructor

/// Open from existing socket connection.
/// Closes any existing connections and resets all internal values beforehand.
/// Simply calls open(sockNo, sockNo) internally.
void Socket::Connection::open(int sockNo){
  open(sockNo, sockNo);
}

/// Simulate a socket using two file descriptors.
/// \param write The filedescriptor to write to.
/// \param read The filedescriptor to read from.
Socket::Connection::Connection(int write, int read){
  clear();
  open(write, read);
}// Socket::Connection basic constructor

/// Open from two existing file descriptors.
/// Closes any existing connections and resets all internal values beforehand.
void Socket::Connection::open(int write, int read){
  drop();
  clear();
  sSend = write;
  if (write != read){
    sRecv = read;
  }else{
    sRecv = -1;
  }
  isTrueSocket = Socket::checkTrueSocket(sSend);
  setBoundAddr();
}

void Socket::Connection::clear(){
  sSend = -1;
  sRecv = -1;
  isTrueSocket = false;
  up = 0;
  down = 0;
  conntime = Util::bootSecs();
  Error = false;
  skipCount = 0;
  memset(&remoteaddr, 0, sizeof(remoteaddr));
#ifdef SSL
  sslConnected = false;
  server_fd = 0;
  ssl = 0;
  conf = 0;
  ctr_drbg = 0;
  entropy = 0;
#endif
}

/// Create a new disconnected base socket. This is a basic constructor for placeholder purposes.
/// A socket created like this is always disconnected and should/could be overwritten at some point.
Socket::Connection::Connection(){
  clear();
}// Socket::Connection basic constructor

void Socket::Connection::resetCounter(){
  up = 0;
  down = 0;
}

void Socket::Connection::addUp(const uint32_t i){
  up += i;
}

void Socket::Connection::addDown(const uint32_t i){
  down += i;
}

/// Internally used call to make an file descriptor blocking or not.
void setFDBlocking(int FD, bool blocking){
  int flags = fcntl(FD, F_GETFL, 0);
  if (!blocking){
    flags |= O_NONBLOCK;
  }else{
    flags &= !O_NONBLOCK;
  }
  fcntl(FD, F_SETFL, flags);
}

/// Internally used call to make an file descriptor blocking or not.
bool isFDBlocking(int FD){
  int flags = fcntl(FD, F_GETFL, 0);
  return !(flags & O_NONBLOCK);
}

/// Set this socket to be blocking (true) or nonblocking (false).
void Socket::Connection::setBlocking(bool blocking){
#ifdef SSL
  if (sslConnected){
    if (server_fd->fd >= 0){setFDBlocking(server_fd->fd, blocking);}
    return;
  }
#endif
  if (sSend >= 0){setFDBlocking(sSend, blocking);}
  if (sRecv >= 0 && sSend != sRecv){setFDBlocking(sRecv, blocking);}
}

/// Set this socket to be blocking (true) or nonblocking (false).
bool Socket::Connection::isBlocking(){
#ifdef SSL
  if (sslConnected){return isFDBlocking(server_fd->fd);}
#endif
  if (sSend >= 0){return isFDBlocking(sSend);}
  if (sRecv >= 0){return isFDBlocking(sRecv);}
  return false;
}

/// Close connection. The internal socket is closed and then set to -1.
/// If the connection is already closed, nothing happens.
/// This function calls shutdown, thus making the socket unusable in all other
/// processes as well. Do not use on shared sockets that are still in use.
void Socket::Connection::close(){
  if (sSend != -1){shutdown(sSend, SHUT_RDWR);}
  drop();
}// Socket::Connection::close

/// Close connection. The internal socket is closed and then set to -1.
/// If the connection is already closed, nothing happens.
/// This function does *not* call shutdown, allowing continued use in other
/// processes.
void Socket::Connection::drop(){
#ifdef SSL
  if (sslConnected){
    DONTEVEN_MSG("SSL close");
    if (ssl){mbedtls_ssl_close_notify(ssl);}
    if (server_fd){
      mbedtls_net_free(server_fd);
      delete server_fd;
      server_fd = 0;
    }
    if (ssl){
      mbedtls_ssl_free(ssl);
      delete ssl;
      ssl = 0;
    }
    if (conf){
      mbedtls_ssl_config_free(conf);
      delete conf;
      conf = 0;
    }
    if (ctr_drbg){
      mbedtls_ctr_drbg_free(ctr_drbg);
      delete ctr_drbg;
      ctr_drbg = 0;
    }
    if (entropy){
      mbedtls_entropy_free(entropy);
      delete entropy;
      entropy = 0;
    }
    sslConnected = false;
    return;
  }
#endif
  if (connected()){
    if (sSend != -1){
      HIGH_MSG("Socket %d closed", sSend);
      errno = EINTR;
      while (::close(sSend) != 0 && errno == EINTR){}
      if (sRecv == sSend){sRecv = -1;}
      sSend = -1;
    }
    if (sRecv != -1){
      errno = EINTR;
      while (::close(sRecv) != 0 && errno == EINTR){}
      sRecv = -1;
    }
  }
}// Socket::Connection::drop

/// Returns internal socket number.
int Socket::Connection::getSocket(){
#ifdef SSL
  if (sslConnected){return server_fd->fd;}
#endif
  if (sSend != -1){return sSend;}
  return sRecv;
}

/// Returns non-piped internal socket number.
int Socket::Connection::getPureSocket(){
#ifdef SSL
  if (sslConnected){return server_fd->fd;}
#endif
  if (!isTrueSocket){return -1;}
  return sSend;
}

/// Returns a string describing the last error that occurred.
/// Only reports errors if an error actually occurred - returns the host address or empty string
/// otherwise.
std::string Socket::Connection::getError(){
  return lastErr;
}

/// Create a new Unix Socket. This socket will (try to) connect to the given address right away.
/// \param address String containing the location of the Unix socket to connect to.
/// \param nonblock Whether the socket should be nonblocking. False by default.
Socket::Connection::Connection(std::string address, bool nonblock){
  clear();
  open(address, nonblock);
}// Socket::Connection Unix Constructor

/// Open Unix connection.
/// Closes any existing connections and resets all internal values beforehand.
void Socket::Connection::open(std::string address, bool nonblock){
  drop();
  clear();
  isTrueSocket = true;
  sSend = socket(PF_UNIX, SOCK_STREAM, 0);
  if (sSend < 0){
    lastErr = strerror(errno);
    FAIL_MSG("Could not create socket! Error: %s", lastErr.c_str());
    return;
  }
  sockaddr_un addr;
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, address.c_str(), address.size() + 1);
  int r = connect(sSend, (sockaddr *)&addr, sizeof(addr));
  if (r == 0){
    if (nonblock){
      int flags = fcntl(sSend, F_GETFL, 0);
      flags |= O_NONBLOCK;
      fcntl(sSend, F_SETFL, flags);
    }
  }else{
    lastErr = strerror(errno);
    FAIL_MSG("Could not connect to %s! Error: %s", address.c_str(), lastErr.c_str());
    close();
  }
}

#ifdef SSL
/// Local-only function for debugging SSL sockets
static void my_debug(void *ctx, int level, const char *file, int line, const char *str){
  ((void)level);
  fprintf((FILE *)ctx, "%s:%04d: %s", file, line, str);
  fflush((FILE *)ctx);
}

/// Takes a just-accepted socket and SSL-ifies it.
bool Socket::Connection::sslAccept(mbedtls_ssl_config * sslConf, mbedtls_ctr_drbg_context * dbgCtx){
  int ret;
  server_fd = new mbedtls_net_context;
  mbedtls_net_init(server_fd);
  server_fd->fd = getSocket();

  ssl = new mbedtls_ssl_context;
  mbedtls_ssl_init(ssl);
  if ((ret = mbedtls_ctr_drbg_reseed(dbgCtx, (const unsigned char *)"child", 5)) != 0){
    FAIL_MSG("Could not reseed");
    close();
    return false;
  }

  // Set up the SSL connection
  if ((ret = mbedtls_ssl_setup(ssl, sslConf)) != 0){
    FAIL_MSG("Could not set up SSL connection");
    close();
    return false;
  }

  // Inform mbedtls how we'd like to use the connection (uses default bio handlers)
  // We tell it to use non-blocking IO here
  mbedtls_net_set_nonblock(server_fd);
  mbedtls_ssl_set_bio(ssl, server_fd, mbedtls_net_send, mbedtls_net_recv, NULL);
  // do the SSL handshake
  while ((ret = mbedtls_ssl_handshake(ssl)) != 0){
    if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE){
      char error_buf[200];
      mbedtls_strerror(ret, error_buf, 200);
      WARN_MSG("Could not handshake, SSL error: %s (%d)", error_buf, ret);
      close();
      return false;
    }else{
      Util::sleep(20);
    }
  }
  sslConnected = true;
  HIGH_MSG("Started SSL connection handler");
  return true;
}

#endif

/// Create a new TCP Socket. This socket will (try to) connect to the given host/port right away.
/// \param host String containing the hostname to connect to.
/// \param port String containing the port to connect to.
/// \param nonblock Whether the socket should be nonblocking.
Socket::Connection::Connection(std::string host, int port, bool nonblock, bool with_ssl){
  clear();
  open(host, port, nonblock, with_ssl);
}

/// Open TCP connection.
/// Closes any existing connections and resets all internal values beforehand.
void Socket::Connection::open(std::string host, int port, bool nonblock, bool with_ssl){
  drop();
  clear();
  if (with_ssl){
#ifdef SSL
    mbedtls_debug_set_threshold(0);
    server_fd = new mbedtls_net_context;
    ssl = new mbedtls_ssl_context;
    conf = new mbedtls_ssl_config;
    ctr_drbg = new mbedtls_ctr_drbg_context;
    entropy = new mbedtls_entropy_context;
    mbedtls_net_init(server_fd);
    mbedtls_ssl_init(ssl);
    mbedtls_ssl_config_init(conf);
    mbedtls_ctr_drbg_init(ctr_drbg);
    mbedtls_entropy_init(entropy);
    DONTEVEN_MSG("SSL init");
    if (mbedtls_ctr_drbg_seed(ctr_drbg, mbedtls_entropy_func, entropy, (const unsigned char *)"meow", 4) != 0){
      lastErr = "SSL socket init failed";
      FAIL_MSG("SSL socket init failed");
      close();
      return;
    }
    DONTEVEN_MSG("SSL connect");
#endif
  }

  isTrueSocket = true;
  struct addrinfo *result, *rp, hints;
  std::stringstream ss;
  ss << port;

  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_ADDRCONFIG;
  int s = getaddrinfo(host.c_str(), ss.str().c_str(), &hints, &result);
  if (s != 0){
    lastErr = gai_strmagic(s);
    FAIL_MSG("Could not connect to %s:%i! Error: %s", host.c_str(), port, lastErr.c_str());
    close();
    return;
  }

  lastErr = "";
  for (rp = result; rp; rp = rp->ai_next){
    sSend = socket(rp->ai_family, SOCK_STREAM | SOCK_NONBLOCK, rp->ai_protocol);
    if (sSend < 0){continue;}
    //Ensure we can handle interrupted system call case
    int ret = 0;
    do{
      ret = connect(sSend, rp->ai_addr, rp->ai_addrlen);
    }while(ret && errno == EINTR);
    int sockErr;
    socklen_t sockErrLen = sizeof sockErr;
    sockErr = errno;
    size_t waitTime = 5;
    while (ret && sockErr == EINPROGRESS && waitTime){
      struct timeval timeout;
      timeout.tv_sec = 1;
      timeout.tv_usec = 0;
      fd_set wList, eList;
      FD_ZERO(&wList);
      FD_ZERO(&eList);
      FD_SET(sSend, &wList);
      FD_SET(sSend, &eList);
      int r = select(sSend+1,NULL,&wList,&eList,&timeout);
      if (r < 0 && errno != EINTR){
        sockErr = errno;
        break;
      }
      if (r > 0){
        getsockopt(sSend, SOL_SOCKET, SO_ERROR, &sockErr, &sockErrLen);
        ret = sockErr;
      }
      --waitTime;
    }
    if (!ret){
      HIGH_MSG("Connect success!");
      memcpy(&remoteaddr, rp->ai_addr, rp->ai_addrlen);
      break;
    }
    if (!waitTime){
      HIGH_MSG("Connect timeout!");
      lastErr += "connect timeout";
    }else{
      HIGH_MSG("Connect error: %s", strerror(sockErr));
      lastErr += strerror(sockErr);
    }
    ::close(sSend);
  }
  freeaddrinfo(result);

  if (rp == 0){
    FAIL_MSG("Could not connect to %s! Error: %s", host.c_str(), lastErr.c_str());
    close();
    return;
  }
  if (!nonblock){setFDBlocking(sSend, true);}
  int optval = 1;
  setsockopt(sSend, SOL_SOCKET, SO_KEEPALIVE, &optval, sizeof(optval));
  setBoundAddr();

  if (with_ssl){
#ifdef SSL
    server_fd->fd = sSend;
    sSend = -1;
    int ret = 0;
    if ((ret = mbedtls_ssl_config_defaults(conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
                                           MBEDTLS_SSL_PRESET_DEFAULT)) != 0){
      char estr[200];
      mbedtls_strerror(ret, estr, 200);
      lastErr = estr;
      FAIL_MSG("SSL config failed: %d: %s", ret, lastErr.c_str());
      close();
      return;
    }
    mbedtls_ssl_conf_authmode(conf, MBEDTLS_SSL_VERIFY_NONE);
    mbedtls_ssl_conf_rng(conf, mbedtls_ctr_drbg_random, ctr_drbg);
    mbedtls_ssl_conf_dbg(conf, my_debug, stderr);
    if ((ret = mbedtls_ssl_setup(ssl, conf)) != 0){
      char estr[200];
      mbedtls_strerror(ret, estr, 200);
      lastErr = estr;
      FAIL_MSG("SSL setup error %d: %s", ret, lastErr.c_str());
      close();
      return;
    }
    if ((ret = mbedtls_ssl_set_hostname(ssl, host.c_str())) != 0){
      char estr[200];
      mbedtls_strerror(ret, estr, 200);
      lastErr = estr;
      FAIL_MSG("SSL setup error %d: %s", ret, lastErr.c_str());
      close();
      return;
    }
    mbedtls_ssl_set_bio(ssl, server_fd, mbedtls_net_send, mbedtls_net_recv, NULL);
    while ((ret = mbedtls_ssl_handshake(ssl)) != 0){
      if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE){
        char estr[200];
        mbedtls_strerror(ret, estr, 200);
        lastErr = estr;
        FAIL_MSG("SSL handshake error %d: %s", ret, lastErr.c_str());
        close();
        return;
      }
    }
    sslConnected = true;
    DONTEVEN_MSG("SSL connect success");
    return;
#endif
    FAIL_MSG("Attempted to open SSL socket without SSL support compiled in!");
    return;
  }

}

/// Returns the connected-state for this socket.
/// Note that this function might be slightly behind the real situation.
/// The connection status is updated after every read/write attempt, when errors occur
/// and when the socket is closed manually.
/// \returns True if socket is connected, false otherwise.
bool Socket::Connection::connected() const{
#ifdef SSL
  if (sslConnected){return true;}
#endif
  return (sSend >= 0) || (sRecv >= 0);
}

/// Returns the time this socket has been connected.
unsigned int Socket::Connection::connTime(){
  return conntime;
}

/// Returns total amount of bytes sent.
uint64_t Socket::Connection::dataUp(){
  return up;
}

/// Returns total amount of bytes received.
uint64_t Socket::Connection::dataDown(){
  return down;
}

/// Updates the downbuffer internal variable.
/// Returns true if new data was received, false otherwise.
bool Socket::Connection::spool(bool strictMode){
  /// \todo Provide better mechanism to prevent overbuffering.
  if (!strictMode && downbuffer.size() > 5000){
    if (!downbuffer.compact()){
      close();
      return false;
    }
    return true;
  }else{
    return iread(downbuffer);
  }
}

bool Socket::Connection::peek(){
  /// clear buffer
  downbuffer.clear();
  return iread(downbuffer, MSG_PEEK);
}

/// Returns a reference to the download buffer.
Socket::Buffer &Socket::Connection::Received(){
  return downbuffer;
}

/// Returns a reference to the download buffer.
const Socket::Buffer &Socket::Connection::Received() const{
  return downbuffer;
}

/// Will not buffer anything but always send right away. Blocks.
/// Any data that could not be send will block until it can be send or the connection is severed.
void Socket::Connection::SendNow(const char *data, size_t len){
  bool bing = isBlocking();
  if (!bing){setBlocking(true);}
  unsigned int i = iwrite(data, std::min((long unsigned int)len, SOCKETSIZE));
  while (i < len && connected()){
    i += iwrite(data + i, std::min((long unsigned int)(len - i), SOCKETSIZE));
  }
  if (!bing){setBlocking(false);}
}

/// Will not buffer anything but always send right away. Blocks.
/// Any data that could not be send will block until it can be send or the connection is severed.
void Socket::Connection::SendNow(const char *data){
  int len = strlen(data);
  SendNow(data, len);
}

/// Will not buffer anything but always send right away. Blocks.
/// Any data that could not be send will block until it can be send or the connection is severed.
void Socket::Connection::SendNow(const std::string &data){
  SendNow(data.data(), data.size());
}

void Socket::Connection::skipBytes(uint32_t byteCount){
  INFO_MSG("Skipping first %" PRIu32 " bytes going to socket", byteCount);
  skipCount = byteCount;
}

/// Incremental write call. This function tries to write len bytes to the socket from the buffer,
/// returning the amount of bytes it actually wrote.
/// \param buffer Location of the buffer to write from.
/// \param len Amount of bytes to write.
/// \returns The amount of bytes actually written.
unsigned int Socket::Connection::iwrite(const void *buffer, int len){
#ifdef SSL
  if (sslConnected){
    DONTEVEN_MSG("SSL iwrite");
    if (!connected() || len < 1){return 0;}
    int r;
    r = mbedtls_ssl_write(ssl, (const unsigned char *)buffer, len);
    if (r < 0){
      switch (errno){
      case MBEDTLS_ERR_SSL_WANT_WRITE: return 0; break;
      case MBEDTLS_ERR_SSL_WANT_READ: return 0; break;
      case EWOULDBLOCK: return 0; break;
      case EINTR: return 0; break;
      default:
        Error = true;
        lastErr = strerror(errno);
        INSANE_MSG("Could not iwrite data! Error: %s", lastErr.c_str());
        close();
        return 0;
        break;
      }
    }
    if (r == 0 && (sSend >= 0)){
      DONTEVEN_MSG("Socket closed by remote");
      close();
    }
    up += r;
    return r;
  }
#endif
  if (!connected() || len < 1){return 0;}
  if (skipCount){
    // We have bytes to skip writing.
    // Pretend we write them, but don't really.
    if (len <= skipCount){
      skipCount -= len;
      return len;
    }else{
      unsigned int toSkip = skipCount;
      skipCount = 0;
      return iwrite((((char *)buffer) + toSkip), len - toSkip) + toSkip;
    }
  }
  int r;
  if (isTrueSocket){
    r = send(sSend, buffer, len, 0);
  }else{
    r = write(sSend, buffer, len);
  }
  if (r < 0){
    switch (errno){
    case EWOULDBLOCK: return 0; break;
    case EINTR: return 0; break;
    default:
      Error = true;
      lastErr = strerror(errno);
      INSANE_MSG("Could not iwrite data! Error: %s", lastErr.c_str());
      close();
      return 0;
      break;
    }
  }
  if (r == 0 && (sSend >= 0)){
    DONTEVEN_MSG("Socket closed by remote");
    close();
  }
  up += r;
  return r;
}// Socket::Connection::iwrite

/// Incremental read call. This function tries to read len bytes to the buffer from the socket,
/// returning the amount of bytes it actually read.
/// \param buffer Location of the buffer to read to.
/// \param len Amount of bytes to read.
/// \param flags Flags to use in the recv call. Ignored on fake sockets.
/// \returns The amount of bytes actually read.
int Socket::Connection::iread(void *buffer, int len, int flags){
#ifdef SSL
  if (sslConnected){
    DONTEVEN_MSG("SSL iread");
    if (!connected() || len < 1){return 0;}
    int r;
    /// \TODO Flags ignored... Bad.
    r = mbedtls_ssl_read(ssl, (unsigned char *)buffer, len);
    if (r < 0){
      switch (errno){
      case MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY:
        close();
        return 0;
        break;
      case MBEDTLS_ERR_SSL_WANT_WRITE: return 0; break;
      case MBEDTLS_ERR_SSL_WANT_READ: return 0; break;
      case EWOULDBLOCK: return 0; break;
      case EINTR: return 0; break;
      default:
        Error = true;
        char estr[200];
        mbedtls_strerror(r, estr, 200);
        lastErr = estr;
        INFO_MSG("Read returns %d: %s (%s)", r, estr, lastErr.c_str());
        close();
        return 0;
        break;
      }
    }
    if (r == 0){
      DONTEVEN_MSG("Socket closed by remote");
      close();
    }
    down += r;
    return r;
  }
#endif
  if (!connected() || len < 1){return 0;}
  int r;
  if (sRecv != -1 || !isTrueSocket){
    r = read(sRecv, buffer, len);
  }else{
    r = recv(sSend, buffer, len, flags);
  }
  if (r < 0){
    switch (errno){
    case EWOULDBLOCK: return 0; break;
    case EINTR: return 0; break;
    default:
      Error = true;
      lastErr = strerror(errno);
      INSANE_MSG("Could not iread data! Error: %s", lastErr.c_str());
      close();
      return 0;
      break;
    }
  }
  if (r == 0){
    DONTEVEN_MSG("Socket closed by remote");
    close();
  }
  down += r;
  return r;
}// Socket::Connection::iread

/// Read call that is compatible with Socket::Buffer.
/// Data is read using iread (which is nonblocking if the Socket::Connection itself is),
/// then appended to end of buffer.
/// \param buffer Socket::Buffer to append data to.
/// \param flags Flags to use in the recv call. Ignored on fake sockets.
/// \return True if new data arrived, false otherwise.
bool Socket::Connection::iread(Buffer &buffer, int flags){
  char cbuffer[BUFFER_BLOCKSIZE];
  int num = iread(cbuffer, BUFFER_BLOCKSIZE, flags);
  if (num < 1){return false;}
  buffer.append(cbuffer, num);
  return true;
}// iread

/// Incremental write call that is compatible with std::string.
/// Data is written using iwrite (which is nonblocking if the Socket::Connection itself is),
/// then removed from front of buffer.
/// \param buffer std::string to remove data from.
/// \return True if more data was sent, false otherwise.
bool Socket::Connection::iwrite(std::string &buffer){
  if (buffer.size() < 1){return false;}
  unsigned int tmp = iwrite((void *)buffer.c_str(), buffer.size());
  if (!tmp){return false;}
  buffer = buffer.substr(tmp);
  return true;
}// iwrite

/// Gets hostname for connection, if available.
std::string Socket::Connection::getHost() const{
  return remotehost;
}

/// Gets locally bound host for connection, if available.
std::string Socket::Connection::getBoundAddress() const{
  return boundaddr;
}

/// Gets binary IPv6 address for connection, if available.
/// Guaranteed to be either empty or 16 bytes long.
std::string Socket::Connection::getBinHost() const{
  return getIPv6BinAddr(remoteaddr);
}

/// Sets hostname for connection manually.
/// Overwrites the detected host, thus possibly making it incorrect.
void Socket::Connection::setHost(std::string host){
  remotehost = host;
  struct addrinfo *result, hints;
  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_ADDRCONFIG | AI_V4MAPPED;
  hints.ai_protocol = 0;
  hints.ai_canonname = NULL;
  hints.ai_addr = NULL;
  hints.ai_next = NULL;
  int s = getaddrinfo(host.c_str(), 0, &hints, &result);
  if (s != 0){return;}
  if (result){memcpy(&remoteaddr, result->ai_addr, result->ai_addrlen);}
  freeaddrinfo(result);
}

/// Returns true if these sockets are the same socket.
/// Does not check the internal stats - only the socket itself.
bool Socket::Connection::operator==(const Connection &B) const{
  return sSend == B.sSend && sRecv == B.sRecv;
}

/// Returns true if these sockets are not the same socket.
/// Does not check the internal stats - only the socket itself.
bool Socket::Connection::operator!=(const Connection &B) const{
  return sSend != B.sSend || sRecv != B.sRecv;
}

/// Returns true if the socket is valid.
/// Aliases for Socket::Connection::connected()
Socket::Connection::operator bool() const{
  return connected();
}

// Copy constructor
Socket::Connection::Connection(const Connection &rhs){
  clear();
  if (!rhs){return;}
#if DEBUG >= DLVL_DEVEL
#ifdef SSL
  HIGH_MSG("Copying %s socket", rhs.sslConnected ? "SSL" : "regular");
#else
  HIGH_MSG("Copying regular socket");
#endif
#endif
  conntime = rhs.conntime;
  isTrueSocket = rhs.isTrueSocket;
  remotehost = rhs.remotehost;
  boundaddr = rhs.boundaddr;
  remoteaddr = rhs.remoteaddr;
  lastErr = rhs.lastErr;
  up = rhs.up;
  down = rhs.down;
  downbuffer = rhs.downbuffer;
#ifdef SSL
  if (!rhs.sslConnected){
#endif
    if (rhs.sSend >= 0){sSend = dup(rhs.sSend);}
    if (rhs.sRecv >= 0){sRecv = dup(rhs.sRecv);}
#if DEBUG >= DLVL_DEVEL
    HIGH_MSG("Socket original = (%d / %d), copy = (%d / %d)", rhs.sSend, rhs.sRecv, sSend, sRecv);
#endif
#ifdef SSL
  }
#endif
}

// Assignment constructor
Socket::Connection &Socket::Connection::operator=(const Socket::Connection &rhs){
  drop();
  clear();
  if (!rhs){return *this;}
#if DEBUG >= DLVL_DEVEL
#ifdef SSL
  HIGH_MSG("Assigning %s socket", rhs.sslConnected ? "SSL" : "regular");
#else
  HIGH_MSG("Assigning regular socket");
#endif
#endif
  conntime = rhs.conntime;
  isTrueSocket = rhs.isTrueSocket;
  remotehost = rhs.remotehost;
  boundaddr = rhs.boundaddr;
  remoteaddr = rhs.remoteaddr;
  lastErr = rhs.lastErr;
  up = rhs.up;
  down = rhs.down;
  downbuffer = rhs.downbuffer;
#ifdef SSL
  if (!rhs.sslConnected){
#endif
    if (rhs.sSend >= 0){sSend = dup(rhs.sSend);}
    if (rhs.sRecv >= 0){sRecv = dup(rhs.sRecv);}
#if DEBUG >= DLVL_DEVEL
    HIGH_MSG("Socket original = (%d / %d), copy = (%d / %d)", rhs.sSend, rhs.sRecv, sSend, sRecv);
#endif
#ifdef SSL
  }
#endif
  return *this;
}

/// Returns true if the given address can be matched with the remote host.
/// Can no longer return true after any socket error have occurred.
bool Socket::Connection::isAddress(const std::string &addr){
  // Retrieve current socket binary address
  std::string myBinAddr = getBinHost();
  return isBinAddress(myBinAddr, addr);
}

bool Socket::Connection::isLocal(){
  return Socket::isLocal(remotehost);
}

/// Create a new base Server. The socket is never connected, and a placeholder for later
/// connections.
Socket::Server::Server(){
  sock = -1;
}// Socket::Server base Constructor

/// Create a new Server from existing socket.
Socket::Server::Server(int fromSock){
  sock = fromSock;
}

/// Create a new TCP Server. The socket is immediately bound and set to listen.
/// A maximum of 100 connections will be accepted between accept() calls.
/// Any further connections coming in will be dropped.
/// \param port The TCP port to listen on
/// \param hostname (optional) The interface to bind to. The default is 0.0.0.0 (all interfaces).
/// \param nonblock (optional) Whether accept() calls will be nonblocking. Default is false
/// (blocking).
Socket::Server::Server(int port, std::string hostname, bool nonblock){
  if (!IPv6bind(port, hostname, nonblock) && !IPv4bind(port, hostname, nonblock)){
    FAIL_MSG("Could not create socket %s:%i! Error: %s", hostname.c_str(), port, errors.c_str());
    sock = -1;
  }
}// Socket::Server TCP Constructor

/// Attempt to bind an IPv6 socket.
/// \param port The TCP port to listen on
/// \param hostname The interface to bind to. The default is 0.0.0.0 (all interfaces).
/// \param nonblock Whether accept() calls will be nonblocking. Default is false (blocking).
/// \return True if successful, false otherwise.
bool Socket::Server::IPv6bind(int port, std::string hostname, bool nonblock){
  sock = socket(AF_INET6, SOCK_STREAM, 0);
  if (sock < 0){
    errors = strerror(errno);
    ERROR_MSG("Could not create IPv6 socket %s:%i! Error: %s", hostname.c_str(), port, errors.c_str());
    return false;
  }
  int on = 1;
  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
#ifdef __CYGWIN__
  on = 0;
  setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, &on, sizeof(on));
#endif
  if (nonblock){
    int flags = fcntl(sock, F_GETFL, 0);
    flags |= O_NONBLOCK;
    fcntl(sock, F_SETFL, flags);
  }
  struct sockaddr_in6 addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin6_family = AF_INET6;
  addr.sin6_port = htons(port); // set port
  // set interface, 0.0.0.0 (default) is all
  if (hostname == "0.0.0.0" || hostname.length() == 0){
    addr.sin6_addr = in6addr_any;
  }else{
    if (inet_pton(AF_INET6, hostname.c_str(), &addr.sin6_addr) != 1){
      errors = strerror(errno);
      HIGH_MSG("Could not convert '%s' to a valid IPv6 address", hostname.c_str());
      close();
      return false;
    }
  }
  int ret = bind(sock, (sockaddr *)&addr, sizeof(addr)); // do the actual bind
  if (ret == 0){
    ret = listen(sock, 100); // start listening, backlog of 100 allowed
    if (ret == 0){
      DEVEL_MSG("IPv6 socket success @ %s:%i", hostname.c_str(), port);
      return true;
    }else{
      errors = strerror(errno);
      ERROR_MSG("IPv6 listen failed! Error: %s", errors.c_str());
      close();
      return false;
    }
  }else{
    errors = strerror(errno);
    ERROR_MSG("IPv6 Binding %s:%i failed (%s)", hostname.c_str(), port, errors.c_str());
    close();
    return false;
  }
}

/// Attempt to bind an IPv4 socket.
/// \param port The TCP port to listen on
/// \param hostname The interface to bind to. The default is 0.0.0.0 (all interfaces).
/// \param nonblock Whether accept() calls will be nonblocking. Default is false (blocking).
/// \return True if successful, false otherwise.
bool Socket::Server::IPv4bind(int port, std::string hostname, bool nonblock){
  sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0){
    errors = strerror(errno);
    ERROR_MSG("Could not create IPv4 socket %s:%i! Error: %s", hostname.c_str(), port, errors.c_str());
    return false;
  }
  int on = 1;
  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
  if (nonblock){
    int flags = fcntl(sock, F_GETFL, 0);
    flags |= O_NONBLOCK;
    fcntl(sock, F_SETFL, flags);
  }
  struct sockaddr_in addr4;
  memset(&addr4, 0, sizeof(addr4));
  addr4.sin_family = AF_INET;
  addr4.sin_port = htons(port); // set port
  // set interface, 0.0.0.0 (default) is all
  if (hostname == "0.0.0.0" || hostname.length() == 0){
    addr4.sin_addr.s_addr = INADDR_ANY;
  }else{
    if (inet_pton(AF_INET, hostname.c_str(), &addr4.sin_addr) != 1){
      errors = strerror(errno);
      HIGH_MSG("Could not convert '%s' to a valid IPv4 address", hostname.c_str());
      close();
      return false;
    }
  }
  int ret = bind(sock, (sockaddr *)&addr4, sizeof(addr4)); // do the actual bind
  if (ret == 0){
    ret = listen(sock, 100); // start listening, backlog of 100 allowed
    if (ret == 0){
      DEVEL_MSG("IPv4 socket success @ %s:%i", hostname.c_str(), port);
      return true;
    }else{
      errors = strerror(errno);
      ERROR_MSG("IPv4 listen failed! Error: %s", errors.c_str());
      close();
      return false;
    }
  }else{
    errors = strerror(errno);
    ERROR_MSG("IPv4 Binding %s:%i failed (%s)", hostname.c_str(), port, errors.c_str());
    close();
    return false;
  }
}

/// Create a new Unix Server. The socket is immediately bound and set to listen.
/// A maximum of 100 connections will be accepted between accept() calls.
/// Any further connections coming in will be dropped.
/// The address used will first be unlinked - so it succeeds if the Unix socket already existed.
/// Watch out for this behaviour - it will delete any file located at address! \param address The
/// location of the Unix socket to bind to. \param nonblock (optional) Whether accept() calls will
/// be nonblocking. Default is false (blocking).
Socket::Server::Server(std::string address, bool nonblock){
  unlink(address.c_str());
  sock = socket(AF_UNIX, SOCK_STREAM, 0);
  if (sock < 0){
    errors = strerror(errno);
    ERROR_MSG("Could not create unix socket %s! Error: %s", address.c_str(), errors.c_str());
    return;
  }
  if (nonblock){
    int flags = fcntl(sock, F_GETFL, 0);
    flags |= O_NONBLOCK;
    fcntl(sock, F_SETFL, flags);
  }
  sockaddr_un addr;
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, address.c_str(), address.size() + 1);
  int ret = bind(sock, (sockaddr *)&addr, sizeof(addr));
  if (ret == 0){
    ret = listen(sock, 100); // start listening, backlog of 100 allowed
    if (ret == 0){
      return;
    }else{
      errors = strerror(errno);
      ERROR_MSG("Unix listen failed! Error: %s", errors.c_str());
      close();
      return;
    }
  }else{
    errors = strerror(errno);
    ERROR_MSG("Unix Binding %s failed (%s)", address.c_str(), errors.c_str());
    close();
    return;
  }
}// Socket::Server Unix Constructor

/// Accept any waiting connections. If the Socket::Server is blocking, this function will block
/// until there is an incoming connection. If the Socket::Server is nonblocking, it might return a
/// Socket::Connection that is not connected, so check for this. \param nonblock (optional) Whether
/// the newly connected socket should be nonblocking. Default is false (blocking). \returns A
/// Socket::Connection, which may or may not be connected, depending on settings and circumstances.
Socket::Connection Socket::Server::accept(bool nonblock){
  if (sock < 0){return Socket::Connection(-1);}
  struct sockaddr_in6 tmpaddr;
  socklen_t len = sizeof(tmpaddr);
  int r = ::accept(sock, (sockaddr *)&tmpaddr, &len);
  // set the socket to be nonblocking, if requested.
  // we could do this through accept4 with a flag, but that call is non-standard...
  if (r < 0){
    if ((errno != EWOULDBLOCK) && (errno != EAGAIN) && (errno != EINTR)){
      if (errno != EINVAL){
        FAIL_MSG("Error during accept: %s. Closing server socket %d.", strerror(errno), sock);
      }
      close();
    }
    return Socket::Connection();
  }

  if (nonblock){
    int flags = fcntl(r, F_GETFL, 0);
    flags |= O_NONBLOCK;
    fcntl(r, F_SETFL, flags);
  }
  int optval = 1;
  int optlen = sizeof(optval);
  setsockopt(r, SOL_SOCKET, SO_KEEPALIVE, &optval, optlen);
  return Socket::Connection(r);
}

/// Set this socket to be blocking (true) or nonblocking (false).
void Socket::Server::setBlocking(bool blocking){
  if (sock >= 0){setFDBlocking(sock, blocking);}
}

/// Set this socket to be blocking (true) or nonblocking (false).
bool Socket::Server::isBlocking(){
  if (sock >= 0){return isFDBlocking(sock);}
  return false;
}

/// Close connection. The internal socket is closed and then set to -1.
/// If the connection is already closed, nothing happens.
/// This function calls shutdown, thus making the socket unusable in all other
/// processes as well. Do not use on shared sockets that are still in use.
void Socket::Server::close(){
  if (sock != -1){shutdown(sock, SHUT_RDWR);}
  drop();
}// Socket::Server::close

/// Close connection. The internal socket is closed and then set to -1.
/// If the connection is already closed, nothing happens.
/// This function does *not* call shutdown, allowing continued use in other
/// processes.
void Socket::Server::drop(){
  if (connected()){
    if (sock != -1){
      HIGH_MSG("ServerSocket %d closed", sock);
      errno = EINTR;
      while (::close(sock) != 0 && errno == EINTR){}
      sock = -1;
    }
  }
}// Socket::Server::drop

/// Returns the connected-state for this socket.
/// Note that this function might be slightly behind the real situation.
/// The connection status is updated after every accept attempt, when errors occur
/// and when the socket is closed manually.
/// \returns True if socket is connected, false otherwise.
bool Socket::Server::connected() const{
  return (sock >= 0);
}// Socket::Server::connected

/// Returns internal socket number.
int Socket::Server::getSocket(){
  return sock;
}


#ifdef SSL
static int dTLS_recv(void *s, unsigned char *buf, size_t len){
  return ((Socket::UDPConnection*)s)->dTLSRead(buf, len);
}

static int dTLS_send(void *s, const unsigned char *buf, size_t len){
  return ((Socket::UDPConnection*)s)->dTLSWrite(buf, len);
}
#endif


/// Create a new UDP Socket.
/// Will attempt to create an IPv6 UDP socket, on fail try a IPV4 UDP socket.
/// If both fail, prints an DLVL_FAIL debug message.
/// \param nonblock Whether the socket should be nonblocking.
Socket::UDPConnection::UDPConnection(bool nonblock){
  init(nonblock);
}// Socket::UDPConnection UDP Contructor

void Socket::UDPConnection::init(bool _nonblock, int _family){
  lastPace = 0;
  boundPort = 0;
  family = _family;
  hasDTLS = false;
  isConnected = false;
  wasEncrypted = false;
  pretendReceive = false;
  ignoreSendErrors = false;
  sock = socket(family, SOCK_DGRAM, 0);
  if (sock == -1 && family == AF_INET6){
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    family = AF_INET;
  }
  if (sock == -1){
    FAIL_MSG("Could not create UDP socket: %s", strerror(errno));
  }else{
    isBlocking = !_nonblock;
    if (_nonblock){setBlocking(!_nonblock);}
    checkRecvBuf();
  }

  {
    // Allow address re-use
    int on = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
  }

  up = 0;
  down = 0;
  hasReceiveData = false;
#ifdef __CYGWIN__
  data.allocate(SOCKETSIZE);
#else
  data.allocate(2048);
#endif
}

void Socket::UDPConnection::assimilate(int _sock){
  if (sock != -1){close();}
  sock = _sock;
  { // Extract socket family
    struct sockaddr_storage fin_addr;
    socklen_t alen = sizeof(fin_addr);
    if (getsockname(sock, (struct sockaddr *)&fin_addr, &alen) == 0){
      family = fin_addr.ss_family;
      if (family == AF_INET6){
        boundPort = ntohs(((struct sockaddr_in6 *)&fin_addr)->sin6_port);
      }else{
        boundPort = ntohs(((struct sockaddr_in *)&fin_addr)->sin_port);
      }
    }
  }
}

#if HAVE_UPSTREAM_MBEDTLS_SRTP
#if MBEDTLS_VERSION_MAJOR > 2
static void dtlsExtractKeyData( void *user, mbedtls_ssl_key_export_type type, const unsigned char *ms, size_t, const unsigned char client_random[32], const unsigned char server_random[32], mbedtls_tls_prf_types tls_prf_type){
#else
static int dtlsExtractKeyData( void *user, const unsigned char *ms, const unsigned char *, size_t, size_t, size_t, const unsigned char client_random[32], const unsigned char server_random[32], mbedtls_tls_prf_types tls_prf_type){
#endif
  Socket::UDPConnection *udpSock = static_cast<Socket::UDPConnection *>(user);
  memcpy(udpSock->master_secret, ms, sizeof(udpSock->master_secret));
  memcpy(udpSock->randbytes, client_random, 32);
  memcpy(udpSock->randbytes + 32, server_random, 32);
  udpSock->tls_prf_type = tls_prf_type;
#if MBEDTLS_VERSION_MAJOR == 2
  return 0;
#endif
}
#endif

#ifdef SSL
void Socket::UDPConnection::initDTLS(mbedtls_x509_crt *cert, mbedtls_pk_context *key){
  hasDTLS = true;
  nextDTLSRead = 0;
  nextDTLSReadLen = 0;

  int r = 0;
  char mbedtls_msg[1024];

  // Null out the contexts before use
  memset((void *)&entropy_ctx, 0x00, sizeof(entropy_ctx));
  memset((void *)&rand_ctx, 0x00, sizeof(rand_ctx));
  memset((void *)&ssl_ctx, 0x00, sizeof(ssl_ctx));
  memset((void *)&ssl_conf, 0x00, sizeof(ssl_conf));
  memset((void *)&cookie_ctx, 0x00, sizeof(cookie_ctx));
  memset((void *)&timer_ctx, 0x00, sizeof(timer_ctx));
  // Initialize contexts
  mbedtls_entropy_init(&entropy_ctx);
  mbedtls_ctr_drbg_init(&rand_ctx);
  mbedtls_ssl_init(&ssl_ctx);
  mbedtls_ssl_config_init(&ssl_conf);
  mbedtls_ssl_cookie_init(&cookie_ctx);

  /* seed and setup the random number generator */
  r = mbedtls_ctr_drbg_seed(&rand_ctx, mbedtls_entropy_func, &entropy_ctx, (const unsigned char *)"mist-srtp", 9);
  if (r){
    mbedtls_strerror(r, mbedtls_msg, sizeof(mbedtls_msg));
    FAIL_MSG("dTLS could not init drbg seed: %s", mbedtls_msg);
    return;
  }

  /* load defaults into our ssl_conf */
  r = mbedtls_ssl_config_defaults(&ssl_conf, MBEDTLS_SSL_IS_SERVER, MBEDTLS_SSL_TRANSPORT_DATAGRAM,
                                  MBEDTLS_SSL_PRESET_DEFAULT);
  if (r){
    mbedtls_strerror(r, mbedtls_msg, sizeof(mbedtls_msg));
    FAIL_MSG("dTLS could not set defaults: %s", mbedtls_msg);
    return;
  }

  mbedtls_ssl_conf_authmode(&ssl_conf, MBEDTLS_SSL_VERIFY_NONE);
  mbedtls_ssl_conf_rng(&ssl_conf, mbedtls_ctr_drbg_random, &rand_ctx);
  mbedtls_ssl_conf_ca_chain(&ssl_conf, cert, NULL);
  mbedtls_ssl_conf_cert_profile(&ssl_conf, &mbedtls_x509_crt_profile_default);
  //mbedtls_ssl_conf_dbg(&ssl_conf, print_mbedtls_debug_message, stdout);
  //mbedtls_debug_set_threshold(10);

  // enable SRTP support (non-fatal on error)
#if !HAVE_UPSTREAM_MBEDTLS_SRTP
  mbedtls_ssl_srtp_profile srtpPro[] ={MBEDTLS_SRTP_AES128_CM_HMAC_SHA1_80, MBEDTLS_SRTP_AES128_CM_HMAC_SHA1_32};
  r = mbedtls_ssl_conf_dtls_srtp_protection_profiles(&ssl_conf, srtpPro, sizeof(srtpPro) / sizeof(srtpPro[0]));
#else
  static mbedtls_ssl_srtp_profile srtpPro[] ={MBEDTLS_TLS_SRTP_AES128_CM_HMAC_SHA1_80, MBEDTLS_TLS_SRTP_AES128_CM_HMAC_SHA1_32, MBEDTLS_TLS_SRTP_UNSET};
  r = mbedtls_ssl_conf_dtls_srtp_protection_profiles(&ssl_conf, srtpPro);
#endif
  if (r){
    mbedtls_strerror(r, mbedtls_msg, sizeof(mbedtls_msg));
    WARN_MSG("dTLS could not set SRTP profiles: %s", mbedtls_msg);
  }

#if HAVE_UPSTREAM_MBEDTLS_SRTP && MBEDTLS_VERSION_MAJOR == 2
  mbedtls_ssl_conf_export_keys_ext_cb(&ssl_conf, dtlsExtractKeyData, this);
#endif

  /* cert certificate chain + key, so we can verify the client-hello signed data */
  r = mbedtls_ssl_conf_own_cert(&ssl_conf, cert, key);
  if (r){
    mbedtls_strerror(r, mbedtls_msg, sizeof(mbedtls_msg));
    FAIL_MSG("dTLS could not set certificate: %s", mbedtls_msg);
    return;
  }

  // cookie setup (to prevent ddos, server-only)
  r = mbedtls_ssl_cookie_setup(&cookie_ctx, mbedtls_ctr_drbg_random, &rand_ctx);
  if (r){
    mbedtls_strerror(r, mbedtls_msg, sizeof(mbedtls_msg));
    FAIL_MSG("dTLS could not set SSL cookie: %s", mbedtls_msg);
    return;
  }
  mbedtls_ssl_conf_dtls_cookies(&ssl_conf, mbedtls_ssl_cookie_write, mbedtls_ssl_cookie_check, &cookie_ctx);

  // setup the ssl context
  r = mbedtls_ssl_setup(&ssl_ctx, &ssl_conf);
  if (r){
    mbedtls_strerror(r, mbedtls_msg, sizeof(mbedtls_msg));
    FAIL_MSG("dTLS could not setup: %s", mbedtls_msg);
    return;
  }

#if MBEDTLS_VERSION_MAJOR > 2
   mbedtls_ssl_set_export_keys_cb(&ssl_ctx, dtlsExtractKeyData, this);
#endif

  // set input/output callbacks
  mbedtls_ssl_set_bio(&ssl_ctx, (void *)this, dTLS_send, dTLS_recv, NULL);
  mbedtls_ssl_set_timer_cb(&ssl_ctx, &timer_ctx, mbedtls_timing_set_delay, mbedtls_timing_get_delay);

  // set transport ID (non-fatal on error)
  r = mbedtls_ssl_set_client_transport_id(&ssl_ctx, (const unsigned char *)"mist", 4);
  if (r){
    mbedtls_strerror(r, mbedtls_msg, sizeof(mbedtls_msg));
    WARN_MSG("dTLS could not set transport ID: %s", mbedtls_msg);
  }
}

void Socket::UDPConnection::deinitDTLS(){
  if (hasDTLS){
    mbedtls_entropy_free(&entropy_ctx);
    mbedtls_ctr_drbg_free(&rand_ctx);
    mbedtls_ssl_free(&ssl_ctx);
    mbedtls_ssl_config_free(&ssl_conf);
    mbedtls_ssl_cookie_free(&cookie_ctx);
    hasDTLS = true;
  }
}

int Socket::UDPConnection::dTLSRead(unsigned char *buf, size_t _len){
  if (!nextDTLSReadLen){return MBEDTLS_ERR_SSL_WANT_READ;}
  size_t len = _len;
  if (len > nextDTLSReadLen){len = nextDTLSReadLen;}
  memcpy(buf, nextDTLSRead, len);
  nextDTLSReadLen = 0;
  return len;
}

int Socket::UDPConnection::dTLSWrite(const unsigned char *buf, size_t len){
  sendPaced((const char *)buf, len, false);
  return len;
}

void Socket::UDPConnection::dTLSReset(){
  char mbedtls_msg[1024];
  int r = mbedtls_ssl_session_reset(&ssl_ctx);
  if (r){
    mbedtls_strerror(r, mbedtls_msg, sizeof(mbedtls_msg));
    FAIL_MSG("dTLS could not reset session: %s", mbedtls_msg);
    return;
  }

  // set transport ID (non-fatal on error)
  r = mbedtls_ssl_set_client_transport_id(&ssl_ctx, (const unsigned char *)"mist", 4);
  if (r){
    mbedtls_strerror(r, mbedtls_msg, sizeof(mbedtls_msg));
    WARN_MSG("dTLS could not set transport ID: %s", mbedtls_msg);
  }
}
#endif //if SSL

///Checks if the UDP receive buffer is at least 1 mbyte, attempts to increase and warns user through log message on failure.
void Socket::UDPConnection::checkRecvBuf(){
  if (sock == -1){return;}
  int recvbuf = 0;
  int origbuf = 0;
  socklen_t slen = sizeof(recvbuf);
  getsockopt(sock, SOL_SOCKET, SO_RCVBUF, (void*)&recvbuf, &slen);
  origbuf = recvbuf;
  if (recvbuf < 1024*1024){
    recvbuf = 1024*1024;
    setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (void*)&recvbuf, sizeof(recvbuf));
    slen = sizeof(recvbuf);
    getsockopt(sock, SOL_SOCKET, SO_RCVBUF, (void*)&recvbuf, &slen);
#ifdef __linux__
#ifndef __CYGWIN__
    if (recvbuf < 1024*1024){
      recvbuf = 1024*1024;
      setsockopt(sock, SOL_SOCKET, SO_RCVBUFFORCE, (void*)&recvbuf, sizeof(recvbuf));
      slen = sizeof(recvbuf);
      getsockopt(sock, SOL_SOCKET, SO_RCVBUF, (void*)&recvbuf, &slen);
    }
#endif
#endif
    if (recvbuf < 200*1024){
      recvbuf = 200*1024;
      setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (void*)&recvbuf, sizeof(recvbuf));
      slen = sizeof(recvbuf);
      getsockopt(sock, SOL_SOCKET, SO_RCVBUF, (void*)&recvbuf, &slen);
#ifdef __linux__
#ifndef __CYGWIN__
      if (recvbuf < 200*1024){
        recvbuf = 200*1024;
        setsockopt(sock, SOL_SOCKET, SO_RCVBUFFORCE, (void*)&recvbuf, sizeof(recvbuf));
        slen = sizeof(recvbuf);
        getsockopt(sock, SOL_SOCKET, SO_RCVBUF, (void*)&recvbuf, &slen);
      }
#endif
#endif
    }
    if (recvbuf < 200*1024){
      WARN_MSG("Your UDP receive buffer is set < 200 kbyte (%db) and the kernel denied our request for an increase. It's recommended to set your net.core.rmem_max setting to at least 200 kbyte for best results.", origbuf);
    }
  }
}

/// Copies a UDP socket, re-allocating local copies of any needed structures.
/// The data/data_size/data_len variables are *not* copied over.
Socket::UDPConnection::UDPConnection(const UDPConnection &o){
  init(!o.isBlocking, o.family);
  INFO_MSG("Copied socket of type %s", addrFam(o.family));
  if (o.destAddr.size()){destAddr = o.destAddr;}
  if (o.recvAddr.size()){recvAddr = o.recvAddr;}
  if (o.data.size()){
    data = o.data;
    pretendReceive = true;
  }
  hasReceiveData = o.hasReceiveData;
}

/// Close the UDP socket
void Socket::UDPConnection::close(){
  if (sock != -1){
    errno = EINTR;
    while (::close(sock) != 0 && errno == EINTR){}
    sock = -1;
  }
}

/// Closes the UDP socket, cleans up any memory allocated by the socket.
Socket::UDPConnection::~UDPConnection(){
  close();
#ifdef SSL
  deinitDTLS();
#endif
}


bool Socket::UDPConnection::operator==(const Socket::UDPConnection& b) const{
  // UDP sockets are equal if they refer to the same underlying socket or are both closed
  if (sock == b.sock){return true;}
  // If either is closed (and the other is not), not equal.
  if (sock == -1 || b.sock == -1){return false;}
  size_t recvSize = recvAddr.size();
  if (b.recvAddr.size() < recvSize){recvSize = b.recvAddr.size();}
  size_t destSize = destAddr.size();
  if (b.destAddr.size() < destSize){destSize = b.destAddr.size();}
  // They are equal if they hold the same local and remote address.
  if (recvSize && destSize && destAddr && b.destAddr && recvAddr && b.recvAddr){
    if (!memcmp(recvAddr, b.recvAddr, recvSize) && !memcmp(destAddr, b.destAddr, destSize)){
      return true;
    }
  }
  // All other cases, not equal
  return false;
}

Socket::UDPConnection::operator bool() const{return sock != -1;}

// Sets socket family type (to IPV4 or IPV6) (AF_INET=2, AF_INET6=10)
void Socket::UDPConnection::setSocketFamily(int AF_TYPE){\
  INFO_MSG("Switching UDP socket from %s to %s", addrFam(family), addrFam(AF_TYPE));
  family = AF_TYPE;
}

/// Allocates enough space for the largest type of address we support, so that receive calls can write to it.
void Socket::UDPConnection::allocateDestination(){
  if (!destAddr.size()){
    destAddr.truncate(0);
    destAddr.allocate(sizeof(sockaddr_in6));
    memset(destAddr, 0, sizeof(sockaddr_in6));
    ((struct sockaddr *)(char*)destAddr)->sa_family = AF_UNSPEC;
    destAddr.append(0, sizeof(sockaddr_in6));
  }
  if (!recvAddr.size()){
    recvAddr.truncate(0);
    recvAddr.allocate(sizeof(sockaddr_in6));
    memset(recvAddr, 0, sizeof(sockaddr_in6));
    ((struct sockaddr *)(char*)recvAddr)->sa_family = AF_UNSPEC;
    recvAddr.append(0, sizeof(sockaddr_in6));
  }
#ifdef HASPKTINFO
  const int opt = 1;
  if (setsockopt(sock, IPPROTO_IP, IP_PKTINFO, &opt, sizeof(opt))){
    WARN_MSG("Could not set IPv4 packet info receiving enabled!");
  }
  if (family == AF_INET6){
    if (setsockopt(sock, IPPROTO_IPV6, IPV6_RECVPKTINFO, &opt, sizeof(opt))){
      WARN_MSG("Could not set IPv6 packet info receiving enabled!");
    }
  }
#endif
}

/// Stores the properties of the receiving end of this UDP socket.
/// This will be the receiving end for all SendNow calls.
void Socket::UDPConnection::SetDestination(std::string destIp, uint32_t port){
  DONTEVEN_MSG("Setting destination to %s:%u", destIp.c_str(), port);

  std::deque<std::string> addrs = getAddrs(destIp, port, family);
  for (std::deque<std::string>::iterator it = addrs.begin(); it != addrs.end(); ++it){
    if (setDestination((sockaddr*)it->data(), it->size())){return;}
  }
  destAddr.truncate(0);
  allocateDestination();
  FAIL_MSG("Could not set destination for UDP socket: %s:%d", destIp.c_str(), port);
}// Socket::UDPConnection SetDestination

bool Socket::UDPConnection::setDestination(sockaddr * addr, size_t size){
  // UDP sockets can on-the-fly switch between IPv4/IPv6 if necessary
  if (family != addr->sa_family){
    if (ignoreSendErrors){return false;}
    WARN_MSG("Switching UDP socket from %s to %s", addrFam(family), addrFam(((sockaddr*)(char*)destAddr)->sa_family));
    close();
    family = addr->sa_family;
    sock = socket(family, SOCK_DGRAM, 0);
    {
      // Allow address re-use
      int on = 1;
      setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    }
    if (family == AF_INET6){
      const int optval = 0;
      if (setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, &optval, sizeof(optval)) < 0){
        WARN_MSG("Could not set IPv6 UDP socket to be dual-stack! %s", strerror(errno));
      }
    }
    checkRecvBuf();
    if (boundPort){
      INFO_MSG("Rebinding to %s:%d %s", boundAddr.c_str(), boundPort, boundMulti.c_str());
      bind(boundPort, boundAddr, boundMulti);
    }
  }
  hasReceiveData = false;
  destAddr.assign(addr, size);
  {
    std::string trueDest;
    uint32_t truePort;
    GetDestination(trueDest, truePort);
    HIGH_MSG("Set UDP destination to %s:%d (%s)", trueDest.c_str(), truePort, addrFam(family));
  }
  return true;
}

const Util::ResizeablePointer & Socket::UDPConnection::getRemoteAddr() const{
  return destAddr;
}

/// Gets the properties of the receiving end of this UDP socket.
/// This will be the receiving end for all SendNow calls.
void Socket::UDPConnection::GetDestination(std::string &destIp, uint32_t &port){
  if (!destAddr.size()){
    destIp = "";
    port = 0;
    return;
  }
  char addr_str[INET6_ADDRSTRLEN + 1];
  addr_str[INET6_ADDRSTRLEN] = 0; // set last byte to zero, to prevent walking out of the array
  if (((struct sockaddr *)(char*)destAddr)->sa_family == AF_INET6){
    if (inet_ntop(AF_INET6, &(((struct sockaddr_in6 *)(char*)destAddr)->sin6_addr), addr_str, INET6_ADDRSTRLEN) != 0){
      destIp = addr_str;
      port = ntohs(((struct sockaddr_in6 *)(char*)destAddr)->sin6_port);
      return;
    }
  }
  if (((struct sockaddr_in *)(char*)destAddr)->sin_family == AF_INET){
    if (inet_ntop(AF_INET, &(((struct sockaddr_in *)(char*)destAddr)->sin_addr), addr_str, INET6_ADDRSTRLEN) != 0){
      destIp = addr_str;
      port = ntohs(((struct sockaddr_in *)(char*)destAddr)->sin_port);
      return;
    }
  }
  destIp = "";
  port = 0;
  FAIL_MSG("Could not get destination for UDP socket");
}// Socket::UDPConnection GetDestination

/// Gets the properties of the receiving end of the local UDP socket.
/// This will be the sending end for all SendNow calls.
void Socket::UDPConnection::GetLocalDestination(std::string &destIp, uint32_t &port){
  if (!recvAddr.size()){
    destIp = "";
    port = 0;
    return;
  }
  char addr_str[INET6_ADDRSTRLEN + 1];
  addr_str[INET6_ADDRSTRLEN] = 0; // set last byte to zero, to prevent walking out of the array
  if (((struct sockaddr *)(char*)recvAddr)->sa_family == AF_INET6){
    if (inet_ntop(AF_INET6, &(((struct sockaddr_in6 *)(char*)recvAddr)->sin6_addr), addr_str, INET6_ADDRSTRLEN) != 0){
      destIp = addr_str;
      port = ntohs(((struct sockaddr_in6 *)(char*)recvAddr)->sin6_port);
      return;
    }
  }
  if (((struct sockaddr *)(char*)recvAddr)->sa_family == AF_INET){
    if (inet_ntop(AF_INET, &(((struct sockaddr_in *)(char*)recvAddr)->sin_addr), addr_str, INET6_ADDRSTRLEN) != 0){
      destIp = addr_str;
      port = ntohs(((struct sockaddr_in *)(char*)recvAddr)->sin_port);
      return;
    }
  }
  destIp = "";
  port = 0;
  FAIL_MSG("Could not get destination for UDP socket");
}// Socket::UDPConnection GetDestination

/// Gets the properties of the receiving end of this UDP socket.
/// This will be the receiving end for all SendNow calls.
std::string Socket::UDPConnection::getBinDestination(){
  std::string binList;
  if (destAddr.size()){binList = getIPv6BinAddr(*(sockaddr_in6*)(char*)destAddr);}
  if (binList.size() < 16){return std::string("\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000", 16);}
  return binList.substr(0, 16);
}// Socket::UDPConnection GetDestination

/// Returns the port number of the receiving end of this socket.
/// Returns 0 on error.
uint32_t Socket::UDPConnection::getDestPort() const{
  if (!destAddr.size()){return 0;}
  if (((const struct sockaddr *)(const char*)destAddr)->sa_family == AF_INET6){
    return ntohs(((const struct sockaddr_in6 *)(const char*)destAddr)->sin6_port);
  }
  if (((const struct sockaddr *)(const char*)destAddr)->sa_family == AF_INET){
    return ntohs(((const struct sockaddr_in *)(const char*)destAddr)->sin_port);
  }
  return 0;
}

/// Sets the socket to be blocking if the parameters is true.
/// Sets the socket to be non-blocking otherwise.
void Socket::UDPConnection::setBlocking(bool blocking){
  if (sock >= 0){
    setFDBlocking(sock, blocking);
    isBlocking = blocking;
  }
}

void Socket::UDPConnection::setIgnoreSendErrors(bool ign){
  ignoreSendErrors = ign;
}

/// Sends a UDP datagram using the buffer sdata.
/// This function simply calls SendNow(const char*, size_t)
void Socket::UDPConnection::SendNow(const std::string &sdata){
  SendNow(sdata.c_str(), sdata.size());
}

/// Sends a UDP datagram using the buffer sdata.
/// sdata is required to be NULL-terminated.
/// This function simply calls SendNow(const char*, size_t)
void Socket::UDPConnection::SendNow(const char *sdata){
  int len = strlen(sdata);
  SendNow(sdata, len);
}

/// Sends a UDP datagram using the buffer sdata of length len.
/// Does not do anything if len < 1.
/// Prints an DLVL_FAIL level debug message if sending failed.
void Socket::UDPConnection::SendNow(const char *sdata, size_t len){
  SendNow(sdata, len, (sockaddr*)(char*)destAddr, destAddr.size());
}

/// Sends a UDP datagram using the buffer sdata of length len.
/// Does not do anything if len < 1.
/// Prints an DLVL_FAIL level debug message if sending failed.
void Socket::UDPConnection::SendNow(const char *sdata, size_t len, sockaddr * dAddr, size_t dAddrLen){
  if (len < 1 || sock == -1){return;}
  if (isConnected){
    int r = send(sock, sdata, len, 0);
    if (r > 0){
      up += r;
    }else{
      if (ignoreSendErrors){return;}
      if (errno == EDESTADDRREQ){
        close();
        return;
      }
      FAIL_MSG("Could not send UDP data through %d: %s", sock, strerror(errno));
    }
    return;
  }
#ifdef HASPKTINFO
  if (hasReceiveData && recvAddr.size()){
    msghdr mHdr;
    char msg_control[0x100];
    iovec iovec;
    iovec.iov_base = (void*)sdata;
    iovec.iov_len = len;
    mHdr.msg_name = dAddr;
    mHdr.msg_namelen = dAddrLen;
    mHdr.msg_iov = &iovec;
    mHdr.msg_iovlen = 1;
    mHdr.msg_control = msg_control;
    mHdr.msg_controllen = sizeof(msg_control);
    mHdr.msg_flags = 0;
    int cmsg_space = 0;
    cmsghdr * cmsg = CMSG_FIRSTHDR(&mHdr);
    if (family == AF_INET){
      cmsg->cmsg_level = IPPROTO_IP;
      cmsg->cmsg_type = IP_PKTINFO;

      struct in_pktinfo in_pktinfo;
      memcpy(&(in_pktinfo.ipi_spec_dst), &(((sockaddr_in*)(char*)recvAddr)->sin_family), sizeof(in_pktinfo.ipi_spec_dst));
      in_pktinfo.ipi_ifindex = recvInterface;
      cmsg->cmsg_len = CMSG_LEN(sizeof(in_pktinfo));
      *(struct in_pktinfo*)CMSG_DATA(cmsg) = in_pktinfo;
      cmsg_space += CMSG_SPACE(sizeof(in_pktinfo));
    }else if (family == AF_INET6){
      cmsg->cmsg_level = IPPROTO_IPV6;
      cmsg->cmsg_type = IPV6_PKTINFO;

      struct in6_pktinfo in6_pktinfo;
      memcpy(&(in6_pktinfo.ipi6_addr), &(((sockaddr_in6*)(char*)recvAddr)->sin6_addr), sizeof(in6_pktinfo.ipi6_addr));
      in6_pktinfo.ipi6_ifindex = recvInterface;
      cmsg->cmsg_len = CMSG_LEN(sizeof(in6_pktinfo));
      *(struct in6_pktinfo*)CMSG_DATA(cmsg) = in6_pktinfo;
      cmsg_space += CMSG_SPACE(sizeof(in6_pktinfo));
    }
    mHdr.msg_controllen = cmsg_space;

    int r = sendmsg(sock, &mHdr, 0);
    if (r > 0){
      up += r;
    }else{
      if (errno != ENETUNREACH && !ignoreSendErrors){
        FAIL_MSG("Could not send UDP data through %d: %s", sock, strerror(errno));
      }
    }
    return;
  }else{
#endif
    int r = sendto(sock, sdata, len, 0, dAddr, dAddrLen);
    if (r > 0){
      up += r;
    }else{
      if (errno != ENETUNREACH && !ignoreSendErrors){
        FAIL_MSG("Could not send UDP data through %d: %s", sock, strerror(errno));
      }
    }
#ifdef HASPKTINFO
  }
#endif
}

/// Queues sdata, len for sending over this socket.
/// If there has been enough time since the last packet, sends immediately.
/// Warning: never call sendPaced for the same socket from a different thread!
/// Note: Only actually encrypts if initDTLS was called in the past.
void Socket::UDPConnection::sendPaced(const char *sdata, size_t len, bool encrypt){
#ifdef SSL 
  if (hasDTLS && encrypt){
#if MBEDTLS_VERSION_MAJOR > 2
    if (!mbedtls_ssl_is_handshake_over(&ssl_ctx)){
#else
    if (ssl_ctx.state != MBEDTLS_SSL_HANDSHAKE_OVER){
#endif
      WARN_MSG("Attempting to write encrypted data before handshake completed! Data was thrown away.");
      return;
    }
    int write = mbedtls_ssl_write(&ssl_ctx, (unsigned char*)sdata, len);
    if (write <= 0){WARN_MSG("Could not write DTLS packet!");}
  }else{
#endif
    if (!paceQueue.size() && (!lastPace || Util::getMicros(lastPace) > 10000)){
      SendNow(sdata, len);
      lastPace = Util::getMicros();
    }else{
      paceQueue.push_back(Util::ResizeablePointer());
      paceQueue.back().assign(sdata, len);
      // Try to send a packet, if time allows
      //sendPaced(0);
    }
#ifdef SSL
  }
#endif
}

// Gets time in microseconds until next sendPaced call would send something
size_t Socket::UDPConnection::timeToNextPace(uint64_t uTime){
  size_t qSize = paceQueue.size();
  if (!qSize){return std::string::npos;} // No queue? No time. Return highest possible value.
  if (!uTime){uTime = Util::getMicros();}
  uint64_t paceWait = uTime - lastPace; // Time we've waited so far already

  // Target clearing the queue in 25ms at most.
  uint64_t targetTime = 25000 / qSize;
  // If this slows us to below 1 packet per 5ms, go that speed instead.
  if (targetTime > 5000){targetTime = 5000;}
  // If the wait is over, send now.
  if (paceWait >= targetTime){return 0;}
  // Return remaining wait time
  return targetTime - paceWait;
}

/// Spends uSendWindow microseconds either sending paced packets or sleeping, whichever is more appropriate
/// Warning: never call sendPaced for the same socket from a different thread!
void Socket::UDPConnection::sendPaced(uint64_t uSendWindow){
  uint64_t currPace = Util::getMicros();
  uint64_t uTime = currPace;
  do{
    uint64_t sleepTime = uSendWindow - (uTime - currPace);
    uint64_t nextPace = timeToNextPace(uTime);
    if (sleepTime > nextPace){sleepTime = nextPace;}

    // Not sleeping? Send now!
    if (!sleepTime){
      SendNow(*paceQueue.begin(), paceQueue.begin()->size());
      paceQueue.pop_front();
      lastPace = uTime;
      continue;
    }

    {
      // Use select to wait until a packet arrives or until the next packet should be sent
      fd_set rfds;
      struct timeval T;
      T.tv_sec = sleepTime / 1000000;
      T.tv_usec = sleepTime % 1000000;
      // Watch configured FD's for input
      FD_ZERO(&rfds);
      int maxFD = getSock();
      FD_SET(maxFD, &rfds);
      int r = select(maxFD + 1, &rfds, NULL, NULL, &T);
      // If we can read the socket, immediately return and stop waiting
      if (r > 0){return;}
    }
    uTime = Util::getMicros();
  }while(uTime - currPace < uSendWindow);
}

std::string Socket::UDPConnection::getBoundAddress(){
  std::string boundaddr;
  uint32_t boundport;
  Socket::getSocketName(sock, boundaddr, boundport);
  return boundaddr;
}

uint16_t Socket::UDPConnection::getBoundPort() const{
  return boundPort;
}

/// Bind to a port number, returning the bound port.
/// If that fails, returns zero.
/// \arg port Port to bind to, required.
/// \arg iface Interface address to listen for packets on (may be multicast address)
/// \arg multicastInterfaces Comma-separated list of interfaces to listen on for multicast packets.
/// Optional, left out means automatically chosen by kernel. \return Actually bound port number, or
/// zero on error.
uint16_t Socket::UDPConnection::bind(int port, std::string iface, const std::string &multicastInterfaces){
  close(); // we open a new socket for each attempt
  int addr_ret;
  bool multicast = false;
  bool repeatWithIPv4 = false;
  struct addrinfo hints, *addr_result, *rp;
  memset(&hints, 0, sizeof(hints));
  hints.ai_flags = AI_ADDRCONFIG | AI_PASSIVE | AI_V4MAPPED;
  if (destAddr.size()){
    hints.ai_family = ((struct sockaddr *)(char*)destAddr)->sa_family;
  }else{
    hints.ai_family = AF_INET6;
    repeatWithIPv4 = true;
  }

  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_protocol = IPPROTO_UDP;

  std::stringstream ss;
  ss << port;

repeatAddressFinding:

  if (iface == "0.0.0.0" || iface.length() == 0){
    if ((addr_ret = getaddrinfo(0, ss.str().c_str(), &hints, &addr_result)) != 0){
      FAIL_MSG("Could not resolve %s for UDP: %s", iface.c_str(), gai_strmagic(addr_ret));
      if (repeatWithIPv4 && hints.ai_family != AF_INET){
        hints.ai_family = AF_INET;
        goto repeatAddressFinding;
      }
      return 0;
    }
  }else{
    if ((addr_ret = getaddrinfo(iface.c_str(), ss.str().c_str(), &hints, &addr_result)) != 0){
      FAIL_MSG("Could not resolve %s for UDP: %s", iface.c_str(), gai_strmagic(addr_ret));
      if (repeatWithIPv4 && hints.ai_family != AF_INET){
        hints.ai_family = AF_INET;
        goto repeatAddressFinding;
      }
      return 0;
    }
  }

  std::string err_str;
  uint16_t portNo = 0;
  for (rp = addr_result; rp != NULL; rp = rp->ai_next){
    sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    if (sock == -1){continue;}
    {
      // Allow address re-use
      int on = 1;
      setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    }
    if (rp->ai_family == AF_INET6){
      const int optval = 0;
      if (setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, &optval, sizeof(optval)) < 0){
        WARN_MSG("Could not set IPv6 UDP socket to be dual-stack! %s", strerror(errno));
      }
    }
    checkRecvBuf();
    char human_addr[INET6_ADDRSTRLEN];
    char human_port[16];
    getnameinfo(rp->ai_addr, rp->ai_addrlen, human_addr, INET6_ADDRSTRLEN, human_port, 16,
                NI_NUMERICHOST | NI_NUMERICSERV);
    MEDIUM_MSG("Attempting bind to %s:%s (%s)", human_addr, human_port, addrFam(rp->ai_family));
    family = rp->ai_family;
    hints.ai_family = family;
    if (family == AF_INET6){
      sockaddr_in6 *addr6 = (sockaddr_in6 *)(rp->ai_addr);
      if (memcmp((char *)&(addr6->sin6_addr), "\000\000\000\000\000\000\000\000\000\000\377\377", 12) == 0){
        // IPv6-mapped IPv4 address - 13th byte ([12]) holds the first IPv4 byte
        multicast = (((char *)&(addr6->sin6_addr))[12] & 0xF0) == 0xE0;
      }else{
        //"normal" IPv6 address - prefix ff00::/8
        multicast = (((char *)&(addr6->sin6_addr))[0] == 0xFF);
      }
    }else{
      sockaddr_in *addr4 = (sockaddr_in *)(rp->ai_addr);
      // multicast has a "1110" bit prefix
      multicast = (((char *)&(addr4->sin_addr))[0] & 0xF0) == 0xE0;
#ifdef __CYGWIN__
      if (multicast){((sockaddr_in *)rp->ai_addr)->sin_addr.s_addr = htonl(INADDR_ANY);}
#endif
    }
    if (multicast){
      const int optval = 1;
      if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0){
        WARN_MSG("Could not set multicast UDP socket re-use! %s", strerror(errno));
      }
    }
    if (::bind(sock, rp->ai_addr, rp->ai_addrlen) == 0){
      // get port number
      struct sockaddr_storage fin_addr;
      socklen_t alen = sizeof(fin_addr);
      if (getsockname(sock, (struct sockaddr *)&fin_addr, &alen) == 0){
        if (family == AF_INET6){
          portNo = ntohs(((struct sockaddr_in6 *)&fin_addr)->sin6_port);
        }else{
          portNo = ntohs(((struct sockaddr_in *)&fin_addr)->sin_port);
        }
      }
      boundAddr = iface;
      boundMulti = multicastInterfaces;
      boundPort = portNo;
      INFO_MSG("UDP bind success %d on %s:%u (%s)", sock, human_addr, portNo, addrFam(rp->ai_family));
      break;
    }
    if (err_str.size()){err_str += ", ";}
    err_str += human_addr;
    err_str += ":";
    err_str += strerror(errno);
    close(); // we open a new socket for each attempt
  }
  freeaddrinfo(addr_result);
  if (sock == -1){
    FAIL_MSG("Could not open %s for UDP: %s", iface.c_str(), err_str.c_str());
    if (repeatWithIPv4 && hints.ai_family != AF_INET){
      hints.ai_family = AF_INET;
      goto repeatAddressFinding;
    }
    return 0;
  }

  // handle multicast membership(s)
  if (multicast){
    struct ipv6_mreq mreq6;
    struct ip_mreq mreq4;
    memset(&mreq4, 0, sizeof(mreq4));
    memset(&mreq6, 0, sizeof(mreq6));
    struct addrinfo *reslocal, *resmulti;
    if ((addr_ret = getaddrinfo(iface.c_str(), 0, &hints, &resmulti)) != 0){
      WARN_MSG("Unable to parse multicast address: %s", gai_strmagic(addr_ret));
      close();
      return 0;
    }

    if (!multicastInterfaces.length()){
      if (family == AF_INET6){
        memcpy(&mreq6.ipv6mr_multiaddr, &((sockaddr_in6 *)resmulti->ai_addr)->sin6_addr,
               sizeof(mreq6.ipv6mr_multiaddr));
        if (setsockopt(sock, IPPROTO_IPV6, IPV6_JOIN_GROUP, (char *)&mreq6, sizeof(mreq6)) != 0){
          FAIL_MSG("Unable to register for IPv6 multicast on all interfaces: %s", strerror(errno));
          close();
        }
      }else{
        mreq4.imr_multiaddr = ((sockaddr_in *)resmulti->ai_addr)->sin_addr;
        mreq4.imr_interface.s_addr = INADDR_ANY;
        if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *)&mreq4, sizeof(mreq4)) != 0){
          FAIL_MSG("Unable to register for IPv4 multicast on all interfaces: %s", strerror(errno));
          close();
        }
      }
    }else{
      size_t nxtPos = std::string::npos;
      bool atLeastOne = false;
      for (size_t loc = 0; loc != std::string::npos; loc = (nxtPos == std::string::npos ? nxtPos : nxtPos + 1)){
        nxtPos = multicastInterfaces.find(',', loc);
        std::string curIface =
            multicastInterfaces.substr(loc, (nxtPos == std::string::npos ? nxtPos : nxtPos - loc));
        // do a bit of filtering for IPv6, removing the []-braces, if any
        if (curIface[0] == '['){
          if (curIface[curIface.size() - 1] == ']'){
            curIface = curIface.substr(1, curIface.size() - 2);
          }else{
            curIface = curIface.substr(1, curIface.size() - 1);
          }
        }
        if (family == AF_INET6){
          INFO_MSG("Registering for IPv6 multicast on interface %s", curIface.c_str());
          if ((addr_ret = getaddrinfo(curIface.c_str(), 0, &hints, &reslocal)) != 0){
            WARN_MSG("Unable to resolve IPv6 interface address %s: %s", curIface.c_str(), gai_strmagic(addr_ret));
            continue;
          }
          memcpy(&mreq6.ipv6mr_multiaddr, &((sockaddr_in6 *)resmulti->ai_addr)->sin6_addr,
                 sizeof(mreq6.ipv6mr_multiaddr));
          mreq6.ipv6mr_interface = ((sockaddr_in6 *)reslocal->ai_addr)->sin6_scope_id;
          if (setsockopt(sock, IPPROTO_IPV6, IPV6_JOIN_GROUP, (char *)&mreq6, sizeof(mreq6)) != 0){
            FAIL_MSG("Unable to register for IPv6 multicast on interface %s (%u): %s", curIface.c_str(),
                     ((sockaddr_in6 *)reslocal->ai_addr)->sin6_scope_id, strerror(errno));
          }else{
            atLeastOne = true;
          }
        }else{
          INFO_MSG("Registering for IPv4 multicast on interface %s", curIface.c_str());
          if ((addr_ret = getaddrinfo(curIface.c_str(), 0, &hints, &reslocal)) != 0){
            WARN_MSG("Unable to resolve IPv4 interface address %s: %s", curIface.c_str(), gai_strmagic(addr_ret));
            continue;
          }
          mreq4.imr_multiaddr = ((sockaddr_in *)resmulti->ai_addr)->sin_addr;
          mreq4.imr_interface = ((sockaddr_in *)reslocal->ai_addr)->sin_addr;
          if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *)&mreq4, sizeof(mreq4)) != 0){
            FAIL_MSG("Unable to register for IPv4 multicast on interface %s: %s", curIface.c_str(),
                     strerror(errno));
          }else{
            atLeastOne = true;
          }
        }
        if (!atLeastOne){close();}
        freeaddrinfo(reslocal); // free resolved interface addr
      }// loop over all interfaces
    }
    freeaddrinfo(resmulti); // free resolved multicast addr
  }
  return portNo;
}

bool Socket::UDPConnection::connect(){
  if (!recvAddr.size() || !destAddr.size()){
    WARN_MSG("Attempting to connect a UDP socket without local and/or remote address!");
    return false;
  }

  {
    std::string destIp;
    uint32_t port = 0;
    char addr_str[INET6_ADDRSTRLEN + 1];
    if (((struct sockaddr *)(char*)recvAddr)->sa_family == AF_INET6){
      if (inet_ntop(AF_INET6, &(((struct sockaddr_in6 *)(char*)recvAddr)->sin6_addr), addr_str, INET6_ADDRSTRLEN) != 0){
        destIp = addr_str;
        port = ntohs(((struct sockaddr_in6 *)(char*)recvAddr)->sin6_port);
      }
    }
    if (((struct sockaddr *)(char*)recvAddr)->sa_family == AF_INET){
      if (inet_ntop(AF_INET, &(((struct sockaddr_in *)(char*)recvAddr)->sin_addr), addr_str, INET6_ADDRSTRLEN) != 0){
        destIp = addr_str;
        port = ntohs(((struct sockaddr_in *)(char*)recvAddr)->sin_port);
      }
    }
    int ret = ::bind(sock, (const struct sockaddr*)(char*)recvAddr, recvAddr.size());
    if (!ret){
      INFO_MSG("Bound socket %d to %s:%" PRIu32, sock, destIp.c_str(), port);
    }else{
      FAIL_MSG("Failed to bind socket %d (%s) %s:%" PRIu32 ": %s", sock, addrFam(((struct sockaddr *)(char*)recvAddr)->sa_family), destIp.c_str(), port, strerror(errno));
      return false;
    }
  }

  {
    std::string destIp;
    uint32_t port;
    char addr_str[INET6_ADDRSTRLEN + 1];
    if (((struct sockaddr *)(char*)destAddr)->sa_family == AF_INET6){
      if (inet_ntop(AF_INET6, &(((struct sockaddr_in6 *)(char*)destAddr)->sin6_addr), addr_str, INET6_ADDRSTRLEN) != 0){
        destIp = addr_str;
        port = ntohs(((struct sockaddr_in6 *)(char*)destAddr)->sin6_port);
      }
    }
    if (((struct sockaddr *)(char*)destAddr)->sa_family == AF_INET){
      if (inet_ntop(AF_INET, &(((struct sockaddr_in *)(char*)destAddr)->sin_addr), addr_str, INET6_ADDRSTRLEN) != 0){
        destIp = addr_str;
        port = ntohs(((struct sockaddr_in *)(char*)destAddr)->sin_port);
      }
    }
    int ret = ::connect(sock, (const struct sockaddr*)(char*)destAddr, destAddr.size());
    if (!ret){
      INFO_MSG("Connected socket to %s:%" PRIu32, destIp.c_str(), port);
    }else{
      FAIL_MSG("Failed to connect socket to %s:%" PRIu32 ": %s", destIp.c_str(), port, strerror(errno));
      return false;
    }
  }
  isConnected = true;
  return true;
}


/// Attempt to receive a UDP packet.
/// This will automatically allocate or resize the internal data buffer if needed.
/// If a packet is received, it will be placed in the "data" member, with it's length in "data_len".
/// \return True if a packet was received, false otherwise.
bool Socket::UDPConnection::Receive(){
  if (pretendReceive){
    pretendReceive = false;
    return onData();
  }
  if (sock == -1){return false;}
  data.truncate(0);
  if (isConnected){
    int r = recv(sock, data, data.rsize(), MSG_TRUNC | MSG_DONTWAIT);
    if (r == -1){
      if (errno != EAGAIN){
        INFO_MSG("UDP receive: %d (%s)", errno, strerror(errno));
        if (errno == ECONNREFUSED){close();}
      }
      return false;
    }
    if (r > 0){
      data.append(0, r);
      down += r;
      if (data.rsize() < (unsigned int)r){
        INFO_MSG("Doubling UDP socket buffer from %" PRIu32 " to %" PRIu32, data.rsize(), data.rsize()*2);
        data.allocate(data.rsize()*2);
      }
      return onData();
    }
    return false;
  }
  sockaddr_in6 addr;
  socklen_t destsize = sizeof(addr);
  //int r = recvfrom(sock, data, data.rsize(), MSG_TRUNC | MSG_DONTWAIT, (sockaddr *)&addr, &destsize);
  msghdr mHdr;
  memset(&mHdr, 0, sizeof(mHdr));
  char ctrl[0x100];
  iovec dBufs;
  dBufs.iov_base = data;
  dBufs.iov_len = data.rsize();
  mHdr.msg_name = &addr;
  mHdr.msg_namelen = destsize;
  mHdr.msg_control = ctrl;
  mHdr.msg_controllen = 0x100;
  mHdr.msg_iov = &dBufs;
  mHdr.msg_iovlen = 1;
  int r = recvmsg(sock, &mHdr, MSG_TRUNC | MSG_DONTWAIT);
  destsize = mHdr.msg_namelen;
  if (r == -1){
    if (errno != EAGAIN){INFO_MSG("UDP receive: %d (%s)", errno, strerror(errno));}
    return false;
  }
  if (destAddr.size() && destsize){destAddr.assign(&addr, destsize);}
#ifdef HASPKTINFO
  if (recvAddr.size()){
    for ( struct cmsghdr *cmsg = CMSG_FIRSTHDR(&mHdr); cmsg != NULL; cmsg = CMSG_NXTHDR(&mHdr, cmsg)){
      if (cmsg->cmsg_level == IPPROTO_IP && cmsg->cmsg_type == IP_PKTINFO){
        struct in_pktinfo* pi = (in_pktinfo*)CMSG_DATA(cmsg);
        if (family == AF_INET6){
          struct sockaddr_in6 * recvCast = (sockaddr_in6*)(char*)recvAddr;
          recvCast->sin6_port = htons(boundPort);
          recvCast->sin6_family = AF_INET6;
          memcpy(((char*)&(recvCast->sin6_addr)) + 12, &(pi->ipi_spec_dst), sizeof(pi->ipi_spec_dst));
          memset((void*)&(recvCast->sin6_addr), 0, 10);
          memset((char*)&(recvCast->sin6_addr) + 10, 255, 2);
        }else{
          struct sockaddr_in * recvCast = (sockaddr_in*)(char*)recvAddr;
          recvCast->sin_port = htons(boundPort);
          recvCast->sin_family = AF_INET;
          memcpy(&(recvCast->sin_addr), &(pi->ipi_spec_dst), sizeof(pi->ipi_spec_dst));
        }
        recvInterface = pi->ipi_ifindex;
        hasReceiveData = true;
      }
      if (cmsg->cmsg_level == IPPROTO_IPV6 && cmsg->cmsg_type == IPV6_PKTINFO){
        struct in6_pktinfo* pi = (in6_pktinfo*)CMSG_DATA(cmsg);
        struct sockaddr_in6 * recvCast = (sockaddr_in6*)(char*)recvAddr;
        recvCast->sin6_family = AF_INET6;
        recvCast->sin6_port = htons(boundPort);
        memcpy(&(recvCast->sin6_addr), &(pi->ipi6_addr), sizeof(pi->ipi6_addr));
        recvInterface = pi->ipi6_ifindex;
        hasReceiveData = true;
      }
    }
  }
#endif
  data.append(0, r);
  down += r;
  //Handle UDP packets that are too large
  if (data.rsize() < (unsigned int)r){
    INFO_MSG("Doubling UDP socket buffer from %" PRIu32 " to %" PRIu32, data.rsize(), data.rsize()*2);
    data.allocate(data.rsize()*2);
  }
  return onData();
}

bool Socket::UDPConnection::onData(){
  wasEncrypted = false;
  if (!data.size()){return false;}
  int r = data.size();
#ifdef SSL
  uint8_t fb = 0;
  if (r){fb = (uint8_t)data[0];}
  if (r && hasDTLS && fb > 19 && fb < 64){
    if (nextDTLSReadLen){
      INFO_MSG("Overwriting %zu bytes of unread dTLS data!", nextDTLSReadLen);
    }
    nextDTLSRead = data;
    nextDTLSReadLen = data.size();
    // Complete dTLS handshake if needed
#if MBEDTLS_VERSION_MAJOR > 2
    if (!mbedtls_ssl_is_handshake_over(&ssl_ctx)){
#else
    if (ssl_ctx.state != MBEDTLS_SSL_HANDSHAKE_OVER){
#endif
      do{
        r = mbedtls_ssl_handshake(&ssl_ctx);
        switch (r){
        case 0:{ // Handshake complete
          INFO_MSG("dTLS handshake complete!");

#if !HAVE_UPSTREAM_MBEDTLS_SRTP
          int extrRes = 0;
          uint8_t keying_material[MBEDTLS_DTLS_SRTP_MAX_KEY_MATERIAL_LENGTH];
          size_t keying_material_len = sizeof(keying_material);
          extrRes = mbedtls_ssl_get_dtls_srtp_key_material(&ssl_ctx, keying_material, &keying_material_len);
          if (extrRes){
            char mbedtls_msg[1024];
            mbedtls_strerror(extrRes, mbedtls_msg, sizeof(mbedtls_msg));
            WARN_MSG("dTLS could not extract keying material: %s", mbedtls_msg);
            return Receive();
          }

          mbedtls_ssl_srtp_profile srtp_profile = mbedtls_ssl_get_dtls_srtp_protection_profile(&ssl_ctx);
          switch (srtp_profile){
          case MBEDTLS_SRTP_AES128_CM_HMAC_SHA1_80:{
            cipher = "SRTP_AES128_CM_SHA1_80";
            break;
          }
          case MBEDTLS_SRTP_AES128_CM_HMAC_SHA1_32:{
            cipher = "SRTP_AES128_CM_SHA1_32";
            break;
          }
          default:{
            WARN_MSG("Unhandled SRTP profile, cannot extract keying material.");
            return Receive();
          }
          }
#else
          uint8_t keying_material[MBEDTLS_TLS_SRTP_MAX_MKI_LENGTH] = {};
          mbedtls_dtls_srtp_info info = {};
          mbedtls_ssl_get_dtls_srtp_negotiation_result(&ssl_ctx, &info);

          if (mbedtls_ssl_tls_prf(tls_prf_type, master_secret, sizeof(master_secret), "EXTRACTOR-dtls_srtp", randbytes,sizeof( randbytes ), keying_material, sizeof( keying_material )) != 0){
            ERROR_MSG("mbedtls_ssl_tls_prf failed to create keying_material");
            return Receive();
          }
#if MBEDTLS_VERSION_MAJOR > 2
          mbedtls_ssl_srtp_profile chosen_profile = info.private_chosen_dtls_srtp_profile;
#else
          mbedtls_ssl_srtp_profile chosen_profile = info.chosen_dtls_srtp_profile;
#endif
          switch (chosen_profile){
          case MBEDTLS_TLS_SRTP_AES128_CM_HMAC_SHA1_80:{
            cipher = "SRTP_AES128_CM_SHA1_80";
            break;
          }
          case MBEDTLS_TLS_SRTP_AES128_CM_HMAC_SHA1_32:{
            cipher = "SRTP_AES128_CM_SHA1_32";
            break;
          }
          case MBEDTLS_TLS_SRTP_UNSET: {
            WARN_MSG("Wasn't able to negotiate the use of DTLS-SRTP");
            return Receive();
          }
          default:{
            WARN_MSG("Unhandled SRTP profile: %hu, cannot extract keying material.", chosen_profile);
            return Receive();
          }
          }
#endif

          remote_key.assign((char *)(&keying_material[0]) + 0, 16);
          local_key.assign((char *)(&keying_material[0]) + 16, 16);
          remote_salt.assign((char *)(&keying_material[0]) + 32, 14);
          local_salt.assign((char *)(&keying_material[0]) + 46, 14);
          return Receive(); // No application-level data to read
        }
        case MBEDTLS_ERR_SSL_HELLO_VERIFY_REQUIRED:{
          dTLSReset();
          return Receive(); // No application-level data to read
        }
        case MBEDTLS_ERR_SSL_WANT_READ:{
          return Receive(); // No application-level data to read
        }
        default:{
          char mbedtls_msg[1024];
          mbedtls_strerror(r, mbedtls_msg, sizeof(mbedtls_msg));
          WARN_MSG("dTLS could not handshake: %s", mbedtls_msg);
          return Receive(); // No application-level data to read
        }
        }
      }while (r == MBEDTLS_ERR_SSL_WANT_WRITE);
    }else{
      int read = mbedtls_ssl_read(&ssl_ctx, (unsigned char *)(char*)data, data.size());
      if (read <= 0){
        // Non-encrypted read (encrypted read fail)
        return true;
      }
      // Encrypted read success
      wasEncrypted = true;
      data.truncate(read);
      return true;
    }
  }
#endif
  return r > 0;
}

int Socket::UDPConnection::getSock(){
  return sock;
}
