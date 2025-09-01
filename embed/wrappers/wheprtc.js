mistplayers.wheprtc = {
  name: "WebRTC player (WHEP)",
  mimes: ["whep"],
  priority: MistUtil.object.keys(mistplayers).length + 1,
  isMimeSupported: function (mimetype) {
    return (this.mimes.indexOf(mimetype) == -1 ? false : true);
  },
  isBrowserSupported: function (mimetype,source,MistVideo) {
    
    if (!("RTCPeerConnection" in window) || !("RTCRtpReceiver" in window)) { return false; }

    //check if MistServer is compiled with datachannel support
    if (!("capa" in MistVideo.info) || !("datachannels" in MistVideo.info.capa) || !MistVideo.info.capa.datachannels) {
      return false;
    }
    
    //check for http/https mismatch
    if (location.protocol != MistUtil.http.url.split(source.url).protocol) {
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
    //subtitles over the datachannel are supported
    if (hassubtitles) { tracktypes.push("subtitle"); }

    return tracktypes.length ? tracktypes : false;
  },
  player: function(){}
};
var p = mistplayers.wheprtc.player;
p.prototype = new MistPlayer();
p.prototype.build = function (MistVideo,callback) {
  var main = this;
  
  //this.debugging = true; //enable extra messages to dev console
  
  var video = document.createElement("video");
  this.setSize = function(size){
    video.style.width = size.width+"px";
    video.style.height = size.height+"px";
  };

  function myWHEP() {
    var whep = this;
    this.connection = { connectionState: "new" };
    this.connecting = false; // will contain promise while connecting
    this.control = false;

    let was_connected = false;

    this.onmessage = {}; //listeners for control channel: do not save on control channel to keep the listeners even if the connection was reset

    // Connects using WHEP
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
      this.connection = new RTCPeerConnection();

      this.connection.onconnectionstatechange = function(e){
        if (MistVideo.destroyed) { return; } //the player doesn't exist any more

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

      // Set up the datachannel for control messages
      this.control = new MistUtil.shared.ControlChannel(this.connection.createDataChannel("MistControl"),MistVideo,this.onmessage);
      this.control.addListener("channel_timeout").then(function(){
        MistVideo.log("WebRTC: control channel timeout - try next combo","error");
        MistVideo.nextCombo("control channel timeout");
      });
      this.control.addListener("channel_error").then(function(){
        if (whep.control.was_connected) {
          MistVideo.log("Attempting to reconnect control channel");
          this.control = new MistUtil.shared.ControlChannel(whep.connection.createDataChannel("MistControl"),MistVideo,this.onmessage);
        }
      });

      //live passthrough of the debugging flag
      Object.defineProperty(this.control,"debugging",{
        get: function(){
          return main.debugging; 
        }
      });

      // Add metadata channel
      this.meta = this.connection.createDataChannel("*",{"protocol":"JSON"});

      var offer, answer;
      // Create offer
      this.connecting = this.connection.createOffer({offerToReceiveVideo: true, offerToReceiveAudio: true}).then(function(o){
        offer = o;
        if (main.debugging) console.log("Offer:",MistUtil.format.offer2human(offer.sdp));
        return whep.connection.setLocalDescription(offer);
      }).then(function(){
        // Do WHEP request
        return fetch(url, {method:'POST', headers: {'Content-Type': 'application/sdp'}, body: offer.sdp});
      }).then(function(response){
        return response.text();
      }).then(function(a){
        answer = a;
        if (main.debugging) console.log('Answer:',MistUtil.format.offer2human(answer));
        // Act on response
        return whep.connection.setRemoteDescription({type: 'answer', sdp: answer});
      }).then(function(){
        MistVideo.log("Connected to "+url);
        whep.connecting = false;
        was_connected = true;
        //MistVideo.container.removeAttribute("data-loading");
        return answer;
      }).catch(function(e){
        whep.connecting = false;
        MistVideo.showError("WHEP connection failed: "+e);
      });

      return this.connecting;
    }

    this.close = function(){
      return new Promise(function(resolve,reject){
        if (!whep.connection || (whep.connection.connectionState == "closed")) { resolve(); }
        whep.connection.close();
        var func = function() {
          if (!whep.connection || (whep.connection.connectionState == "closed")) { resolve(); }
          else {
            console.warn("not yet",whep.connection.connectionState);
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
  this.WHEP = new myWHEP();

  this.api = new MistUtil.shared.ControlChannelAPI(main.WHEP,MistVideo,video);
    
  callback(video);
};
