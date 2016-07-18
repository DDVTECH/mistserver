#include <mist/config.h>
#include "analyser.h"
#include <set>

struct StreamData{
  long timeScale;
  std::string media;
  std::string initialization;
  std::string initURL;
  long trackID;        
  unsigned int adaptationSet;
  unsigned char trackType;    
};


struct seekPos {
  ///\brief Less-than comparison for seekPos structures.
  ///\param rhs The seekPos to compare with.
  ///\return Whether this object is smaller than rhs.
  bool operator < (const seekPos & rhs) const {
    if ((seekTime*rhs.timeScale) < (rhs.seekTime*timeScale)) {
      return true;
    } else {
      if ( (seekTime*rhs.timeScale) == (rhs.seekTime*timeScale)){
        if (adaptationSet < rhs.adaptationSet){
          return true;
        } else if (adaptationSet == rhs.adaptationSet){
          if (trackID < rhs.trackID) {
            return true;
          }
        }          
      }
    }
    return false;
  }
  
  long timeScale;
  long long unsigned int bytePos;     /// ?
  long long unsigned int seekTime;        ///start
  long long unsigned int duration;     ///duration
  unsigned int trackID;               ///stores representation ID
  unsigned int adaptationSet;                 ///stores type
  unsigned char trackType;                 ///stores type
  std::string url;
  
  
};

class dashAnalyser : public analysers 
{
    
  public:
    dashAnalyser(Util::Config config);
    ~dashAnalyser();
    bool packetReady();
    void PreProcessing();
    //int Analyse();
    int doAnalyse();
//    void doValidate();
    bool hasInput();
    void PostProcessing();

  private:
    unsigned int port;
    std::string url;
    std::string server;
    long long int startTime;
    long long int abortTime;
    Socket::Connection conn;
    std::string urlPrependStuff;
    unsigned int pos;
    std::set<seekPos> currentPos;
    std::vector<StreamData> streamData;

    


};
