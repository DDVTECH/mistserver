#include <vector>
#include <string.h>
#include <string>

class AMFType {
  public:
    double NumValue(){return numval;};
    std::string StrValue(){return strval;};
    AMFType(double val){strval = ""; numval = val;};
    AMFType(std::string val){strval = val; numval = 0;};
  private:
    std::string strval;
    double numval;
};//AMFType

//scans the vector for the indice, returns the next AMFType from it or null
AMFType * getAMF(std::vector<AMFType> * vect, std::string indice){
  std::vector<AMFType>::iterator it;
  for (it=vect->begin(); it < vect->end(); it++){
    if ((*it).StrValue() == indice){it++; return &(*it);}
  }
  return 0;
}//getAMF

std::vector<AMFType> * parseAMF(unsigned char * data, unsigned int len){
  std::vector<AMFType> * ret = new std::vector<AMFType>;
  unsigned int i = 0;
  std::string tmpstr;
  unsigned int tmpi = 0;
  unsigned char tmpdbl[8];
  while (i < len){
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
        ret->push_back(*(double*)tmpdbl);
        fprintf(stderr, "AMF: Number %f\n", *(double*)tmpdbl);
        i += 8;
        break;
      case 0x01://bool
        if (data[i+1] == 0){
          ret->push_back((double)0);
          fprintf(stderr, "AMF: Bool false\n");
        }else{
          ret->push_back((double)1);
          fprintf(stderr, "AMF: Bool true\n");
        }
        ++i;
        break;
      case 0x0C://long string
        tmpi = data[i+1]*256*256*256+data[i+2]*256*256+data[i+3]*256+data[i+4];
        tmpstr = (char*)(data+i+5);
        ret->push_back(tmpstr);
        i += tmpi + 4;
        fprintf(stderr, "AMF: String %s\n", tmpstr.c_str());
        break;
      case 0x02://string
        tmpi = data[i+1]*256+data[i+2];
        tmpstr = (char*)(data+i+3);
        ret->push_back(tmpstr);
        i += tmpi + 2;
        fprintf(stderr, "AMF: String %s\n", tmpstr.c_str());
        break;
      case 0x05://null
      case 0x06://undefined
      case 0x0D://unsupported
        fprintf(stderr, "AMF: Null\n");
        ret->push_back((double)0);
        break;
      case 0x03://object
        ++i;
        while (data[i] + data[i+1] != 0){
          tmpi = data[i]*256+data[i+1];
          tmpstr = (char*)(data+i+2);
          ret->push_back(tmpstr);
          i += tmpi + 2;
          fprintf(stderr, "AMF: Indice %s\n", tmpstr.c_str());
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
              ret->push_back(*(double*)tmpdbl);
              fprintf(stderr, "AMF: Value Number %f\n", *(double*)tmpdbl);
              i += 8;
              break;
            case 0x01://bool
              if (data[i+1] == 0){
                ret->push_back((double)0);
                fprintf(stderr, "AMF: Value Bool false\n");
              }else{
                ret->push_back((double)1);
                fprintf(stderr, "AMF: Value Bool true\n");
              }
              ++i;
              break;
            case 0x0C://long string
              tmpi = data[i+1]*256*256*256+data[i+2]*256*256+data[i+3]*256+data[i+4];
              tmpstr = (char*)(data+i+5);
              ret->push_back(tmpstr);
              i += tmpi + 4;
              fprintf(stderr, "AMF: Value String %s\n", tmpstr.c_str());
              break;
            case 0x02://string
              tmpi = data[i+1]*256+data[i+2];
              tmpstr = (char*)(data+i+3);
              ret->push_back(tmpstr);
              i += tmpi + 2;
              fprintf(stderr, "AMF: Value String %s\n", tmpstr.c_str());
              break;
            case 0x05://null
            case 0x06://undefined
            case 0x0D://unsupported
              fprintf(stderr, "AMF: Value Null\n");
              ret->push_back((double)0);
              break;
            default:
              fprintf(stderr, "Error: Unknown AMF object contents type %hhx - returning.\n", data[i]);
              break;
          }
          ++i;
        }
        i += 2;
        break;
      case 0x07://reference
      case 0x08://array
      case 0x0A://strict array
      case 0x0B://date
      case 0x0F://XML
      case 0x10://typed object
      case 0x11://AMF+
      default:
        fprintf(stderr, "Error: Unknown AMF type %hhx - returning.\n", data[i]);
        return ret;
        break;
    }
    ++i;
  }
  return ret;
}//parseAMF
