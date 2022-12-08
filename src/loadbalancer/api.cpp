#include "api.h"
#include "util_load.h"
#include <fstream>
#include <mist/auth.h>
#include <mist/encode.h>
#include <mist/encryption.h>
namespace Loadbalancer{
  /**
   * allow connection threads to be made to call handleRequests
   */
  int handleRequest(Socket::Connection &conn){
    return handleRequests(conn, 0, 0);
  }
  /**
   * function to select the api function wanted
   */
  int handleRequests(Socket::Connection &conn, HTTP::Websocket *webSock = 0, LoadBalancer *LB = 0){
    HTTP::Parser H;
    while (conn){
      // Handle websockets
      if (webSock){
        if (webSock->readFrame()){
          LB = onWebsocketFrame(webSock, conn.getHost(), LB);
          continue;
        }else{
          Util::sleep(100);
          continue;
        }
      }else if ((conn.spool() || conn.Received().size()) && H.Read(conn)){
        // Handle upgrade to websocket if the output supports it
        std::string upgradeHeader = H.GetHeader("Upgrade");
        Util::stringToLower(upgradeHeader);
        if (upgradeHeader == "websocket"){
          INFO_MSG("Switching to Websocket mode");
          conn.setBlocking(false);
          webSock = new HTTP::Websocket(conn, H);
          if (!(*webSock)){
            delete webSock;
            webSock = 0;
            continue;
          }
          H.Clean();
          continue;
        }

        // handle non-websocket connections
        std::string pathvar = HTTP::URL(H.url).path;
        Util::StringParser path(pathvar, pathdelimiter);
        std::string api = path.next();

        if (!H.method.compare("PUT") && !api.compare("stream")){
          stream(conn, H, path.next(), path.next(), true);
          lastPromethNode.numSuccessRequests++;
          continue;
        }
        if (!H.method.compare("GET") && !api.compare("salt")){// request your salt
          H.Clean();
          H.SetHeader("Content-Type", "text/plain");
          H.SetBody(userAuth.at(path.next()).second);
          H.setCORSHeaders();
          H.SendResponse("200", "OK", conn);
          H.Clean();
          lastPromethNode.numSuccessRequests++;
          continue;
        }

        if (H.url.substr(0, passphrase.size() + 6) == "/" + passphrase + ".json"){
          H.Clean();
          H.SetHeader("Content-Type", "text/json");
          H.setCORSHeaders();
          H.StartResponse("200", "OK", H, conn, true);
          H.Chunkify(handlePrometheus().toString(), conn);
          continue;
        }

        // Authentication
        std::string creds = H.GetHeader("Authorization");

        // auth with username and password
        if (!creds.substr(0, 5).compare("Basic")){
          std::string auth = Encodings::Base64::decode(creds.substr(6, creds.size()));
          Util::StringParser cred(auth, authDelimiter);
          // check if user exists
          std::map<std::string, std::pair<std::string, std::string> >::iterator user =
              userAuth.find(cred.next());
          // check password
          if (user == userAuth.end() ||
              ((*user).second.first).compare(Secure::sha256(cred.next() + (*user).second.second))){
            H.SetBody("invalid credentials");
            H.setCORSHeaders();
            H.SendResponse("403", "Forbidden", conn);
            H.Clean();
            conn.close();
            lastPromethNode.badAuth++;
            continue;
          }
          lastPromethNode.goodAuth++;
        }
        // auth with bearer token
        else if (!creds.substr(0, 7).compare("Bearer ")){
          if (!bearerTokens.count(creds.substr(7, creds.size()))){
            H.SetBody("invalid token");
            H.setCORSHeaders();
            H.SendResponse("403", "Forbidden", conn);
            H.Clean();
            conn.close();
            lastPromethNode.badAuth++;
            continue;
          }
          lastPromethNode.goodAuth++;
        }
        // whitelist ipv6 & ipv4
        else if (conn.getHost().size()){
          bool found = false;
          std::set<std::string>::iterator it = whitelist.begin();
          while (it != whitelist.end()){
            if (Socket::isBinAddress(conn.getBinHost(), *it)){
              found = true;
              break;
            }
            it++;
          }
          if (!found){
            H.SetBody("not in whitelist");
            H.setCORSHeaders();
            H.SendResponse("403", "Forbidden", conn);
            H.Clean();
            conn.close();
            lastPromethNode.badAuth++;
            continue;
          }
          lastPromethNode.goodAuth++;
        }
        // block other auth forms including none
        else{
          H.SetBody("no credentials given");
          H.setCORSHeaders();
          H.SendResponse("403", "Forbidden", conn);
          H.Clean();
          conn.close();
          lastPromethNode.badAuth++;
          continue;
        }

        // API METHODS
        if (!H.method.compare("PUT")){
          // save config
          if (!api.compare("save")){
            saveFile(true);
            H.Clean();
            H.SetHeader("Content-Type", "text/plain");
            H.SetBody("OK");
            H.setCORSHeaders();
            H.SendResponse("204", "OK", conn);
            H.Clean();
            lastPromethNode.numSuccessRequests++;
          }
          // load config
          else if (!api.compare("load")){
            loadFile(true);
            H.Clean();
            H.SetHeader("Content-Type", "text/plain");
            H.SetBody("OK");
            H.setCORSHeaders();
            H.SendResponse("204", "OK", conn);
            H.Clean();
            lastPromethNode.numSuccessRequests++;
          }
          // add load balancer to mesh
          else if (!api.compare("loadbalancers")){
            std::string loadbalancer = path.next();
            new tthread::thread(addLB, (void *)&loadbalancer);
            H.Clean();
            H.SetHeader("Content-Type", "text/plain");
            H.SetBody("OK");
            H.setCORSHeaders();
            H.SendResponse("200", "OK", conn);
            H.Clean();
            lastPromethNode.numSuccessRequests++;
          }
          // Get/set weights
          else if (!api.compare("weights")){
            JSON::Value ret = setWeights(path, true);
            H.Clean();
            H.SetHeader("Content-Type", "text/plain");
            H.SetBody(ret.toString());
            H.setCORSHeaders();
            H.SendResponse("200", "OK", conn);
            H.Clean();
            lastPromethNode.numSuccessRequests++;
          }
          // Add server to list
          else if (!api.compare("servers")){
            std::string ret;
            addServer(ret, path.next(), true);
            H.Clean();
            H.SetHeader("Content-Type", "text/plain");
            H.SetBody(ret.c_str());
            H.setCORSHeaders();
            H.SendResponse("200", "OK", conn);
            H.Clean();
            lastPromethNode.numSuccessRequests++;
          }else if (!api.compare("balancing")){
            balance(path);
            lastPromethNode.numSuccessRequests++;
          }else if (!api.compare("standby")){
            std::string name = path.next();
            std::set<hostEntry *>::iterator it = hosts.begin();
            while (!name.compare((*it)->name) && it != hosts.end()) it++;
            if (it != hosts.end()){
              setStandBy(*it, path.nextInt());
              H.Clean();
              H.SetHeader("Content-Type", "text/plain");
              H.setCORSHeaders();
              H.SendResponse("200", "OK", conn);
              H.Clean();
              lastPromethNode.numSuccessRequests++;
            }else{
              lastPromethNode.numFailedRequests++;
              H.Clean();
              H.SetHeader("Content-Type", "text/plain");
              H.SetBody("invalid server name");
              H.setCORSHeaders();
              H.SendResponse("200", "OK", conn);
              H.Clean();
            }
          }
          // auth
          else if (!api.compare("auth")){
            api = path.next();
            // add bearer token
            if (!api.compare("bearer")){
              std::string bearer = path.next();
              bearerTokens.insert(bearer);
              H.Clean();
              H.SetHeader("Content-Type", "text/plain");
              H.SetBody("OK");
              H.setCORSHeaders();
              H.SendResponse("200", "OK", conn);
              H.Clean();
              lastPromethNode.numSuccessRequests++;
              // start save timer
              time(&prevconfigChange);
              if (saveTimer == 0) saveTimer = new tthread::thread(saveTimeCheck, NULL);
            }
            // add user acount
            else if (!api.compare("user")){
              std::string userName = path.next();
              std::string salt = generateSalt();
              std::string password = Secure::sha256(path.next() + salt);
              userAuth[userName] = std::pair<std::string, std::string>(password, salt);
              H.Clean();
              H.SetHeader("Content-Type", "text/plain");
              H.SetBody("OK");
              H.setCORSHeaders();
              H.SendResponse("200", "OK", conn);
              H.Clean();
              JSON::Value j;
              j[addUserKey] = userName;
              j[addPassKey] = password;
              j[addSaltKey] = salt;
              for (std::set<LoadBalancer *>::iterator it = loadBalancers.begin();
                   it != loadBalancers.end(); it++){
                (*it)->send(j.asString());
              }
              lastPromethNode.numSuccessRequests++;
              // start save timer
              time(&prevconfigChange);
              if (saveTimer == 0) saveTimer = new tthread::thread(saveTimeCheck, NULL);
            }
            // add whitelist policy
            else if (!api.compare("whitelist")){
              whitelist.insert(H.body);
              JSON::Value j;
              j[addWhitelistKey] = H.body;
              for (std::set<LoadBalancer *>::iterator it = loadBalancers.begin();
                   it != loadBalancers.end(); it++){
                (*it)->send(j.asString());
              }
              H.Clean();
              H.SetHeader("Content-Type", "text/plain");
              H.SetBody("OK");
              H.setCORSHeaders();
              H.SendResponse("200", "OK", conn);
              H.Clean();
              lastPromethNode.numSuccessRequests++;
              // start save timer
              time(&prevconfigChange);
              if (saveTimer == 0) saveTimer = new tthread::thread(saveTimeCheck, NULL);
            }
            // handle none api
            else{
              H.Clean();
              H.SetHeader("Content-Type", "text/plain");
              H.SetBody("invalid");
              H.setCORSHeaders();
              H.SendResponse("200", "OK", conn);
              H.Clean();
              lastPromethNode.numIllegalRequests++;
            }
          }
          // handle none api
          else{
            H.Clean();
            H.SetHeader("Content-Type", "text/plain");
            H.SetBody("invalid");
            H.setCORSHeaders();
            H.SendResponse("200", "OK", conn);
            H.Clean();
            lastPromethNode.numIllegalRequests++;
          }
        }else if (!H.method.compare("GET")){
          if (!api.compare("loadbalancers")){
            std::string out = getLoadBalancerList();
            H.Clean();
            H.SetHeader("Content-Type", "text/plain");
            H.SetBody(out);
            H.setCORSHeaders();
            H.SendResponse("200", "OK", conn);
            H.Clean();
            lastPromethNode.numSuccessRequests++;
          }
          // Get server list
          else if (!api.compare("servers")){
            JSON::Value ret = serverList();
            H.Clean();
            H.SetHeader("Content-Type", "text/plain");
            H.SetBody(ret.toPrettyString());
            H.setCORSHeaders();
            H.SendResponse("200", "OK", conn);
            H.Clean();
            lastPromethNode.numSuccessRequests++;
          }
          // Request viewer count
          else if (!api.compare("viewers")){
            JSON::Value ret = getViewers();
            H.Clean();
            H.SetHeader("Content-Type", "text/plain");
            H.SetBody(ret.toPrettyString());
            H.setCORSHeaders();
            H.SendResponse("200", "OK", conn);
            H.Clean();
            lastPromethNode.numSuccessRequests++;
          }
          // Request full stream statistics
          else if (!api.compare("streamstats")){
            JSON::Value ret = getStreamStats(path.next());
            H.Clean();
            H.SetHeader("Content-Type", "text/plain");
            H.SetBody(ret.toPrettyString());
            H.setCORSHeaders();
            H.SendResponse("200", "OK", conn);
            H.Clean();
            lastPromethNode.numSuccessRequests++;
          }
          // get stream viewer count
          else if (!api.compare("stream")){
            uint64_t count = getStream(path.next());
            H.Clean();
            H.SetHeader("Content-Type", "text/plain");
            H.SetBody(JSON::Value(count).asString());
            H.setCORSHeaders();
            H.SendResponse("200", "OK", conn);
            H.Clean();
            lastPromethNode.numSuccessRequests++;
          }
          // Find source for given stream
          else if (!api.compare("source")){
            std::string source = path.next();
            getSource(conn, H, source, path.next(), true);
          }
          // Find optimal ingest point
          else if (!api.compare("ingest")){
            std::string ingest = path.next();
            getIngest(conn, H, ingest, path.next(), true);
          }
          // Find host(s) status
          else if (!api.compare("host")){
            std::string host = path.next();
            if (!host.size()){
              JSON::Value ret = getAllHostStates();
              H.Clean();
              H.SetHeader("Content-Type", "text/plain");
              H.SetBody(ret.toPrettyString());
              H.setCORSHeaders();
              H.SendResponse("200", "OK", conn);
              H.Clean();
              lastPromethNode.numSuccessRequests++;
            }else{
              JSON::Value ret = getHostState(host);
              H.Clean();
              H.SetHeader("Content-Type", "text/plain");
              H.SetBody(ret.toPrettyString());
              H.setCORSHeaders();
              H.SendResponse("200", "OK", conn);
              H.Clean();
              lastPromethNode.numSuccessRequests++;
            }
            // Get weights
          }else if (!api.compare("weights")){
            JSON::Value ret = setWeights(Util::StringParser(empty, empty), false);
            H.Clean();
            H.SetHeader("Content-Type", "text/plain");
            H.SetBody(ret.toString());
            H.setCORSHeaders();
            H.SendResponse("200", "OK", conn);
            H.Clean();
            lastPromethNode.numSuccessRequests++;
          }else if (!api.compare("balancing")){
            JSON::Value ret;
            ret["balancing Interval"] = balancingInterval;
            ret["minstandby"] = minstandby;
            ret["maxstandby"] = maxstandby;
            ret["highCappacityTriggerBW"] = highCappacityTriggerBW;
            ret["highCappacityTriggerCPU"] = highCappacityTriggerCPU;
            ret["highCappacityTriggerRAM"] = highCappacityTriggerRAM;
            ret["lowCappacityTriggerBW"] = lowCappacityTriggerBW;
            ret["lowCappacityTriggerCPU"] = lowCappacityTriggerCPU;
            ret["lowCappacityTriggerRAM"] = lowCappacityTriggerRAM;
            ret["cappacityTriggerBW"] = cappacityTriggerBW;
            ret["cappacityTriggerCPU"] = cappacityTriggerCPU;
            ret["cappacityTriggerRAM"] = cappacityTriggerRAM;
            ret["cappacityTriggerCPUDec"] = cappacityTriggerCPUDec;
            ret["cappacitytriggerBWDec"] = cappacitytriggerBWDec;
            ret["cappacityTriggerRAMDec"] = cappacityTriggerRAMDec;
            ret["SERVERMONITORLIMIT"] = serverMonitorLimit;
            H.Clean();
            H.SetHeader("Content-Type", "text/plain");
            H.SetBody(ret.toString());
            H.setCORSHeaders();
            H.SendResponse("200", "OK", conn);
            H.Clean();
            lastPromethNode.numSuccessRequests++;
          }else if (!api.compare("auth")){
            api = path.next();
            // add bearer token
            if (!api.compare("bearer")){
              JSON::Value j = bearerTokens;
              H.Clean();
              H.SetHeader("Content-Type", "text/plain");
              H.SetBody(j.asString());
              H.setCORSHeaders();
              H.SendResponse("200", "OK", conn);
              H.Clean();
              lastPromethNode.numSuccessRequests++;
            }
            // add user acount
            else if (!api.compare("user")){
              JSON::Value j = userAuth;
              H.Clean();
              H.SetHeader("Content-Type", "text/plain");
              H.SetBody(j.asString());
              H.setCORSHeaders();
              H.SendResponse("200", "OK", conn);
              H.Clean();
              lastPromethNode.numSuccessRequests++;
            }
            // add whitelist policy
            else if (!api.compare("whitelist")){
              JSON::Value j = whitelist;
              H.Clean();
              H.SetHeader("Content-Type", "text/plain");
              H.SetBody(j.asString());
              H.setCORSHeaders();
              H.SendResponse("200", "OK", conn);
              H.Clean();
              lastPromethNode.numSuccessRequests++;
            }
            // handle none api
            else{
              H.Clean();
              H.SetHeader("Content-Type", "text/plain");
              H.SetBody("invalid");
              H.setCORSHeaders();
              H.SendResponse("200", "OK", conn);
              H.Clean();
              conn.close();
              lastPromethNode.numIllegalRequests++;
            }
          }
          // handle none api
          else{
            H.Clean();
            H.SetHeader("Content-Type", "text/plain");
            H.SetBody("invalid");
            H.setCORSHeaders();
            H.SendResponse("200", "OK", conn);
            H.Clean();
            conn.close();
            lastPromethNode.numIllegalRequests++;
          }
        }else if (!H.method.compare("DELETE")){
          // remove load balancer from mesh
          if (!api.compare("loadbalancers")){
            std::string loadbalancer = path.next();
            removeLB(loadbalancer, true);
            H.Clean();
            H.SetHeader("Content-Type", "text/plain");
            H.SetBody("OK");
            H.setCORSHeaders();
            H.SendResponse("200", "OK", conn);
            H.Clean();
            lastPromethNode.numSuccessRequests++;
          }
          // Remove server from list
          else if (!api.compare("servers")){
            std::string s = path.next();
            JSON::Value ret = delServer(s, true);
            H.Clean();
            H.SetHeader("Content-Type", "text/plain");
            H.SetBody(ret.toPrettyString());
            H.setCORSHeaders();
            H.SendResponse("200", "OK", conn);
            H.Clean();
            lastPromethNode.numSuccessRequests++;
          }
          // auth
          else if (!api.compare("auth")){
            api = path.next();
            // del bearer token
            if (!api.compare("bearer")){
              bearerTokens.erase(path.next());
              H.Clean();
              H.SetHeader("Content-Type", "text/plain");
              H.SetBody("OK");
              H.setCORSHeaders();
              H.SendResponse("200", "OK", conn);
              H.Clean();
              lastPromethNode.numSuccessRequests++;
              // start save timer
              time(&prevconfigChange);
              if (saveTimer == 0) saveTimer = new tthread::thread(saveTimeCheck, NULL);
            }
            // del user acount
            else if (!api.compare("user")){
              std::string userName = path.next();
              userAuth.erase(userName);
              H.Clean();
              H.SetHeader("Content-Type", "text/plain");
              H.SetBody("OK");
              H.setCORSHeaders();
              H.SendResponse("200", "OK", conn);
              H.Clean();
              JSON::Value j;
              j[rUserKey] = userName;
              for (std::set<LoadBalancer *>::iterator it = loadBalancers.begin();
                   it != loadBalancers.end(); it++){
                (*it)->send(j.asString());
              }
              lastPromethNode.numSuccessRequests++;
              // start save timer
              time(&prevconfigChange);
              if (saveTimer == 0) saveTimer = new tthread::thread(saveTimeCheck, NULL);
            }
            // del whitelist policy
            else if (!api.compare("whitelist")){
              std::set<std::string>::iterator it = whitelist.begin();
              while (it != whitelist.end()){
                if (!(*it).compare(H.body)){
                  whitelist.erase(it);
                  it = whitelist.begin();
                }else
                  it++;
              }

              JSON::Value j;
              j[rUserKey] = H.body;
              for (std::set<LoadBalancer *>::iterator it = loadBalancers.begin();
                   it != loadBalancers.end(); it++){
                (*it)->send(j.asString());
              }
              H.Clean();
              H.SetHeader("Content-Type", "text/plain");
              H.SetBody("OK");
              H.setCORSHeaders();
              H.SendResponse("200", "OK", conn);
              H.Clean();
              lastPromethNode.numSuccessRequests++;
              // start save timer
              time(&prevconfigChange);
              if (saveTimer == 0) saveTimer = new tthread::thread(saveTimeCheck, NULL);
            }
            // handle none api
            else{
              H.Clean();
              H.SetHeader("Content-Type", "text/plain");
              H.SetBody("invalid");
              H.setCORSHeaders();
              H.SendResponse("200", "OK", conn);
              H.Clean();
              lastPromethNode.numIllegalRequests++;
            }
          }
          // handle none api
          else{
            H.Clean();
            H.SetHeader("Content-Type", "text/plain");
            H.SetBody("invalid");
            H.setCORSHeaders();
            H.SendResponse("200", "OK", conn);
            H.Clean();
            lastPromethNode.numIllegalRequests++;
          }
        }
        // handle none api
        else{
          H.Clean();
          H.SetHeader("Content-Type", "text/plain");
          H.SetBody("invalid");
          H.setCORSHeaders();
          H.SendResponse("200", "OK", conn);
          H.Clean();
          lastPromethNode.numIllegalRequests++;
        }
      }
    }
    // check if this is a load balancer connection
    if (LB){
      if (!LB->Go_Down){// check if load balancer crashed
        LB->state = false;
        WARN_MSG("restarting connection of load balancer: %s", LB->getName().c_str());
        int tmp = 0;
        if (lastPromethNode.numReconnectLB.count(LB->getName())){
          tmp = lastPromethNode.numReconnectLB.at(LB->getName());
        }
        lastPromethNode.numReconnectLB.insert(std::pair<std::string, int>(LB->getName(), tmp + 1));
        new tthread::thread(reconnectLB, (void *)LB);
      }else{// shutdown load balancer
        LB->Go_Down = true;
        loadBalancers.erase(LB);
        delete LB;
        INFO_MSG("shuting Down connection");
      }
    }
    conn.close();
    return 0;
  }
  /**
   * handle websockets only used for other load balancers
   * \return loadbalancer corisponding to this socket
   */
  LoadBalancer *onWebsocketFrame(HTTP::Websocket *webSock, std::string name, LoadBalancer *LB){
    std::string frame(webSock->data, webSock->data.size());
    if (!frame.substr(0, frame.size()).compare("ident")){
      webSock->sendFrame(identifier);
      lastPromethNode.numLBSuccessRequests++;
    }else if (!frame.substr(0, frame.find(":")).compare("auth")){
      // send response to challenge
      std::string auth = frame.substr(frame.find(":") + 1);
      std::string pass = Secure::sha256(passHash + auth);
      webSock->sendFrame(pass);

      // send own challenge
      std::string salt = generateSalt();
      webSock->sendFrame(salt);
      lastPromethNode.numLBSuccessRequests++;
    }else if (!frame.substr(0, frame.find(":")).compare("salt")){
      // check responce
      std::string salt = frame.substr(frame.find(":") + 1, frame.find(";") - frame.find(":") - 1);
      std::map<std::string, time_t>::iterator saltIndex = activeSalts.find(salt);

      if (saltIndex == activeSalts.end()){
        webSock->sendFrame("noAuth");
        webSock->getSocket().close();
        WARN_MSG("no salt")
        lastPromethNode.numLBFailedRequests++;
        return LB;
      }

      if (!Secure::sha256(passHash + salt)
               .compare(frame.substr(frame.find(";") + 1, frame.find(" ") - frame.find(";") - 1))){
        // auth successful
        webSock->sendFrame("OK");
        // remove monitored servers to receive new data
        for (std::set<hostEntry *>::iterator it = hosts.begin(); it != hosts.end(); it++){
          delServer((*it)->name, false);
        }
        // remove load balancers to receive new data
        std::set<LoadBalancer *>::iterator it = loadBalancers.begin();
        while (loadBalancers.size()){
          (*it)->send("close");
          (*it)->Go_Down = true;
          loadBalancers.erase(it);
          it = loadBalancers.begin();
        }

        LB = new LoadBalancer(webSock, frame.substr(frame.find(" ") + 1, frame.size()),
                              frame.substr(frame.find(" "), frame.size()));
        loadBalancers.insert(LB);
        INFO_MSG("Load balancer added");
        checkServerMonitors();
        lastPromethNode.numLBSuccessRequests++;
      }else{
        webSock->sendFrame("noAuth");
        INFO_MSG("unautherized load balancer");
        LB = 0;
        lastPromethNode.numLBFailedRequests++;
      }

    }
    // close bad auth
    else if (!frame.substr(0, frame.find(":")).compare("noAuth")){
      webSock->getSocket().close();
      lastPromethNode.numLBSuccessRequests++;
    }
    // close authenticated load balancer
    else if (!frame.compare("close")){
      LB->Go_Down = true;
      loadBalancers.erase(LB);
      webSock->getSocket().close();
      lastPromethNode.numLBSuccessRequests++;
    }else if (LB && !frame.substr(0, 1).compare("{")){
      JSON::Value newVals = JSON::fromString(frame);
      if (newVals.isMember(addLoadbalancerKey)){
        new tthread::thread(addLB, (void *)&(newVals[addLoadbalancerKey].asStringRef()));
        lastPromethNode.numLBSuccessRequests++;
      }else if (newVals.isMember(removeLoadbalancerKey)){
        removeLB(newVals[removeLoadbalancerKey], false);
        lastPromethNode.numLBSuccessRequests++;
      }else if (newVals.isMember(updateHostKey)){
        updateHost(newVals[updateHostKey]);
        lastPromethNode.numLBSuccessRequests++;
      }else if (newVals.isMember(weightsKey)){
        setWeights(newVals[weightsKey]);
        lastPromethNode.numLBSuccessRequests++;
      }else if (newVals.isMember(addServerKey)){
        std::string ret;
        addServer(ret, newVals[addServerKey], false);
        lastPromethNode.numLBSuccessRequests++;
      }else if (newVals.isMember(removeServerKey)){
        delServer(newVals[removeServerKey].asString(), false);
        lastPromethNode.numLBSuccessRequests++;
      }else if (newVals.isMember(sendConfigKey)){
        configFromString(newVals[sendConfigKey]);
        lastPromethNode.numLBSuccessRequests++;
      }else if (newVals.isMember(addViewerKey)){
        // find host
        for (std::set<hostEntry *>::iterator it = hosts.begin(); it != hosts.end(); it++){
          if (newVals[hostKey].asString().compare((*it)->details->host)){
            // call add viewer function
            jsonForEach(newVals[addViewerKey], i){
              for (std::set<hostEntry *>::iterator it = hosts.begin(); it != hosts.end(); it++){
                if (!i.key().compare((*it)->name)){
                  (*it)->details->prevAddBandwidth += i.num();
                  continue;
                }
              }
            }
          }
        }
        lastPromethNode.numLBSuccessRequests++;
      }else if (newVals.isMember(saveKey)){
        saveFile();
        lastPromethNode.numLBSuccessRequests++;
      }else if (newVals.isMember(loadKey)){
        loadFile();
        lastPromethNode.numLBSuccessRequests++;
      }else if (newVals.isMember(balanceKey)){
        balance(newVals[balanceKey]);
        lastPromethNode.numLBSuccessRequests++;
      }else if (newVals.isMember(standbyKey)){
        std::set<hostEntry *>::iterator it = hosts.begin();
        while (!newVals[standbyKey].asString().compare((*it)->name) && it != hosts.end()) it++;
        if (it != hosts.end()) setStandBy(*it, newVals[lockKey]);
        lastPromethNode.numLBSuccessRequests++;
      }else if (newVals.isMember(removeStandbyKey)){
        std::set<hostEntry *>::iterator it = hosts.begin();
        while (!newVals[standbyKey].asString().compare((*it)->name) && it != hosts.end()) it++;
        if (it != hosts.end()) removeStandBy(*it);
        lastPromethNode.numLBSuccessRequests++;
      }else if (newVals.isMember(addWhitelistKey)){
        whitelist.insert(newVals[addWhitelistKey].asString());
        lastPromethNode.numLBSuccessRequests++;
      }else if (newVals.isMember(addPassKey) && newVals.isMember(addUserKey) && newVals.isMember(addSaltKey)){
        userAuth[newVals[addUserKey].asString()] = std::pair<std::string, std::string>(
            newVals[addPassKey].asString(), newVals[addSaltKey].asString());
        lastPromethNode.numLBSuccessRequests++;
      }else if (newVals.isMember(rWhitelistKey)){
        std::set<std::string>::iterator it = whitelist.begin();
        while (it != whitelist.end()){
          if (!(*it).compare(newVals[rWhitelistKey].asString())){
            whitelist.erase(it);
            it = whitelist.begin();
          }else
            it++;
        }
        lastPromethNode.numLBSuccessRequests++;
      }else if (newVals.isMember(rUserKey)){
        userAuth.erase(newVals[rUserKey].asString());
        lastPromethNode.numLBSuccessRequests++;
      }else{
        lastPromethNode.numLBIllegalRequests++;
      }
    }else{
      lastPromethNode.numLBIllegalRequests++;
    }
    return LB;
  }

