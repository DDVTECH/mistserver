/// \file dtsc.cpp
/// Holds all code for DDVTECH Stream Container parsing/generation.

#include "dtsc.h"
#include <string.h> //for memcmp
#include <arpa/inet.h> //for htonl/ntohl
#include <stdio.h> //for fprint, stderr

char DTSC::Magic_Header[] = "DTSC";
char DTSC::Magic_Packet[] = "DTPD";

/// Initializes a DTSC::Stream with only one packet buffer.
DTSC::Stream::Stream(){
  datapointer = 0;
  buffercount = 1;
}

/// Initializes a DTSC::Stream with a minimum of rbuffers packet buffers.
/// The actual buffer count may not at all times be the requested amount.
DTSC::Stream::Stream(unsigned int rbuffers){
  datapointer = 0;
  if (rbuffers < 1){rbuffers = 1;}
  buffercount = rbuffers;
}

/// Returns the time in milliseconds of the last received packet.
/// This is _not_ the time this packet was received, only the stored time.
unsigned int DTSC::Stream::getTime(){
  return buffers.front().getContentP("time")->NumValue();
}

/// Attempts to parse a packet from the given std::string buffer.
/// Returns true if successful, removing the parsed part from the buffer string.
/// Returns false if invalid or not enough data is in the buffer.
/// \arg buffer The std::string buffer to attempt to parse.
bool DTSC::Stream::parsePacket(std::string & buffer){
  uint32_t len;
  static bool syncing = false;
  if (buffer.length() > 8){
    if (memcmp(buffer.c_str(), DTSC::Magic_Header, 4) == 0){
      len = ntohl(((uint32_t *)buffer.c_str())[1]);
      if (buffer.length() < len+8){return false;}
      metadata = DTSC::parseDTMI((unsigned char*)buffer.c_str() + 8, len);
      buffer.erase(0, len+8);
      return false;
    }
    if (memcmp(buffer.c_str(), DTSC::Magic_Packet, 4) == 0){
      len = ntohl(((uint32_t *)buffer.c_str())[1]);
      if (buffer.length() < len+8){return false;}
      buffers.push_front(DTSC::DTMI("empty", DTMI_ROOT));
      buffers.front() = DTSC::parseDTMI((unsigned char*)buffer.c_str() + 8, len);
      datapointertype = INVALID;
      if (buffers.front().getContentP("data")){
        datapointer = &(buffers.front().getContentP("data")->StrValue());
        if (buffers.front().getContentP("datatype")){
          std::string tmp = buffers.front().getContentP("datatype")->StrValue();
          if (tmp == "video"){datapointertype = VIDEO;}
          if (tmp == "audio"){datapointertype = AUDIO;}
          if (tmp == "meta"){datapointertype = META;}
        }
      }else{
        datapointer = 0;
      }
      buffer.erase(0, len+8);
      while (buffers.size() > buffercount){buffers.pop_back();}
      advanceRings();
      syncing = false;
      return true;
    }
    #if DEBUG >= 2
    if (!syncing){
      std::cerr << "Error: Invalid DTMI data detected - re-syncing" << std::endl;
      syncing = true;
    }
    #endif
    size_t magic_search = buffer.find(Magic_Packet);
    if (magic_search == std::string::npos){
      buffer.clear();
    }else{
      buffer.erase(0, magic_search);
    }
  }
  return false;
}

/// Returns a direct pointer to the data attribute of the last received packet, if available.
/// Returns NULL if no valid pointer or packet is available.
std::string & DTSC::Stream::lastData(){
  return *datapointer;
}

/// Returns the packed in this buffer number.
/// \arg num Buffer number.
DTSC::DTMI & DTSC::Stream::getPacket(unsigned int num){
  return buffers[num];
}

/// Returns the type of the last received packet.
DTSC::datatype DTSC::Stream::lastType(){
  return datapointertype;
}

