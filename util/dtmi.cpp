/// \file dtmi.cpp
/// Holds all code for DDVTECH MediaInfo parsing/generation.

#include "dtmi.h"
#include <cstdio> //needed for stderr only

/// Returns the std::string Indice for the current object, if available.
/// Returns an empty string if no indice exists.
std::string DTSC::DTMI::Indice(){return myIndice;};

/// Returns the DTSC::DTMItype AMF0 object type for this object.
DTSC::DTMItype DTSC::DTMI::GetType(){return myType;};

/// Returns the numeric value of this object, if available.
/// If this object holds no numeric value, 0 is returned.
uint64_t DTSC::DTMI::NumValue(){return numval;};

/// Returns the std::string value of this object, if available.
/// If this object holds no string value, an empty string is returned.
std::string DTSC::DTMI::StrValue(){return strval;};

/// Returns the C-string value of this object, if available.
/// If this object holds no string value, an empty C-string is returned.
const char * DTSC::DTMI::Str(){return strval.c_str();};

/// Returns a count of the amount of objects this object currently holds.
/// If this object is not a container type, this function will always return 0.
int DTSC::DTMI::hasContent(){return contents.size();};

/// Adds an DTSC::DTMI to this object. Works for all types, but only makes sense for container types.
void DTSC::DTMI::addContent(DTSC::DTMI c){contents.push_back(c);};

/// Returns a pointer to the object held at indice i.
/// Returns AMF::AMF0_DDV_CONTAINER of indice "error" if no object is held at this indice.
/// \param i The indice of the object in this container.
DTSC::DTMI* DTSC::DTMI::getContentP(int i){return &contents.at(i);};

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
  return DTSC::DTMI("error", DTMI::DTMI_ROOT);
};

/// Default constructor.
/// Simply fills the data with DTSC::DTMI("error", AMF0_DDV_CONTAINER)
DTSC::DTMI::DTMI(){
  *this = DTSC::DTMI("error", DTMI::DTMI_ROOT);
};//default constructor

/// Constructor for numeric objects.
/// The object type is by default AMF::AMF0_NUMBER, but this can be forced to a different value.
/// \param indice The string indice of this object in its container, or empty string if none. Numeric indices are automatic.
/// \param val The numeric value of this object. Numeric AMF0 objects only support double-type values.
/// \param setType The object type to force this object to.
DTSC::DTMI::DTMI(std::string indice, double val, DTSC::DTMItype setType){//num type initializer
  myIndice = indice;
  myType = setType;
  strval = "";
  numval = val;
};

/// Constructor for string objects.
/// The object type is by default AMF::AMF0_STRING, but this can be forced to a different value.
/// There is no need to manually change the type to AMF::AMF0_LONGSTRING, this will be done automatically.
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
/// The object type is by default AMF::AMF0_OBJECT, but this can be forced to a different value.
/// \param indice The string indice of this object in its container, or empty string if none. Numeric indices are automatic.
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
    case DTMItype::DTMI_INT: std::cerr << "Integer"; break;
    case DTMItype::DTMI_STRING: std::cerr << "String"; break;
    case DTMItype::DTMI_OBJECT: std::cerr << "Object"; break;
    case DTMItype::DTMI_OBJ_END: std::cerr << "Object end"; break;
    case DTMItype::DTMI_ROOT: std::cerr << "Root Node"; break;
  }
  // print my string indice, if available
  std::cerr << " " << myIndice << " ";
  // print my numeric or string contents
  switch (myType){
    case DTMItype::DTMI_INT: std::cerr << numval; break;
    case DTMItype::DTMI_STRING: std::cerr << strval; break;
    default: break;//we don't care about the rest, and don't want a compiler warning...
  }
  std::cerr << std::endl;
  // if I hold other objects, print those too, recursively.
  if (contents.size() > 0){
    for (std::vector<DTSC::DTMI>::iterator it = contents.begin(); it != contents.end(); it++){it->Print(indent+"  ");}
  }
};//print