  /**
   * set balancing settings received through API
   */
  void balance(Util::StringParser path){
    JSON::Value j;
    std::string api = path.next();
    while (!api.compare("minstandby") || !api.compare("maxstandby") ||
           !api.compare(cappacityTriggerCPUDecKEY) || !api.compare(cappacityTriggerRAMDECKEY) ||
           !api.compare(cappacitytriggerBWDecKEY) || !api.compare(cappacityTriggercpuKey) ||
           !api.compare(cappacityTriggerRAMKEY) || !api.compare(cappacityTriggerbwKey) ||
           !api.compare(highCappacityTriggercpuKey) || !api.compare(highCappacityTriggerRAMKEY) ||
           !api.compare(highCappacityTriggerbwKey) || !api.compare(lowCappacityTriggercpuKey) ||
           !api.compare(LOWcappacityTriggerRAMKEY) || !api.compare(lowCappacityTriggerbwKey) ||
           !api.compare(balancingIntervalKEY) || !api.compare(serverMonitorLimitKey)){
      if (!api.compare("minstandby")){
        int newVal = path.nextInt();
        if (newVal > maxstandby){
          minstandby = newVal;
          j[balanceKey][minstandbyKEY] = minstandby;
        }
      }else if (!api.compare("maxstandby")){
        int newVal = path.nextInt();
        if (newVal < minstandby){
          maxstandby = newVal;
          j[balanceKey][maxstandbyKEY] = maxstandby;
        }
      }
      if (!api.compare(cappacityTriggerCPUDecKEY)){
        double newVal = path.nextDouble();
        if (newVal >= 0 && newVal <= 1){
          cappacityTriggerCPUDec = newVal;
          j[balanceKey][cappacityTriggerCPUDecKEY] = cappacityTriggerCPUDec;
        }
      }else if (!api.compare(cappacityTriggerRAMDECKEY)){
        double newVal = path.nextDouble();
        if (newVal >= 0 && newVal <= 1){
          cappacityTriggerRAMDec = newVal;
          j[balanceKey][cappacityTriggerRAMDECKEY] = cappacityTriggerRAMDec;
        }
      }else if (!api.compare(cappacitytriggerBWDecKEY)){
        double newVal = path.nextDouble();
        if (newVal >= 0 && newVal <= 1){
          cappacitytriggerBWDec = newVal;
          j[balanceKey][cappacitytriggerBWDecKEY] = cappacitytriggerBWDec;
        }
      }else if (!api.compare(cappacityTriggercpuKey)){
        double newVal = path.nextDouble();
        if (newVal >= 0 && newVal <= 1){
          cappacityTriggerCPU = newVal;
          j[balanceKey][cappacityTriggercpuKey] = cappacityTriggerCPU;
        }
      }else if (!api.compare(cappacityTriggerRAMKEY)){
        double newVal = path.nextDouble();
        if (newVal >= 0 && newVal <= 1){
          cappacityTriggerRAM = newVal;
          j[balanceKey][cappacityTriggerRAMKEY] = cappacityTriggerRAM;
        }
      }else if (!api.compare(cappacityTriggerbwKey)){
        double newVal = path.nextDouble();
        if (newVal >= 0 && newVal <= 1){
          cappacityTriggerBW = newVal;
          j[balanceKey][cappacityTriggerbwKey] = cappacityTriggerBW;
        }
      }else if (!api.compare(highCappacityTriggercpuKey)){
        double newVal = path.nextDouble();
        if (newVal >= 0 && newVal <= cappacityTriggerCPU){
          highCappacityTriggerCPU = newVal;
          j[balanceKey][highCappacityTriggercpuKey] = highCappacityTriggerCPU;
        }
      }else if (!api.compare(highCappacityTriggerRAMKEY)){
        double newVal = path.nextDouble();
        if (newVal >= 0 && newVal <= cappacityTriggerRAM){
          highCappacityTriggerRAM = newVal;
          j[balanceKey][highCappacityTriggerRAMKEY] = highCappacityTriggerRAM;
        }
      }else if (!api.compare(highCappacityTriggerbwKey)){
        double newVal = path.nextDouble();
        if (newVal >= 0 && newVal <= cappacityTriggerBW){
          highCappacityTriggerBW = newVal;
          j[balanceKey][highCappacityTriggerbwKey] = highCappacityTriggerBW;
        }
      }
      if (!api.compare(lowCappacityTriggercpuKey)){
        double newVal = path.nextDouble();
        if (newVal >= 0 && newVal <= highCappacityTriggerCPU){
          lowCappacityTriggerCPU = newVal;
          j[balanceKey][lowCappacityTriggercpuKey] = lowCappacityTriggerCPU;
        }
      }else if (!api.compare(LOWcappacityTriggerRAMKEY)){
        double newVal = path.nextDouble();
        if (newVal >= 0 && newVal <= highCappacityTriggerRAM){
          lowCappacityTriggerRAM = newVal;
          j[balanceKey][LOWcappacityTriggerRAMKEY] = lowCappacityTriggerRAM;
        }
      }else if (!api.compare(lowCappacityTriggerbwKey)){
        double newVal = path.nextDouble();
        if (newVal >= 0 && newVal <= highCappacityTriggerBW){
          lowCappacityTriggerBW = newVal;
          j[balanceKey][lowCappacityTriggerbwKey] = lowCappacityTriggerBW;
        }
      }else if (!api.compare(balancingIntervalKEY)){
        int newVal = path.nextInt();
        if (newVal >= 0){
          balancingInterval = newVal;
          j[balanceKey][balancingIntervalKEY] = balancingInterval;
        }
      }else if (!api.compare(serverMonitorLimitKey)){
        int newVal = path.nextInt();
        if (newVal >= 0){
          serverMonitorLimit = newVal;
          j[balanceKey][serverMonitorLimitKey] = serverMonitorLimit;
        }
      }else{
        path.next();
      }
      api = path.next();
    }
    for (std::set<LoadBalancer *>::iterator it = loadBalancers.begin(); it != loadBalancers.end(); it++){
      (*it)->send(j.asString());
    }
    // start save timer
    time(&prevconfigChange);
    if (saveTimer == 0) saveTimer = new tthread::thread(saveTimeCheck, NULL);
  }
  /**
   * set balancing settings receiverd from load balancers
   */
  void balance(JSON::Value newVals){
    if (newVals.isMember("minstandby")){
      int newVal = newVals[minstandbyKEY].asInt();
      if (newVal > maxstandby){minstandby = newVal;}
    }
    if (newVals.isMember("maxstandby")){
      int newVal = newVals[maxstandbyKEY].asInt();
      if (newVal < minstandby){maxstandby = newVal;}
    }
    if (newVals.isMember(cappacityTriggerCPUDecKEY)){
      double newVal = newVals[cappacityTriggerCPUDecKEY].asDouble();
      if (newVal >= 0 && newVal <= 1){cappacityTriggerCPUDec = newVal;}
    }
    if (newVals.isMember(cappacityTriggerRAMDECKEY)){
      double newVal = newVals[cappacityTriggerRAMDECKEY].asDouble();
      if (newVal >= 0 && newVal <= 1){cappacityTriggerRAMDec = newVal;}
    }
    if (newVals.isMember(cappacitytriggerBWDecKEY)){
      double newVal = newVals[cappacitytriggerBWDecKEY].asDouble();
      if (newVal >= 0 && newVal <= 1){cappacitytriggerBWDec = newVal;}
    }
    if (newVals.isMember(cappacityTriggercpuKey)){
      double newVal = newVals[cappacityTriggercpuKey].asDouble();
      if (newVal >= 0 && newVal <= 1){cappacityTriggerCPU = newVal;}
    }
    if (newVals.isMember(cappacityTriggerRAMKEY)){
      double newVal = newVals[cappacityTriggerRAMKEY].asDouble();
      if (newVal >= 0 && newVal <= 1){cappacityTriggerRAM = newVal;}
    }else if (newVals.isMember(cappacityTriggerbwKey)){
      double newVal = newVals[cappacityTriggerbwKey].asDouble();
      if (newVal >= 0 && newVal <= 1){cappacityTriggerBW = newVal;}
    }
    if (newVals[highCappacityTriggercpuKey]){
      double newVal = newVals[highCappacityTriggercpuKey].asDouble();
      if (newVal >= 0 && newVal <= cappacityTriggerCPU){highCappacityTriggerCPU = newVal;}
    }
    if (newVals.isMember(highCappacityTriggerRAMKEY)){
      double newVal = newVals[highCappacityTriggerRAMKEY].asDouble();
      if (newVal >= 0 && newVal <= cappacityTriggerRAM){highCappacityTriggerRAM = newVal;}
    }
    if (newVals.isMember(highCappacityTriggerbwKey)){
      double newVal = newVals[highCappacityTriggerbwKey].asDouble();
      if (newVal >= 0 && newVal <= cappacityTriggerBW){highCappacityTriggerBW = newVal;}
    }
    if (newVals.isMember(lowCappacityTriggercpuKey)){
      double newVal = newVals[lowCappacityTriggercpuKey].asDouble();
      if (newVal >= 0 && newVal <= highCappacityTriggerCPU){lowCappacityTriggerCPU = newVal;}
    }
    if (newVals.isMember(LOWcappacityTriggerRAMKEY)){
      double newVal = newVals[LOWcappacityTriggerRAMKEY].asDouble();
      if (newVal >= 0 && newVal <= highCappacityTriggerRAM){lowCappacityTriggerRAM = newVal;}
    }
    if (newVals.isMember(lowCappacityTriggerbwKey)){
      double newVal = newVals[lowCappacityTriggerbwKey].asDouble();
      if (newVal >= 0 && newVal <= highCappacityTriggerBW){lowCappacityTriggerBW = newVal;}
    }
    if (newVals.isMember(balancingIntervalKEY)){
      int newVal = newVals[balancingIntervalKEY].asInt();
      if (newVal >= 0){balancingInterval = newVal;}
    }
    if (newVals.isMember(serverMonitorLimitKey)){
      int newVal = newVals[serverMonitorLimitKey].asInt();
      if (newVal >= 0){serverMonitorLimit = newVal;}
    }
    // start save timer
    time(&prevconfigChange);
    if (saveTimer == 0) saveTimer = new tthread::thread(saveTimeCheck, NULL);
  }

