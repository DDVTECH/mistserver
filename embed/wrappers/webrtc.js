mistplayers.webrtc = {
  name: "WebRTC player (WS)",
  mimes: ["webrtc"],
  priority: MistUtil.object.keys(mistplayers).length + 1,
  isMimeSupported: function (mimetype) {
    return (this.mimes.indexOf(mimetype) == -1 ? false : true);
  },
  isBrowserSupported: function (mimetype,source,MistVideo) {
    
    if ((!("WebSocket" in window)) || (!("RTCPeerConnection" in window) || (!("RTCRtpReceiver" in window)))) { return false; }
    
    //check for http/https mismatch
    if (location.protocol.replace(/^http/,"ws") != MistUtil.http.url.split(source.url.replace(/^http/,"ws")).protocol) {
      MistVideo.log("HTTP/HTTPS mismatch for this source");
      return false;
    }


    //check if both audio and video have at least one playable track
    //gather track types and codec strings
    var playabletracks = {};
    var hassubtitles = false;
    for (var i in MistVideo.info.meta.tracks) {
      if (MistVideo.info.meta.tracks[i].type == "meta") {
        if (MistVideo.info.meta.tracks[i].codec == "subtitle") { hassubtitles = true; }
        continue;
      }
      if (!(MistVideo.info.meta.tracks[i].type in playabletracks)) {
        playabletracks[MistVideo.info.meta.tracks[i].type] = {};
      }
      playabletracks[MistVideo.info.meta.tracks[i].type][MistVideo.info.meta.tracks[i].codec] = 1;
    }

    var tracktypes = [];
    for (var type in playabletracks) {
      var playable = false;

      for (var codec in playabletracks[type]) {
        var supported = RTCRtpReceiver.getCapabilities(type).codecs;
        for (var i in supported) {
          if (supported[i].mimeType.toLowerCase() == (type+"/"+codec).toLowerCase()) {
            playable = true;
            break;
          }
        }
      }
      if (playable) {
        tracktypes.push(type);
      }
    }

    //either subtitles over the datachannel or over a websocket are supported
    if (hassubtitles) { tracktypes.push("subtitle"); }

    return tracktypes.length ? tracktypes : false;
    
    //return true;
  },
  player: function(){}
};
var p = mistplayers.webrtc.player;
p.prototype = new MistPlayer();
p.prototype.build = function (MistVideo,callback) {

  var main = this;

  //this.debugging = true; //enable extra messages to dev console

  var video = document.createElement("video");
  this.setSize = function(size){
    video.style.width = size.width+"px";
    video.style.height = size.height+"px";
  };

  function myRTC() {
    var webrtc = this;
    this.connection = false;
    this.connecting = false; // will contain promise while connecting
    this.control = false;

    let was_connected = false;

    this.onmessage = {}; //listeners for control channel: do not save on control channel to keep the listeners even if the connection was reset

    // Connects using websocket
    this.connect = function(){
      if (this.connecting) {
        //already connecting
        return this.connecting;
      }
      if (this.connection.connectionState == "connected") {
        //already connected
        return new Promise(function(resolve,reject){ resolve(); });
      }

      MistVideo.container.setAttribute("data-loading","");

      url = MistVideo.source.url;
      MistVideo.log('Connecting to ' + url);
      
      this.control = new MistUtil.shared.ControlChannel(new WebSocket(url),MistVideo,this.onmessage);
      this.control.addListener("channel_timeout").then(function(){
        MistVideo.log("WebRTC: control channel timeout - try next combo","error");
        MistVideo.nextCombo("control channel timeout");
      });
      this.control.addListener("channel_error").then(function(){
        if (webrtc.control.was_connected) {
          MistVideo.log("Attempting to reconnect control channel");
          this.control = new MistUtil.shared.ControlChannel(new WebSocket(url),MistVideo,this.onmessage);
        }
        else {
          MistVideo.log("WebRTC: control channel error - try next combo","error");
          MistVideo.nextCombo("control channel error");
        }
      });
      //live passthrough of the debugging flag
      Object.defineProperty(this.control,"debugging",{
        get: function(){
          return main.debugging; 
        }
      });

      this.step = 0;

      if (this.connection) this.connection.close();
      this.connection = new RTCPeerConnection();

      this.connection.onconnectionstatechange = function(e){
        if (MistVideo.destroyed) new Promise(function(resolve,reject){ reject(); }); //the player doesn't exist any more

        switch (this.connectionState) {
          case "failed": {
            if (!was_connected) {
              // WebRTC will never work (firewall maybe?)
              MistVideo.log("The WebRTC UDP connection failed, trying next combo.","error");
              MistVideo.nextCombo();
            }
            else {
              // The webRTC connection was closed - probably end of stream
              MistVideo.log("The WebRTC UDP connection was closed");
            }
            break;
          }
          case "connected":
          case "disconnected":
          case "closed":
          case "new":
          case "connecting":
          default: {
            MistVideo.log("The WebRTC UDP connection state changed to "+this.connectionState);
            break;
          }
        }
      };
      this.connection.oniceconnectionstatechange = function(e){
        if (MistVideo.destroyed) { return; } //the player doesn't exist any more
        switch (this.iceConnectionState) {
          case "failed": {
            MistVideo.showError("The WebRTC ICE connection "+this.iceConnectionState);
            break;
          }
          case "disconnected":
          case "closed": 
          case "new":
          case "checking":
          case "connected":
          case "completed":
          default: {
            MistVideo.log("The WebRTC ICE connection state changed to "+this.iceConnectionState);
            break;
          }
        }
      };
      this.connection.addEventListener("signalingstatechange",function(){
        MistVideo.log("The WebRTC signaling state changed to "+this.signalingState);
      });

      this.connection.addTransceiver("audio", { direction: "recvonly" });
      this.connection.addTransceiver("video", { direction: "recvonly" });

      // Handle receiving a media track from the other side
      this.connection.ontrack = function(e){
        if (MistVideo.destroyed) { return; }
        if (main.debugging) console.log("Received media track",e.track);
        video.srcObject = e.streams[0];
      }

      // Add metadata channel
      this.meta = this.connection.createDataChannel("*",{"protocol":"JSON"});

      var offer, answer;
      // Create offer
      this.connecting = this.connection.createOffer({offerToReceiveVideo: true, offerToReceiveAudio: true}).then(function(o){
        webrtc.step++; //1
        offer = o;
        if (main.debugging) console.log("Offer:",MistUtil.format.offer2human(offer.sdp),"State:",webrtc.connection.connectionState);
        return webrtc.connection.setLocalDescription(offer);
      }).then(function(){
        webrtc.step++; //2
        // Do WS request
        webrtc.control.send({type: "offer_sdp", offer_sdp: offer.sdp});
        return webrtc.control.addListener("on_answer_sdp");
      }).then(function(a){
        webrtc.step++; //3
        answer = a.answer_sdp;
        if (main.debugging) console.log("Answer:",MistUtil.format.offer2human(answer));
        // Act on response
        return webrtc.connection.setRemoteDescription({type: "answer", sdp: answer});
      }).then(function(){
        webrtc.step++; //4
        MistVideo.log("Connected to "+url);
        webrtc.connecting = false;
        was_connected = true;
        //MistVideo.container.removeAttribute("data-loading");
        return answer;
      }).catch(function(e){
        webrtc.connecting = false;
        MistVideo.showError("WebRTC connection failed: "+e);
      });

      return this.connecting;
    }

    this.close = function(){
      return new Promise(function(resolve,reject){
        if (!webrtc.connection || (webrtc.connection.connectionState == "closed")) { resolve(); }
        webrtc.connection.close();
        var func = function() {
          if (!webrtc.connection || (webrtc.connection.connectionState == "closed")) { resolve(); }
          else {
            console.warn("not yet",webrtc.connection.connectionState);
            MistVideo.timers.start(function(){
              func();
            },100);
          }
        }
        func();
      });
    };

    this.connect();

  }
  this.webrtc = new myRTC();

  this.api = new MistUtil.shared.ControlChannelAPI(main.webrtc,MistVideo,video);

  callback(video);
  
};
