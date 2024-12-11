/// \file amf.cpp
/// Holds all code for the AMF namespace.

#include "amf.h"
#include "defines.h"
#include <sstream>

AMF::Object nullPtr("error", AMF::AMF0_DDV_CONTAINER);

/// Returns the std::string Indice for the current object, if available.
/// Returns an empty string if no indice exists.
std::string AMF::Object::Indice() const{
  return myIndice;
}

/// Returns the AMF::obj0type AMF0 object type for this object.
AMF::obj0type AMF::Object::GetType(){
  return myType;
}

/// Returns the numeric value of this object, if available.
/// If this object holds no numeric value, 0 is returned.
double AMF::Object::NumValue(){
  return numval;
}

/// Returns the std::string value of this object, if available.
/// If this object holds no string value, an empty string is returned.
std::string AMF::Object::StrValue(){
  return strval;
}

/// Returns the C-string value of this object, if available.
/// If this object holds no string value, an empty C-string is returned.
const char *AMF::Object::Str(){
  return strval.c_str();
}

/// Returns a count of the amount of objects this object currently holds.
/// If this object is not a container type, this function will always return 0.
int AMF::Object::hasContent(){
  return contents.size();
}

/// Adds an AMF::Object to this object. Works for all types, but only makes sense for container
/// types.
void AMF::Object::addContent(AMF::Object c){
  contents.push_back(c);
}

/// Returns a pointer to the object held at indice i.
/// Returns AMF::AMF0_DDV_CONTAINER of indice "error" if no object is held at this indice.
/// \param i The indice of the object in this container.
AMF::Object *AMF::Object::getContentP(unsigned int i){
  if (i >= contents.size()){return &nullPtr;}
  return &contents.at(i);
}

/// Returns a copy of the object held at indice i.
/// Returns a AMF::AMF0_DDV_CONTAINER of indice "error" if no object is held at this indice.
/// \param i The indice of the object in this container.
AMF::Object AMF::Object::getContent(unsigned int i){
  if (i >= contents.size()){return AMF::Object("error", AMF0_DDV_CONTAINER);}
  return contents.at(i);
}

/// Returns a pointer to the object held at indice s.
/// Returns NULL if no object is held at this indice.
/// \param s The indice of the object in this container.
AMF::Object *AMF::Object::getContentP(std::string s){
  for (std::vector<AMF::Object>::iterator it = contents.begin(); it != contents.end(); it++){
    if (it->Indice() == s){return &(*it);}
  }
  return &nullPtr;
}

/// Returns a copy of the object held at indice s.
/// Returns a AMF::AMF0_DDV_CONTAINER of indice "error" if no object is held at this indice.
/// \param s The indice of the object in this container.
AMF::Object AMF::Object::getContent(std::string s){
  for (std::vector<AMF::Object>::iterator it = contents.begin(); it != contents.end(); it++){
    if (it->Indice() == s){return *it;}
  }
  return AMF::Object("error", AMF0_DDV_CONTAINER);
}

/// Default constructor.
/// Simply fills the data with AMF::Object("error", AMF0_DDV_CONTAINER)
AMF::Object::Object(){
  *this = AMF::Object("error", AMF0_DDV_CONTAINER);
}// default constructor

/// Constructor for numeric objects.
/// The object type is by default AMF::AMF0_NUMBER, but this can be forced to a different value.
/// \param indice The string indice of this object in its container, or empty string if none.
/// Numeric indices are automatic. \param val The numeric value of this object. Numeric AMF0 objects
/// only support double-type values. \param setType The object type to force this object to.
AMF::Object::Object(std::string indice, double val, AMF::obj0type setType){// num type initializer
  myIndice = indice;
  myType = setType;
  strval = "";
  numval = val;
}

/// Constructor for string objects.
/// The object type is by default AMF::AMF0_STRING, but this can be forced to a different value.
/// There is no need to manually change the type to AMF::AMF0_LONGSTRING, this will be done
/// automatically. \param indice The string indice of this object in its container, or empty string
/// if none. Numeric indices are automatic. \param val The string value of this object. \param
/// setType The object type to force this object to.
AMF::Object::Object(std::string indice, std::string val,
                    AMF::obj0type setType){// str type initializer
  myIndice = indice;
  myType = setType;
  strval = val;
  numval = 0;
}

/// Constructor for string objects.
/// The object type is by default AMF::AMF0_STRING, but this can be forced to a different value.
/// There is no need to manually change the type to AMF::AMF0_LONGSTRING, this will be done
/// automatically. \param indice The string indice of this object in its container, or empty string
/// if none. Numeric indices are automatic. \param val The string value of this object. \param
/// setType The object type to force this object to.
AMF::Object::Object(std::string indice, const char *val,
                    AMF::obj0type setType){// str type initializer
  myIndice = indice;
  myType = setType;
  strval = val;
  numval = 0;
}

/// Constructor for container objects.
/// The object type is by default AMF::AMF0_OBJECT, but this can be forced to a different value.
/// \param indice The string indice of this object in its container, or empty string if none.
/// Numeric indices are automatic. \param setType The object type to force this object to.
AMF::Object::Object(std::string indice, AMF::obj0type setType){// object type initializer
  myIndice = indice;
  myType = setType;
  strval = "";
  numval = 0;
}