  /**
   * set and get weights
   */
  JSON::Value setWeights(Util::StringParser path, bool resend){
    std::string newVals = path.next();
    while (!newVals.compare("cpu") || !newVals.compare("ram") || !newVals.compare("bw") ||
           !newVals.compare("geo") || !newVals.compare("bonus")){
      int num = path.nextInt();
      if (!newVals.compare("cpu")){
        weight_cpu = num;
      }else if (!newVals.compare("ram")){
        weight_ram = num;
      }else if (!newVals.compare("bw")){
        weight_bw = num;
      }else if (!newVals.compare("geo")){
        weight_geo = num;
      }else if (!newVals.compare("bonus")){
        weight_bonus = num;
      }
      newVals = path.next();
    }

    // create json for sending
    JSON::Value ret;
    ret[cpuKey] = weight_cpu;
    ret[ramKey] = weight_ram;
    ret[bwKey] = weight_bw;
    ret[geoKey] = weight_geo;
    ret[bonusKey] = weight_bonus;

    if (resend){
      JSON::Value j;
      j[weightsKey] = ret;
      for (std::set<LoadBalancer *>::iterator it = loadBalancers.begin(); it != loadBalancers.end(); ++it){
        (*it)->send(j.asString());
      }
    }

    // start save timer
    time(&prevconfigChange);
    if (saveTimer == 0) saveTimer = new tthread::thread(saveTimeCheck, NULL);

    return ret;
  }
  /**
   * set weights for websockets
   */
  void setWeights(const JSON::Value newVals){
    WARN_MSG("%s", newVals.asString().c_str())
    if (!newVals.isMember(cpuKey)){weight_cpu = newVals[cpuKey].asInt();}
    if (!newVals.isMember(ramKey)){weight_ram = newVals[ramKey].asInt();}
    if (!newVals.isMember(bwKey)){weight_bw = newVals[bwKey].asInt();}
    if (!newVals.isMember(geoKey)){weight_geo = newVals[geoKey].asInt();}
    if (!newVals.isMember(bonusKey)){weight_bonus = newVals[bonusKey].asInt();}
    // start save timer
    time(&prevconfigChange);
    if (saveTimer == 0) saveTimer = new tthread::thread(saveTimeCheck, NULL);
  }

