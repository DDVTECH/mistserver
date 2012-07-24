/// \file conn_http.cpp
/// Contains the main code for the HTTP Connector

#include <iostream>
#include <queue>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <getopt.h>
#include <ctime>
#include <set>
#include "tinythread.h"
#include <mist/socket.h>
#include <mist/http_parser.h>
#include <mist/config.h>

/// Holds everything unique to HTTP Connector.
namespace Connector_HTTP{

  std::set<tthread::thread *> active_threads; ///< Holds currently active threads
  std::set<tthread::thread *> done_threads; ///< Holds threads that are done and ready to be joined.
  tthread::mutex thread_mutex; ///< Mutex for adding/removing threads.

  /// Handles requests without associated handler, displaying a nice friendly error message.
  void Handle_None(HTTP::Parser & H, Socket::Connection * conn){
    H.Clean();
    H.SetBody("<!DOCTYPE html><html><head><title>Unsupported Media Type</title></head><body><h1>Unsupported Media Type</h1>The server isn't quite sure what you wanted to receive from it.</body></html>");
    conn->Send(H.BuildResponse("415", "Unsupported Media Type"));
  }

  /// Handles internal requests.
  void Handle_Internal(HTTP::Parser & H, Socket::Connection * conn){

    if (H.url == "/crossdomain.xml"){
      H.Clean();
      H.SetHeader("Content-Type", "text/xml");
      H.SetBody("<?xml version=\"1.0\"?><!DOCTYPE cross-domain-policy SYSTEM \"http://www.adobe.com/xml/dtds/cross-domain-policy.dtd\"><cross-domain-policy><allow-access-from domain=\"*\" /><site-control permitted-cross-domain-policies=\"all\"/></cross-domain-policy>");
      conn->Send(H.BuildResponse("200", "OK"));
      return;
    }//crossdomain.xml

    if (H.url.length() > 10 && H.url.substr(0, 7) == "/embed_" && H.url.substr(H.url.length() - 3, 3) == ".js"){
      std::string streamname = H.url.substr(7, H.url.length() - 10);
      JSON::Value ServConf = JSON::fromFile("/tmp/mist/streamlist");
      std::string response;
      H.Clean();
      H.SetHeader("Content-Type", "application/javascript");
      response = "// Generating embed code for stream " + streamname + "\n\n";
      if (ServConf["streams"].isMember(streamname)){
        std::string streamurl = "http://" + H.GetHeader("Host") + "/" + streamname + ".flv";
        response += "// Stream URL: " + streamurl + "\n\n";
        response += "document.write('<object width=\"600\" height=\"409\"><param name=\"movie\" value=\"http://fpdownload.adobe.com/strobe/FlashMediaPlayback.swf\"></param><param name=\"flashvars\" value=\"src="+HTTP::Parser::urlencode(streamurl)+"&controlBarMode=floating\"></param><param name=\"allowFullScreen\" value=\"true\"></param><param name=\"allowscriptaccess\" value=\"always\"></param><embed src=\"http://fpdownload.adobe.com/strobe/FlashMediaPlayback.swf\" type=\"application/x-shockwave-flash\" allowscriptaccess=\"always\" allowfullscreen=\"true\" width=\"600\" height=\"409\" flashvars=\"src="+HTTP::Parser::urlencode(streamurl)+"&controlBarMode=floating\"></embed></object>');\n";
      }else{
        response += "// Stream not available at this server.\nalert(\"This stream is currently not available at this server.\\\\nPlease try again later!\");";
      }
      response += "";
      H.SetBody(response);
      conn->Send(H.BuildResponse("200", "OK"));
      return;
    }//embed code generator

    Handle_None(H, conn);//anything else doesn't get handled
  }