/// Returns true if the current stream contains at least one video track.
bool DTSC::Stream::hasVideo(){
  return (metadata.getContentP("video") != 0);
}

/// Returns true if the current stream contains at least one audio track.
bool DTSC::Stream::hasAudio(){
  return (metadata.getContentP("audio") != 0);
}

/// Returns a packed DTSC packet, ready to sent over the network.
std::string & DTSC::Stream::outPacket(unsigned int num){
  static std::string emptystring;
  if (num >= buffers.size()) return emptystring;
  buffers[num].Pack(true);
  return buffers[num].packed;
}

/// Returns a packed DTSC header, ready to sent over the network.
std::string & DTSC::Stream::outHeader(){
  if ((metadata.packed.length() < 4) || !metadata.netpacked){
    metadata.Pack(true);
    metadata.packed.replace(0, 4, Magic_Header);
  }
  return metadata.packed;
}

/// advances all given out and internal Ring classes to point to the new buffer, after one has been added.
/// Also updates the internal keyframes ring, as well as marking rings as starved if they are.
/// Unsets waiting rings, updating them with their new buffer number.
void DTSC::Stream::advanceRings(){
  std::deque<DTSC::Ring>::iterator dit;
  std::set<DTSC::Ring *>::iterator sit;
  for (sit = rings.begin(); sit != rings.end(); sit++){
    (*sit)->b++;
    if ((*sit)->waiting){(*sit)->waiting = false; (*sit)->b = 0;}
    if ((*sit)->starved || ((*sit)->b >= buffers.size())){(*sit)->starved = true; (*sit)->b = 0;}
  }
  for (dit = keyframes.begin(); dit != keyframes.end(); dit++){
    dit->b++;
    if (dit->b >= buffers.size()){keyframes.erase(dit); break;}
  }
  if ((lastType() == VIDEO) && (buffers.front().getContentP("keyframe"))){
    keyframes.push_front(DTSC::Ring(0));
  }
  //increase buffer size if no keyframes available
  if ((buffercount > 1) && (keyframes.size() < 1)){buffercount++;}
}

/// Constructs a new Ring, at the given buffer position.
/// \arg v Position for buffer.
DTSC::Ring::Ring(unsigned int v){
  b = v;
  waiting = false;
  starved = false;
}

/// Requests a new Ring, which will be created and added to the internal Ring list.
/// This Ring will be kept updated so it always points to valid data or has the starved boolean set.
/// Don't forget to call dropRing() for all requested Ring classes that are no longer neccessary!
DTSC::Ring * DTSC::Stream::getRing(){
  DTSC::Ring * tmp;
  if (keyframes.size() == 0){
    tmp = new DTSC::Ring(0);
  }else{
    tmp = new DTSC::Ring(keyframes[0].b);
  }
  rings.insert(tmp);
  return tmp;
}

/// Deletes a given out Ring class from memory and internal Ring list.
/// Checks for NULL pointers and invalid pointers, silently discarding them.
void DTSC::Stream::dropRing(DTSC::Ring * ptr){
  if (rings.find(ptr) != rings.end()){
    rings.erase(ptr);
    delete ptr;
  }
}

/// Properly cleans up the object for erasing.
/// Drops all Ring classes that have been given out.
DTSC::Stream::~Stream(){
  std::set<DTSC::Ring *>::iterator sit;
  for (sit = rings.begin(); sit != rings.end(); sit++){delete (*sit);}
}

/// Returns the std::string Indice for the current object, if available.
/// Returns an empty string if no indice exists.
std::string DTSC::DTMI::Indice(){return myIndice;};

/// Returns the DTSC::DTMItype AMF0 object type for this object.
DTSC::DTMItype DTSC::DTMI::GetType(){return myType;};

/// Returns the numeric value of this object, if available.
/// If this object holds no numeric value, 0 is returned.
uint64_t & DTSC::DTMI::NumValue(){return numval;};

/// Returns the std::string value of this object, if available.
/// If this object holds no string value, an empty string is returned.
std::string & DTSC::DTMI::StrValue(){return strval;};

