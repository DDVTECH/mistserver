#include <vector>
#include <string.h>
#include <string>

#define AMF0_NUMBER 0x00
#define AMF0_BOOL 0x01
#define AMF0_STRING 0x02
#define AMF0_OBJECT 0x03
#define AMF0_MOVIECLIP 0x04
#define AMF0_NULL 0x05
#define AMF0_UNDEFINED 0x06
#define AMF0_REFERENCE 0x07
#define AMF0_ECMA_ARRAY 0x08
#define AMF0_OBJ_END 0x09
#define AMF0_STRICT_ARRAY 0x0A
#define AMF0_DATE 0x0B
#define AMF0_LONGSTRING 0x0C
#define AMF0_UNSUPPORTED 0x0D
#define AMF0_RECORDSET 0x0E
#define AMF0_XMLDOC 0x0F
#define AMF0_TYPED_OBJ 0x10
#define AMF0_UPGRADE 0x11
#define AMF0_DDV_CONTAINER 0xFF

class AMFType {
  public:
    std::string Indice(){return myIndice;};
    unsigned char GetType(){return myType;};
    double NumValue(){return numval;};
    std::string StrValue(){return strval;};
    const char * Str(){return strval.c_str();};
    int hasContent(){
      if (!contents){return 0;}
      return contents->size();
    };
    void addContent(AMFType c){if (contents != 0){contents->push_back(c);}};
    AMFType* getContentP(int i){if (contents != 0){return &contents->at(i);}else{return 0;}};
    AMFType getContent(int i){if (contents != 0){return contents->at(i);}else{return AMFType("error");}};
    AMFType* getContentP(std::string s){
      if (contents != 0){
        for (std::vector<AMFType>::iterator it = contents->begin(); it != contents->end(); it++){
          if (it->Indice() == s){
            return &(*it);
          }
        }
      }
      return this;
    };
    AMFType getContent(std::string s){
      if (contents != 0){
        for (std::vector<AMFType>::iterator it = contents->begin(); it != contents->end(); it++){
          if (it->Indice() == s){
            return *it;
          }
        }
      }
      return AMFType("error");
    };
    AMFType(std::string indice, double val, unsigned char setType = AMF0_NUMBER){//num type initializer
      myIndice = indice;
      myType = setType;
      strval = "";
      numval = val;
      contents = 0;
    };
    AMFType(std::string indice, std::string val, unsigned char setType = AMF0_STRING){//str type initializer
      myIndice = indice;
      myType = setType;
      strval = val;
      numval = 0;
      contents = 0;
    };
    AMFType(std::string indice, unsigned char setType = AMF0_OBJECT){//object type initializer
      myIndice = indice;
      myType = setType;
      strval = "";
      numval = 0;
      contents = new std::vector<AMFType>;
    };
    ~AMFType(){if (contents != 0){delete contents;contents=0;}};
    AMFType& operator=(const AMFType &a) {
      myIndice = a.myIndice;
      myType = a.myType;
      strval = a.strval;
      numval = a.numval;
      if (contents){
        if (a.contents != contents){
          delete contents;
          if (a.contents){
            contents = new std::vector<AMFType>;
            for (std::vector<AMFType>::iterator it = a.contents->begin(); it < a.contents->end(); it++){
              contents->push_back(*it);
            }
          }else{
            contents = 0;
          }
        }
      }else{
        if (a.contents){
          contents = new std::vector<AMFType>;
          for (std::vector<AMFType>::iterator it = a.contents->begin(); it < a.contents->end(); it++){
            contents->push_back(*it);
          }
        }
      }
      return *this;
    };//= operator
    AMFType(const AMFType &a){
      myIndice = a.myIndice;
      myType = a.myType;
      strval = a.strval;
      numval = a.numval;
      if (a.contents){
        contents = new std::vector<AMFType>;
        for (std::vector<AMFType>::iterator it = a.contents->begin(); it < a.contents->end(); it++){
          contents->push_back(*it);
        }
      }else{contents = 0;}
    };//copy constructor
    void Print(std::string indent = ""){
      std::cerr << indent;
      switch (myType){
        case AMF0_NUMBER: std::cerr << "Number"; break;
        case AMF0_BOOL: std::cerr << "Bool"; break;
        case AMF0_STRING://short string
        case AMF0_LONGSTRING: std::cerr << "String"; break;
        case AMF0_OBJECT: std::cerr << "Object"; break;
        case AMF0_MOVIECLIP: std::cerr << "MovieClip"; break;
        case AMF0_NULL: std::cerr << "Null"; break;
        case AMF0_UNDEFINED: std::cerr << "Undefined"; break;
        case AMF0_REFERENCE: std::cerr << "Reference"; break;
        case AMF0_ECMA_ARRAY: std::cerr << "ECMA Array"; break;
        case AMF0_OBJ_END: std::cerr << "Object end"; break;
        case AMF0_STRICT_ARRAY: std::cerr << "Strict Array"; break;
        case AMF0_DATE: std::cerr << "Date"; break;
        case AMF0_UNSUPPORTED: std::cerr << "Unsupported"; break;
        case AMF0_RECORDSET: std::cerr << "Recordset"; break;
        case AMF0_XMLDOC: std::cerr << "XML Document"; break;
        case AMF0_TYPED_OBJ: std::cerr << "Typed Object"; break;
        case AMF0_UPGRADE: std::cerr << "Upgrade to AMF3"; break;
        case AMF0_DDV_CONTAINER: std::cerr << "DDVTech Container"; break;
      }
      std::cerr << " " << myIndice << " ";
      switch (myType){
        case AMF0_NUMBER: case AMF0_BOOL: case AMF0_REFERENCE: case AMF0_DATE: std::cerr << numval; break;
        case AMF0_STRING: case AMF0_LONGSTRING: case AMF0_XMLDOC: case AMF0_TYPED_OBJ: std::cerr << strval; break;
      }
      std::cerr << std::endl;
      if (contents){
        for (std::vector<AMFType>::iterator it = contents->begin(); it != contents->end(); it++){it->Print(indent+"  ");}
      }
    };//print
    std::string Pack(){
      std::string r = "";
      if ((myType == AMF0_STRING) && (strval.size() > 0xFFFF)){myType = AMF0_LONGSTRING;}
      if (myType != AMF0_DDV_CONTAINER){r += myType;}
      switch (myType){
        case AMF0_NUMBER:
          r += *(((char*)&numval)+7); r += *(((char*)&numval)+6);
          r += *(((char*)&numval)+5); r += *(((char*)&numval)+4);
          r += *(((char*)&numval)+3); r += *(((char*)&numval)+2);
          r += *(((char*)&numval)+1); r += *(((char*)&numval));
          break;
        case AMF0_DATE:
          r += *(((char*)&numval)+7); r += *(((char*)&numval)+6);
          r += *(((char*)&numval)+5); r += *(((char*)&numval)+4);
          r += *(((char*)&numval)+3); r += *(((char*)&numval)+2);
          r += *(((char*)&numval)+1); r += *(((char*)&numval));
          r += (char)0;//timezone always 0
          r += (char)0;//timezone always 0
          break;
        case AMF0_BOOL:
          r += (char)numval;
          break;
        case AMF0_STRING:
          r += strval.size() / 256;
          r += strval.size() % 256;
          r += strval;
          break;
        case AMF0_LONGSTRING:
        case AMF0_XMLDOC://is always a longstring
          r += strval.size() / (256*256*256);
          r += strval.size() / (256*256);
          r += strval.size() / 256;
          r += strval.size() % 256;
          r += strval;
          break;
        case AMF0_TYPED_OBJ:
          r += Indice().size() / 256;
          r += Indice().size() % 256;
          r += Indice();
          //is an object, with the classname first
        case AMF0_OBJECT:
          if (contents){
            for (std::vector<AMFType>::iterator it = contents->begin(); it != contents->end(); it++){
              r += it->Indice().size() / 256;
              r += it->Indice().size() % 256;
              r += it->Indice();
              r += it->Pack();
            }
          }
          r += (char)0; r += (char)0; r += (char)9;
          break;
        case AMF0_MOVIECLIP:
        case AMF0_NULL:
        case AMF0_UNDEFINED:
        case AMF0_RECORDSET:
        case AMF0_UNSUPPORTED:
          //no data to add
          break;
        case AMF0_REFERENCE:
          r += (char)((int)numval / 256);
          r += (char)((int)numval % 256);
          break;
        case AMF0_ECMA_ARRAY:{
          int arrlen = 0;
          if (contents){
            arrlen = getContentP("length")->NumValue();
            r += arrlen / (256*256*256); r += arrlen / (256*256); r += arrlen / 256; r += arrlen % 256;
            for (std::vector<AMFType>::iterator it = contents->begin(); it != contents->end(); it++){
              r += it->Indice().size() / 256;
              r += it->Indice().size() % 256;
              r += it->Indice();
              r += it->Pack();
            }
          }else{
            r += (char)0; r += (char)0; r += (char)0; r += (char)0;
          }
          r += (char)0; r += (char)0; r += (char)9;
          } break;
        case AMF0_STRICT_ARRAY:{
          int arrlen = 0;
          if (contents){
            arrlen = getContentP("length")->NumValue();
            r += arrlen / (256*256*256); r += arrlen / (256*256); r += arrlen / 256; r += arrlen % 256;
            for (std::vector<AMFType>::iterator it = contents->begin(); it != contents->end(); it++){
              r += it->Pack();
            }
          }else{
            r += (char)0; r += (char)0; r += (char)0; r += (char)0;
          }
        } break;
        case AMF0_DDV_CONTAINER://only send contents
          if (contents){
            for (std::vector<AMFType>::iterator it = contents->begin(); it != contents->end(); it++){
              r += it->Pack();
            }
          }
          break;
      }
      return r;
    };//pack
  protected:
    std::string myIndice;
    unsigned char myType;
    std::string strval;
    double numval;
    std::vector<AMFType> * contents;
};//AMFType