/// Packs the AMF object to a std::string for transfer over the network.
/// If the object is a container type, this function will call itself recursively and contain all contents.
std::string DTSC::DTMI::Pack(){
  std::string r = "";
  //skip output of DDV container types, they do not exist. Only output their contents.
  if (myType != DTMItype::DTMI_ROOT){r += myType;}
  //output the properly formatted data stream for this object's contents.
  switch (myType){
    case DTMItype::DTMI_INT:
      r += *(((char*)&numval)+7); r += *(((char*)&numval)+6);
      r += *(((char*)&numval)+5); r += *(((char*)&numval)+4);
      r += *(((char*)&numval)+3); r += *(((char*)&numval)+2);
      r += *(((char*)&numval)+1); r += *(((char*)&numval));
      break;
    case DTMItype::DTMI_STRING:
      r += strval.size() / (256*256*256);
      r += strval.size() / (256*256);
      r += strval.size() / 256;
      r += strval.size() % 256;
      r += strval;
      break;
    case DTMItype::DTMI_OBJECT:
      if (contents.size() > 0){
        for (std::vector<DTSC::DTMI>::iterator it = contents.begin(); it != contents.end(); it++){
          r += it->Indice().size() / 256;
          r += it->Indice().size() % 256;
          r += it->Indice();
          r += it->Pack();
        }
      }
      r += (char)0; r += (char)0; r += (char)9;
      break;
    case DTMItype::DTMI_ROOT://only send contents
      if (contents.size() > 0){
        for (std::vector<DTSC::DTMI>::iterator it = contents.begin(); it != contents.end(); it++){
          r += it->Pack();
        }
      }
      break;
  }
  return r;
};//pack

/// Parses a single AMF0 type - used recursively by the AMF::parse() functions.
/// This function updates i every call with the new position in the data.
/// \param data The raw data to parse.
/// \param len The size of the raw data.
/// \param i Current parsing position in the raw data.
/// \param name Indice name for any new object created.
/// \returns A single DTSC::DTMI, parsed from the raw data.
DTSC::DTMI DTSC::parseOneDTMI(const unsigned char *& data, unsigned int &len, unsigned int &i, std::string name){
  std::string tmpstr;
  unsigned int tmpi = 0;
  unsigned char tmpdbl[8];
  #if DEBUG >= 10
  fprintf(stderr, "Note: AMF type %hhx found. %i bytes left\n", data[i], len-i);
  #endif
  switch (data[i]){
    case DTMI::DTMI_INT:
      tmpdbl[7] = data[i+1];
      tmpdbl[6] = data[i+2];
      tmpdbl[5] = data[i+3];
      tmpdbl[4] = data[i+4];
      tmpdbl[3] = data[i+5];
      tmpdbl[2] = data[i+6];
      tmpdbl[1] = data[i+7];
      tmpdbl[0] = data[i+8];
      i+=9;//skip 8(a double)+1 forwards
      return DTSC::DTMI(name, *(uint64_t*)tmpdbl, AMF::AMF0_NUMBER);
      break;
    case DTMI::DTMI_STRING:
      tmpi = data[i+1]*256*256*256+data[i+2]*256*256+data[i+3]*256+data[i+4];//set tmpi to UTF-8-long length
      tmpstr.clear();//clean tmpstr, just to be sure
      tmpstr.append((const char *)data+i+5, (size_t)tmpi);//add the string data
      i += tmpi + 5;//skip length+size+1 forwards
      return DTSC::DTMI(name, tmpstr, AMF::AMF0_LONGSTRING);
      break;
    case DTMI::DTMI_OBJECT:{
      ++i;
      DTSC::DTMI ret(name, DTMI::DTMI_OBJECT);
      while (data[i] + data[i+1] != 0){//while not encountering 0x0000 (we assume 0x000009)
        tmpi = data[i]*256+data[i+1];//set tmpi to the UTF-8 length
        tmpstr.clear();//clean tmpstr, just to be sure
        tmpstr.append((const char*)data+i+2, (size_t)tmpi);//add the string data
        i += tmpi + 2;//skip length+size forwards
        ret.addContent(AMF::parseOne(data, len, i, tmpstr));//add content, recursively parsed, updating i, setting indice to tmpstr
      }
      i += 3;//skip 0x000009
      return ret;
      } break;
  }
  #if DEBUG >= 2
  fprintf(stderr, "Error: Unimplemented DTMI type %hhx - returning.\n", data[i]);
  #endif
  return DTSC::DTMI("error", DTMI::DTMI_ROOT);
}//parseOne

/// Parses a C-string to a valid DTSC::DTMI.
/// This function will find all AMF objects in the string and return
/// them all packed in a single AMF::AMF0_DDV_CONTAINER DTSC::DTMI.
DTSC::DTMI DTSC::parseDTMI(const unsigned char * data, unsigned int len){
  DTSC::DTMI ret("returned", DTMI::DTMI_ROOT);//container type
  unsigned int i = 0, j = 0;
  while (i < len){
    ret.addContent(AMF::parseOne(data, len, i, ""));
    if (i > j){j = i;}else{return ret;}
  }
  return ret;
}//parse

/// Parses a std::string to a valid DTSC::DTMI.
/// This function will find all AMF objects in the string and return
/// them all packed in a single AMF::AMF0_DDV_CONTAINER DTSC::DTMI.
DTSC::DTMI DTSC::parseDTMI(std::string data){
  return parse((const unsigned char*)data.c_str(), data.size());
}//parse