/// Return the contents as a human-readable string.
/// If this object contains other objects, it will call itself recursively
/// and print all nested content as well.
std::string AMF::Object::Print(std::string indent){
  std::stringstream st;
  st << indent;
  // print my type
  switch (myType){
  case AMF::AMF0_NUMBER: st << "Number"; break;
  case AMF::AMF0_BOOL: st << "Bool"; break;
  case AMF::AMF0_STRING: // short string
  case AMF::AMF0_LONGSTRING: st << "String"; break;
  case AMF::AMF0_OBJECT: st << "Object"; break;
  case AMF::AMF0_MOVIECLIP: st << "MovieClip"; break;
  case AMF::AMF0_NULL: st << "Null"; break;
  case AMF::AMF0_UNDEFINED: st << "Undefined"; break;
  case AMF::AMF0_REFERENCE: st << "Reference"; break;
  case AMF::AMF0_ECMA_ARRAY: st << "ECMA Array"; break;
  case AMF::AMF0_OBJ_END: st << "Object end"; break;
  case AMF::AMF0_STRICT_ARRAY: st << "Strict Array"; break;
  case AMF::AMF0_DATE: st << "Date"; break;
  case AMF::AMF0_UNSUPPORTED: st << "Unsupported"; break;
  case AMF::AMF0_RECORDSET: st << "Recordset"; break;
  case AMF::AMF0_XMLDOC: st << "XML Document"; break;
  case AMF::AMF0_TYPED_OBJ: st << "Typed Object"; break;
  case AMF::AMF0_UPGRADE: st << "Upgrade to AMF3"; break;
  case AMF::AMF0_DDV_CONTAINER: st << "DDVTech Container"; break;
  }
  // print my string indice, if available
  st << " " << myIndice << " ";
  // print my numeric or string contents
  switch (myType){
  case AMF::AMF0_NUMBER:
  case AMF::AMF0_BOOL:
  case AMF::AMF0_REFERENCE:
  case AMF::AMF0_DATE: st << numval; break;
  case AMF::AMF0_STRING:
  case AMF::AMF0_LONGSTRING:
  case AMF::AMF0_XMLDOC:
  case AMF::AMF0_TYPED_OBJ: st << strval; break;
  default: break; // we don't care about the rest, and don't want a compiler warning...
  }
  st << std::endl;
  // if I hold other objects, print those too, recursively.
  if (contents.size() > 0){
    for (std::vector<AMF::Object>::iterator it = contents.begin(); it != contents.end(); it++){
      st << it->Print(indent + "  ");
    }
  }
  return st.str();
}// print

JSON::Value AMF::Object::toJSON() const{
  switch (myType){
  case AMF::AMF0_NUMBER:
  case AMF::AMF0_DATE:
  case AMF::AMF0_REFERENCE:
    return numval;
  case AMF::AMF0_BOOL:
    return (bool)numval;
  case AMF::AMF0_STRING:
  case AMF::AMF0_LONGSTRING:
  case AMF::AMF0_XMLDOC: // is always a longstring
    return strval;
  case AMF::AMF0_TYPED_OBJ: // is an object, with the classname first
  case AMF::AMF0_OBJECT:
  case AMF::AMF0_ECMA_ARRAY:{
    JSON::Value ret;
    if (contents.size() > 0){
      for (std::vector<AMF::Object>::const_iterator it = contents.begin(); it != contents.end(); it++){
        ret[it->Indice()] = it->toJSON();
      }
    }
    return ret;
  }
  case AMF::AMF0_MOVIECLIP:
  case AMF::AMF0_OBJ_END:
  case AMF::AMF0_UPGRADE:
  case AMF::AMF0_NULL:
  case AMF::AMF0_UNDEFINED:
  case AMF::AMF0_RECORDSET:
  case AMF::AMF0_UNSUPPORTED:
    // no data to add
    return JSON::Value();
  case AMF::AMF0_DDV_CONTAINER: // only send contents
  case AMF::AMF0_STRICT_ARRAY:{
    JSON::Value ret;
    if (contents.size() > 0){
      for (std::vector<AMF::Object>::const_iterator it = contents.begin(); it != contents.end(); it++){
        ret.append(it->toJSON());
      }
    }
    return ret;
  }
  }
  return JSON::Value();
}

/// Packs the AMF object to a std::string for transfer over the network.
/// If the object is a container type, this function will call itself recursively and contain all
/// contents. Tip: When sending multiple AMF objects in one go, put them in a single
/// AMF::AMF0_DDV_CONTAINER for easy transfer.
std::string AMF::Object::Pack(){
  std::string r = "";
  // check for string/longstring conversion
  if ((myType == AMF::AMF0_STRING) && (strval.size() > 0xFFFF)){myType = AMF::AMF0_LONGSTRING;}
  // skip output of DDV container types, they do not exist. Only output their contents.
  if (myType != AMF::AMF0_DDV_CONTAINER){r += myType;}
  // output the properly formatted AMF0 data stream for this object's contents.
  switch (myType){
  case AMF::AMF0_NUMBER:
    r += *(((char *)&numval) + 7);
    r += *(((char *)&numval) + 6);
    r += *(((char *)&numval) + 5);
    r += *(((char *)&numval) + 4);
    r += *(((char *)&numval) + 3);
    r += *(((char *)&numval) + 2);
    r += *(((char *)&numval) + 1);
    r += *(((char *)&numval));
    break;
  case AMF::AMF0_DATE:
    r += *(((char *)&numval) + 7);
    r += *(((char *)&numval) + 6);
    r += *(((char *)&numval) + 5);
    r += *(((char *)&numval) + 4);
    r += *(((char *)&numval) + 3);
    r += *(((char *)&numval) + 2);
    r += *(((char *)&numval) + 1);
    r += *(((char *)&numval));
    r += (char)0; // timezone always 0
    r += (char)0; // timezone always 0
    break;
  case AMF::AMF0_BOOL: r += (char)numval; break;
  case AMF::AMF0_STRING:
    r += strval.size() / 256;
    r += strval.size() % 256;
    r += strval;
    break;
  case AMF::AMF0_LONGSTRING:
  case AMF::AMF0_XMLDOC: // is always a longstring
    r += strval.size() / (256 * 256 * 256);
    r += strval.size() / (256 * 256);
    r += strval.size() / 256;
    r += strval.size() % 256;
    r += strval;
    break;
  case AMF::AMF0_TYPED_OBJ: // is an object, with the classname first
    r += Indice().size() / 256;
    r += Indice().size() % 256;
    r += Indice();
  /* no break */
  case AMF::AMF0_OBJECT:
    if (contents.size() > 0){
      for (std::vector<AMF::Object>::iterator it = contents.begin(); it != contents.end(); it++){
        r += it->Indice().size() / 256;
        r += it->Indice().size() % 256;
        r += it->Indice();
        r += it->Pack();
      }
    }
    r += (char)0;
    r += (char)0;
    r += (char)9;
    break;
  case AMF::AMF0_MOVIECLIP:
  case AMF::AMF0_OBJ_END:
  case AMF::AMF0_UPGRADE:
  case AMF::AMF0_NULL:
  case AMF::AMF0_UNDEFINED:
  case AMF::AMF0_RECORDSET:
  case AMF::AMF0_UNSUPPORTED:
    // no data to add
    break;
  case AMF::AMF0_REFERENCE:
    r += (char)((int)numval / 256);
    r += (char)((int)numval % 256);
    break;
  case AMF::AMF0_ECMA_ARRAY:{
    int arrlen = 0;
    if (contents.size() > 0){
      arrlen = contents.size();
      r += arrlen / (256 * 256 * 256);
      r += arrlen / (256 * 256);
      r += arrlen / 256;
      r += arrlen % 256;
      for (std::vector<AMF::Object>::iterator it = contents.begin(); it != contents.end(); it++){
        r += it->Indice().size() / 256;
        r += it->Indice().size() % 256;
        r += it->Indice();
        r += it->Pack();
      }
    }else{
      r += (char)0;
      r += (char)0;
      r += (char)0;
      r += (char)0;
    }
    r += (char)0;
    r += (char)0;
    r += (char)9;
  }break;
  case AMF::AMF0_STRICT_ARRAY:{
    int arrlen = 0;
    if (contents.size() > 0){
      arrlen = contents.size();
      r += arrlen / (256 * 256 * 256);
      r += arrlen / (256 * 256);
      r += arrlen / 256;
      r += arrlen % 256;
      for (std::vector<AMF::Object>::iterator it = contents.begin(); it != contents.end(); it++){
        r += it->Pack();
      }
    }else{
      r += (char)0;
      r += (char)0;
      r += (char)0;
      r += (char)0;
    }
  }break;
  case AMF::AMF0_DDV_CONTAINER: // only send contents
    if (contents.size() > 0){
      for (std::vector<AMF::Object>::iterator it = contents.begin(); it != contents.end(); it++){
        r += it->Pack();
      }
    }
    break;
  }
  return r;
}// pack

