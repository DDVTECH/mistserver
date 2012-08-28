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
#include <openssl/md5.h>
#include <mist/socket.h>
#include <mist/http_parser.h>
#include <mist/config.h>
#include <mist/procs.h>
#include "tinythread.h"

/// Holds everything unique to HTTP Connector.
namespace Connector_HTTP{

  /// Class for keeping track of connections to connectors.
  class ConnConn{
    public:
      Socket::Connection * conn; ///< The socket of this connection
      unsigned int lastuse; ///< Seconds since last use of this connection.
      tthread::mutex in_use; ///< Mutex for this connection.
      /// Constructor that sets the socket and lastuse to 0.
      ConnConn(){
        conn = 0;
        lastuse = 0;
      };
      /// Constructor that sets lastuse to 0, but socket to s.
      ConnConn(Socket::Connection * s){
        conn = s;
        lastuse = 0;
      };
      /// Destructor that deletes the socket if non-null.
      ~ConnConn(){
        if (conn){
          conn->close();
          delete conn;
        }
        conn = 0;
      };
  };

  std::map<std::string, ConnConn *> connconn; ///< Connections to connectors
  std::set<tthread::thread *> active_threads; ///< Holds currently active threads
  std::set<tthread::thread *> done_threads; ///< Holds threads that are done and ready to be joined.
  tthread::mutex thread_mutex; ///< Mutex for adding/removing threads.
  tthread::mutex conn_mutex; ///< Mutex for adding/removing connector connections.
  tthread::mutex timeout_mutex; ///< Mutex for timeout thread.
  tthread::thread * timeouter = 0; ///< Thread that times out connections to connectors.

  void Timeout_Thread(void * n){
    n = 0;//prevent unused variable warning
    tthread::lock_guard<tthread::mutex> guard(timeout_mutex);
    while (true){
      {
        tthread::lock_guard<tthread::mutex> guard(conn_mutex);
        if (connconn.empty()){
          return;
        }
        std::map<std::string, ConnConn*>::iterator it;
        for (it = connconn.begin(); it != connconn.end(); it++){
          if (!it->second->conn->connected() || it->second->lastuse++ > 15){
            if (it->second->in_use.try_lock()){
              it->second->in_use.unlock();
              delete it->second;
              connconn.erase(it);
              it = connconn.begin();//get a valid iterator
              if (it == connconn.end()){return;}
            }
          }
        }
        conn_mutex.unlock();
      }
      usleep(1000000);//sleep 1 second and re-check
    }
  }

  /// Handles requests without associated handler, displaying a nice friendly error message.
  void Handle_None(HTTP::Parser & H, Socket::Connection * conn){
    H.Clean();
    H.SetHeader("Server", "mistserver/" PACKAGE_VERSION "/" + Util::Config::libver);
    H.SetBody("<!DOCTYPE html><html><head><title>Unsupported Media Type</title></head><body><h1>Unsupported Media Type</h1>The server isn't quite sure what you wanted to receive from it.</body></html>");
    conn->Send(H.BuildResponse("415", "Unsupported Media Type"));
  }

  void Handle_Timeout(HTTP::Parser & H, Socket::Connection * conn){
    H.Clean();
    H.SetHeader("Server", "mistserver/" PACKAGE_VERSION "/" + Util::Config::libver);
    H.SetBody("<!DOCTYPE html><html><head><title>Gateway timeout</title></head><body><h1>Gateway timeout</h1>Though the server understood your request and attempted to handle it, somehow handling it took longer than it should. Your request has been cancelled - please try again later.</body></html>");
    conn->Send(H.BuildResponse("504", "Gateway Timeout"));
  }