  /**
   * remove server from the mesh
   */
  JSON::Value delServer(const std::string delserver, bool resend){
    JSON::Value ret;
    if (resend){
      JSON::Value j;
      j[removeServerKey] = delserver;
      for (std::set<LoadBalancer *>::iterator it = loadBalancers.begin(); it != loadBalancers.end(); ++it){
        (*it)->send(j.asString());
      }
    }
    {
      tthread::lock_guard<tthread::mutex> globGuard(globalMutex);

      ret = "Server not monitored - could not delete from monitored server list!";
      std::string name = "";
      std::set<hostEntry *>::iterator it = hosts.begin();
      while (delserver.compare((*it)->name) && it != hosts.end()){it++;}
      if (it != hosts.end()){
        name = (*it)->name;
        cleanupHost(**it);
        ret[name] = stateLookup[STATE_OFF];
      }
    }

    checkServerMonitors();
    // start save timer
    time(&prevconfigChange);
    if (saveTimer == 0) saveTimer = new tthread::thread(saveTimeCheck, NULL);
    return ret;
  }
  /**
   * add server to be monitored
   */
  void addServer(std::string &ret, const std::string addserver, bool resend){
    tthread::lock_guard<tthread::mutex> globGuard(globalMutex);
    if (addserver.size() >= HOSTNAMELEN){return;}
    if (resend){
      JSON::Value j;
      j[addServerKey] = addserver;
      for (std::set<LoadBalancer *>::iterator it = loadBalancers.begin(); it != loadBalancers.end(); ++it){
        (*it)->send(j.asString());
      }
    }
    bool stop = false;
    hostEntry *newEntry = 0;
    for (std::set<hostEntry *>::iterator it = hosts.begin(); it != hosts.end(); it++){
      if ((std::string)(*it)->name == addserver){
        stop = true;
        break;
      }
    }
    if (stop){
      ret = "Server already monitored - add request ignored";
    }else{
      newEntry = new hostEntry();
      initNewHost(*newEntry, addserver);
      hosts.insert(newEntry);
      checkServerMonitors();

      ret = "server starting";
    }
    // start save timer
    time(&prevconfigChange);
    if (saveTimer == 0) saveTimer = new tthread::thread(saveTimeCheck, NULL);
    return;
  }
  /**
   * return server list
   */
  JSON::Value serverList(){
    JSON::Value ret;
    for (std::set<hostEntry *>::iterator it = hosts.begin(); it != hosts.end(); it++){
      ret[(std::string)(*it)->name] = stateLookup[(*it)->state];
    }
    return ret;
  }