/// Parses a single AMF0 type - used recursively by the AMF::parse() functions.
/// This function updates i every call with the new position in the data.
/// \param data The raw data to parse.
/// \param len The size of the raw data.
/// \param i Current parsing position in the raw data.
/// \param name Indice name for any new object created.
/// \returns A single AMF::Object, parsed from the raw data.
AMF::Object AMF::parseOne(const unsigned char *&data, unsigned int &len, unsigned int &i, std::string name){
  std::string tmpstr;
  unsigned int tmpi = 0;
  unsigned char tmpdbl[8];
  double *d; // hack to work around strict aliasing
  switch (data[i]){
  case AMF::AMF0_NUMBER:
    if (i + 8 < len){
      tmpdbl[7] = data[i + 1];
      tmpdbl[6] = data[i + 2];
      tmpdbl[5] = data[i + 3];
      tmpdbl[4] = data[i + 4];
      tmpdbl[3] = data[i + 5];
      tmpdbl[2] = data[i + 6];
      tmpdbl[1] = data[i + 7];
      tmpdbl[0] = data[i + 8];
      d = (double *)tmpdbl;
    }else{
      d = 0;
    }
    i += 9; // skip 8(a double)+1 forwards
    return AMF::Object(name, *d, AMF::AMF0_NUMBER);
    break;
  case AMF::AMF0_DATE:
    if (i + 8 < len){
      tmpdbl[7] = data[i + 1];
      tmpdbl[6] = data[i + 2];
      tmpdbl[5] = data[i + 3];
      tmpdbl[4] = data[i + 4];
      tmpdbl[3] = data[i + 5];
      tmpdbl[2] = data[i + 6];
      tmpdbl[1] = data[i + 7];
      tmpdbl[0] = data[i + 8];
      d = (double *)tmpdbl;
    }else{
      d = 0;
    }
    i += 11; // skip 8(a double)+1+timezone(2) forwards
    return AMF::Object(name, *d, AMF::AMF0_DATE);
    break;
  case AMF::AMF0_BOOL:
    i += 2; // skip bool+1 forwards
    if (data[i - 1] == 0){
      return AMF::Object(name, (double)0, AMF::AMF0_BOOL);
    }else{
      return AMF::Object(name, (double)1, AMF::AMF0_BOOL);
    }
    break;
  case AMF::AMF0_REFERENCE:
    tmpi = data[i + 1] * 256 + data[i + 2]; // get the ref number value as a double
    i += 3;                                 // skip ref+1 forwards
    return AMF::Object(name, (double)tmpi, AMF::AMF0_REFERENCE);
    break;
  case AMF::AMF0_XMLDOC:
    tmpi = data[i + 1] * 256 * 256 * 256 + data[i + 2] * 256 * 256 + data[i + 3] * 256 + data[i + 4]; // set tmpi to UTF-8-long length
    tmpstr.clear();                                          // clean tmpstr, just to be sure
    if (tmpi + i + 5 >= len){tmpi = len - i - 5;}            // ensure we don't go out of bounds
    tmpstr.append((const char *)data + i + 5, (size_t)tmpi); // add the string data
    i += tmpi + 5;                                           // skip length+size+1 forwards
    return AMF::Object(name, tmpstr, AMF::AMF0_XMLDOC);
    break;
  case AMF::AMF0_LONGSTRING:
    tmpi = data[i + 1] * 256 * 256 * 256 + data[i + 2] * 256 * 256 + data[i + 3] * 256 + data[i + 4]; // set tmpi to UTF-8-long length
    tmpstr.clear();                                          // clean tmpstr, just to be sure
    if (tmpi + i + 5 >= len){tmpi = len - i - 5;}            // ensure we don't go out of bounds
    tmpstr.append((const char *)data + i + 5, (size_t)tmpi); // add the string data
    i += tmpi + 5;                                           // skip length+size+1 forwards
    return AMF::Object(name, tmpstr, AMF::AMF0_LONGSTRING);
    break;
  case AMF::AMF0_STRING:
    tmpi = data[i + 1] * 256 + data[i + 2];                  // set tmpi to UTF-8 length
    tmpstr.clear();                                          // clean tmpstr, just to be sure
    if (tmpi + i + 3 >= len){tmpi = len - i - 3;}            // ensure we don't go out of bounds
    tmpstr.append((const char *)data + i + 3, (size_t)tmpi); // add the string data
    i += tmpi + 3;                                           // skip length+size+1 forwards
    return AMF::Object(name, tmpstr, AMF::AMF0_STRING);
    break;
  case AMF::AMF0_NULL:
  case AMF::AMF0_UNDEFINED:
  case AMF::AMF0_UNSUPPORTED:
    ++i;
    return AMF::Object(name, (double)0, (AMF::obj0type)data[i - 1]);
    break;
  case AMF::AMF0_OBJECT:{
    ++i;
    AMF::Object ret(name, AMF::AMF0_OBJECT);
    while (i + 1 < len && data[i] + data[i + 1] != 0){// while not encountering 0x0000 (we assume 0x000009)
      tmpi = data[i] * 256 + data[i + 1]; // set tmpi to the UTF-8 length
      tmpstr.clear();                     // clean tmpstr, just to be sure
      if (tmpi + i + 2 >= len){tmpi = len - i - 2;}            // ensure we don't go out of bounds
      tmpstr.append((const char *)data + i + 2, (size_t)tmpi); // add the string data
      i += tmpi + 2;                                           // skip length+size forwards
      ret.addContent(AMF::parseOne(data, len, i,
                                   tmpstr)); // add content, recursively parsed, updating i, setting indice to tmpstr
    }
    i += 3; // skip 0x000009
    return ret;
  }break;
  case AMF::AMF0_TYPED_OBJ:{
    ++i;
    tmpi = data[i] * 256 + data[i + 1];                      // set tmpi to the UTF-8 length
    tmpstr.clear();                                          // clean tmpstr, just to be sure
    if (tmpi + i + 2 >= len){tmpi = len - i - 2;}            // ensure we don't go out of bounds
    tmpstr.append((const char *)data + i + 2, (size_t)tmpi); // add the string data
    AMF::Object ret(tmpstr, AMF::AMF0_TYPED_OBJ); // the object is not named "name" but tmpstr
    while (i + 1 < len && data[i] + data[i + 1] != 0){// while not encountering 0x0000 (we assume 0x000009)
      tmpi = data[i] * 256 + data[i + 1]; // set tmpi to the UTF-8 length
      tmpstr.clear();                     // clean tmpstr, just to be sure
      tmpstr.append((const char *)data + i + 2, (size_t)tmpi); // add the string data
      i += tmpi + 2;                                           // skip length+size forwards
      ret.addContent(AMF::parseOne(data, len, i,
                                   tmpstr)); // add content, recursively parsed, updating i, setting indice to tmpstr
    }
    i += 3; // skip 0x000009
    return ret;
  }break;
  case AMF::AMF0_ECMA_ARRAY:{
    ++i;
    AMF::Object ret(name, AMF::AMF0_ECMA_ARRAY);
    i += 4;                               // ignore the array length, we re-calculate it
    while (i + 1 < len && data[i] + data[i + 1] != 0){// while not encountering 0x0000 (we assume 0x000009)
      tmpi = data[i] * 256 + data[i + 1]; // set tmpi to the UTF-8 length
      tmpstr.clear();                     // clean tmpstr, just to be sure
      if (tmpi + i + 2 >= len){tmpi = len - i - 2;}            // ensure we don't go out of bounds
      tmpstr.append((const char *)data + i + 2, (size_t)tmpi); // add the string data
      i += tmpi + 2;                                           // skip length+size forwards
      ret.addContent(AMF::parseOne(data, len, i,
                                   tmpstr)); // add content, recursively parsed, updating i, setting indice to tmpstr
    }
    i += 3; // skip 0x000009
    return ret;
  }break;
  case AMF::AMF0_STRICT_ARRAY:{
    AMF::Object ret(name, AMF::AMF0_STRICT_ARRAY);
    tmpi = data[i + 1] * 256 * 256 * 256 + data[i + 2] * 256 * 256 + data[i + 3] * 256 + data[i + 4]; // set tmpi to array length
    i += 5;                                                  // skip size+1 forwards
    while (tmpi > 0){// while not done parsing array
      ret.addContent(AMF::parseOne(data, len, i, "arrVal")); // add content, recursively parsed, updating i
      --tmpi;
    }
    return ret;
  }break;
  }
  ERROR_MSG("Error: Unimplemented AMF type %hhx - returning.", data[i]);
  i = len;
  return AMF::Object("error", AMF::AMF0_DDV_CONTAINER);
}// parseOne

