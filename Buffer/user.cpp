namespace Buffer{
  /// Holds connected users.
  /// Keeps track of what buffer users are using and the connection status.
  class user{
    public:
      tthread::thread * Thread; ///< Holds the thread dealing with this user.
      DTSC::Ring * myRing; ///< Ring of the buffer for this user.
      int MyNum; ///< User ID of this user.
      std::string MyStr; ///< User ID of this user as a string.
      std::string inbuffer; ///< Used to buffer input data.
      int currsend; ///< Current amount of bytes sent.
      Stats lastStats; ///< Holds last known stats for this connection.
      Stats tmpStats; ///< Holds temporary stats for this connection.
      unsigned int curr_up; ///< Holds the current estimated transfer speed up.
      unsigned int curr_down; ///< Holds the current estimated transfer speed down.
      bool gotproperaudio; ///< Whether the user received proper audio yet.
      void * lastpointer; ///< Pointer to data part of current buffer.
      static int UserCount; ///< Global user counter.
      Socket::Connection S; ///< Connection to user
      /// Creates a new user from a newly connected socket.
      /// Also prints "User connected" text to stdout.
      user(Socket::Connection fd){
        S = fd;
        MyNum = UserCount++;
        std::stringstream st;
        st << MyNum;
        MyStr = st.str();
        curr_up = 0;
        curr_down = 0;
        currsend = 0;
        myRing = 0;
        Thread = 0;
        std::cout << "User " << MyNum << " connected" << std::endl;
      }//constructor
      /// Drops held DTSC::Ring class, if one is held.
      ~user(){
        Strm->dropRing(myRing);
      }//destructor
      /// Disconnects the current user. Doesn't do anything if already disconnected.
      /// Prints "Disconnected user" to stdout if disconnect took place.
      void Disconnect(std::string reason) {
        if (S.connected()){S.close();}
        if (Thread != 0){
          if (Thread->joinable()){Thread->join();}
          Thread = 0;
        }
        tthread::lock_guard<tthread::mutex> lock(stats_mutex);
        Storage["curr"].removeMember(MyStr);
        Storage["log"][MyStr]["connector"] = lastStats.connector;
        Storage["log"][MyStr]["up"] = lastStats.up;
        Storage["log"][MyStr]["down"] = lastStats.down;
        Storage["log"][MyStr]["conntime"] = lastStats.conntime;
        Storage["log"][MyStr]["host"] = lastStats.host;
        Storage["log"][MyStr]["start"] = (unsigned int)time(0) - lastStats.conntime;
        std::cout << "Disconnected user " << MyStr << ": " << reason << ". " << lastStats.connector << " transferred " << lastStats.up << " up and " << lastStats.down << " down in " << lastStats.conntime << " seconds to " << lastStats.host << std::endl;
      }//Disconnect
      /// Tries to send the current buffer, returns true if success, false otherwise.
      /// Has a side effect of dropping the connection if send will never complete.
      bool doSend(const char * ptr, int len){
        int r = S.iwrite(ptr+currsend, len-currsend);
        if (r <= 0){
          if (errno == EWOULDBLOCK){return false;}
          Disconnect(S.getError());
          return false;
        }
        currsend += r;
        return (currsend == len);
      }//doSend
      /// Try to send data to this user. Disconnects if any problems occur.
      void Send(){
        if (!myRing){return;}//no ring!
        if (!S.connected()){return;}//cancel if not connected
        if (myRing->waiting){
          tthread::lock_guard<tthread::mutex> guard(transfer_mutex);
          moreData.wait(transfer_mutex);
          return;
        }//still waiting for next buffer?

        if (myRing->starved){
          //if corrupt data, warn and get new DTSC::Ring
          std::cout << "Warning: User was send corrupt video data and send to the next keyframe!" << std::endl;
          Strm->dropRing(myRing);
          myRing = Strm->getRing();
          return;
        }

        //try to complete a send
        if (doSend(Strm->outPacket(myRing->b).c_str(), Strm->outPacket(myRing->b).length())){
          //switch to next buffer
          currsend = 0;
          if (myRing->b <= 0){myRing->waiting = true; return;}//no next buffer? go in waiting mode.
          myRing->b--;
        }//completed a send
      }//send
  };
  int user::UserCount = 0;
}
