#include "output_http.h"

namespace Mist{
  class OutHTTP : public HTTPOutput{
  public:
    OutHTTP(Socket::Connection &conn);
    ~OutHTTP();
    static void init(Util::Config *cfg);
    static bool listenMode();
    virtual void onFail(const std::string &msg, bool critical = false);
    /// preHTTP is disabled in the internal HTTP output, since most don't need the stream alive to work
    virtual void preHTTP(){};
    void HTMLResponse();
    void onHTTP();
    void sendIcon();
    bool websocketHandler();
    JSON::Value getStatusJSON(std::string &reqHost, const std::string &useragent = "");
    bool stayConnected;
    virtual bool onFinish(){return stayConnected;}

  private:
    std::string origStreamName;
    std::string mistPath;
  };


}// namespace Mist

typedef Mist::OutHTTP mistOut;

  /**
   * class handels if redirects are needed 
  */
  class redirectManager{
      public:
      redirectManager(){};
      //used to receive new instructions from the load balancer
      void update();
      //used to check if and where the redirects should take place
      std::string* checkForRedirect();
      
      private:
      static tthread::mutex* managerMutex;
      static std::string* redirect;
      static uint64_t cpu;
      static uint64_t ram;
      static uint64_t bandwidth;
  };