/// Parses a C-string to a valid AMF::Object.
/// This function will find all AMF objects in the string and return
/// them all packed in a single AMF::AMF0_DDV_CONTAINER AMF::Object.
AMF::Object AMF::parse(const unsigned char *data, unsigned int len){
  AMF::Object ret("returned", AMF::AMF0_DDV_CONTAINER); // container type
  unsigned int i = 0, j = 0;
  while (i < len){
    ret.addContent(AMF::parseOne(data, len, i, ""));
    if (i > j){
      j = i;
    }else{
      return ret;
    }
  }
  return ret;
}// parse

/// Parses a std::string to a valid AMF::Object.
/// This function will find all AMF objects in the string and return
/// them all packed in a single AMF::AMF0_DDV_CONTAINER AMF::Object.
AMF::Object AMF::parse(std::string data){
  return AMF::parse((const unsigned char *)data.c_str(), data.size());
}// parse

/// Returns the std::string Indice for the current object, if available.
/// Returns an empty string if no indice exists.
std::string AMF::Object3::Indice() const{
  return myIndice;
}

/// Returns the AMF::obj0type AMF0 object type for this object.
AMF::obj3type AMF::Object3::GetType(){
  return myType;
}

/// Returns the double value of this object, if available.
/// If this object holds no double value, 0 is returned.
double AMF::Object3::DblValue(){
  return dblval;
}