/// Returns the C-string value of this object, if available.
/// If this object holds no string value, an empty C-string is returned.
const char * DTSC::DTMI::Str(){return strval.c_str();};

/// Returns a count of the amount of objects this object currently holds.
/// If this object is not a container type, this function will always return 0.
int DTSC::DTMI::hasContent(){return contents.size();};

/// Returns true if this DTSC::DTMI value is non-default.
/// Non-default means it is either not a root element or has content.
bool DTSC::DTMI::isEmpty(){
  if (myType != DTMI_ROOT){return false;}
  return (hasContent() == 0);
};

/// Adds an DTSC::DTMI to this object. Works for all types, but only makes sense for container types.
/// This function resets DTMI::packed to an empty string, forcing a repack on the next call to DTMI::Pack.
/// If the indice name already exists, replaces the indice.
void DTSC::DTMI::addContent(DTSC::DTMI c){
  std::vector<DTMI>::iterator it;
  for (it = contents.begin(); it != contents.end(); it++){
    if (it->Indice() == c.Indice()){
      contents.erase(it);
      break;
    }
  }
  contents.push_back(c); packed = "";
};

/// Returns a pointer to the object held at indice i.
/// Returns null pointer if no object is held at this indice.
/// \param i The indice of the object in this container.
DTSC::DTMI* DTSC::DTMI::getContentP(int i){
  if (contents.size() <= (unsigned int)i){return 0;}
  return &contents.at(i);
};

/// Returns a copy of the object held at indice i.
/// Returns a AMF::AMF0_DDV_CONTAINER of indice "error" if no object is held at this indice.
/// \param i The indice of the object in this container.
DTSC::DTMI DTSC::DTMI::getContent(int i){return contents.at(i);};

/// Returns a pointer to the object held at indice s.
/// Returns NULL if no object is held at this indice.
/// \param s The indice of the object in this container.
DTSC::DTMI* DTSC::DTMI::getContentP(std::string s){
  for (std::vector<DTSC::DTMI>::iterator it = contents.begin(); it != contents.end(); it++){
    if (it->Indice() == s){return &(*it);}
  }
  return 0;
};

/// Returns a copy of the object held at indice s.
/// Returns a AMF::AMF0_DDV_CONTAINER of indice "error" if no object is held at this indice.
/// \param s The indice of the object in this container.
DTSC::DTMI DTSC::DTMI::getContent(std::string s){
  for (std::vector<DTSC::DTMI>::iterator it = contents.begin(); it != contents.end(); it++){
    if (it->Indice() == s){return *it;}
  }
  return DTSC::DTMI("error", DTMI_ROOT);
};

/// Default constructor.
/// Simply fills the data with DTSC::DTMI("error", AMF0_DDV_CONTAINER)
DTSC::DTMI::DTMI(){
  *this = DTSC::DTMI("error", DTMI_ROOT);
};//default constructor

/// Constructor for numeric objects.
/// The object type is by default DTMItype::DTMI_INT, but this can be forced to a different value.
/// \param indice The string indice of this object in its container, or empty string if none. Numeric indices are automatic.
/// \param val The numeric value of this object. Numeric objects only support uint64_t values.
/// \param setType The object type to force this object to.
DTSC::DTMI::DTMI(std::string indice, uint64_t val, DTSC::DTMItype setType){//num type initializer
  myIndice = indice;
  myType = setType;
  strval = "";
  numval = val;
};

/// Constructor for string objects.
/// \param indice The string indice of this object in its container, or empty string if none. Numeric indices are automatic.
/// \param val The string value of this object.
/// \param setType The object type to force this object to.
DTSC::DTMI::DTMI(std::string indice, std::string val, DTSC::DTMItype setType){//str type initializer
  myIndice = indice;
  myType = setType;
  strval = val;
  numval = 0;
};

