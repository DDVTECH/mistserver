/// \file socket.cpp
/// A handy Socket wrapper library.
/// Written by Jaron Vietor in 2010 for DDVTech

#include "defines.h"
#include "socket.h"
#include "timing.h"
#include "json.h"
#include <cstdlib>
#include <ifaddrs.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sstream>
#include <sys/socket.h>
#include <sys/stat.h>

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
  hints.ai_flags = AI_ADDRCONFIG | AI_V4MAPPED;
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
    port = ntohs(((sockaddr_in6 *)&tmpaddr)->sin6_port);
    HIGH_MSG("Peer IPv6 addr [%s:%" PRIu32 "]", host.c_str(), port);
    return true;
  }
  if (tmpaddr->sa_family == AF_INET){
    host = inet_ntop(AF_INET, &(((sockaddr_in *)tmpaddr)->sin_addr), addrconv, INET6_ADDRSTRLEN);
    port = ntohs(((sockaddr_in *)&tmpaddr)->sin_port);
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
  Blocking = false;
  skipCount = 0;
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
    if (blocking == Blocking){return;}
    if (blocking){
      mbedtls_net_set_block(server_fd);
      Blocking = true;
    }else{
      mbedtls_net_set_nonblock(server_fd);
      Blocking = false;
    }
    return;
  }
#endif
  if (sSend >= 0){setFDBlocking(sSend, blocking);}
  if (sRecv >= 0 && sSend != sRecv){setFDBlocking(sRecv, blocking);}
}