/// Returns the integer value of this object, if available.
/// If this object holds no integer value, 0 is returned.
int AMF::Object3::IntValue(){
  return intval;
}

/// Returns the std::string value of this object, if available.
/// If this object holds no string value, an empty string is returned.
std::string AMF::Object3::StrValue(){
  return strval;
}

/// Returns the C-string value of this object, if available.
/// If this object holds no string value, an empty C-string is returned.
const char *AMF::Object3::Str(){
  return strval.c_str();
}

/// Returns a count of the amount of objects this object currently holds.
/// If this object is not a container type, this function will always return 0.
int AMF::Object3::hasContent(){
  return contents.size();
}

/// Adds an AMF::Object to this object. Works for all types, but only makes sense for container
/// types.
void AMF::Object3::addContent(AMF::Object3 c){
  contents.push_back(c);
}

/// Returns a pointer to the object held at indice i.
/// Returns AMF::AMF3_DDV_CONTAINER of indice "error" if no object is held at this indice.
/// \param i The indice of the object in this container.
AMF::Object3 *AMF::Object3::getContentP(int i){
  return &contents.at(i);
}

/// Returns a copy of the object held at indice i.
/// Returns a AMF::AMF3_DDV_CONTAINER of indice "error" if no object is held at this indice.
/// \param i The indice of the object in this container.
AMF::Object3 AMF::Object3::getContent(int i){
  return contents.at(i);
}

/// Returns a pointer to the object held at indice s.
/// Returns NULL if no object is held at this indice.
/// \param s The indice of the object in this container.
AMF::Object3 *AMF::Object3::getContentP(std::string s){
  for (std::vector<AMF::Object3>::iterator it = contents.begin(); it != contents.end(); it++){
    if (it->Indice() == s){return &(*it);}
  }
  return 0;
}

/// Returns a copy of the object held at indice s.
/// Returns a AMF::AMF0_DDV_CONTAINER of indice "error" if no object is held at this indice.
/// \param s The indice of the object in this container.
AMF::Object3 AMF::Object3::getContent(std::string s){
  for (std::vector<AMF::Object3>::iterator it = contents.begin(); it != contents.end(); it++){
    if (it->Indice() == s){return *it;}
  }
  return AMF::Object3("error", AMF3_DDV_CONTAINER);
}

/// Default constructor.
/// Simply fills the data with AMF::Object3("error", AMF3_DDV_CONTAINER)
AMF::Object3::Object3(){
  *this = AMF::Object3("error", AMF3_DDV_CONTAINER);
}// default constructor

/// Constructor for double objects.
/// The object type is by default AMF::AMF3_DOUBLE, but this can be forced to a different value.
/// \param indice The string indice of this object in its container, or empty string if none.
/// Numeric indices are automatic. \param val The numeric value of this object. Double AMF3 objects
/// only support double-type values. \param setType The object type to force this object to.
AMF::Object3::Object3(std::string indice, double val,
                      AMF::obj3type setType){// num type initializer
  myIndice = indice;
  myType = setType;
  strval = "";
  dblval = val;
  intval = 0;
}

/// Constructor for integer objects.
/// The object type is by default AMF::AMF3_INTEGER, but this can be forced to a different value.
/// \param indice The string indice of this object in its container, or empty string if none.
/// Numeric indices are automatic. \param val The numeric value of this object. Integer AMF3 objects
/// only support integer-type values. \param setType The object type to force this object to.
AMF::Object3::Object3(std::string indice, int val, AMF::obj3type setType){// num type initializer
  myIndice = indice;
  myType = setType;
  strval = "";
  dblval = val;
  intval = 0;
}

/// Constructor for string objects.
/// The object type is by default AMF::AMF0_STRING, but this can be forced to a different value.
/// There is no need to manually change the type to AMF::AMF0_LONGSTRING, this will be done
/// automatically. \param indice The string indice of this object in its container, or empty string
/// if none. Numeric indices are automatic. \param val The string value of this object. \param
/// setType The object type to force this object to.
AMF::Object3::Object3(std::string indice, std::string val,
                      AMF::obj3type setType){// str type initializer
  myIndice = indice;
  myType = setType;
  strval = val;
  dblval = 0;
  intval = 0;
}

/// Constructor for container objects.
/// The object type is by default AMF::AMF0_OBJECT, but this can be forced to a different value.
/// \param indice The string indice of this object in its container, or empty string if none.
/// Numeric indices are automatic. \param setType The object type to force this object to.
AMF::Object3::Object3(std::string indice, AMF::obj3type setType){// object type initializer
  myIndice = indice;
  myType = setType;
  strval = "";
  dblval = 0;
  intval = 0;
}