/// Constructor for container objects.
/// \param indice The string indice of this object in its container, or empty string if none.
/// \param setType The object type to force this object to.
DTSC::DTMI::DTMI(std::string indice, DTSC::DTMItype setType){//object type initializer
  myIndice = indice;
  myType = setType;
  strval = "";
  numval = 0;
};

/// Prints the contents of this object to std::cerr.
/// If this object contains other objects, it will call itself recursively
/// and print all nested content in a nice human-readable format.
void DTSC::DTMI::Print(std::string indent){
  std::cerr << indent;
  // print my type
  switch (myType){
    case DTMI_INT: std::cerr << "Integer"; break;
    case DTMI_STRING: std::cerr << "String"; break;
    case DTMI_OBJECT: std::cerr << "Object"; break;
    case DTMI_OBJ_END: std::cerr << "Object end"; break;
    case DTMI_ROOT: std::cerr << "Root Node"; break;
  }
  // print my string indice, if available
  std::cerr << " " << myIndice << " ";
  // print my numeric or string contents
  switch (myType){
    case DTMI_INT: std::cerr << numval; break;
    case DTMI_STRING:
      if (strval.length() > 200 || ((strval.length() > 1) && ( (strval[0] < 'A') || (strval[0] > 'z') ) )){
        std::cerr << strval.length() << " bytes of data";
      }else{
        std::cerr << strval;
      }
      break;
    default: break;//we don't care about the rest, and don't want a compiler warning...
  }
  std::cerr << std::endl;
  // if I hold other objects, print those too, recursively.
  if (contents.size() > 0){
    for (std::vector<DTSC::DTMI>::iterator it = contents.begin(); it != contents.end(); it++){it->Print(indent+"  ");}
  }
};//print

/// Packs the DTMI to a std::string for transfer over the network.
/// If a packed version already exists, does not regenerate it.
/// If the object is a container type, this function will call itself recursively and contain all contents.
/// \arg netpack If true, will pack as a full DTMI packet, if false only as the contents without header.
std::string DTSC::DTMI::Pack(bool netpack){
  if (packed != ""){
    if (netpacked == netpack){return packed;}
    if (netpacked){
      packed.erase(0, 8);
    }else{
      unsigned int size = htonl(packed.length());
      packed.insert(0, (char*)&size, 4);
      packed.insert(0, Magic_Packet);
    }
    netpacked = !netpacked;
    return packed;
  }
  std::string r = "";
  r += myType;
  //output the properly formatted data stream for this object's contents.
  switch (myType){
    case DTMI_INT:
      r += *(((char*)&numval)+7); r += *(((char*)&numval)+6);
      r += *(((char*)&numval)+5); r += *(((char*)&numval)+4);
      r += *(((char*)&numval)+3); r += *(((char*)&numval)+2);
      r += *(((char*)&numval)+1); r += *(((char*)&numval));
      break;
    case DTMI_STRING:
      r += strval.size() / (256*256*256);
      r += strval.size() / (256*256);
      r += strval.size() / 256;
      r += strval.size() % 256;
      r += strval;
      break;
    case DTMI_OBJECT:
    case DTMI_ROOT:
      if (contents.size() > 0){
        for (std::vector<DTSC::DTMI>::iterator it = contents.begin(); it != contents.end(); it++){
          r += it->Indice().size() / 256;
          r += it->Indice().size() % 256;
          r += it->Indice();
          r += it->Pack();
        }
      }
      r += (char)0x0; r += (char)0x0; r += (char)0xEE;
      break;
    case DTMI_OBJ_END:
      break;
  }
  packed = r;
  netpacked = netpack;
  if (netpacked){
    unsigned int size = htonl(packed.length());
    packed.insert(0, (char*)&size, 4);
    packed.insert(0, Magic_Packet);
  }
  return packed;
};//pack