  /**
   * receive server updates and adds new foreign hosts if needed
   */
  void updateHost(JSON::Value newVals){
    std::string hostName = newVals[hostnameKey].asString();
    std::set<hostEntry *>::iterator i = hosts.begin();
    while (i != hosts.end()){
      if (hostName == (*i)->name) break;
      i++;
    }
    if (i == hosts.end()){
      INFO_MSG("unknown host update failed")
    }else{
      (*i)->details->update(newVals[fillstateout], newVals[fillStreamOut],
                            newVals[scoreSourceKey].asInt(), newVals[scoreRateKey].asInt(),
                            newVals[outputsKey], newVals[confStreamKey], newVals[streamsKey],
                            newVals[tagKey], newVals[cpuKey].asInt(), newVals[servLatiKey].asDouble(),
                            newVals[servLongiKey].asDouble(), newVals[binhostKey].asString().c_str(),
                            newVals[hostKey].asString(), newVals[toAddKey].asInt(),
                            newVals[currBandwidthKey].asInt(), newVals[availBandwidthKey].asInt(),
                            newVals[currRAMKey].asInt(), newVals[ramMaxKey].asInt());
    }
  }

  /**
   * remove load balancer from mesh
   */
  void removeLB(std::string removeLoadBalancer, bool resend){
    JSON::Value j;
    j[removeLoadbalancerKey] = removeLoadBalancer;

    // remove load balancer
    std::set<LoadBalancer *>::iterator it = loadBalancers.begin();
    while (it != loadBalancers.end()){
      if (!(*it)->getName().compare(removeLoadBalancer)){
        INFO_MSG("removeing load balancer: %s", removeLoadBalancer.c_str());
        identifiers.erase((*it)->getIdent());
        (*it)->send("close");
        (*it)->Go_Down = true;
        loadBalancers.erase(it);
        it = loadBalancers.end();
      }else{
        it++;
      }
    }
    // notify the last load balancers
    if (resend){
      for (std::set<LoadBalancer *>::iterator it = loadBalancers.begin(); it != loadBalancers.end(); it++){
        (*it)->send(j.asString());
      }
    }
    checkServerMonitors();
    // start save timer
    time(&prevconfigChange);
    if (saveTimer == 0) saveTimer = new tthread::thread(saveTimeCheck, NULL);
  }
  /**
   * add load balancer to mesh
   */
  void addLB(void *p){
    std::string *addLoadBalancer = (std::string *)p;
    if (addLoadBalancer->find(":") == -1){addLoadBalancer->append(":8042");}

    Socket::Connection conn(addLoadBalancer->substr(0, addLoadBalancer->find(":")),
                            atoi(addLoadBalancer->substr(addLoadBalancer->find(":") + 1).c_str()),
                            false, false);

    HTTP::URL url("ws://" + (*addLoadBalancer));
    HTTP::Websocket *ws = new HTTP::Websocket(conn, url);

    ws->sendFrame("ident");

    // check responce
    int reset = 0;
    while (!ws->readFrame()){
      reset++;
      if (reset >= 20){
        WARN_MSG("auth failed: connection timeout");
        int tmp = 0;
        if (lastPromethNode.numFailedConnectLB.count(*addLoadBalancer)){
          tmp = lastPromethNode.numFailedConnectLB.at(*addLoadBalancer);
        }
        lastPromethNode.numFailedConnectLB.insert(std::pair<std::string, int>(*addLoadBalancer, tmp + 1));
        return;
      }
      sleep(1);
    }

    std::string ident(ws->data, ws->data.size());

    for (std::set<std::string>::iterator i = identifiers.begin(); i != identifiers.end(); i++){
      if (!(*i).compare(ident)){
        ws->sendFrame("noAuth");
        conn.close();
        WARN_MSG("load balancer already connected");
        int tmp = 0;
        if (lastPromethNode.numFailedConnectLB.count(*addLoadBalancer)){
          tmp = lastPromethNode.numFailedConnectLB.at(*addLoadBalancer);
        }
        lastPromethNode.numFailedConnectLB.insert(std::pair<std::string, int>(*addLoadBalancer, tmp + 1));
        return;
      }
    }

    // send challenge
    std::string salt = generateSalt();
    ws->sendFrame("auth:" + salt);

    // check responce
    reset = 0;
    while (!ws->readFrame()){
      reset++;
      if (reset >= 20){
        WARN_MSG("auth failed: connection timeout");
        int tmp = 0;
        if (lastPromethNode.numFailedConnectLB.count(*addLoadBalancer)){
          tmp = lastPromethNode.numFailedConnectLB.at(*addLoadBalancer);
        }
        lastPromethNode.numFailedConnectLB.insert(std::pair<std::string, int>(*addLoadBalancer, tmp + 1));
        return;
      }
      sleep(1);
    }
    std::string result(ws->data, ws->data.size());

    if (Secure::sha256(passHash + salt).compare(result)){
      // unautherized
      WARN_MSG("unautherised");
      int tmp = 0;
      if (lastPromethNode.numFailedConnectLB.count(*addLoadBalancer)){
        tmp = lastPromethNode.numFailedConnectLB.at(*addLoadBalancer);
      }
      lastPromethNode.numFailedConnectLB.insert(std::pair<std::string, int>(*addLoadBalancer, tmp + 1));
      ws->sendFrame("noAuth");
      return;
    }
    // send response to challenge
    reset = 0;
    while (!ws->readFrame()){
      reset++;
      if (reset >= 20){
        WARN_MSG("auth failed: connection timeout");
        int tmp = 0;
        if (lastPromethNode.numFailedConnectLB.count(*addLoadBalancer)){
          tmp = lastPromethNode.numFailedConnectLB.at(*addLoadBalancer);
        }
        lastPromethNode.numFailedConnectLB.insert(std::pair<std::string, int>(*addLoadBalancer, tmp + 1));
        return;
      }
      sleep(1);
    }
    std::string auth(ws->data, ws->data.size());
    std::string pass = Secure::sha256(passHash + auth);

    ws->sendFrame("salt:" + auth + ";" + pass + " " + myName);

    reset = 0;
    while (!ws->readFrame()){
      reset++;
      if (reset >= 20){
        WARN_MSG("auth failed: connection timeout");
        int tmp = 0;
        if (lastPromethNode.numFailedConnectLB.count(*addLoadBalancer)){
          tmp = lastPromethNode.numFailedConnectLB.at(*addLoadBalancer);
        }
        lastPromethNode.numFailedConnectLB.insert(std::pair<std::string, int>(*addLoadBalancer, tmp + 1));
        return;
      }
      sleep(1);
    }
    std::string check(ws->data, ws->data.size());
    if (check == "OK"){
      INFO_MSG("Successful authentication of load balancer %s", addLoadBalancer->c_str());
      LoadBalancer *LB = new LoadBalancer(ws, *addLoadBalancer, ident);
      loadBalancers.insert(LB);
      identifiers.insert(ident);

      JSON::Value z;
      z[sendConfigKey] = configToString();
      LB->send(z.asString());

      // start save timer
      time(&prevconfigChange);
      if (saveTimer == 0) saveTimer = new tthread::thread(saveTimeCheck, NULL);

      int tmp = 0;
      if (lastPromethNode.numSuccessConnectLB.count(*addLoadBalancer)){
        tmp = lastPromethNode.numSuccessConnectLB.at(*addLoadBalancer);
      }
      lastPromethNode.numSuccessConnectLB.insert(std::pair<std::string, int>(*addLoadBalancer, tmp + 1));

      // start monitoring
      handleRequests(conn, ws, LB);
    }else if (check == "noAuth"){
      addLB(addLoadBalancer);
    }
    return;
  }
  /**
   * reconnect to load balancer
   */
  void reconnectLB(void *p){
    LoadBalancer *LB = (LoadBalancer *)p;
    identifiers.erase(LB->getIdent());
    std::string addLoadBalancer = LB->getName();

    Socket::Connection conn(addLoadBalancer.substr(0, addLoadBalancer.find(":")),
                            atoi(addLoadBalancer.substr(addLoadBalancer.find(":") + 1).c_str()), false, false);

    HTTP::URL url("ws://" + (addLoadBalancer));
    HTTP::Websocket *ws = new HTTP::Websocket(conn, url);

    ws->sendFrame("ident");

    // check responce
    int reset = 0;
    while (!ws->readFrame()){
      reset++;
      if (reset >= 20){
        WARN_MSG("auth failed: connection timeout");
        int tmp = 0;
        if (lastPromethNode.numFailedConnectLB.count(addLoadBalancer)){
          tmp = lastPromethNode.numFailedConnectLB.at(LB->getName());
        }
        lastPromethNode.numFailedConnectLB.insert(std::pair<std::string, int>(LB->getName(), tmp + 1));
        reconnectLB(p);
        return;
      }
      sleep(1);
    }

    std::string ident(ws->data, ws->data.size());

    for (std::set<std::string>::iterator i = identifiers.begin(); i != identifiers.end(); i++){
      if (!(*i).compare(ident)){
        ws->sendFrame("noAuth");
        conn.close();
        WARN_MSG("load balancer already connected");
        int tmp = 0;
        if (lastPromethNode.numFailedConnectLB.count(addLoadBalancer)){
          tmp = lastPromethNode.numFailedConnectLB.at(LB->getName());
        }
        lastPromethNode.numFailedConnectLB.insert(std::pair<std::string, int>(LB->getName(), tmp + 1));
        return;
      }
    }

    // send challenge
    std::string salt = generateSalt();
    ws->sendFrame("auth:" + salt);

    // check responce
    reset = 0;
    while (!ws->readFrame()){
      reset++;
      if (reset >= 20){
        WARN_MSG("auth failed: connection timeout");
        int tmp = 0;
        if (lastPromethNode.numFailedConnectLB.count(addLoadBalancer)){
          tmp = lastPromethNode.numFailedConnectLB.at(LB->getName());
        }
        lastPromethNode.numFailedConnectLB.insert(std::pair<std::string, int>(LB->getName(), tmp + 1));
        reconnectLB(p);
        return;
      }
      sleep(1);
    }
    std::string result(ws->data, ws->data.size());

    if (Secure::sha256(passHash + salt).compare(result)){
      // unautherized
      WARN_MSG("unautherised");
      int tmp = 0;
      if (lastPromethNode.numFailedConnectLB.count(addLoadBalancer)){
        tmp = lastPromethNode.numFailedConnectLB.at(LB->getName());
      }
      lastPromethNode.numFailedConnectLB.insert(std::pair<std::string, int>(LB->getName(), tmp + 1));
      ws->sendFrame("noAuth");
      return;
    }
    // send response to challenge
    reset = 0;
    while (!ws->readFrame()){
      reset++;
      if (reset >= 20){
        WARN_MSG("auth failed: connection timeout");
        int tmp = 0;
        if (lastPromethNode.numFailedConnectLB.count(addLoadBalancer)){
          tmp = lastPromethNode.numFailedConnectLB.at(LB->getName());
        }
        lastPromethNode.numFailedConnectLB.insert(std::pair<std::string, int>(LB->getName(), tmp + 1));
        reconnectLB(p);
        return;
      }
      sleep(1);
    }
    std::string auth(ws->data, ws->data.size());
    std::string pass = Secure::sha256(passHash + auth);

    ws->sendFrame("salt:" + auth + ";" + pass + " " + myName);

    reset = 0;
    while (!ws->readFrame()){
      reset++;
      if (reset >= 20){
        WARN_MSG("auth failed: connection timeout");
        int tmp = 0;
        if (lastPromethNode.numFailedConnectLB.count(addLoadBalancer)){
          tmp = lastPromethNode.numFailedConnectLB.at(LB->getName());
        }
        lastPromethNode.numFailedConnectLB.insert(std::pair<std::string, int>(LB->getName(), tmp + 1));
        reconnectLB(p);
        return;
      }
      sleep(1);
    }
    std::string check(ws->data, ws->data.size());
    if (check == "OK"){
      INFO_MSG("Successful authentication of load balancer %s", addLoadBalancer.c_str());
      LoadBalancer *LB = new LoadBalancer(ws, addLoadBalancer, ident);
      loadBalancers.insert(LB);
      identifiers.insert(ident);
      LB->state = true;

      JSON::Value z;
      z[sendConfigKey] = configToString();
      LB->send(z.asString());

      int tmp = 0;
      if (lastPromethNode.numSuccessConnectLB.count(LB->getName())){
        tmp = lastPromethNode.numSuccessConnectLB.at(LB->getName());
      }
      lastPromethNode.numSuccessConnectLB.insert(std::pair<std::string, int>(LB->getName(), tmp + 1));
      // start monitoring
      handleRequests(conn, ws, LB);
    }else{
      reconnectLB(p);
    }
    return;
  }
  /**
   * returns load balancer list
   */
  std::string getLoadBalancerList(){
    std::string out = "\"loadbalancers\": [";
    for (std::set<LoadBalancer *>::iterator it = loadBalancers.begin(); it != loadBalancers.end(); ++it){
      if (it != loadBalancers.begin()){out += ", ";}
      out += "\"" + (*it)->getName() + "\"";
    }
    out += "]";
    return out;
  }