/// Return the contents as a human-readable string.
/// If this object contains other objects, it will call itself recursively
/// and print all nested content as well.
std::string AMF::Object3::Print(std::string indent){
  std::stringstream st;
  st << indent;
  // print my type
  switch (myType){
  case AMF::AMF3_UNDEFINED: st << "Undefined"; break;
  case AMF::AMF3_NULL: st << "Null"; break;
  case AMF::AMF3_FALSE: st << "False"; break;
  case AMF::AMF3_TRUE: st << "True"; break;
  case AMF::AMF3_INTEGER: st << "Integer"; break;
  case AMF::AMF3_DOUBLE: st << "Double"; break;
  case AMF::AMF3_STRING: st << "String"; break;
  case AMF::AMF3_XMLDOC: st << "XML Doc"; break;
  case AMF::AMF3_DATE: st << "Date"; break;
  case AMF::AMF3_ARRAY: st << "Array"; break;
  case AMF::AMF3_OBJECT: st << "Object"; break;
  case AMF::AMF3_XML: st << "XML"; break;
  case AMF::AMF3_BYTES: st << "ByteArray"; break;
  case AMF::AMF3_DDV_CONTAINER: st << "DDVTech Container"; break;
  }
  // print my string indice, if available
  st << " " << myIndice << " ";
  // print my numeric or string contents
  switch (myType){
  case AMF::AMF3_INTEGER: st << intval; break;
  case AMF::AMF3_DOUBLE: st << dblval; break;
  case AMF::AMF3_STRING:
  case AMF::AMF3_XMLDOC:
  case AMF::AMF3_XML:
  case AMF::AMF3_BYTES:
    if (intval > 0){
      st << "REF" << intval;
    }else{
      st << strval;
    }
    break;
  case AMF::AMF3_DATE:
    if (intval > 0){
      st << "REF" << intval;
    }else{
      st << dblval;
    }
    break;
  case AMF::AMF3_ARRAY:
  case AMF::AMF3_OBJECT:
    if (intval > 0){st << "REF" << intval;}
    break;
  default: break; // we don't care about the rest, and don't want a compiler warning...
  }
  st << std::endl;
  // if I hold other objects, print those too, recursively.
  if (contents.size() > 0){
    for (std::vector<AMF::Object3>::iterator it = contents.begin(); it != contents.end(); it++){
      st << it->Print(indent + "  ");
    }
  }
  return st.str();
}// print

/// Packs the AMF object to a std::string for transfer over the network.
/// If the object is a container type, this function will call itself recursively and contain all
/// contents. Tip: When sending multiple AMF objects in one go, put them in a single
/// AMF::AMF0_DDV_CONTAINER for easy transfer.
std::string AMF::Object3::Pack(){
  /// \TODO Implement
  std::string r = "";
  return r;
}// pack

JSON::Value AMF::Object3::toJSON() const{
  /// \TODO Implement
  return JSON::Value();
}