AMFType parseOneAMF(const unsigned char *& data, unsigned int &len, unsigned int &i, std::string name){
  std::string tmpstr;
  unsigned int tmpi = 0;
  unsigned char tmpdbl[8];
  switch (data[i]){
    case AMF0_NUMBER:
      tmpdbl[7] = data[i+1];
      tmpdbl[6] = data[i+2];
      tmpdbl[5] = data[i+3];
      tmpdbl[4] = data[i+4];
      tmpdbl[3] = data[i+5];
      tmpdbl[2] = data[i+6];
      tmpdbl[1] = data[i+7];
      tmpdbl[0] = data[i+8];
      i+=9;//skip 8(a double)+1 forwards
      return AMFType(name, *(double*)tmpdbl, AMF0_NUMBER);
      break;
    case AMF0_DATE:
      tmpdbl[7] = data[i+1];
      tmpdbl[6] = data[i+2];
      tmpdbl[5] = data[i+3];
      tmpdbl[4] = data[i+4];
      tmpdbl[3] = data[i+5];
      tmpdbl[2] = data[i+6];
      tmpdbl[1] = data[i+7];
      tmpdbl[0] = data[i+8];
      i+=11;//skip 8(a double)+1+timezone(2) forwards
      return AMFType(name, *(double*)tmpdbl, AMF0_DATE);
      break;
    case AMF0_BOOL:
      i+=2;//skip bool+1 forwards
      if (data[i-1] == 0){
        return AMFType(name, (double)0, AMF0_BOOL);
      }else{
        return AMFType(name, (double)1, AMF0_BOOL);
      }
      break;
    case AMF0_REFERENCE:
      tmpi = data[i+1]*256+data[i+2];//get the ref number value as a double
      i+=3;//skip ref+1 forwards
      return AMFType(name, (double)tmpi, AMF0_REFERENCE);
      break;
    case AMF0_XMLDOC:
      tmpi = data[i+1]*256*256*256+data[i+2]*256*256+data[i+3]*256+data[i+4];//set tmpi to UTF-8-long length
      tmpstr.clear();//clean tmpstr, just to be sure
      tmpstr.append((const char *)data+i+5, (size_t)tmpi);//add the string data
      i += tmpi + 5;//skip length+size+1 forwards
      return AMFType(name, tmpstr, AMF0_XMLDOC);
      break;
    case AMF0_LONGSTRING:
      tmpi = data[i+1]*256*256*256+data[i+2]*256*256+data[i+3]*256+data[i+4];//set tmpi to UTF-8-long length
      tmpstr.clear();//clean tmpstr, just to be sure
      tmpstr.append((const char *)data+i+5, (size_t)tmpi);//add the string data
      i += tmpi + 5;//skip length+size+1 forwards
      return AMFType(name, tmpstr, AMF0_LONGSTRING);
      break;
    case AMF0_STRING:
      tmpi = data[i+1]*256+data[i+2];//set tmpi to UTF-8 length
      tmpstr.clear();//clean tmpstr, just to be sure
      tmpstr.append((const char *)data+i+3, (size_t)tmpi);//add the string data
      i += tmpi + 3;//skip length+size+1 forwards
      return AMFType(name, tmpstr, AMF0_STRING);
      break;
    case AMF0_NULL:
    case AMF0_UNDEFINED:
    case AMF0_UNSUPPORTED:
      ++i;
      return AMFType(name, (double)0, data[i-1]);
      break;
    case AMF0_OBJECT:{
      ++i;
      AMFType ret = AMFType(name, (unsigned char)AMF0_OBJECT);
      while (data[i] + data[i+1] != 0){//while not encountering 0x0000 (we assume 0x000009)
        tmpi = data[i]*256+data[i+1];//set tmpi to the UTF-8 length
        tmpstr.clear();//clean tmpstr, just to be sure
        tmpstr.append((const char*)data+i+2, (size_t)tmpi);//add the string data
        i += tmpi + 2;//skip length+size forwards
        ret.addContent(parseOneAMF(data, len, i, tmpstr));//add content, recursively parsed, updating i, setting indice to tmpstr
      }
      i += 3;//skip 0x000009
      return ret;
      } break;
    case AMF0_TYPED_OBJ:{
      ++i;
      tmpi = data[i]*256+data[i+1];//set tmpi to the UTF-8 length
      tmpstr.clear();//clean tmpstr, just to be sure
      tmpstr.append((const char*)data+i+2, (size_t)tmpi);//add the string data
      AMFType ret = AMFType(tmpstr, (unsigned char)AMF0_TYPED_OBJ);//the object is not named "name" but tmpstr
      while (data[i] + data[i+1] != 0){//while not encountering 0x0000 (we assume 0x000009)
        tmpi = data[i]*256+data[i+1];//set tmpi to the UTF-8 length
        tmpstr.clear();//clean tmpstr, just to be sure
        tmpstr.append((const char*)data+i+2, (size_t)tmpi);//add the string data
        i += tmpi + 2;//skip length+size forwards
        ret.addContent(parseOneAMF(data, len, i, tmpstr));//add content, recursively parsed, updating i, setting indice to tmpstr
      }
      i += 3;//skip 0x000009
      return ret;
    } break;
    case AMF0_ECMA_ARRAY:{
      ++i;
      AMFType ret = AMFType(name, (unsigned char)AMF0_ECMA_ARRAY);
      i += 4;//ignore the array length, we re-calculate it
      while (data[i] + data[i+1] != 0){//while not encountering 0x0000 (we assume 0x000009)
        tmpi = data[i]*256+data[i+1];//set tmpi to the UTF-8 length
        tmpstr.clear();//clean tmpstr, just to be sure
        tmpstr.append((const char*)data+i+2, (size_t)tmpi);//add the string data
        i += tmpi + 2;//skip length+size forwards
        ret.addContent(parseOneAMF(data, len, i, tmpstr));//add content, recursively parsed, updating i, setting indice to tmpstr
      }
      i += 3;//skip 0x000009
      return ret;
    } break;
    case AMF0_STRICT_ARRAY:{
      AMFType ret = AMFType(name, (unsigned char)AMF0_STRICT_ARRAY);
      tmpi = data[i+1]*256*256*256+data[i+2]*256*256+data[i+3]*256+data[i+4];//set tmpi to array length
      i += 5;//skip size+1 forwards
      while (tmpi > 0){//while not done parsing array
        ret.addContent(parseOneAMF(data, len, i, "arrVal"));//add content, recursively parsed, updating i
        --tmpi;
      }
      return ret;
    } break;
  }
  #if DEBUG >= 2
  fprintf(stderr, "Error: Unimplemented AMF type %hhx - returning.\n", data[i]);
  #endif
  return AMFType("error", (unsigned char)0xFF);
}//parseOneAMF

AMFType parseAMF(const unsigned char * data, unsigned int len){
  AMFType ret("returned", (unsigned char)0xFF);//container type
  unsigned int i = 0;
  while (i < len){ret.addContent(parseOneAMF(data, len, i, ""));}
  return ret;
}//parseAMF
AMFType parseAMF(std::string data){return parseAMF((const unsigned char*)data.c_str(), data.size());}