/// Set this socket to be blocking (true) or nonblocking (false).
bool Socket::Connection::isBlocking(){
#ifdef SSL
  if (sslConnected){return Blocking;}
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

/// Returns a string describing the last error that occured.
/// Only reports errors if an error actually occured - returns the host address or empty string
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
    int ret = 0;
    if ((ret = mbedtls_net_connect(server_fd, host.c_str(), JSON::Value(port).asString().c_str(),
                                   MBEDTLS_NET_PROTO_TCP)) != 0){
      char estr[200];
      mbedtls_strerror(ret, estr, 200);
      lastErr = estr;
      FAIL_MSG("SSL connect failed: %d: %s", ret, lastErr.c_str());
      close();
      return;
    }
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
    isTrueSocket = true;
    setBoundAddr();
    Blocking = true;
    if (nonblock){setBlocking(false);}
    DONTEVEN_MSG("SSL connect success");
    return;
#endif
    FAIL_MSG("Attempted to open SSL socket without SSL support compiled in!");
    return;
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
  for (rp = result; rp != NULL; rp = rp->ai_next){
    sSend = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    if (sSend < 0){continue;}
    //Ensure we can handle interrupted system call case
    int ret = 0;
    do{
      ret = connect(sSend, rp->ai_addr, rp->ai_addrlen);
    }while(ret && errno == EINTR);
    if (!ret){
      memcpy(&remoteaddr, rp->ai_addr, rp->ai_addrlen);
      break;
    }
    lastErr += strerror(errno);
    ::close(sSend);
  }
  freeaddrinfo(result);

  if (rp == 0){
    FAIL_MSG("Could not connect to %s! Error: %s", host.c_str(), lastErr.c_str());
    close();
  }else{
    if (nonblock){
      int flags = fcntl(sSend, F_GETFL, 0);
      flags |= O_NONBLOCK;
      fcntl(sSend, F_SETFL, flags);
    }
    int optval = 1;
    int optlen = sizeof(optval);
    setsockopt(sSend, SOL_SOCKET, SO_KEEPALIVE, &optval, optlen);
    setBoundAddr();
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
  if (!strictMode && downbuffer.size() > 10000){
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

/// Create a new UDP Socket.
/// Will attempt to create an IPv6 UDP socket, on fail try a IPV4 UDP socket.
/// If both fail, prints an DLVL_FAIL debug message.
/// \param nonblock Whether the socket should be nonblocking.
Socket::UDPConnection::UDPConnection(bool nonblock){
  lastPace = 0;
  boundPort = 0;
  family = AF_INET6;
  sock = socket(AF_INET6, SOCK_DGRAM, 0);
  if (sock == -1){
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    family = AF_INET;
  }
  if (sock == -1){
    FAIL_MSG("Could not create UDP socket: %s", strerror(errno));
  }else{
    if (nonblock){setBlocking(!nonblock);}
    checkRecvBuf();
  }
  up = 0;
  down = 0;
  destAddr = 0;
  destAddr_size = 0;
#ifdef __CYGWIN__
  data.allocate(SOCKETSIZE);
#else
  data.allocate(2048);
#endif
}// Socket::UDPConnection UDP Contructor

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
  lastPace = 0;
  boundPort = 0;
  family = AF_INET6;
  sock = socket(AF_INET6, SOCK_DGRAM, 0);
  if (sock == -1){
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    family = AF_INET;
  }
  if (sock == -1){FAIL_MSG("Could not create UDP socket: %s", strerror(errno));}
  checkRecvBuf();
  up = 0;
  down = 0;
  if (o.destAddr && o.destAddr_size){
    destAddr = malloc(o.destAddr_size);
    destAddr_size = o.destAddr_size;
    if (destAddr){memcpy(destAddr, o.destAddr, o.destAddr_size);}
  }else{
    destAddr = 0;
    destAddr_size = 0;
  }
  data.allocate(2048);
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
  if (destAddr){
    free(destAddr);
    destAddr = 0;
  }
}

// Sets socket family type (to IPV4 or IPV6) (AF_INET=2, AF_INET6=10)
void Socket::UDPConnection::setSocketFamily(int AF_TYPE){\
  INFO_MSG("Switching UDP socket from %s to %s", addrFam(family), addrFam(AF_TYPE));
  family = AF_TYPE;
}

/// Allocates enough space for the largest type of address we support, so that receive calls can write to it.
void Socket::UDPConnection::allocateDestination(){
  if (destAddr && destAddr_size < sizeof(sockaddr_in6)){
    free(destAddr);
    destAddr = 0;
  }
  if (!destAddr){
    destAddr = malloc(sizeof(sockaddr_in6));
    if (destAddr){
      destAddr_size = sizeof(sockaddr_in6);
      memset(destAddr, 0, sizeof(sockaddr_in6));
      ((struct sockaddr_in *)destAddr)->sin_family = AF_UNSPEC;
    }
  }
}

/// Stores the properties of the receiving end of this UDP socket.
/// This will be the receiving end for all SendNow calls.
void Socket::UDPConnection::SetDestination(std::string destIp, uint32_t port){
  DONTEVEN_MSG("Setting destination to %s:%u", destIp.c_str(), port);
  // UDP sockets can switch between IPv4 and IPv6 on demand.
  // We change IPv4-mapped IPv6 addresses into IPv4 addresses for Windows-sillyness reasons.
  if (destIp.substr(0, 7) == "::ffff:"){destIp = destIp.substr(7);}
  struct addrinfo *result, *rp, hints;
  std::stringstream ss;
  ss << port;

  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = family;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_flags = AI_ADDRCONFIG | AI_ALL;
  hints.ai_protocol = IPPROTO_UDP;
  hints.ai_canonname = NULL;
  hints.ai_addr = NULL;
  hints.ai_next = NULL;
  int s = getaddrinfo(destIp.c_str(), ss.str().c_str(), &hints, &result);
  if (s != 0){
    hints.ai_family = AF_UNSPEC;
    s = getaddrinfo(destIp.c_str(), ss.str().c_str(), &hints, &result);
    if (s != 0){
      FAIL_MSG("Could not connect UDP socket to %s:%i! Error: %s", destIp.c_str(), port, gai_strmagic(s));
      return;
    }
  }

  for (rp = result; rp != NULL; rp = rp->ai_next){
    // assume success
    if (destAddr){
      free(destAddr);
      destAddr = 0;
    }
    destAddr_size = rp->ai_addrlen;
    destAddr = malloc(destAddr_size);
    if (!destAddr){return;}
    memcpy(destAddr, rp->ai_addr, rp->ai_addrlen);
    if (family != rp->ai_family){
      INFO_MSG("Switching UDP socket from %s to %s", addrFam(family), addrFam(rp->ai_family));
      close();
      family = rp->ai_family;
      sock = socket(family, SOCK_DGRAM, 0);
      checkRecvBuf();
      if (boundPort){
        INFO_MSG("Rebinding to %s:%d %s", boundAddr.c_str(), boundPort, boundMulti.c_str());
        bind(boundPort, boundAddr, boundMulti);
      }
    }
    {
      std::string trueDest;
      uint32_t truePort;
      GetDestination(trueDest, truePort);
      HIGH_MSG("Set UDP destination: %s:%d => %s:%d (%s)", destIp.c_str(), port, trueDest.c_str(), truePort, addrFam(family));
    }
    freeaddrinfo(result);
    return;
    //\todo Possibly detect and handle failure
  }
  freeaddrinfo(result);
  free(destAddr);
  destAddr = 0;
  FAIL_MSG("Could not set destination for UDP socket: %s:%d", destIp.c_str(), port);
}// Socket::UDPConnection SetDestination

/// Gets the properties of the receiving end of this UDP socket.
/// This will be the receiving end for all SendNow calls.
void Socket::UDPConnection::GetDestination(std::string &destIp, uint32_t &port){
  if (!destAddr || !destAddr_size){
    destIp = "";
    port = 0;
    return;
  }
  char addr_str[INET6_ADDRSTRLEN + 1];
  addr_str[INET6_ADDRSTRLEN] = 0; // set last byte to zero, to prevent walking out of the array
  if (((struct sockaddr_in *)destAddr)->sin_family == AF_INET6){
    if (inet_ntop(AF_INET6, &(((struct sockaddr_in6 *)destAddr)->sin6_addr), addr_str, INET6_ADDRSTRLEN) != 0){
      destIp = addr_str;
      port = ntohs(((struct sockaddr_in6 *)destAddr)->sin6_port);
      return;
    }
  }
  if (((struct sockaddr_in *)destAddr)->sin_family == AF_INET){
    if (inet_ntop(AF_INET, &(((struct sockaddr_in *)destAddr)->sin_addr), addr_str, INET6_ADDRSTRLEN) != 0){
      destIp = addr_str;
      port = ntohs(((struct sockaddr_in *)destAddr)->sin_port);
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
  if (destAddr && destAddr_size){binList = getIPv6BinAddr(*(sockaddr_in6*)destAddr);}
  if (binList.size() < 16){return std::string("\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000", 16);}
  return binList.substr(0, 16);
}// Socket::UDPConnection GetDestination

/// Returns the port number of the receiving end of this socket.
/// Returns 0 on error.
uint32_t Socket::UDPConnection::getDestPort() const{
  if (!destAddr || !destAddr_size){return 0;}
  if (((struct sockaddr_in *)destAddr)->sin_family == AF_INET6){
    return ntohs(((struct sockaddr_in6 *)destAddr)->sin6_port);
  }
  if (((struct sockaddr_in *)destAddr)->sin_family == AF_INET){
    return ntohs(((struct sockaddr_in *)destAddr)->sin_port);
  }
  return 0;
}

/// Sets the socket to be blocking if the parameters is true.
/// Sets the socket to be non-blocking otherwise.
void Socket::UDPConnection::setBlocking(bool blocking){
  if (sock >= 0){setFDBlocking(sock, blocking);}
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
  if (len < 1){return;}
  int r = sendto(sock, sdata, len, 0, (sockaddr *)destAddr, destAddr_size);
  if (r > 0){
    up += r;
  }else{
    FAIL_MSG("Could not send UDP data through %d: %s", sock, strerror(errno));
  }
}

void Socket::UDPConnection::sendPaced(const char *sdata, size_t len){
  if (!paceQueue.size() && (!lastPace || Util::getMicros(lastPace) > 10000)){
    SendNow(sdata, len);
    lastPace = Util::getMicros();
  }else{
    paceQueue.push_back(Util::ResizeablePointer());
    paceQueue.back().assign(sdata, len);
    // Try to send a packet, if time allows
    //sendPaced(0);
  }
}

/// Spends uSendWindow microseconds either sending paced packets or sleeping, whichever is more appropriate
void Socket::UDPConnection::sendPaced(uint64_t uSendWindow){
  uint64_t currPace = Util::getMicros();
  do{
    uint64_t uTime = Util::getMicros();
    uint64_t sleepTime = uTime - currPace;
    if (sleepTime > uSendWindow){
      sleepTime = 0;
    }else{
      sleepTime = uSendWindow - sleepTime;
    }
    uint64_t paceWait = uTime - lastPace;
    size_t qSize = paceQueue.size();
    // If the queue is complete, wait out the remainder of the time
    if (!qSize){
      Util::usleep(sleepTime);
      return;
    }
    // Otherwise, target clearing the queue in 25ms at most.
    uint64_t targetTime = 25000 / qSize;
    // If this slows us to below 1 packet per 5ms, go that speed instead.
    if (targetTime > 5000){targetTime = 5000;}
    // If the wait is over, send now.
    if (paceWait >= targetTime){
      SendNow(*paceQueue.begin(), paceQueue.begin()->size());
      paceQueue.pop_front();
      lastPace = uTime;
      continue;
    }
    // Otherwise, wait for the smaller of remaining wait time or remaining send window time.
    if (targetTime - paceWait < sleepTime){sleepTime = targetTime - paceWait;}
    Util::usleep(sleepTime);
  }while(Util::getMicros(currPace) < uSendWindow);
}

std::string Socket::UDPConnection::getBoundAddress(){
  std::string boundaddr;
  uint32_t boundport;
  Socket::getSocketName(sock, boundaddr, boundport);
  return boundaddr;
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
  struct addrinfo hints, *addr_result, *rp;
  memset(&hints, 0, sizeof(hints));
  hints.ai_flags = AI_ADDRCONFIG | AI_PASSIVE | AI_V4MAPPED;
  if (destAddr && destAddr_size){
    hints.ai_family = ((struct sockaddr_in *)destAddr)->sin_family;
  }else{
    hints.ai_family = AF_UNSPEC;
  }

  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_protocol = IPPROTO_UDP;

  std::stringstream ss;
  ss << port;

  if (iface == "0.0.0.0" || iface.length() == 0){
    if ((addr_ret = getaddrinfo(0, ss.str().c_str(), &hints, &addr_result)) != 0){
      FAIL_MSG("Could not resolve %s for UDP: %s", iface.c_str(), gai_strmagic(addr_ret));
      return 0;
    }
  }else{
    if ((addr_ret = getaddrinfo(iface.c_str(), ss.str().c_str(), &hints, &addr_result)) != 0){
      FAIL_MSG("Could not resolve %s for UDP: %s", iface.c_str(), gai_strmagic(addr_ret));
      return 0;
    }
  }

  std::string err_str;
  uint16_t portNo = 0;
  for (rp = addr_result; rp != NULL; rp = rp->ai_next){
    sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    if (sock == -1){continue;}
    if (rp->ai_family == AF_INET6){
      const int optval = 0;
      if (setsockopt(sock, SOL_SOCKET, IPV6_V6ONLY, &optval, sizeof(optval)) < 0){
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
      INFO_MSG("UDP bind success on %s:%u (%s)", human_addr, portNo, addrFam(rp->ai_family));
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

/// Attempt to receive a UDP packet.
/// This will automatically allocate or resize the internal data buffer if needed.
/// If a packet is received, it will be placed in the "data" member, with it's length in "data_len".
/// \return True if a packet was received, false otherwise.
bool Socket::UDPConnection::Receive(){
  if (sock == -1){return false;}
  data.truncate(0);
  sockaddr_in6 addr;
  socklen_t destsize = sizeof(addr);
  int r = recvfrom(sock, data, data.rsize(), MSG_TRUNC | MSG_DONTWAIT, (sockaddr *)&addr, &destsize);
  if (r == -1){
    if (errno != EAGAIN){INFO_MSG("UDP receive: %d (%s)", errno, strerror(errno));}
    return false;
  }
  if (destAddr && destsize && destAddr_size >= destsize){memcpy(destAddr, &addr, destsize);}
  data.append(0, r);
  down += r;
  //Handle UDP packets that are too large
  if (data.rsize() < (unsigned int)r){
    INFO_MSG("Doubling UDP socket buffer from %" PRIu32 " to %" PRIu32, data.rsize(), data.rsize()*2);
    data.allocate(data.rsize()*2);
  }
  return (r > 0);
}

int Socket::UDPConnection::getSock(){
  return sock;
}
