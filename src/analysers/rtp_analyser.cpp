#include <cstdlib>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <string>
#include <string.h>
#include <vector>
#include <sstream>
#include <mist/socket.h>
#include <mist/config.h>
#include <mist/rtp.h>
#include <mist/http_parser.h>

//rtsp://krabs:1935/vod/gear1.mp4

namespace Analysers {
  int analyseRTP(){
    Socket::Connection conn("localhost", 554, true);
    //Socket::Connection conn("krabs", 1935, true);
    HTTP::Parser HTTP_R, HTTP_S;//HTTP Receiver en HTTP Sender.
    int step = 0;
    /*1 = sent describe
      2 = recd describe
      3 = sent setup
      4 = received setup
      5 = sent play"*/
    std::vector<std::string> tracks;
    std::vector<Socket::UDPConnection> connections;
    unsigned int trackIt = 0;
    while (conn.connected()){
    //  std::cerr << "loopy" << std::endl;
      if(step == 0){
        HTTP_S.protocol = "RTSP/1.0";
        HTTP_S.method = "DESCRIBE";
        //rtsp://krabs:1935/vod/gear1.mp4
        //rtsp://localhost/g1
        HTTP_S.url = "rtsp://localhost/steers";
        //HTTP_S.url = "rtsp://krabs:1935/vod/steers.mp4";
        HTTP_S.SetHeader("CSeq",1);
        HTTP_S.SendRequest(conn); 
        step++;
        
      }else if(step == 2){
        std::cerr <<"setup " << tracks[trackIt] << std::endl;
        HTTP_S.method = "SETUP";
        HTTP_S.url = "rtsp://localhost/steers/" + tracks[trackIt];
        //HTTP_S.url = "rtsp://krabs:1935/vod/steers.mp4/" + tracks[trackIt];
        HTTP_S.SetHeader("CSeq",2+trackIt);
        std::stringstream ss;
        ss << "RTP/steersVP;unicast;client_port="<< 20000 + 2*trackIt<<"-"<< 20001 + 2*trackIt;
        HTTP_S.SetHeader("Transport",ss.str());//make client ports, 4200 + 2*offset
        trackIt++;
        step++;
        HTTP_S.SendRequest(conn);
        std::cerr << "step " << step << "/\\"<< ss.str()<<std::endl;
      }else if(step == 4){
        std::cerr << "Play!!!1" << std::endl;
        HTTP_S.method = "PLAY";
        HTTP_S.url = "rtsp://localhost/steers";
        //HTTP_S.url = "rtsp://krabs:1935/vod/steers.mp4";
        HTTP_S.SetHeader("Range","npt=0.000-");
        HTTP_S.SendRequest(conn);
        step++;
        std::cerr << "step for play.." << step << std::endl;
      }      
      
      if (conn.Received().size() || conn.spool()){ 
        if (HTTP_R.Read(conn)){
          if(step == 1){
            std::cerr << "recvd desc" << std::endl;  
            for(size_t ml = HTTP_R.body.find("a=control:",HTTP_R.body.find("m=")); ml != std::string::npos; ml = HTTP_R.body.find("a=control:",ml+1)){
              std::cerr << "found trekk" << std::endl;
              tracks.push_back(HTTP_R.body.substr(ml+10,HTTP_R.body.find_first_of("\r\n",ml)-(ml+10)));
              connections.push_back(Socket::UDPConnection());
            }
            for(unsigned int x = 0; x < connections.size();x++){
              connections[x].SetDestination("127.0.0.1",666);
              connections[x].bind(20000+2*x);
              connections[x].setBlocking(true);
            }
            step++;
          }else if(step == 3){
            std::cerr << "recvd setup" << std::endl;
            std::cerr << "trackIt: " << trackIt << " size " << tracks.size() << std::endl;
            if(trackIt < tracks.size())
              step--;
            else
              step++;
            std::cerr << HTTP_R.GetHeader("Transport");
          }
          HTTP_R.Clean();
        }
      }//!
      if(step == 5){

        for(unsigned int cx = 0; cx <  connections.size(); cx++){
         // std::cerr <<"PLAY MF" << std::endl;
          if(connections[cx].Receive()){
            RTP::Packet* pakketje = new RTP::Packet(connections[cx].data, connections[cx].data_len);   
            /*std::cout << "Version = " << pakketje->getVersion() << std::endl;
            std::cout << "Padding = " << pakketje->getPadding() << std::endl;
            std::cout << "Extension = " << pakketje->getExtension() << std::endl;
            std::cout << "Contributing sources = " << pakketje->getContribCount() << std::endl;
            std::cout << "Marker = " << pakketje->getMarker() << std::endl;
            std::cout << "Payload Type = " << pakketje->getPayloadType() << std::endl;
            std::cout << "Sequence = " << pakketje->getSequence() << std::endl;
            std::cout << "Timestamp = " << pakketje->getTimeStamp() << std::endl;
            std::cout << "SSRC = " << pakketje->getSSRC() << std::endl;
            std::cout << "datalen: " << connections[cx].data_len << std::endl;  
            std::cout << "payload:" << std::endl;*/
            
            if(pakketje->getPayloadType() == 97){
              int h264type = (int)(connections[cx].data[12] & 0x1f);
              std::cout << h264type << " - ";
              if(h264type == 0){
                std::cout << "unspecified - ";
              }else if(h264type == 1){
                std::cout << "Coded slice of a non-IDR picture - ";
              }else if(h264type == 2){
                std::cout << "Coded slice data partition A - ";
              }else if(h264type == 3){
                std::cout << "Coded slice data partition B - ";
              }else if(h264type == 4){
                std::cout << "Coded slice data partition C - ";
              }else if(h264type == 5){
                std::cout << "Coded slice of an IDR picture - ";
              }else if(h264type == 6){
                std::cout << "Supplemental enhancement information (SEI) - ";
              }else if(h264type == 7){
                std::cout << "Sequence parameter set  - ";
              }else if(h264type == 8){
                std::cout << "Picture parameter set  - ";
              }else if(h264type == 9){
                std::cout << "Access unit delimiter  - ";
              }else if(h264type == 10){
                std::cout << "End of sequence  - ";
              }else if(h264type == 11){
                std::cout << "End of stream - ";
              }else if(h264type == 12){
                std::cout << "Filler data - ";
              }else if(h264type == 13){
                std::cout << "Sequence parameter set extension - ";
              }else if(h264type == 14){
                std::cout << "Prefix NAL unit - ";
              }else if(h264type == 15){
                std::cout << "Subset sequence parameter set - ";
              }else if(h264type == 16){
                std::cout << "Reserved - ";
              }else if(h264type == 17){
                std::cout << "Reserved - ";
              }else if(h264type == 18){
                std::cout << "Reserved - ";
              }else if(h264type == 19){
                std::cout << "Coded slice of an auxiliary coded picture without partitioning  - ";
              }else if(h264type == 20){
                std::cout << "Coded slice extension - ";
              }else if(h264type == 21){
                std::cout << "Reserved - ";
              }else if(h264type == 22){
                std::cout << "Reserved - ";
              }else if(h264type == 23){
                std::cout << "Reserved - ";
              }else if(h264type == 24){
                std::cout << "stap a - ";
              }else if(h264type == 25){
                std::cout << "stap b - ";
              }else if(h264type == 26){
                std::cout << "mtap16 - ";
              }else if(h264type == 27){
                std::cout << "mtap24 - ";
              }else if(h264type == 28){
                std::cout << "fu a - ";
              }else if(h264type == 29){
                std::cout << "fu b - ";
              }else if(h264type == 30){
                std::cout << "Unspecified - ";
              }else if(h264type == 31){
                std::cout << "Unspecified - ";
              }
              
              
              
              
              for(unsigned int i = 13 ; i < connections[cx].data_len;i++){
               std::cout << std::hex <<std::setw(2) << std::setfill('0') << (int)connections[cx].data[i]<< std::dec;
              }
            std::cout << std::endl<<std::endl;
            }
            delete pakketje;   
          }                 
        }
      }
    }
    return 666;
  }





}



int main(int argc, char ** argv){
  Util::Config conf = Util::Config(argv[0]);
  conf.parseArgs(argc, argv);
  return Analysers::analyseRTP();
}