  /**
   * return server data of a server
   */
  JSON::Value getHostState(const std::string host){
    JSON::Value ret;
    for (std::set<hostEntry *>::iterator it = hosts.begin(); it != hosts.end(); it++){
      if ((*it)->state == STATE_OFF){continue;}
      if ((*it)->details->host == host){
        ret = stateLookup[(*it)->state];
        if ((*it)->state != STATE_ACTIVE){continue;}
        (*it)->details->fillState(ret);
        break;
      }
    }
    return ret;
  }
  /**
   * return all server data
   */
  JSON::Value getAllHostStates(){
    JSON::Value ret;
    for (std::set<hostEntry *>::iterator it = hosts.begin(); it != hosts.end(); it++){
      if ((*it)->state == STATE_OFF){continue;}
      ret[(*it)->details->host] = stateLookup[(*it)->state];
      if ((*it)->state != STATE_ACTIVE){continue;}
      (*it)->details->fillState(ret[(*it)->details->host]);
    }
    return ret;
  }

  /**
   * return viewer counts of streams
   */
  JSON::Value getViewers(){
    JSON::Value ret;
    for (std::set<hostEntry *>::iterator it = hosts.begin(); it != hosts.end(); it++){
      if ((*it)->state == STATE_OFF){continue;}
      (*it)->details->fillStreams(ret);
    }
    return ret;
  }
  /**
   * get view count of a stream
   */
  uint64_t getStream(const std::string stream){
    uint64_t count = 0;
    for (std::set<hostEntry *>::iterator it = hosts.begin(); it != hosts.end(); it++){
      if ((*it)->state == STATE_OFF){continue;}
      count += (*it)->details->getViewers(stream);
    }
    return count;
  }
  /**
   * return stream stats
   */
  JSON::Value getStreamStats(const std::string streamStats){
    JSON::Value ret;
    for (std::set<hostEntry *>::iterator it = hosts.begin(); it != hosts.end(); it++){
      if ((*it)->state == STATE_OFF){continue;}
      (*it)->details->fillStreamStats(streamStats, ret);
    }
    return ret;
  }

