#include <vector>
#include <string.h>
#include <string>

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
    AMFType(std::string indice, double val, unsigned char setType = 0x00){//num type initializer
      myIndice = indice;
      myType = setType;
      strval = "";
      numval = val;
      contents = 0;
    };
    AMFType(std::string indice, std::string val, unsigned char setType = 0x02){//str type initializer
      myIndice = indice;
      myType = setType;
      strval = val;
      numval = 0;
      contents = 0;
    };
    AMFType(std::string indice, unsigned char setType = 0x03){//object type initializer
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
        case 0x00: std::cerr << "Number"; break;
        case 0x01: std::cerr << "Bool"; break;
        case 0x02://short string
        case 0x0C: std::cerr << "String"; break;
        case 0x03: std::cerr << "Object"; break;
        case 0x08: std::cerr << "ECMA Array"; break;
        case 0x05: std::cerr << "Null"; break;
        case 0x06: std::cerr << "Undefined"; break;
        case 0x0D: std::cerr << "Unsupported"; break;
        case 0xFF: std::cerr << "Container"; break;
      }
      std::cerr << " " << myIndice << " ";
      switch (myType){
        case 0x00: case 0x01: std::cerr << numval; break;
        case 0x02: case 0x0C: std::cerr << strval; break;
      }
      std::cerr << std::endl;
      if (contents){
        for (std::vector<AMFType>::iterator it = contents->begin(); it != contents->end(); it++){it->Print(indent+"  ");}
      }
    };//print
    std::string Pack(){
      std::string r = "";
      if ((myType == 0x02) && (strval.size() > 0xFFFF)){myType = 0x0C;}
      if (myType != 0xFF){r += myType;}
      switch (myType){
        case 0x00://number
          r += *(((char*)&numval)+7); r += *(((char*)&numval)+6);
          r += *(((char*)&numval)+5); r += *(((char*)&numval)+4);
          r += *(((char*)&numval)+3); r += *(((char*)&numval)+2);
          r += *(((char*)&numval)+1); r += *(((char*)&numval));
          break;
        case 0x01://bool
          r += (char)numval;
          break;
        case 0x02://short string
          r += strval.size() / 256;
          r += strval.size() % 256;
          r += strval;
          break;
        case 0x0C://long string
          r += strval.size() / (256*256*256);
          r += strval.size() / (256*256);
          r += strval.size() / 256;
          r += strval.size() % 256;
          r += strval;
          break;
        case 0x03://object
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
        case 0x08:{//array
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
        case 0xFF://container - our own type - do not send, only send contents
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
  char * helperchar = 0;
  std::string tmpstr;
  unsigned int tmpi = 0;
  unsigned char tmpdbl[8];
  switch (data[i]){
    case 0x00://number
      tmpdbl[7] = data[i+1];
      tmpdbl[6] = data[i+2];
      tmpdbl[5] = data[i+3];
      tmpdbl[4] = data[i+4];
      tmpdbl[3] = data[i+5];
      tmpdbl[2] = data[i+6];
      tmpdbl[1] = data[i+7];
      tmpdbl[0] = data[i+8];
      i+=9;
      return AMFType(name, *(double*)tmpdbl, 0x00);
      break;
    case 0x01://bool
      i+=2;
      if (data[i-1] == 0){
        return AMFType(name, (double)0, 0x01);
      }else{
        return AMFType(name, (double)1, 0x01);
      }
      break;
    case 0x0C://long string
      tmpi = data[i+1]*256*256*256+data[i+2]*256*256+data[i+3]*256+data[i+4];
      helperchar = (char*)malloc(tmpi+1);
      memcpy(helperchar, data+i+5, tmpi);
      helperchar[tmpi] = 0;
      tmpstr = helperchar;
      free(helperchar);
      i += tmpi + 5;
      return AMFType(name, tmpstr, 0x0C);
      break;
    case 0x02://string
      tmpi = data[i+1]*256+data[i+2];
      helperchar = (char*)malloc(tmpi+1);
      memcpy(helperchar, data+i+3, tmpi);
      helperchar[tmpi] = 0;
      tmpstr = helperchar;
      free(helperchar);
      i += tmpi + 3;
      return AMFType(name, tmpstr, 0x02);
      break;
    case 0x05://null
    case 0x06://undefined
    case 0x0D://unsupported
      ++i;
      return AMFType(name, (double)0, data[i-1]);
      break;
    case 0x03:{//object
      ++i;
      AMFType ret = AMFType(name, data[i-1]);
      while (data[i] + data[i+1] != 0){
        tmpi = data[i]*256+data[i+1];
        tmpstr = (char*)(data+i+2);
        i += tmpi + 2;
        ret.addContent(parseOneAMF(data, len, i, tmpstr));
      }
      i += 3;
      return ret;
      } break;
    case 0x08:{//ECMA array
      ++i;
      AMFType ret = AMFType(name, data[i-1]);
      i += 4;
      while (data[i] + data[i+1] != 0){
        tmpi = data[i]*256+data[i+1];
        tmpstr = (char*)(data+i+2);
        i += tmpi + 2;
        ret.addContent(parseOneAMF(data, len, i, tmpstr));
      }
      i += 3;
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