/// Parses a single AMF3 type - used recursively by the AMF::parse3() functions.
/// This function updates i every call with the new position in the data.
/// \param data The raw data to parse.
/// \param len The size of the raw data.
/// \param i Current parsing position in the raw data.
/// \param name Indice name for any new object created.
/// \returns A single AMF::Object3, parsed from the raw data.
AMF::Object3 AMF::parseOne3(const unsigned char *&data, unsigned int &len, unsigned int &i, std::string name){
  std::string tmpstr;
  unsigned int tmpi = 0;
  unsigned int arrsize = 0;
  unsigned char tmpdbl[8];
  double *d; // hack to work around strict aliasing
  switch (data[i]){
  case AMF::AMF3_UNDEFINED:
  case AMF::AMF3_NULL:
  case AMF::AMF3_FALSE:
  case AMF::AMF3_TRUE:
    ++i;
    return AMF::Object3(name, (AMF::obj3type)data[i - 1]);
    break;
  case AMF::AMF3_INTEGER:
    if (data[i + 1] < 0x80){
      tmpi = data[i + 1];
      i += 2;
    }else{
      tmpi = (data[i + 1] & 0x7F) << 7; // strip the upper bit, shift 7 up.
      if (data[i + 2] < 0x80){
        tmpi |= data[i + 2];
        i += 3;
      }else{
        tmpi = (tmpi | (data[i + 2] & 0x7F)) << 7; // strip the upper bit, shift 7 up.
        if (data[i + 3] < 0x80){
          tmpi |= data[i + 3];
          i += 4;
        }else{
          tmpi = (tmpi | (data[i + 3] & 0x7F)) << 8; // strip the upper bit, shift 7 up.
          tmpi |= data[i + 4];
          i += 5;
        }
      }
    }
    tmpi = (tmpi << 3) >> 3; // fix sign bit
    return AMF::Object3(name, (int)tmpi, AMF::AMF3_INTEGER);
    break;
  case AMF::AMF3_DOUBLE:
    tmpdbl[7] = data[i + 1];
    tmpdbl[6] = data[i + 2];
    tmpdbl[5] = data[i + 3];
    tmpdbl[4] = data[i + 4];
    tmpdbl[3] = data[i + 5];
    tmpdbl[2] = data[i + 6];
    tmpdbl[1] = data[i + 7];
    tmpdbl[0] = data[i + 8];
    i += 9; // skip 8(a double)+1 forwards
    d = (double *)tmpdbl;
    return AMF::Object3(name, *d, AMF::AMF3_DOUBLE);
    break;
  case AMF::AMF3_STRING:
    if (data[i + 1] < 0x80){
      tmpi = data[i + 1];
      i += 2;
    }else{
      tmpi = (data[i + 1] & 0x7F) << 7; // strip the upper bit, shift 7 up.
      if (data[i + 2] < 0x80){
        tmpi |= data[i + 2];
        i += 3;
      }else{
        tmpi = (tmpi | (data[i + 2] & 0x7F)) << 7; // strip the upper bit, shift 7 up.
        if (data[i + 3] < 0x80){
          tmpi |= data[i + 3];
          i += 4;
        }else{
          tmpi = (tmpi | (data[i + 3] & 0x7F)) << 8; // strip the upper bit, shift 7 up.
          tmpi |= data[i + 4];
          i += 5;
        }
      }
    }
    tmpi = (tmpi << 3) >> 3; // fix sign bit
    if ((tmpi & 1) == 0){
      return AMF::Object3(name, (int)((tmpi >> 1) + 1), AMF::AMF3_STRING); // reference type
    }
    tmpstr.clear();                                             // clean tmpstr, just to be sure
    tmpstr.append((const char *)data + i, (size_t)(tmpi >> 1)); // add the string data
    i += (tmpi >> 1);                                           // skip length+size+1 forwards
    return AMF::Object3(name, tmpstr, AMF::AMF3_STRING);        // normal type
    break;
  case AMF::AMF3_XMLDOC:
    if (data[i + 1] < 0x80){
      tmpi = data[i + 1];
      i += 2;
    }else{
      tmpi = (data[i + 1] & 0x7F) << 7; // strip the upper bit, shift 7 up.
      if (data[i + 2] < 0x80){
        tmpi |= data[i + 2];
        i += 3;
      }else{
        tmpi = (tmpi | (data[i + 2] & 0x7F)) << 7; // strip the upper bit, shift 7 up.
        if (data[i + 3] < 0x80){
          tmpi |= data[i + 3];
          i += 4;
        }else{
          tmpi = (tmpi | (data[i + 3] & 0x7F)) << 8; // strip the upper bit, shift 7 up.
          tmpi |= data[i + 4];
          i += 5;
        }
      }
    }
    tmpi = (tmpi << 3) >> 3; // fix sign bit
    if ((tmpi & 1) == 0){
      return AMF::Object3(name, (int)((tmpi >> 1) + 1), AMF::AMF3_XMLDOC); // reference type
    }
    tmpstr.clear();                                             // clean tmpstr, just to be sure
    tmpstr.append((const char *)data + i, (size_t)(tmpi >> 1)); // add the string data
    i += (tmpi >> 1);                                           // skip length+size+1 forwards
    return AMF::Object3(name, tmpstr, AMF::AMF3_XMLDOC);        // normal type
    break;
  case AMF::AMF3_XML:
    if (data[i + 1] < 0x80){
      tmpi = data[i + 1];
      i += 2;
    }else{
      tmpi = (data[i + 1] & 0x7F) << 7; // strip the upper bit, shift 7 up.
      if (data[i + 2] < 0x80){
        tmpi |= data[i + 2];
        i += 3;
      }else{
        tmpi = (tmpi | (data[i + 2] & 0x7F)) << 7; // strip the upper bit, shift 7 up.
        if (data[i + 3] < 0x80){
          tmpi |= data[i + 3];
          i += 4;
        }else{
          tmpi = (tmpi | (data[i + 3] & 0x7F)) << 8; // strip the upper bit, shift 7 up.
          tmpi |= data[i + 4];
          i += 5;
        }
      }
    }
    tmpi = (tmpi << 3) >> 3; // fix sign bit
    if ((tmpi & 1) == 0){
      return AMF::Object3(name, (int)((tmpi >> 1) + 1), AMF::AMF3_XML); // reference type
    }
    tmpstr.clear();                                             // clean tmpstr, just to be sure
    tmpstr.append((const char *)data + i, (size_t)(tmpi >> 1)); // add the string data
    i += (tmpi >> 1);                                           // skip length+size+1 forwards
    return AMF::Object3(name, tmpstr, AMF::AMF3_XML);           // normal type
    break;
  case AMF::AMF3_BYTES:
    if (data[i + 1] < 0x80){
      tmpi = data[i + 1];
      i += 2;
    }else{
      tmpi = (data[i + 1] & 0x7F) << 7; // strip the upper bit, shift 7 up.
      if (data[i + 2] < 0x80){
        tmpi |= data[i + 2];
        i += 3;
      }else{
        tmpi = (tmpi | (data[i + 2] & 0x7F)) << 7; // strip the upper bit, shift 7 up.
        if (data[i + 3] < 0x80){
          tmpi |= data[i + 3];
          i += 4;
        }else{
          tmpi = (tmpi | (data[i + 3] & 0x7F)) << 8; // strip the upper bit, shift 7 up.
          tmpi |= data[i + 4];
          tmpi = (tmpi << 3) >> 3; // fix sign bit
          i += 5;
        }
      }
    }
    if ((tmpi & 1) == 0){
      return AMF::Object3(name, (int)((tmpi >> 1) + 1), AMF::AMF3_BYTES); // reference type
    }
    tmpstr.clear();                                             // clean tmpstr, just to be sure
    tmpstr.append((const char *)data + i, (size_t)(tmpi >> 1)); // add the string data
    i += (tmpi >> 1);                                           // skip length+size+1 forwards
    return AMF::Object3(name, tmpstr, AMF::AMF3_BYTES);         // normal type
    break;
  case AMF::AMF3_DATE:
    if (data[i + 1] < 0x80){
      tmpi = data[i + 1];
      i += 2;
    }else{
      tmpi = (data[i + 1] & 0x7F) << 7; // strip the upper bit, shift 7 up.
      if (data[i + 2] < 0x80){
        tmpi |= data[i + 2];
        i += 3;
      }else{
        tmpi = (tmpi | (data[i + 2] & 0x7F)) << 7; // strip the upper bit, shift 7 up.
        if (data[i + 3] < 0x80){
          tmpi |= data[i + 3];
          i += 4;
        }else{
          tmpi = (tmpi | (data[i + 3] & 0x7F)) << 8; // strip the upper bit, shift 7 up.
          tmpi |= data[i + 4];
          i += 5;
        }
      }
    }
    tmpi = (tmpi << 3) >> 3; // fix sign bit
    if ((tmpi & 1) == 0){
      return AMF::Object3(name, (int)((tmpi >> 1) + 1), AMF::AMF3_DATE); // reference type
    }
    tmpdbl[7] = data[i];
    tmpdbl[6] = data[i + 1];
    tmpdbl[5] = data[i + 2];
    tmpdbl[4] = data[i + 3];
    tmpdbl[3] = data[i + 4];
    tmpdbl[2] = data[i + 5];
    tmpdbl[1] = data[i + 6];
    tmpdbl[0] = data[i + 7];
    d = (double *)tmpdbl;
    i += 8; // skip a double forwards
    return AMF::Object3(name, *d, AMF::AMF3_DATE);
    break;
  case AMF::AMF3_ARRAY:{
    if (data[i + 1] < 0x80){
      tmpi = data[i + 1];
      i += 2;
    }else{
      tmpi = (data[i + 1] & 0x7F) << 7; // strip the upper bit, shift 7 up.
      if (data[i + 2] < 0x80){
        tmpi |= data[i + 2];
        i += 3;
      }else{
        tmpi = (tmpi | (data[i + 2] & 0x7F)) << 7; // strip the upper bit, shift 7 up.
        if (data[i + 3] < 0x80){
          tmpi |= data[i + 3];
          i += 4;
        }else{
          tmpi = (tmpi | (data[i + 3] & 0x7F)) << 8; // strip the upper bit, shift 7 up.
          tmpi |= data[i + 4];
          i += 5;
        }
      }
    }
    tmpi = (tmpi << 3) >> 3; // fix sign bit
    if ((tmpi & 1) == 0){
      return AMF::Object3(name, (int)((tmpi >> 1) + 1), AMF::AMF3_ARRAY); // reference type
    }
    AMF::Object3 ret(name, AMF::AMF3_ARRAY);
    arrsize = tmpi >> 1;
    do{
      if (data[i + 1] < 0x80){
        tmpi = data[i + 1];
        i += 2;
      }else{
        tmpi = (data[i + 1] & 0x7F) << 7; // strip the upper bit, shift 7 up.
        if (data[i + 2] < 0x80){
          tmpi |= data[i + 2];
          i += 3;
        }else{
          tmpi = (tmpi | (data[i + 2] & 0x7F)) << 7; // strip the upper bit, shift 7 up.
          if (data[i + 3] < 0x80){
            tmpi |= data[i + 3];
            i += 4;
          }else{
            tmpi = (tmpi | (data[i + 3] & 0x7F)) << 8; // strip the upper bit, shift 7 up.
            tmpi |= data[i + 4];
            i += 5;
          }
        }
      }
      tmpi = (tmpi << 3) >> 4; // fix sign bit, ignore references for now...
      /// \todo Fix references?
      if (tmpi > 0){
        tmpstr.clear();                                      // clean tmpstr, just to be sure
        tmpstr.append((const char *)data + i, (size_t)tmpi); // add the string data
        ret.addContent(AMF::parseOne3(data, len, i, tmpstr)); // add content, recursively parsed, updating i
      }
    }while (tmpi > 0);
    while (arrsize > 0){// while not done parsing array
      ret.addContent(AMF::parseOne3(data, len, i, "arrVal")); // add content, recursively parsed, updating i
      --arrsize;
    }
    return ret;
  }break;
  case AMF::AMF3_OBJECT:{
    if (data[i + 1] < 0x80){
      tmpi = data[i + 1];
      i += 2;
    }else{
      tmpi = (data[i + 1] & 0x7F) << 7; // strip the upper bit, shift 7 up.
      if (data[i + 2] < 0x80){
        tmpi |= data[i + 2];
        i += 3;
      }else{
        tmpi = (tmpi | (data[i + 2] & 0x7F)) << 7; // strip the upper bit, shift 7 up.
        if (data[i + 3] < 0x80){
          tmpi |= data[i + 3];
          i += 4;
        }else{
          tmpi = (tmpi | (data[i + 3] & 0x7F)) << 8; // strip the upper bit, shift 7 up.
          tmpi |= data[i + 4];
          i += 5;
        }
      }
    }
    tmpi = (tmpi << 3) >> 3; // fix sign bit
    if ((tmpi & 1) == 0){
      return AMF::Object3(name, (int)((tmpi >> 1) + 1), AMF::AMF3_OBJECT); // reference type
    }
    AMF::Object3 ret(name, AMF::AMF3_OBJECT);
    bool isdynamic = false;
    if ((tmpi & 2) == 0){// traits by reference, skip for now
      /// \todo Implement traits by reference. Or references in general, of course...
    }else{
      isdynamic = ((tmpi & 8) == 8);
      arrsize = tmpi >> 4; // count of sealed members
      /// \todo Read in arrsize sealed member names, then arrsize sealed members.
    }
    if (isdynamic){
      do{
        if (data[i + 1] < 0x80){
          tmpi = data[i + 1];
          i += 2;
        }else{
          tmpi = (data[i + 1] & 0x7F) << 7; // strip the upper bit, shift 7 up.
          if (data[i + 2] < 0x80){
            tmpi |= data[i + 2];
            i += 3;
          }else{
            tmpi = (tmpi | (data[i + 2] & 0x7F)) << 7; // strip the upper bit, shift 7 up.
            if (data[i + 3] < 0x80){
              tmpi |= data[i + 3];
              i += 4;
            }else{
              tmpi = (tmpi | (data[i + 3] & 0x7F)) << 8; // strip the upper bit, shift 7 up.
              tmpi |= data[i + 4];
              i += 5;
            }
          }
        }
        tmpi = (tmpi << 3) >> 4; // fix sign bit, ignore references for now...
        /// \todo Fix references?
        if (tmpi > 0){
          tmpstr.clear();                                      // clean tmpstr, just to be sure
          tmpstr.append((const char *)data + i, (size_t)tmpi); // add the string data
          ret.addContent(AMF::parseOne3(data, len, i, tmpstr)); // add content, recursively parsed, updating i
        }
      }while (tmpi > 0); // keep reading dynamic values until empty string
    }// dynamic types
    return ret;
  }break;
  }
  ERROR_MSG("Error: Unimplemented AMF3 type %hhx - returning.", data[i]);
  i = len;
  return AMF::Object3("error", AMF::AMF3_DDV_CONTAINER);
}// parseOne

/// Parses a C-string to a valid AMF::Object3.
/// This function will find all AMF3 objects in the string and return
/// them all packed in a single AMF::AMF3_DDV_CONTAINER AMF::Object3.
AMF::Object3 AMF::parse3(const unsigned char *data, unsigned int len){
  AMF::Object3 ret("returned", AMF::AMF3_DDV_CONTAINER); // container type
  unsigned int i = 0, j = 0;
  while (i < len){
    ret.addContent(AMF::parseOne3(data, len, i, ""));
    if (i > j){
      j = i;
    }else{
      return ret;
    }
  }
  return ret;
}// parse

/// Parses a std::string to a valid AMF::Object3.
/// This function will find all AMF3 objects in the string and return
/// them all packed in a single AMF::AMF3_DDV_CONTAINER AMF::Object3.
AMF::Object3 AMF::parse3(std::string data){
  return AMF::parse3((const unsigned char *)data.c_str(), data.size());
}// parse