  /// Handles requests without associated handler, displaying a nice friendly error message.
  void Handle_Through_Connector(HTTP::Parser & H, Socket::Connection * conn, std::string & connector){
    H.Clean();
    H.SetBody("<!DOCTYPE html><html><head><title>Handled</title></head><body><h1>"+connector+"</h1>Handling as: "+connector+"</body></html>");
    conn->Send(H.BuildResponse("200", "OK"));
  }

  /// Returns the name of the HTTP connector the given request should be served by.
  /// Can currently return:
  /// - none (request not supported)
  /// - internal (request fed from information internal to this connector)
  /// - dynamic (request fed from http_dynamic connector)
  /// - progressive (request fed from http_progressive connector)
  std::string getHTTPType(HTTP::Parser & H){
    if ((H.url.find("Seg") != std::string::npos) && (H.url.find("Frag") != std::string::npos)){return "dynamic";}
    if (H.url.find("f4m") != std::string::npos){return "dynamic";}
    if (H.url.length() > 4){
      std::string ext = H.url.substr(H.url.length() - 4, 4);
      if (ext == ".flv"){return "progressive";}
      if (ext == ".mp3"){return "progressive";}
    }
    if (H.url == "/crossdomain.xml"){return "internal";}
    if (H.url.length() > 10 && H.url.substr(0, 7) == "/embed_" && H.url.substr(H.url.length() - 3, 3) == ".js"){return "internal";}
    return "none";
  }

  /// Function handling a single connection
  void Handle_HTTP_Connection(void * pointer){
    Socket::Connection * conn = (Socket::Connection *)pointer;
    conn->setBlocking(false);//do not block on conn.spool() when no data is available
    HTTP::Parser Client;
    while (conn->connected()){
      if (conn->spool()){
        if (Client.Read(conn->Received())){
          std::string handler = getHTTPType(Client);
          #if DEBUG >= 4
          std::cout << "Received request: " << Client.url << " => " << handler << std::endl;
          #endif
          if (handler == "none" || handler == "internal"){
            if (handler == "internal"){
              Handle_Internal(Client, conn);
            }else{
              Handle_None(Client, conn);
            }
          }else{
            Handle_Through_Connector(Client, conn, handler);
          }
          Client.Clean(); //clean for any possible next requests
        }else{
          #if DEBUG >= 3
          fprintf(stderr, "Could not parse the following:\n%s\n", conn->Received().c_str());
          #endif
        }
      }
    }
    //close and remove the connection
    conn->close();
    delete conn;
    //remove this thread from active_threads and add it to done_threads.
    thread_mutex.lock();
    for (std::set<tthread::thread *>::iterator it = active_threads.begin(); it != active_threads.end(); it++){
      if ((*it)->get_id() == tthread::this_thread::get_id()){
        tthread::thread * T = (*it);
        active_threads.erase(T);
        done_threads.insert(T);
        break;
      }
    }
    thread_mutex.unlock();
  }

};//Connector_HTTP namespace


int main(int argc, char ** argv){
  Util::Config conf(argv[0], PACKAGE_VERSION);
  conf.addConnectorOptions(8080);
  conf.parseArgs(argc, argv);
  Socket::Server server_socket = Socket::Server(conf.getInteger("listen_port"), conf.getString("listen_interface"));
  if (!server_socket.connected()){return 1;}
  conf.activate();

  while (server_socket.connected() && conf.is_active){
    Socket::Connection S = server_socket.accept();
    if (S.connected()){//check if the new connection is valid
      Connector_HTTP::thread_mutex.lock();
      tthread::thread * T = new tthread::thread(Connector_HTTP::Handle_HTTP_Connection, (void *)(new Socket::Connection(S)));
      Connector_HTTP::active_threads.insert(T);
      while (!Connector_HTTP::done_threads.empty()){
        T = *Connector_HTTP::done_threads.begin();
        T->join();
        Connector_HTTP::done_threads.erase(T);
        delete T;
      }
      Connector_HTTP::thread_mutex.unlock();
    }
  }//while connected and not requested to stop
  server_socket.close();
  return 0;
}//main