  /**
   * return the best source of a stream for inter server replication
   */
  void getSource(Socket::Connection conn, HTTP::Parser H, const std::string source,
                 const std::string fback, bool repeat = true){
    H.Clean();
    H.SetHeader("Content-Type", "text/plain");
    INFO_MSG("Finding source for stream %s", source.c_str());
    std::string bestHost = "";
    std::map<std::string, int32_t> tagAdjust;
    if (H.GetVar("tag_adjust") != ""){fillTagAdjust(tagAdjust, H.GetVar("tag_adjust"));}
    if (H.hasHeader("X-Tag-Adjust")){fillTagAdjust(tagAdjust, H.GetHeader("X-Tag-Adjust"));}
    double lat = 0;
    double lon = 0;
    if (H.GetVar("lat") != ""){
      lat = atof(H.GetVar("lat").c_str());
      H.SetVar("lat", "");
    }
    if (H.GetVar("lon") != ""){
      lon = atof(H.GetVar("lon").c_str());
      H.SetVar("lon", "");
    }
    if (H.hasHeader("X-Latitude")){lat = atof(H.GetHeader("X-Latitude").c_str());}
    if (H.hasHeader("X-Longitude")){lon = atof(H.GetHeader("X-Longitude").c_str());}
    uint64_t bestScore = 0;

    for (std::set<hostEntry *>::iterator it = hosts.begin(); it != hosts.end(); it++){
      if ((*it)->state != STATE_ONLINE){continue;}
      if (Socket::matchIPv6Addr(std::string((*it)->details->binHost, 16), conn.getBinHost(), 0)){
        INFO_MSG("Ignoring same-host entry %s", (*it)->details->host.data());
        continue;
      }
      uint64_t score = (*it)->details->source(source, tagAdjust, 0, lat, lon);
      if (score > bestScore){
        bestHost = "dtsc://" + (*it)->details->host;
        bestScore = score;
      }
    }
    if (bestScore == 0){
      if (repeat){
        extraServer();
        getSource(conn, H, source, fback, false);
      }
      if (fback.size()){
        bestHost = fback;
      }else{
        bestHost = fallback;
      }
      lastPromethNode.numFailedSource++;
      FAIL_MSG("No source for %s found!", source.c_str());
    }else{
      lastPromethNode.numSuccessSource++;
      INFO_MSG("Winner: %s scores %" PRIu64, bestHost.c_str(), bestScore);
    }
    H.SetBody(bestHost);
    H.setCORSHeaders();
    H.SendResponse("200", "OK", conn);
    H.Clean();
  }
  /**
   * return optimal server to start new stream on
   */
  void getIngest(Socket::Connection conn, HTTP::Parser H, const std::string ingest,
                 const std::string fback, bool repeat = true){
    H.Clean();
    H.SetHeader("Content-Type", "text/plain");
    double cpuUse = atoi(ingest.c_str());
    INFO_MSG("Finding ingest point for CPU usage %.2f", cpuUse);
    std::string bestHost = "";
    std::map<std::string, int32_t> tagAdjust;
    if (H.GetVar("tag_adjust") != ""){fillTagAdjust(tagAdjust, H.GetVar("tag_adjust"));}
    if (H.hasHeader("X-Tag-Adjust")){fillTagAdjust(tagAdjust, H.GetHeader("X-Tag-Adjust"));}
    double lat = 0;
    double lon = 0;
    if (H.GetVar("lat") != ""){
      lat = atof(H.GetVar("lat").c_str());
      H.SetVar("lat", "");
    }
    if (H.GetVar("lon") != ""){
      lon = atof(H.GetVar("lon").c_str());
      H.SetVar("lon", "");
    }
    if (H.hasHeader("X-Latitude")){lat = atof(H.GetHeader("X-Latitude").c_str());}
    if (H.hasHeader("X-Longitude")){lon = atof(H.GetHeader("X-Longitude").c_str());}

    uint64_t bestScore = 0;
    for (std::set<hostEntry *>::iterator it = hosts.begin(); it != hosts.end(); it++){
      if ((*it)->state != STATE_ONLINE){continue;}
      uint64_t score = (*it)->details->source("", tagAdjust, cpuUse * 10, lat, lon);
      if (score > bestScore){
        bestHost = (*it)->details->host;
        bestScore = score;
      }
    }
    if (bestScore == 0){
      if (repeat){
        extraServer();
        getIngest(conn, H, ingest, fback, false);
        return;
      }
      if (fback.size()){
        bestHost = fback;
      }else{
        bestHost = fallback;
      }
      lastPromethNode.numFailedIngest++;
      FAIL_MSG("No ingest point found!");
    }else{
      lastPromethNode.numSuccessIngest++;
      INFO_MSG("Winner: %s scores %" PRIu64, bestHost.c_str(), bestScore);
    }
    H.SetBody(bestHost);
    H.setCORSHeaders();
    H.SendResponse("200", "OK", conn);
    H.Clean();
  }

  /**
   * create stream
   */
  void stream(Socket::Connection conn, HTTP::Parser H, std::string proto, std::string streamName, bool repeat){
    H.Clean();
    H.SetHeader("Content-Type", "text/plain");
    // Balance given stream
    std::map<std::string, int32_t> tagAdjust;
    if (H.GetVar("tag_adjust") != ""){
      fillTagAdjust(tagAdjust, H.GetVar("tag_adjust"));
      H.SetVar("tag_adjust", "");
    }
    if (H.hasHeader("X-Tag-Adjust")){fillTagAdjust(tagAdjust, H.GetHeader("X-Tag-Adjust"));}
    H.SetVar("proto", "");
    std::string vars = H.allVars();
    if (streamName == "favicon.ico"){
      H.Clean();
      H.SendResponse("404", "No favicon", conn);
      H.Clean();
      lastPromethNode.numIllegalViewer++;
      return;
    }
    INFO_MSG("Balancing stream %s", streamName.c_str());
    hostEntry *bestHost = 0;
    uint64_t bestScore = 0;
    for (std::set<hostEntry *>::iterator it = hosts.begin(); it != hosts.end(); it++){
      if ((*it)->state != STATE_ACTIVE){continue;}
      uint64_t score = (*it)->details->rate(streamName, tagAdjust);
      if (score > bestScore){
        bestHost = *it;
        bestScore = score;
      }
    }
    if (!bestScore || !bestHost){
      if (repeat){
        extraServer();
        stream(conn, H, proto, streamName, false);
      }else{
        H.Clean();
        H.SetHeader("Content-Type", "text/plain");
        H.setCORSHeaders();
        H.SetBody(fallback);
        lastPromethNode.numFailedViewer++;
        FAIL_MSG("All servers seem to be out of bandwidth!");
      }
    }else{
      INFO_MSG("Winner: %s scores %" PRIu64, bestHost->details->host.c_str(), bestScore);
      bestHost->details->addViewer(streamName, true);
      H.Clean();
      H.SetHeader("Content-Type", "text/plain");
      H.setCORSHeaders();
      H.SetBody(bestHost->details->host);
      lastPromethNode.numSuccessViewer++;
      int tmp = 0;
      if (lastPromethNode.numStreams.count(bestHost->name)){
        tmp = lastPromethNode.numStreams.at(bestHost->name);
      }
      lastPromethNode.numStreams.insert(std::pair<std::string, int>(bestHost->name, tmp + 1));
    }
    if (proto != "" && bestHost && bestScore){
      H.SetHeader("Content-Type", "text/plain");
      H.Clean();
      H.setCORSHeaders();
      H.SetHeader("Location", bestHost->details->getUrl(streamName, proto) + vars);
      H.SetBody(H.GetHeader("Location"));
      H.SendResponse("307", "Redirecting", conn);
      H.Clean();
    }else{
      H.SendResponse("200", "OK", conn);
      H.Clean();
    }
  }// if HTTP request received
  /**
   * add viewer to stream on server
   */
  void addViewer(std::string stream, const std::string addViewer){
    for (std::set<hostEntry *>::iterator it = hosts.begin(); it != hosts.end(); it++){
      if ((*it)->name == addViewer){
        (*it)->details->addViewer(stream, true);
        break;
      }
    }
  }