/// Parses a single AMF0 type - used recursively by the AMF::parse() functions.
/// This function updates i every call with the new position in the data.
/// \param data The raw data to parse.
/// \param len The size of the raw data.
/// \param i Current parsing position in the raw data.
/// \param name Indice name for any new object created.
/// \returns A single DTSC::DTMI, parsed from the raw data.
DTSC::DTMI DTSC::parseOneDTMI(const unsigned char *& data, unsigned int &len, unsigned int &i, std::string name){
  unsigned int tmpi = 0;
  unsigned char tmpdbl[8];
  #if DEBUG >= 10
  fprintf(stderr, "Note: AMF type %hhx found. %i bytes left\n", data[i], len-i);
  #endif
  switch (data[i]){
    case DTMI_INT:
      tmpdbl[7] = data[i+1];
      tmpdbl[6] = data[i+2];
      tmpdbl[5] = data[i+3];
      tmpdbl[4] = data[i+4];
      tmpdbl[3] = data[i+5];
      tmpdbl[2] = data[i+6];
      tmpdbl[1] = data[i+7];
      tmpdbl[0] = data[i+8];
      i+=9;//skip 8(an uint64_t)+1 forwards
      return DTSC::DTMI(name, *(uint64_t*)tmpdbl, DTMI_INT);
      break;
    case DTMI_STRING:{
      tmpi = data[i+1]*256*256*256+data[i+2]*256*256+data[i+3]*256+data[i+4];//set tmpi to UTF-8-long length
      std::string tmpstr = std::string((const char *)data+i+5, (size_t)tmpi);//set the string data
      i += tmpi + 5;//skip length+size+1 forwards
      return DTSC::DTMI(name, tmpstr, DTMI_STRING);
      } break;
    case DTMI_ROOT:{
      ++i;
      DTSC::DTMI ret(name, DTMI_ROOT);
      while (data[i] + data[i+1] != 0){//while not encountering 0x0000 (we assume 0x0000EE)
        tmpi = data[i]*256+data[i+1];//set tmpi to the UTF-8 length
        std::string tmpstr = std::string((const char *)data+i+2, (size_t)tmpi);//set the string data
        i += tmpi + 2;//skip length+size forwards
        ret.addContent(parseOneDTMI(data, len, i, tmpstr));//add content, recursively parsed, updating i, setting indice to tmpstr
      }
      i += 3;//skip 0x0000EE
      return ret;
    } break;
    case DTMI_OBJECT:{
      ++i;
      DTSC::DTMI ret(name, DTMI_OBJECT);
      while (data[i] + data[i+1] != 0){//while not encountering 0x0000 (we assume 0x0000EE)
        tmpi = data[i]*256+data[i+1];//set tmpi to the UTF-8 length
        std::string tmpstr = std::string((const char *)data+i+2, (size_t)tmpi);//set the string data
        i += tmpi + 2;//skip length+size forwards
        ret.addContent(parseOneDTMI(data, len, i, tmpstr));//add content, recursively parsed, updating i, setting indice to tmpstr
      }
      i += 3;//skip 0x0000EE
      return ret;
    } break;
  }
  #if DEBUG >= 2
  fprintf(stderr, "Error: Unimplemented DTMI type %hhx - returning.\n", data[i]);
  #endif
  return DTSC::DTMI("error", DTMI_ROOT);
}//parseOne

/// Parses a C-string to a valid DTSC::DTMI.
/// This function will find one DTMI object in the string and return it.
DTSC::DTMI DTSC::parseDTMI(const unsigned char * data, unsigned int len){
  DTSC::DTMI ret;//container type
  unsigned int i = 0;
  ret = parseOneDTMI(data, len, i, "");
  ret.packed = std::string((char*)data, (size_t)len);
  ret.netpacked = false;
  return ret;
}//parse

/// Parses a std::string to a valid DTSC::DTMI.
/// This function will find one DTMI object in the string and return it.
DTSC::DTMI DTSC::parseDTMI(std::string data){
  return parseDTMI((const unsigned char*)data.c_str(), data.size());
}//parse
