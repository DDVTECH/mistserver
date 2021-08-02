mistplayers.webrtc = {
  name: "WebRTC player",
  mimes: ["webrtc"],
  priority: MistUtil.object.keys(mistplayers).length + 1,
  isMimeSupported: function (mimetype) {
    return (this.mimes.indexOf(mimetype) == -1 ? false : true);
  },
  isBrowserSupported: function (mimetype,source,MistVideo) {
    
    if ((!("WebSocket" in window)) || (!("RTCPeerConnection" in window))) { return false; }
    
    //check for http/https mismatch
    if (location.protocol.replace(/^http/,"ws") != MistUtil.http.url.split(source.url.replace(/^http/,"ws")).protocol) {
      MistVideo.log("HTTP/HTTPS mismatch for this source");
      return false;
    }
    
    return true;
  },
  player: function(){}
};
var p = mistplayers.webrtc.player;
p.prototype = new MistPlayer();
p.prototype.build = function (MistVideo,callback) {
  var me = this;
  
  if ((typeof WebRTCBrowserEqualizerLoaded == "undefined") || (!WebRTCBrowserEqualizerLoaded)) {
    //load it
    var scripttag = document.createElement("script");
    scripttag.src = MistVideo.urlappend(MistVideo.options.host+"/webrtc.js");
    MistVideo.log("Retrieving webRTC browser equalizer code from "+scripttag.src);
    document.head.appendChild(scripttag);
    scripttag.onerror = function(){
      MistVideo.showError("Failed to load webrtc browser equalizer",{nextCombo:5});
    }
    scripttag.onload = function(){
      me.build(MistVideo,callback);
    }
    return;
  }
  
  var video = document.createElement("video");
  video.setAttribute("playsinline",""); //iphones. effin' iphones.
  
  //apply options
  var attrs = ["autoplay","loop","poster"];
  for (var i in attrs) {
    var attr = attrs[i];
    if (MistVideo.options[attr]) {
      video.setAttribute(attr,(MistVideo.options[attr] === true ? "" : MistVideo.options[attr]));
    }
  }
  if (MistVideo.options.muted) {
    video.muted = true; //don't use attribute because of Chrome bug
  }
  if (MistVideo.info.type == "live") {
    video.loop = false;
  }
  if (MistVideo.options.controls == "stock") {
    video.setAttribute("controls","");
  }
  video.setAttribute("crossorigin","anonymous");
  this.setSize = function(size){
    video.style.width = size.width+"px";
    video.style.height = size.height+"px";
  };
  MistUtil.event.addListener(video,"loadeddata",correctSubtitleSync);
  MistUtil.event.addListener(video,"seeked",correctSubtitleSync);
  
  if (!MistVideo.options.autoplay) {
    MistUtil.event.addListener(video,"canplay",function(){
      var onplay = MistUtil.event.addListener(video,"play",function(){
        MistVideo.log("Pausing because autoplay is disabled");
        var onpause = MistUtil.event.addListener(video,"pause",function(){
          MistVideo.options.autoplay = false;
          MistUtil.event.removeListener(onpause);
        });
        me.api.pause();
        MistUtil.event.removeListener(onplay);
      });
    });
  }
  
  var seekoffset = 0;
  var hasended = false;
  var currenttracks = [];
  this.listeners = {
    on_connected: function() {
      seekoffset = 0;
      hasended = false;
      this.webrtc.play();
      MistUtil.event.send("webrtc_connected",null,video);
    },
    on_disconnected: function() {
      MistUtil.event.send("webrtc_disconnected",null,video);
      MistVideo.log("Websocket sent on_disconnect");
      /*
        If a VoD file ends, we receive an on_stop, but no on_disconnect
        If a live stream ends, we receive an on_disconnect, but no on_stop
        If MistOutWebRTC crashes, we receive an on_stop and then an on_disconnect
      */
      if (!hasended) {
        //MistVideo.showError("Connection to media server ended unexpectedly.");
        video.pause();
      }
      
      //this.webrtc.signaling.ws.close();
    },
    on_answer_sdp: function (ev) {
      if (!ev.result) {
        MistVideo.showError("Failed to open stream.");
        this.on_disconnected();
        return;
      }
      MistVideo.log("SDP answer received");
    },
    on_time: function(ev) {
      //timeupdate
      var oldoffset = seekoffset;
      seekoffset = ev.current*1e-3 - video.currentTime;
      if (Math.abs(oldoffset - seekoffset) > 1) { correctSubtitleSync(); }
      
      if ((!("paused" in ev) || (!ev.paused)) && (video.paused)) {
        video.play();
      }
      
      var d = (ev.end == 0 ? Infinity : ev.end*1e-3);
      if (d != duration) {
        duration = d;
        MistUtil.event.send("durationchange",d,video);
      }
      
      if ((ev.tracks) && (currenttracks != ev.tracks)) {
        var tracks = MistVideo.info ? MistUtil.tracks.parse(MistVideo.info.meta.tracks) : [];
        for (var i in ev.tracks) {
          if (currenttracks.indexOf(ev.tracks[i]) < 0) {
            //find track type
            var type;
            for (var j in tracks) {
              if (ev.tracks[i] in tracks[j]) {
                type = j;
                break;
              }
            }
            if (!type) {
              //track type not found, this should not happen
              continue;
            }
            
            //create an event to pass this to the skin
            MistUtil.event.send("playerUpdate_trackChanged",{
              type: type,
              trackid: ev.tracks[i]
            },MistVideo.video);
          }
        }

        currenttracks = ev.tracks;
      }
      
      if (MistVideo.reporting && ev.tracks) {
        MistVideo.reporting.stats.d.tracks = ev.tracks.join(",");
      }
    },
    on_seek: function(e){
      var thisPlayer = this;
      MistUtil.event.send("seeked",seekoffset,video);
      
      //set playback rate to auto if seek was to live point
      if (e.live_point) {
        thisPlayer.webrtc.playbackrate("auto");
      }
      
      if ("seekPromise" in this.webrtc.signaling){
        video.play().then(function(){
          if ("seekPromise" in thisPlayer.webrtc.signaling) {
            thisPlayer.webrtc.signaling.seekPromise.resolve("Play promise resolved");
          }
        }).catch(function(){
          if ("seekPromise" in thisPlayer.webrtc.signaling) {
            thisPlayer.webrtc.signaling.seekPromise.reject("Play promise rejected");
          }
        });
      }
      else { video.play(); }
    },
    on_speed: function(e){
      this.webrtc.play_rate = e.play_rate_curr;
      MistUtil.event.send("ratechange",e,video);
    },
    on_stop: function(){
      MistVideo.log("Websocket sent on_stop");
      video.pause();
      MistUtil.event.send("ended",null,video);
      hasended = true;
    }
  };
  
  
  function WebRTCPlayer(){
    this.peerConn = null;
    this.localOffer = null;
    this.isConnected = false;
    this.isConnecting = false;
    this.play_rate = "auto";
    var thisWebRTCPlayer = this;
    
    this.on_event = function(ev) {
      switch (ev.type) {
        case "on_connected": {
          thisWebRTCPlayer.isConnected = true;
          thisWebRTCPlayer.isConnecting = false;
          break;
        }
        case "on_answer_sdp": {
          thisWebRTCPlayer.peerConn
          .setRemoteDescription({ type: "answer", sdp: ev.answer_sdp  })
          .then(function(){}, function(err) {
            console.error(err);
          });
          break;
        }
        case "on_disconnected": {
          thisWebRTCPlayer.isConnected = false;
          break;
        }
      }
      if (ev.type in me.listeners) {
        return me.listeners[ev.type].call(me,ev);
      }
      MistVideo.log("Unhandled WebRTC event "+ev.type+": "+JSON.stringify(ev));
      return false;
    };
    
    this.connect = function(callback){
      thisWebRTCPlayer.isConnecting = true;
      MistVideo.container.setAttribute("data-loading","connecting"); //show loading icon while setting up the connection
      
      //chrome on android has a bug where H264 is not available immediately after the tab is opened: https://bugs.chromium.org/p/webrtc/issues/detail?id=11620
      //this workaround tries 5x with 100ms intervals before continuing
      function checkH264(n){
        var p = new Promise(function(resolve,reject){
          function promise_body(n){
            try {
              var r = RTCRtpReceiver.getCapabilities("video");
              for (var i = 0; i < r.codecs.length; i++) {
                if (r.codecs[i].mimeType == "video/H264") {
                  resolve("H264 found :)");
                  return;
                }
              }
              if (n > 0) {
                setTimeout(function(){
                  promise_body(n-1);
                },100);
              }
              else {
                reject("H264 not found :(");
              }
            } catch (e) { resolve("Checker unavailable"); }
          }
          promise_body(n);
        });
        
        return p;
      };
      checkH264(5).catch(function(){
        MistVideo.log("Beware: this device does not seem to be able to play H264.");
      }).finally(function(){
        thisWebRTCPlayer.signaling = new WebRTCSignaling(thisWebRTCPlayer.on_event);
        var opts = {};
        if (MistVideo.options.RTCIceServers) {
          opts.iceServers = MistVideo.options.RTCIceServers;
        }
        else if (MistVideo.source.RTCIceServers) {
          opts.iceServers = MistVideo.source.RTCIceServers;
        }
        thisWebRTCPlayer.peerConn = new RTCPeerConnection(opts);
        thisWebRTCPlayer.peerConn.ontrack = function(ev) {
          video.srcObject = ev.streams[0];
          if (callback) { callback(); }
        };
        thisWebRTCPlayer.peerConn.onconnectionstatechange = function(e){
          if (MistVideo.destroyed) { return; } //the player doesn't exist any more
          switch (this.connectionState) {
            case "failed": {
              //WebRTC will never work (firewall maybe?)
              MistVideo.log("UDP connection failed, trying next combo.","error");
              MistVideo.nextCombo();
              break;
            }
            case "connected": {
              MistVideo.container.removeAttribute("data-loading");
            }
            case "disconnected":
            case "closed":
            case "new":
            case "connecting":
            default: {
              MistVideo.log("WebRTC connection state changed to "+this.connectionState);
              break;
            }
          }
        };
        thisWebRTCPlayer.peerConn.oniceconnectionstatechange = function(e){
          if (MistVideo.destroyed) { return; } //the player doesn't exist any more
          switch (this.iceConnectionState) {
            case "failed": {
              MistVideo.showError("ICE connection "+this.iceConnectionState);
              break;
            }
            case "disconnected":
            case "closed": 
            case "new":
            case "checking":
            case "connected":
            case "completed":
            default: {
              MistVideo.log("WebRTC ICE connection state changed to "+this.iceConnectionState);
              break;
            }
          }
        };
      });
    };
    
    this.play = function(){
      if (!this.isConnected) {
        throw "Not connected, cannot play";
      }
      
      this.peerConn
      .createOffer({
        offerToReceiveAudio: true,
        offerToReceiveVideo: true,
      })
      .then(function(offer){
        thisWebRTCPlayer.localOffer = offer;
        thisWebRTCPlayer.peerConn
        .setLocalDescription(offer)
        .then(function(){
          thisWebRTCPlayer.signaling.sendOfferSDP(thisWebRTCPlayer.localOffer.sdp);
        }, function(err){console.error(err)});
      }, function(err){ throw err; });
    };
    
    this.stop = function(){
      if (!this.isConnected) { throw "Not connected, cannot stop." }
      this.signaling.send({type: "stop"});
    };
    this.seek = function(seekTime){
      var p = new Promise(function(resolve,reject){
        if (!thisWebRTCPlayer.isConnected || !thisWebRTCPlayer.signaling) {
          if (thisWebRTCPlayer.isConnecting) {
            
            var listener = MistUtil.event.addListener(MistVideo.video,"loadstart",function(){
              thisWebRTCPlayer.seek(seekTime);
              MistUtil.event.removeListener(listener);
            });
            return reject("Not connected yet, will seek once connected");
          }
          else {
            return reject("Failed seek: not connected");
          }
        }
        thisWebRTCPlayer.signaling.send({type: "seek", "seek_time": (seekTime == "live" ? "live" : seekTime*1e3)});
        if ("seekPromise" in thisWebRTCPlayer.signaling) {
          thisWebRTCPlayer.signaling.seekPromise.reject("Doing new seek");
        }
        
        thisWebRTCPlayer.signaling.seekPromise = {
          resolve: function(msg){
            resolve("seeked");
            delete thisWebRTCPlayer.signaling.seekPromise;
          },
          reject: function(msg) {
            reject("Failed to seek: "+msg);
            delete thisWebRTCPlayer.signaling.seekPromise;
          }
        };
      });
      return p;
    };
    this.pause = function(){
      if (!this.isConnected) { throw "Not connected, cannot pause." }
      this.signaling.send({type: "hold"});
    };
    this.setTrack = function(obj){
      if (!this.isConnected) { throw "Not connected, cannot set track." }
      obj.type = "tracks";
      this.signaling.send(obj);
    };
    this.playbackrate = function(value) {
      if (typeof value == "undefined") {
        return (me.webrtc.play_rate == "auto" ? 1 : me.webrtc.play_rate);
      }
      
      if (!this.isConnected) { throw "Not connected, cannot change playback rate." }
      
      this.signaling.send({
        type: "set_speed",
        play_rate: value
      });
      
    };
    this.getStats = function(callback){
      this.peerConn.getStats().then(function(d){
        var output = {};
        var entries = Array.from(d.entries());
        for (var i in entries) {
          var value = entries[i];
          if (value[1].type == "inbound-rtp") {
            output[value[0]] = value[1];
          }
        }
        callback(output);
      });
    };
    //input only
    /*
    this.sendVideoBitrate = function(bitrate) {
      this.send({type: "video_bitrate", video_bitrate: bitrate});
    };
    */
    
    this.connect();
  }
  function WebRTCSignaling(onEvent){
    this.ws = null;
    
    this.ws = new WebSocket(MistVideo.source.url.replace(/^http/,"ws"));
    
    var ignoreopen = false;
    
    this.ws.onopen = function() {
      onEvent({type: "on_connected"});
    };

    this.ws.timeOut = MistVideo.timers.start(function(){
      if (MistVideo.player.webrtc.signaling.ws.readyState == 0) {
        MistVideo.log("WebRTC: socket timeout - try next combo");
        MistVideo.nextCombo();
      }
    },5e3);

    
    this.ws.onmessage = function(e) {
      try {
        var cmd = JSON.parse(e.data);
        onEvent(cmd);
      }
      catch (err) {
        console.error("Failed to parse a response from MistServer",err,e.data);
      }
    };
    
    /* See http://tools.ietf.org/html/rfc6455#section-7.4.1 */
    this.ws.onclose = function(ev) {
      switch (ev.code) {
        case 1006: {
          //MistVideo.showError("WebRTC websocket closed unexpectedly");
        }
        default: {
          onEvent({type: "on_disconnected", code: ev.code});
          break;
        }
      }
    }
    
    this.sendOfferSDP = function(sdp) {
      this.send({type: "offer_sdp", offer_sdp: sdp});
    };
    this.send = function(cmd) {
      if (!this.ws) {
        throw "Not initialized, cannot send "+JSON.stringify(cmd);
      }
      this.ws.send(JSON.stringify(cmd));
    }
  };
  this.webrtc = new WebRTCPlayer();
  
  this.api = {};
  
  //override video duration
  var duration;
  Object.defineProperty(this.api,"duration",{
    get: function(){ return duration; }
  });
  
  //override seeking
  Object.defineProperty(this.api,"currentTime",{
    get: function(){
      return seekoffset + video.currentTime;
    },
    set: function(value){
      seekoffset = value - video.currentTime;
      video.pause();
      var promise = me.webrtc.seek(value);
      MistUtil.event.send("seeking",value,video);
      if (promise) {
        promise.catch(function(e){
          //do nothing
          //keep this code because not handling this shows an error message in the console:
          //  (Uncaught (in promise) Failed seek: not connected)
        });
      }
    }
  });
  
  //override playbackrate
  Object.defineProperty(this.api,"playbackRate",{
    get: function(){
      return me.webrtc.playbackrate();
    },
    set: function(value){
      return me.webrtc.playbackrate(value);
      //TODO send playbackrate changed event?
    }
  });
  
  //redirect properties
  //using a function to make sure the "item" is in the correct scope
  function reroute(item) {
    Object.defineProperty(me.api,item,{
      get: function(){ return video[item]; },
      set: function(value){
        return video[item] = value;
      }
    });
  }
  var list = [
    "volume"
    ,"muted"
    ,"loop"
    ,"paused",
    ,"error"
    ,"textTracks"
    ,"webkitDroppedFrameCount"
    ,"webkitDecodedFrameCount"
  ];
  for (var i in list) {
    reroute(list[i]);
  }
  
  //redirect methods
  function redirect(item) {
    if (item in video) {
      me.api[item] = function(){
        return video[item].call(video,arguments);
      };
    }
  }
  var list = ["load","getVideoPlaybackQuality"];
  for (var i in list) {
    redirect(list[i]);
  }
  
  //redirect play
  me.api.play = function(){
    var seekto;
    if (me.api.currentTime) {
      seekto = me.api.currentTime;
    }
    if ((MistVideo.info) && (MistVideo.info.type == "live")) { 
      seekto = "live";
    }
    if (seekto) {
      var p = new Promise(function(resolve,reject){
        if ((!me.webrtc.isConnected) && (me.webrtc.peerConn.iceConnectionState != "completed")) {
          if (!me.webrtc.isConnecting) {
            MistVideo.log("Received call to play while not connected, connecting "+me.webrtc.peerConn.iceConnectionState);
            me.webrtc.connect(function(){
              me.webrtc.seek(seekto).then(function(msg){
                resolve("played "+msg);
              }).catch(function(msg){
                reject(msg);
              });
            });
          }
          else {
            reject("Still connecting");
          }
        }
        else {
          me.webrtc.seek(seekto).then(function(msg){
            resolve("played "+msg);
          }).catch(function(msg){
            reject(msg);
          });
        }
      });
      
      return p;
    }
    else {
      return video.play();
    }
  };
  
  me.api.getStats = function(){
    if (me.webrtc && me.webrtc.isConnected) {
      return new Promise(function(resolve,reject) {
        me.webrtc.peerConn.getStats().then((a) => {
          var r = {
            audio: null,
            video: null
          };
          for (let dictionary of a.values()) {
            if (dictionary.type == "track") {
              //average jitter buffer in seconds
              r[dictionary.kind] = dictionary;
            }
          }
          resolve(r);
        })
      });
    }
  };
  me.api.getLatency = function() {
    var p = MistVideo.player.api.getStats();
    if (p) {
      return new Promise(function(resolve,reject){
        p.then(function(first){
          setTimeout(function(){
            var p = me.api.getStats();
            if (!p) { reject(); return; }
            p.then(function(last){
              var r = {};
              for (var i in first) {
                r[i] = first[i] && last[i] ? (last[i].jitterBufferDelay - first[i].jitterBufferDelay) / (last[i].jitterBufferEmittedCount - first[i].jitterBufferEmittedCount) : null;
              }
              resolve(r);
            },reject);
          },1e3);
        },reject);
      });
    }
  }
  
  //redirect pause
  me.api.pause = function(){
    video.pause();
    try {
      me.webrtc.pause();
    }
    catch (e) {}
    MistUtil.event.send("paused",null,video);
  };
  
  me.api.setTracks = function(obj){
    if (me.webrtc.isConnected) {
      me.webrtc.setTrack(obj);
    }
    else {
      var f = function(){
        me.webrtc.setTrack(obj);
        MistUtil.event.removeListener({type: "webrtc_connected", callback: f, element: video});
      };
      MistUtil.event.addListener(video,"webrtc_connected",f);
    }
  };
  function correctSubtitleSync() {
    if (!me.api.textTracks[0]) { return; }
    var currentoffset = me.api.textTracks[0].currentOffset || 0;
    if (Math.abs(seekoffset - currentoffset) < 1) { return; } //don't bother if the change is small
    var newCues = [];
    for (var i = me.api.textTracks[0].cues.length-1; i >= 0; i--) {
      var cue = me.api.textTracks[0].cues[i];
      me.api.textTracks[0].removeCue(cue);
      if (!("orig" in cue)) {
        cue.orig = {start:cue.startTime,end:cue.endTime};
      }
      cue.startTime = cue.orig.start - seekoffset;
      cue.endTime = cue.orig.end - seekoffset;
      newCues.push(cue);
    }
    for (var i in newCues) {
      me.api.textTracks[0].addCue(newCues[i]);
    }
    me.api.textTracks[0].currentOffset = seekoffset;
  }
  me.api.setSubtitle = function(trackmeta) {
    //remove previous subtitles
    var tracks = video.getElementsByTagName("track");
    for (var i = tracks.length - 1; i >= 0; i--) {
      video.removeChild(tracks[i]);
    }
    if (trackmeta) { //if the chosen track exists
      //add the new one
      var track = document.createElement("track");
      video.appendChild(track);
      track.kind = "subtitles";
      track.label = trackmeta.label;
      track.srclang = trackmeta.lang;
      track.src = trackmeta.src;
      track.setAttribute("default","");
      
      //correct timesync
      track.onload = correctSubtitleSync;
    }
  };
  
  //loop
  MistUtil.event.addListener(video,"ended",function(){
    if (me.api.loop) {
      if (MistVideo.state == "Stream is online") {
        me.webrtc.connect();
      }
    }
  });
  
  if ("decodingIssues" in MistVideo.skin.blueprints) {
    //get additional dev stats
    var vars = ["nackCount","pliCount","packetsLost","packetsReceived","bytesReceived"];
    for (var j in vars) {
      me.api[vars[j]] = 0;
    }
    var f = function() {
      MistVideo.timers.start(function(){
        me.webrtc.getStats(function(d){
          for (var i in d) {
            for (var j in vars) {
              if (vars[j] in d[i]) {
                me.api[vars[j]] = d[i][vars[j]];
              }
            }
            break;
          }
        });
        f();
      },1e3);
    };
    f();
  }

  me.api.ABR_resize = function(size){
    MistVideo.log("Requesting the video track with the resolution that best matches the player size");
    me.api.setTracks({video:"~"+[size.width,size.height].join("x")});
  };
  
  me.api.unload = function(){
    try {
      me.webrtc.stop();
      me.webrtc.signaling.ws.close();
      me.webrtc.peerConn.close();
    } catch (e) {}
  };
  
  callback(video);
  
};
