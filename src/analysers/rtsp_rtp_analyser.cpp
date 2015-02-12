#include <cstdlib>
#include <iostream>
#include <fstream>
#include <string>
#include <string.h>
#include <vector>

#include <mist/config.h>
#include <mist/rtp.h>
#include <mist/socket.h>
#include <mist/http_parser.h>
#include <sstream>

namespace RtspRtp{

  int analyseRtspRtp(std::string rtspUrl){    
    /*//parse hostname
    std::string hostname = rtspUrl.substr(7);
    hostname = hostname.substr(0,hostname.find('/'));
    std::cout << hostname << std::endl;
    HTTP::Parser HTTP_R, HTTP_S;//HTTP Receiver en HTTP Sender.
    Socket::Connection conn(hostname,554,false);//setting rtsp connection
    
    bool optionsSent = false;
    bool optionsRecvd = false;
    bool descSent = false;
    bool descRecvd = false;
    bool setupComplete = false;
    bool playSent = false;
    int CSeq = 1;
    while(conn.connected()){
      if(!optionsSent){
        HTTP_R.protocol="RTSP/1.0";
        HTTP_R.method = "OPTIONS";
        HTTP_R.url = rtspUrl;
        HTTP_R.SetHeader("CSeq",CSeq);
        CSeq++;
        HTTP_R.SetHeader("User-Agent","mistANALyser");
        HTTP_R.SendRequest(conn);
        optionsSent = true;
      }
      
      if (optionsSent&& !optionsRecvd && (conn.Received().size() || conn.spool() )){ 
        if(HTTP_S.Read(conn)){
          std::cout << "recv opts" << std::endl;
          
          std::cout << HTTP_S.BuildResponse(HTTP_S.method,HTTP_S.url);      
          optionsRecvd = true;        
        }
      }
      

      
      if(optionsRecvd && !descSent){
        HTTP_S.Clean();
        HTTP_R.protocol="RTSP/1.0";
        HTTP_R.method = "DESCRIBE";
        HTTP_R.url = rtspUrl;
        HTTP_R.SetHeader("CSeq",CSeq);
        CSeq++;
        HTTP_R.SetHeader("User-Agent","mistANALyser");
        HTTP_R.SendRequest(conn);
        descSent = true;

      }
      
      std::vector<std::string> trackIds;
      
      if (descSent&&!descRecvd && (conn.Received().size() || conn.spool() )){ 

        if(HTTP_S.Read(conn)){
          std::cout << "recv desc2" << std::endl;
          std::cout << HTTP_S.BuildResponse(HTTP_S.method,HTTP_S.url);
          size_t pos = HTTP_S.body.find("m=");
          do{
          //finding all track IDs
             pos = HTTP_S.body.find("a=control:",pos);
             if(pos !=std::string::npos){
              trackIds.push_back(HTTP_S.body.substr(pos+10,HTTP_S.body.find("\r\n",pos)-pos-10 ) );//setting track IDs;
              pos++;
             }
          }while(pos != std::string::npos);
          //we have all the tracks

          descRecvd = true;        
        }
      }

      
      unsigned int setupsSent = 0;
      unsigned int setupsRecvd = 0;
      Socket::UDPConnection connectors[trackIds.size()];
      unsigned int setports[trackIds.size()];
      uint32_t bport = 10000;
      std::string sessionID = "";
      
      std::stringstream setup;
      
      if(descRecvd && !setupComplete){
        //time to setup.
        for(std::vector<std::string>::iterator it = trackIds.begin();it!=trackIds.end();it++){
          std::cout << "setup " << setupsSent<< std::endl;        
          while(!connectors[setupsSent].SetConnection( bport,false) ){
            bport +=2;//finding an available port
          }
          std::cout << "setup" << bport<< std::endl;
          setports[setupsSent] = bport;
          bport +=2;
          if(setupsSent == setupsRecvd){
            //send only one setup
            HTTP_S.Clean();
            HTTP_R.protocol="RTSP/1.0";
            HTTP_R.method = "SETUP";
            HTTP_R.url = rtspUrl+ '/' + *(it);
            setup << "RTP/AVP/UDP;unicast;client_port="<< setports[setupsSent] <<"-" <<setports[setupsSent]+1 ;
            HTTP_R.SetHeader("Transport",setup.str() );
            std:: cout << setup.str()<<std::endl;
            setup.str(std::string());
            setup.clear();
            HTTP_R.SetHeader("CSeq",CSeq);
            CSeq++;
            if(sessionID != ""){
             HTTP_R.SetHeader("Session",sessionID);
            }
            HTTP_R.SetHeader("User-Agent","mistANALyser");  
            HTTP_R.SendRequest(conn);          
            setupsSent ++;   
          }
                   

          
          while(setupsSent == setupsRecvd+1){
            //lets Assume we assume we always receive a response          
            if ( (conn.Received().size() || conn.spool() )){ 
              if(HTTP_S.Read(conn)){
                std::cout << "recv setup" << std::endl;
                std::cout << HTTP_S.BuildResponse(HTTP_S.method,HTTP_S.url);      
                optionsRecvd = true;    
                sessionID = HTTP_S.GetHeader("Session");
                setupsRecvd++;    
              }
            }
          }     
          //set up all parameters, and then after the for loop we have to listen to setups and all. sent if both are equal, and recv if one is sent          
  
        }
        setupComplete = true;
      }
      
      if(setupComplete && !playSent){
      //time to play
        HTTP_S.Clean();
        HTTP_R.protocol="RTSP/1.0";
        HTTP_R.method = "PLAY";
        HTTP_R.url = rtspUrl;
      
        HTTP_R.SetHeader("CSeq",CSeq);
        CSeq++;
        HTTP_R.SetHeader("User-Agent","mistANALyser");
        HTTP_R.SetHeader("Session",sessionID);
        HTTP_R.SendRequest(conn);
        playSent = true;
        std::cout << "sent play" << std::endl;
        char buffer[2000];
        while(!connectors[0].iread((void*)buffer,2000)) {
          std::cout << "buffer";
        }
        std::cout <<"buffer is not empty" << std::endl;
        
      }
      
      //streams set up
      //time to read some packets


      
      
      
      if(descRecvd){
        conn.close();
      }
    }
    conn.close();*/
    return 0; 
  }


}



int main(int argc, char ** argv){
  Util::Config conf = Util::Config(argv[0], PACKAGE_VERSION);
  conf.addOption("url",JSON::fromString("{\"arg\":\"string\",\"short\":\"u\",\"long\":\"url\",\"help\":\"URL To get.\", \"default\":\"rtsp://localhost/s1k\"}"));
  conf.parseArgs(argc, argv);
  return RtspRtp::analyseRtspRtp(conf.getString("url"));
}