  /// Handles internal requests.
  void Handle_Internal(HTTP::Parser & H, Socket::Connection * conn){

    if (H.url == "/crossdomain.xml"){
      H.Clean();
      H.SetHeader("Content-Type", "text/xml");
      H.SetHeader("Server", "mistserver/" PACKAGE_VERSION "/" + Util::Config::libver);
      H.SetBody("<?xml version=\"1.0\"?><!DOCTYPE cross-domain-policy SYSTEM \"http://www.adobe.com/xml/dtds/cross-domain-policy.dtd\"><cross-domain-policy><allow-access-from domain=\"*\" /><site-control permitted-cross-domain-policies=\"all\"/></cross-domain-policy>");
      conn->Send(H.BuildResponse("200", "OK"));
      return;
    }//crossdomain.xml

    if (H.url.length() > 10 && H.url.substr(0, 7) == "/embed_" && H.url.substr(H.url.length() - 3, 3) == ".js"){
      std::string streamname = H.url.substr(7, H.url.length() - 10);
      JSON::Value ServConf = JSON::fromFile("/tmp/mist/streamlist");
      std::string response;
      H.Clean();
      H.SetHeader("Server", "mistserver/" PACKAGE_VERSION "/" + Util::Config::libver);
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

  /// Wrapper function for openssl MD5 implementation
  std::string md5(std::string input){
    char tmp[3];
    std::string ret;
    const unsigned char * res = MD5((const unsigned char*)input.c_str(), input.length(), 0);
    for (int i = 0; i < 16; ++i){
      snprintf(tmp, 3, "%02x", res[i]);
      ret += tmp;
    }
    return ret;
  }

  /// Handles requests without associated handler, displaying a nice friendly error message.
  void Handle_Through_Connector(HTTP::Parser & H, Socket::Connection * conn, std::string & connector){
    //create a unique ID based on a hash of the user agent and host, followed by the stream name and connector
    std::string uid = md5(H.GetHeader("User-Agent")+conn->getHost())+"_"+H.GetVar("stream")+"_"+connector;
    H.SetHeader("X-UID", uid);//add the UID to the headers before copying
    H.SetHeader("X-Origin", conn->getHost());//add the UID to the headers before copying
    std::string request = H.BuildRequest();//copy the request for later forwarding to the connector
    H.Clean();

    //existing connections must be murdered, if any
    if (connconn.count(uid)){
      if (!connconn[uid]->in_use.try_lock()){
        connconn[uid]->conn->close();
        connconn[uid]->in_use.lock();
      }
      connconn[uid]->in_use.unlock();
    }
    //check if a connection exists, and if not create one
    conn_mutex.lock();
    if (!connconn.count(uid) || !connconn[uid]->conn->connected()){
      if (connconn.count(uid)){connconn.erase(uid);}
      connconn[uid] = new ConnConn(new Socket::Connection("/tmp/mist/http_"+connector));
      connconn[uid]->conn->setBlocking(false);//do not block on spool() with no data
    }
    //start a new timeout thread, if neccesary
    if (timeout_mutex.try_lock()){
      if (timeouter){
        timeouter->join();
        delete timeouter;
      }
      timeouter = new tthread::thread(Connector_HTTP::Timeout_Thread, 0);
      timeout_mutex.unlock();
    }
    conn_mutex.unlock();

    //lock the mutex for this connection, and handle the request
    tthread::lock_guard<tthread::mutex> guard(connconn[uid]->in_use);
    //if the server connection is dead, handle as timeout.
    if (!connconn.count(uid) || !connconn[uid]->conn->connected()){
      Handle_Timeout(H, conn);
      return;
    }
    //forward the original request
    connconn[uid]->conn->Send(request);
    connconn[uid]->lastuse = 0;
    unsigned int timeout = 0;
    //wait for a response
    while (connconn.count(uid) && connconn[uid]->conn->connected() && conn->connected()){
      conn->spool();
      if (connconn[uid]->conn->spool()){
        //check if the whole response was received
        if (H.Read(connconn[uid]->conn->Received())){
          break;//continue down below this while loop
        }
      }else{
        //keep trying unless the timeout triggers
        if (timeout++ > 200){
          std::cout << "[20s timeout triggered]" << std::endl;
          Handle_Timeout(H, conn);
          return;
        }else{
          usleep(100000);
        }
      }
    }
    if (!connconn.count(uid) || !connconn[uid]->conn->connected() || !conn->connected()){
      //failure, disconnect and sent error to user
      Handle_Timeout(H, conn);
      return;
    }else{
      //success, check type of response
      if (H.GetHeader("Content-Length") != ""){
        //known length - simply re-send the request with added headers and continue
        H.SetHeader("X-UID", uid);
        H.SetHeader("Server", "mistserver/" PACKAGE_VERSION "/" + Util::Config::libver);
        conn->Send(H.BuildResponse("200", "OK"));
      }else{
        //unknown length
        H.SetHeader("X-UID", uid);
        H.SetHeader("Server", "mistserver/" PACKAGE_VERSION "/" + Util::Config::libver);
        conn->Send(H.BuildResponse("200", "OK"));
        //continue sending data from this socket and keep it permanently in use
        while (connconn.count(uid) && connconn[uid]->conn->connected() && conn->connected()){
          if (connconn[uid]->conn->spool()){
            //forward any and all incoming data directly without parsing
            conn->Send(connconn[uid]->conn->Received());
            connconn[uid]->conn->Received().clear();
          }
          conn->spool();
          usleep(30000);
        }
      }
    }
  }

  /// Returns the name of the HTTP connector the given request should be served by.
  /// Can currently return:
  /// - none (request not supported)
  /// - internal (request fed from information internal to this connector)
  /// - dynamic (request fed from http_dynamic connector)
  /// - progressive (request fed from http_progressive connector)
  std::string getHTTPType(HTTP::Parser & H){
    if ((H.url.find("f4m") != std::string::npos) || ((H.url.find("Seg") != std::string::npos) && (H.url.find("Frag") != std::string::npos))){
      H.SetVar("stream", H.url.substr(1,H.url.find("/",1)-1));
      return "dynamic";
    }
    if (H.url.length() > 4){
      std::string ext = H.url.substr(H.url.length() - 4, 4);
      if (ext == ".flv" || ext == ".mp3"){
        H.SetVar("stream", H.url.substr(1,H.url.length() - 5));
        return "progressive";
      }
    }
    if (H.url == "/crossdomain.xml"){return "internal";}
    if (H.url.length() > 10 && H.url.substr(0, 7) == "/embed_" && H.url.substr(H.url.length() - 3, 3) == ".js"){return "internal";}
    return "none";
  }

  /// Thread for handling a single HTTP connection
  void Handle_HTTP_Connection(void * pointer){
    Socket::Connection * conn = (Socket::Connection *)pointer;
    conn->setBlocking(false);//do not block on conn.spool() when no data is available
    HTTP::Parser Client;
    while (conn->connected()){
      if (conn->spool()){
        if (Client.Read(conn->Received())){
          std::string handler = getHTTPType(Client);
          #if DEBUG >= 4
          std::cout << "Received request: " << Client.url << " => " << handler << " (" << Client.GetVar("stream") << ")" << std::endl;
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
      }else{
        usleep(10000);//sleep 10ms
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

  //start progressive and dynamic handlers from the same folder as this application
  Util::Procs::Start("progressive", (std::string)(argv[0]) + "Progressive -n");
  Util::Procs::Start("dynamic", (std::string)(argv[0]) + "Dynamic -n");
  
  while (server_socket.connected() && conf.is_active){
    Socket::Connection S = server_socket.accept();
    if (S.connected()){//check if the new connection is valid
      //lock the thread mutex and spawn a new thread for this connection
      Connector_HTTP::thread_mutex.lock();
      tthread::thread * T = new tthread::thread(Connector_HTTP::Handle_HTTP_Connection, (void *)(new Socket::Connection(S)));
      Connector_HTTP::active_threads.insert(T);
      //clean up any threads that may have finished
      while (!Connector_HTTP::done_threads.empty()){
        T = *Connector_HTTP::done_threads.begin();
        T->join();
        Connector_HTTP::done_threads.erase(T);
        delete T;
      }
      Connector_HTTP::thread_mutex.unlock();
    }else{
      usleep(100000);//sleep 100ms
    }
  }//while connected and not requested to stop
  server_socket.close();
  Util::Procs::StopAll();
  return 0;
}//main