  /**
   * \returns the config as a string
   */
  std::string configToString(){
    JSON::Value j;
    j[configFallback] = fallback;
    j[configC] = weight_cpu;
    j[configR] = weight_ram;
    j[configBW] = weight_bw;
    j[configWG] = weight_geo;
    j[configWB] = weight_bonus;
    j[configPass] = passHash;
    j[configSPass] = passphrase;
    j[configWhitelist] = whitelist;
    j[configBearer] = bearerTokens;
    j[configUsers] = userAuth;

    // balancing
    j[configMinStandby] = minstandby;
    j[configMaxStandby] = maxstandby;
    j[configCappacityTriggerCPUDec] = cappacityTriggerCPUDec;
    j[configCappacityTriggerBWDec] = cappacitytriggerBWDec;
    j[configCappacityTriggerRAMDEC] = cappacityTriggerRAMDec;
    j[configCappacityTriggerCPU] = cappacityTriggerCPU;
    j[configCappacityTriggerBW] = cappacityTriggerBW;
    j[configCappacityTriggerRAM] = cappacityTriggerRAM;
    j[configHighCappacityTriggerCPU] = highCappacityTriggerCPU;
    j[configHighCappacityTriggerBW] = highCappacityTriggerBW;
    j[configHighCappacityTriggerRAM] = highCappacityTriggerRAM;
    j[configLowCappacityTriggerCPU] = lowCappacityTriggerCPU;
    j[configLowCappacityTriggerBW] = lowCappacityTriggerBW;
    j[configLowcappacityTriggerRAM] = lowCappacityTriggerRAM;
    j[configBalancingInterval] = balancingInterval;
    j[serverMonitorLimitKey] = serverMonitorLimit;
    // serverlist
    std::set<std::string> servers;
    for (std::set<hostEntry *>::iterator it = hosts.begin(); it != hosts.end(); it++){
      if ((*it)->thread != 0){servers.insert((*it)->name);}
    }
    j[configServers] = servers;
    // loadbalancer list
    std::set<std::string> lb;
    lb.insert(myName);
    for (std::set<LoadBalancer *>::iterator it = loadBalancers.begin(); it != loadBalancers.end(); it++){
      lb.insert((*it)->getName());
    }
    j[configLoadbalancer] = lb;
    return j.asString();
  }
  /**
   * save config vars to config file
   * \param resend allows for command to be sent to other load balacners
   */
  void saveFile(bool resend){
    // send command to other load balancers
    if (resend){
      JSON::Value j;
      j[saveKey] = true;
      for (std::set<LoadBalancer *>::iterator it = loadBalancers.begin(); it != loadBalancers.end(); it++){
        (*it)->send(j.asString());
      }
    }
    tthread::lock_guard<tthread::mutex> guard(fileMutex);
    std::ofstream file(fileloc.c_str());

    if (file.is_open()){

      file << configToString().c_str();
      file.flush();
      file.close();
      time(&prevSaveTime);
      INFO_MSG("config saved");
    }else{
      INFO_MSG("save failed");
    }
  }
  /**
   * timer to check if enough time passed since last config change to save to the config file
   */
  void saveTimeCheck(void *){
    if (prevconfigChange < prevSaveTime){
      WARN_MSG("manual save1")
      return;
    }
    time(&now);
    double timeDiff = difftime(now, prevconfigChange);
    while (timeDiff < 60 * saveTimeInterval){
      // check for manual save
      if (prevconfigChange < prevSaveTime){return;}
      // sleep thread for 600 - timeDiff
      sleep(60 * saveTimeInterval - timeDiff);
      time(&now);
      timeDiff = difftime(now, prevconfigChange);
    }
    saveFile();
    saveTimer = 0;
  }
  /**
   * loads the config from a string
   */
  void configFromString(std::string s){
    // change config vars
    JSON::Value j = JSON::fromString(s);
    if(j.isMember(configFallback))
    fallback = j[configFallback].asString();
    if(j.isMember(configC))
    weight_cpu = j[configC].asInt();
    if(j.isMember(configR))
    weight_ram = j[configR].asInt();
    if(j.isMember(configBW))
    weight_bw = j[configBW].asInt();
    if(j.isMember(configWG))
    weight_geo = j[configWG].asInt();
    if(j.isMember(configWB))
    weight_bonus = j[configWB].asInt();
    if(j.isMember(configPass))
    passHash = j[configPass].asString();
    if(j.isMember(configSPass))
    passphrase = j[configSPass].asStringRef();
    if(j.isMember(configBearer))
    bearerTokens = j[configBearer].asStringSet();

    // balancing
    if(j.isMember(configMinStandby))
    minstandby = j[configMinStandby].asInt();
    if(j.isMember(configMaxStandby))
    maxstandby = j[configMaxStandby].asInt();
    if(j.isMember(configCappacityTriggerCPUDec))
    cappacityTriggerCPUDec = j[configCappacityTriggerCPUDec].asDouble(); // percentage om cpu te verminderen
    if(j.isMember(configCappacityTriggerBWDec))
    cappacitytriggerBWDec = j[configCappacityTriggerBWDec].asDouble(); // percentage om bandwidth te verminderen
    if(j.isMember(configCappacityTriggerRAMDEC))
    cappacityTriggerRAMDec = j[configCappacityTriggerRAMDEC].asDouble(); // percentage om ram te verminderen
    if(j.isMember(configCappacityTriggerCPU))
    cappacityTriggerCPU = j[configCappacityTriggerCPU].asDouble(); // max capacity trigger for balancing cpu
    if(j.isMember(configCappacityTriggerBW))
    cappacityTriggerBW = j[configCappacityTriggerBW].asDouble(); // max capacity trigger for balancing bandwidth
    if(j.isMember(configCappacityTriggerRAM))
    cappacityTriggerRAM = j[configCappacityTriggerRAM].asDouble(); // max capacity trigger for balancing ram
    if(j.isMember(configHighCappacityTriggerCPU))
    highCappacityTriggerCPU =
        j[configHighCappacityTriggerCPU].asDouble(); // capacity at which considerd almost full. should be less than cappacityTriggerCPU
    if(j.isMember(configHighCappacityTriggerBW))
    highCappacityTriggerBW =
        j[configHighCappacityTriggerBW].asDouble(); // capacity at which considerd almost full. should be less than cappacityTriggerBW
        if(j.isMember(configHighCappacityTriggerRAM))
    highCappacityTriggerRAM =
        j[configHighCappacityTriggerRAM].asDouble(); // capacity at which considerd almost full. should be less than cappacityTriggerRAM
        if(j.isMember(configLowCappacityTriggerCPU))
    lowCappacityTriggerCPU =
        j[configLowCappacityTriggerCPU].asDouble(); // capacity at which considerd almost full. should be less than cappacityTriggerCPU
        if(j.isMember(configLowCappacityTriggerBW))
    lowCappacityTriggerBW =
        j[configLowCappacityTriggerBW].asDouble(); // capacity at which considerd almost full. should be less than cappacityTriggerBW
        if(j.isMember(configLowcappacityTriggerRAM))
    lowCappacityTriggerRAM =
        j[configLowcappacityTriggerRAM].asDouble(); // capacity at which considerd almost full. should be less than cappacityTriggerRAM
        if(j.isMember(configBalancingInterval))
    balancingInterval = j[configBalancingInterval].asInt();
    if(j.isMember(serverMonitorLimitKey))
    serverMonitorLimit = j[serverMonitorLimitKey].asInt();

    if (highCappacityTriggerCPU > cappacityTriggerCPU)
      highCappacityTriggerCPU = cappacityTriggerCPU;
    if (highCappacityTriggerBW > cappacityTriggerBW) highCappacityTriggerBW = cappacityTriggerBW;
    if (highCappacityTriggerRAM > cappacityTriggerRAM)
      highCappacityTriggerRAM = cappacityTriggerRAM;
    if (lowCappacityTriggerCPU > cappacityTriggerCPU)
      lowCappacityTriggerCPU = highCappacityTriggerCPU;
    if (lowCappacityTriggerBW > cappacityTriggerBW) lowCappacityTriggerBW = highCappacityTriggerBW;
    if (lowCappacityTriggerRAM > cappacityTriggerRAM)
      lowCappacityTriggerRAM = highCappacityTriggerRAM;

    // load whitelist
    if(j.isMember(configWhitelist))
    whitelist = j[configWhitelist].asStringSet();
    if(j.isMember(configUsers))
    userAuth = j[configUsers].asStringPairMap();

    // add new servers
    if(j.isMember(configServers)){
    for (int i = 0; i < j[configServers].size(); i++){
      std::string ret;
      addServer(ret, j[configServers][i], true);
    }}

    // add new load balancers
    if(j.isMember(configLoadbalancer)){
    jsonForEach(j[configLoadbalancer], i){
      if (!(*i).asString().compare(myName)) continue;
      for (std::set<LoadBalancer *>::iterator it = loadBalancers.begin(); it != loadBalancers.end(); it++){
        if ((*it)->getName().compare((*i).asString())){
          new tthread::thread(addLB, (void *)&((*i).asStringRef()));
        }
      }
    }
    }
  }
  /**
   * load config vars from config file
   * \param resend allows for command to be sent sent to other load balancers
   */
  void loadFile(bool resend){
    // send command to other load balancers
    if (resend){
      JSON::Value j;
      j[loadKey] = true;
      for (std::set<LoadBalancer *>::iterator it = loadBalancers.begin(); it != loadBalancers.end(); it++){
        (*it)->send(j.asString());
      }
    }

    tthread::lock_guard<tthread::mutex> guard(fileMutex);
    std::ifstream file(fileloc.c_str());
    std::string data;
    std::string line;
    // read file
    if (file.is_open()){
      while (getline(file, line)){data.append(line);}

      // remove servers
      for (std::set<hostEntry *>::iterator it = hosts.begin(); it != hosts.end(); it++){
        cleanupHost(**it);
      }
      // remove loadbalancers
      std::set<LoadBalancer *>::iterator it = loadBalancers.begin();
      while (loadBalancers.size()){
        (*it)->send("close");
        (*it)->Go_Down = true;
        loadBalancers.erase(it);
        it = loadBalancers.begin();
      }
      configFromString(data);

      file.close();
      INFO_MSG("loaded config");
      checkServerMonitors();

      // send new config to other load balancers
      JSON::Value z;
      z[sendConfigKey] = configToString();
      for (std::set<LoadBalancer *>::iterator it = loadBalancers.begin(); it != loadBalancers.end(); it++){
        (*it)->send(z.asString());
      }

    }else
      WARN_MSG("cant load")
  }
}// namespace Loadbalancer