mistplayers.mews = {
  name: "MSE websocket player",
  mimes: ["ws/video/mp4","ws/video/webm"],
  priority: MistUtil.object.keys(mistplayers).length + 1,
  isMimeSupported: function (mimetype) {
    return (this.mimes.indexOf(mimetype) == -1 ? false : true);
  },
  isBrowserSupported: function (mimetype,source,MistVideo) {
    
    if ((!("WebSocket" in window)) || (!("MediaSource" in window)) || (!("Promise" in window))) { return false; }
    
    //check for http/https mismatch
    if (location.protocol.replace(/^http/,"ws") != MistUtil.http.url.split(source.url.replace(/^http/,"ws")).protocol) {
      MistVideo.log("HTTP/HTTPS mismatch for this source");
      return false;
    }
    
    //it runs on MacOS, but breaks often on seek/track switch etc
    if (navigator.platform.toUpperCase().indexOf('MAC') >= 0) {
      return false;
    }

    //check (and save) codec compatibility
    function translateCodec(track) {
      if (track.codecstring){return track.codecstring;}
      function bin2hex(index) {
        return ("0"+track.init.charCodeAt(index).toString(16)).slice(-2);
      }
      switch (track.codec) {
        case "AAC":
          return "mp4a.40.2";
        case "MP3":
          return "mp4a.40.34";
        case "AC3":
          return "ec-3";
        case "H264":
          return "avc1."+bin2hex(1)+bin2hex(2)+bin2hex(3);
        case "HEVC":
          return "hev1."+bin2hex(1)+bin2hex(6)+bin2hex(7)+bin2hex(8)+bin2hex(9)+bin2hex(10)+bin2hex(11)+bin2hex(12);
        default:
          return track.codec.toLowerCase();
      }
      
    }
    var codecs = {};
    var playabletracks = {};
    var hassubtitles = false;
    for (var i in MistVideo.info.meta.tracks) {
      if (MistVideo.info.meta.tracks[i].type != "meta") {
        /*if (MistVideo.info.meta.tracks[i].codec == "HEVC") {
          //the iPad claims to be able to play MP4/WS H265 tracks.. haha no.
          continue;
        }*/
        codecs[translateCodec(MistVideo.info.meta.tracks[i])] = MistVideo.info.meta.tracks[i];
      }
      else if (MistVideo.info.meta.tracks[i].codec == "subtitle") { hassubtitles = true; }
    }
    var container = mimetype.split("/")[2];
    function test(codecs) {
      //if (container == "webm") { return true; }
      return MediaSource.isTypeSupported("video/"+container+";codecs=\""+codecs+"\"");
    }
    source.supportedCodecs = [];
    for (var i in codecs) {
      //i is the long name (like mp4a.40.2), codecs[i] is the track meta, codecs[i].codec is the short name (like AAC)
      var s = test(i);
      if (s) {
        source.supportedCodecs.push(codecs[i].codec);
        playabletracks[codecs[i].type] = 1;
      }
    }

    if (hassubtitles) {
      //there is a subtitle track, check if there is a webvtt source
      for (var i in MistVideo.info.source) {
        if (MistVideo.info.source[i].type == "html5/text/vtt") {
          playabletracks.subtitle = 1;
          break;
        }
      }
    }

    return MistUtil.object.keys(playabletracks);
  },
  player: function(){}
};
var p = mistplayers.mews.player;
p.prototype = new MistPlayer();
p.prototype.build = function (MistVideo,callback) {
  
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
  
  var player = this;
  //player.debugging = true;
  //player.debugging = "dl"; //download appended data on ms close
  player.built = false;
  
  //this function is called both when the websocket is ready and the media source is ready - both should be open to proceed
  function checkReady() {
    //console.log("checkready",player.ws.readyState,player.ms.readyState);
    if ((player.ws.readyState == player.ws.OPEN) && (player.ms.readyState == "open") && (player.sb)) {
      if (!player.built) {
        callback(video);
        player.built = true;
      }
      if (MistVideo.options.autoplay) {
        player.api.play().catch(function(){});
      }

      return true;
    }
  }
  this.msoninit = []; //array of functions that will be executed once ms is open
  this.msinit = function() {
    return new Promise(function(resolve,reject){ 
      //prepare mediasource
      player.ms = new MediaSource();
      video.src = URL.createObjectURL(player.ms);
      player.ms.onsourceopen = function(){
        for (var i in player.msoninit) {
          player.msoninit[i]();
        }
        player.msoninit = [];
        resolve();
      };
      player.ms.onsourceclose = function(e){
        if (player.debugging) console.error("ms close",e);
        send({type:"stop"}); //stop sending data please something went wrong
      };
      player.ms.onsourceended = function(e){
        if (player.debugging) console.error("ms ended",e);
        
        if (player.debugging == "dl") {
          function downloadBlob (data, fileName, mimeType) {
            var blob, url;
            blob = new Blob([data], {
              type: mimeType
            });
            url = window.URL.createObjectURL(blob);
            downloadURL(url, fileName);
            setTimeout(function() {
              return window.URL.revokeObjectURL(url);
            }, 1000);
          };

          function downloadURL (data, fileName) {
            var a;
            a = document.createElement('a');
            a.href = data;
            a.download = fileName;
            document.body.appendChild(a);
            a.style = 'display: none';
            a.click();
            a.remove();
          };
        
          var l = 0;
          for (var i = 0; i < player.sb.appended.length; i++) {
            l += player.sb.appended[i].length;
          }
          var d = new Uint8Array(l);
          var l = 0;
          for (var i = 0; i < player.sb.appended.length; i++) {
            d.set(player.sb.appended[i],l);
            l += player.sb.appended[i].length;
          }
          
          downloadBlob(d, 'appended.mp4.bin', 'application/octet-stream');
        }
        send({type:"stop"}); //stop sending data please something went wrong
      };
    });
  }
  this.msinit().then(function(){
    if (player.sb) {
      MistVideo.log("Not creating source buffer as one already exists.");
      checkReady();
      return;
    }
  });
  this.onsbinit = [];
  this.sbinit = function(codecs){
    if (!codecs) { MistVideo.showError("Did not receive any codec: nothing to initialize."); return; }

    //console.log("sourcebuffers",player.ms.sourceBuffers.length);
    //console.log("sb init","video/"+MistVideo.source.type.split("/")[2]+";codecs=\""+codecs.join(",")+"\"");
    player.sb = player.ms.addSourceBuffer("video/"+MistVideo.source.type.split("/")[2]+";codecs=\""+codecs.join(",")+"\"");
    player.sb.mode = "segments"; //the fragments will be put in the buffer at the correct time: much better behavior when seeking / not playing from 0s
    
    //save the current source buffer codecs
    player.sb._codecs = codecs;
    
    player.sb._size = 0;
    player.sb.queue = [];
    var do_on_updateend = [];
    player.sb.do_on_updateend = do_on_updateend; //so we can check it from the ws onmessage handler too
    player.sb.appending = null;
    player.sb.appended = [];
    var n = 0;
    player.sb.addEventListener("updateend",function(){
      if (!player.sb) {
        MistVideo.log("Reached updateend but the source buffer is "+JSON.stringify(player.sb)+". ");
        return;
      }
      //player.sb._busy = true;
      //console.log("start updateend");
      
      if (player.debugging) {
        if (player.sb.appending) player.sb.appended.push(player.sb.appending);
        player.sb.appending = null;
      }
      
      //every 500 fragments, clean the buffer (about every 15 sec)
      if (n >= 500) {
        //console.log(n,video.currentTime - video.buffered.start(0));
        n = 0;
        player.sb._clean(10); //keep 10 sec
      }
      else {
        n++;
      }
      
      var do_funcs = do_on_updateend.slice(); //clone the array
      do_on_updateend = [];
      for (var i in do_funcs) {
        //console.log("do_funcs",Number(i)+1,"/",do_funcs.length);
        if (!player.sb) {
          if (player.debugging) { console.warn("I was doing on_updateend but the sb was reset"); } 
          break;
        }
        if (player.sb.updating) {
          //it's updating again >_>
          do_on_updateend.concat(do_funcs.slice(i)); //add the remaining functions to do_on_updateend
          if (player.debugging) { console.warn("I was doing on_updateend but was interrupted"); }
          break;
        }
        do_funcs[i](i < do_funcs.length-1 ? do_funcs.slice(i) : []); //pass remaining do_funcs as argument
      }
      
      if (!player.sb) { return; }

      player.sb._busy = false;
      //console.log("end udpateend");
      //console.log("onupdateend",player.sb.queue.length,player.sb.updating);
      if (player.sb && player.sb.queue.length > 0 && !player.sb.updating && !video.error) {
        //console.log("appending from queue");
        player.sb._append(this.queue.shift());
      }
    });
    player.sb.error = function(e){
      console.error("sb error",e);
    };
    player.sb.abort = function(e){
      console.error("sb abort",e);
    };
    
    player.sb._doNext = function(func) {
      do_on_updateend.push(func);
    };
    player.sb._do = function(func) {
      if (this.updating || this._busy) {
        this._doNext(func);
      }
      else {
        func();
      }
    }
    player.sb._append = function(data) {
      if (!data) { return; }
      if (!data.buffer) { return; }
      if (player.debugging) { player.sb.appending = new Uint8Array(data); }
      if (player.sb._busy) {
        if (player.debugging) console.warn("I wanted to append data, but now I won't because the thingy was still busy. Putting it back in the queue.");
        player.sb.queue.unshift(data);
        return;
      }
      player.sb._busy = true;
      //console.log("appendBuffer");
      try {
        player.sb.appendBuffer(data);
      }
      catch(e){
        switch (e.name) {
          case "QuotaExceededError": {
            if (video.buffered.length) {
              if (video.currentTime - video.buffered.start(0) > 1) {
                //clear as much from the buffer as we can
                MistVideo.log("Triggered QuotaExceededError: cleaning up "+(Math.round((video.currentTime - video.buffered.start(0) - 1)*10)/10)+"s");
                player.sb._clean(1);
              }
              else {
                var bufferEnd = video.buffered.end(video.buffered.length-1);
                MistVideo.log("Triggered QuotaExceededError but there is nothing to clean: skipping ahead "+(Math.round((bufferEnd - video.currentTime)*10)/10)+"s");
                video.currentTime = bufferEnd;
              }
              player.sb._busy = false;
              player.sb._append(data); //now try again
              return;
            }
            break;
          }
          case "InvalidStateError": {
            player.api.pause(); //playback is borked, so stop downloading more data
            if (MistVideo.video.error) {
              //Failed to execute 'appendBuffer' on 'SourceBuffer': The HTMLMediaElement.error attribute is not null

              //the video element error is already triggering the showError()
              return;
            }
            break;
          }
        }
        MistVideo.showError(e.message);
      }
    }
    
    //we're initing the source buffer and there is a msg queue of data built up before the buffer was ready. Start by adding these data fragments to the source buffer
    if (player.msgqueue) {
      //There may be more than one msg queue, i.e. when rapidly switching tracks. Add only one msg queue and always add the oldest msg queue first.
      if (player.msgqueue[0]) {
        var do_do = false; //if there are no messages in the queue, make sure to execute any do_on_updateend functions right away
        if (player.msgqueue[0].length) {
          for (var i in player.msgqueue[0]) {
            if (player.sb.updating || player.sb.queue.length || player.sb._busy) {
              player.sb.queue.push(player.msgqueue[0][i]);
            }
            else {
              //console.log("appending new data");
              player.sb._append(player.msgqueue[0][i]);
            }
          }
        }
        else {
          do_do = true;
        }
        player.msgqueue.shift();
        if (player.msgqueue.length == 0) { player.msgqueue = false; }
        MistVideo.log("The newly initialized source buffer was filled with data from a seperate message queue."+(player.msgqueue ? " "+player.msgqueue.length+" more message queue(s) remain." : ""));
        if (do_do) {
          MistVideo.log("The seperate message queue was empty; manually triggering any onupdateend functions");
          player.sb.dispatchEvent(new Event("updateend"));
        }
      }
    }
    
    //remove everything keepaway secs before the current playback position to keep sourcebuffer from filling up
    player.sb._clean = function(keepaway){
      if (!keepaway) keepaway = 180;
      if (video.currentTime > keepaway) {
        player.sb._do(function(){
          //make sure end time is never 0
          player.sb.remove(0,Math.max(0.1,video.currentTime - keepaway));
        });
      }
    }

    if (player.onsbinit.length) {
      player.onsbinit.shift()();
    }
    checkReady();
  };
  
  this.wsconnect = function(){
    return new Promise(function(resolve,reject){
      //prepare websocket (both data and messages)
      this.ws = new WebSocket(MistVideo.source.url);
      this.ws.binaryType = "arraybuffer";
      
      this.ws.s = this.ws.send;
      this.ws.send = function(){
        if (this.readyState == 1) {
          this.s.apply(this,arguments);
          return true;
        }
        return false;
      };
      this.ws.onopen = function(){
        this.wasConnected = true;
        resolve();
      };
      this.ws.onerror = function(e){
        MistVideo.showError("MP4 over WS: websocket error");
      };
      this.ws.onclose = function(e){
        MistVideo.log("MP4 over WS: websocket closed");
        if (this.wasConnected && (!MistVideo.destroyed) && (!player.sb || !player.sb.paused) && (MistVideo.state == "Stream is online") && (!(MistVideo.video && MistVideo.video.error))) {
          MistVideo.log("MP4 over WS: reopening websocket");
          player.wsconnect().then(function(){
            if (!player.sb) {
              //retrieve codec info
              var f = function(msg){
                //got codec data, set up source buffer
                if (!player.sb) { player.sbinit(msg.data.codecs); }
                else { player.api.play().catch(function(){}); }

                player.ws.removeListener("codec_data",f);
              };
              player.ws.addListener("codec_data",f);
              send({type:"request_codec_data",supported_codecs:MistVideo.source.supportedCodecs});
            }
            else {
              player.api.play();
            }
          },function(){
            Mistvideo.error("Lost connection to the Media Server");
          });
        }
      };
      this.ws.timeOut = MistVideo.timers.start(function(){
        if (player.ws.readyState == 0) {
          MistVideo.log("MP4 over WS: socket timeout - try next combo");
          MistVideo.nextCombo();
        }
      },5e3);

      this.ws.listeners = {}; //kind of event listener list for websocket messages
      this.ws.addListener = function(type,f){
        if (!(type in this.listeners)) { this.listeners[type] = []; }
        this.listeners[type].push(f);
      };
      this.ws.removeListener = function(type,f) {
        if (!(type in this.listeners)) { return; }
        var i = this.listeners[type].indexOf(f);
        if (i < 0) { return; }
        this.listeners[type].splice(i,1);
        return true;
      }
      player.msgqueue = false;
      var requested_rate = 1;
      var serverdelay = [];
      var currenttracks = [];
      this.ws.onmessage = function(e){
        if (!e.data) { throw "Received invalid data"; }
        if (typeof e.data == "string") {
          var msg = JSON.parse(e.data);
          if (player.debugging && (msg.type != "on_time")) { console.log("ws message",msg); }
          switch (msg.type) {
            case "on_stop": {
              //the last fragment has been added to the buffer
              var eObj;
              eObj = MistUtil.event.addListener(video,"waiting",function(e){
                player.sb.paused = true;
                MistUtil.event.send("ended",null,video);
                MistUtil.event.removeListener(eObj);
              });
              player.ws.onclose = function(){}; //don't reopen websocket, just close, it's okay, rly
              break;
            }
            case "on_time": {              
              var buffer = msg.data.current - video.currentTime*1e3;
              var serverDelay = player.ws.serverDelay.get();
              var desiredBuffer = Math.max(100+serverDelay,serverDelay*2);
              var desiredBufferwithJitter = desiredBuffer+(msg.data.jitter ? msg.data.jitter : 0);


              if (MistVideo.info.type != "live") { desiredBuffer += 2000; } //if VoD, keep an extra 2 seconds of buffer

              if (player.debugging) {
                console.log(
                  "on_time received", msg.data.current/1e3,
                  "currtime", video.currentTime,
                  requested_rate+"x",
                  "buffer",Math.round(buffer),"/",Math.round(desiredBuffer),
                  (MistVideo.info.type == "live" ? "latency:"+Math.round(msg.data.end-video.currentTime*1e3)+"ms" : ""),
                  (player.monitor ? "bitrate:"+MistUtil.format.bits(player.monitor.currentBps)+"/s" : ""),
                  "listeners",player.ws.listeners && player.ws.listeners.on_time ? player.ws.listeners.on_time : 0,
                  "msgqueue",player.msgqueue ? player.msgqueue.length : 0,
                  "readyState",MistVideo.video.readyState,msg.data
                );
              }

              if (!player.sb) {
                MistVideo.log("Received on_time, but the source buffer is being cleared right now. Ignoring.");
                break;
              }

              if (lastduration != msg.data.end*1e-3) {
                lastduration = msg.data.end*1e-3;
                MistUtil.event.send("durationchange",null,MistVideo.video);
              }
              MistVideo.info.meta.buffer_window = msg.data.end - msg.data.begin;
              player.sb.paused = false;

              if (MistVideo.info.type == "live") {
                if (requested_rate == 1) {
                  if (msg.data.play_rate_curr == "auto") {
                    if (video.currentTime > 0) { //give it some time to seek to live first when starting up
                      //assume we want to be as live as possible
                      if (buffer > desiredBufferwithJitter*2) {
                        requested_rate = 1 + Math.min(1,((buffer-desiredBufferwithJitter)/desiredBufferwithJitter))*0.08;
                        video.playbackRate *= requested_rate;
                        MistVideo.log("Our buffer ("+Math.round(buffer)+"ms) is big (>"+Math.round(desiredBufferwithJitter*2)+"ms), so increase the playback speed to "+(Math.round(requested_rate*100)/100)+" to catch up.");
                      }
                      else if (buffer < 0) {
                        requested_rate = 0.8;
                        video.playbackRate *= requested_rate;
                        MistVideo.log("Our buffer ("+Math.round(buffer)+"ms) is negative so decrease the playback speed to "+(Math.round(requested_rate*100)/100)+" to let it catch up.");
                      }
                      else if (buffer < desiredBuffer/2) {
                        requested_rate = 1 + Math.min(1,((buffer-desiredBuffer)/desiredBuffer))*0.08;
                        video.playbackRate *= requested_rate;
                        MistVideo.log("Our buffer ("+Math.round(buffer)+"ms) is small (<"+Math.round(desiredBuffer/2)+"ms), so decrease the playback speed to "+(Math.round(requested_rate*100)/100)+" to catch up.");
                      }
                    }
                  }
                }
                else if (requested_rate > 1) {
                  if (buffer < desiredBufferwithJitter) {
                    video.playbackRate /= requested_rate;
                    requested_rate = 1;
                    MistVideo.log("Our buffer ("+Math.round(buffer)+"ms) is small enough (<"+Math.round(desiredBufferwithJitter)+"ms), so return to real time playback.");
                  }
                }
                else {
                  //requested rate < 1
                  if (buffer > desiredBufferwithJitter) {
                    video.playbackRate /= requested_rate;
                    requested_rate = 1;
                    MistVideo.log("Our buffer ("+Math.round(buffer)+"ms) is big enough (>"+Math.round(desiredBufferwithJitter)+"ms), so return to real time playback.");
                  }
                }
              }
              else {
                //it's VoD, change the rate at which the server sends data to try and keep the buffer small
                if (requested_rate == 1) {
                  if (msg.data.play_rate_curr == "auto") {
                    if (buffer < desiredBuffer/2) {
                      if (buffer < -10e3) {
                        //seek to play point
                        send({type: "seek", seek_time: Math.round(video.currentTime*1e3)});
                      }
                      else {
                        //negative buffer? ask for faster delivery
                        requested_rate = 2;
                        MistVideo.log("Our buffer is negative, so request a faster download rate.");
                        send({type: "set_speed", play_rate: requested_rate});
                      }
                    }
                    else if (buffer - desiredBuffer > desiredBuffer) {
                      MistVideo.log("Our buffer is big, so request a slower download rate.");
                      requested_rate = 0.5;
                      send({type: "set_speed", play_rate: requested_rate});
                    }
                  }
                }
                else if (requested_rate > 1) {
                  if (buffer > desiredBuffer) {
                    //we have enough buffer, ask for real time delivery
                    send({type: "set_speed", play_rate: "auto"});
                    requested_rate = 1;
                    MistVideo.log("The buffer is big enough, so ask for realtime download rate.");
                  }
                }
                else { //requested_rate < 1
                  if (buffer < desiredBuffer) {
                    //we have a small enough bugger, ask for real time delivery
                    send({type: "set_speed", play_rate: "auto"});
                    requested_rate = 1;
                    MistVideo.log("The buffer is small enough, so ask for realtime download rate.");
                  }
                }
              }

              if (MistVideo.reporting && msg.data.tracks) {
                MistVideo.reporting.stats.d.tracks = msg.data.tracks.join(",");
              }
              
              //check if the tracks are different than before, and if so, signal the skin to display the playing tracks
              if ((msg.data.tracks) && (currenttracks != msg.data.tracks)) {
                var tracks = MistVideo.info ? MistUtil.tracks.parse(MistVideo.info.meta.tracks) : [];
                for (var i in msg.data.tracks) {
                  if (currenttracks.indexOf(msg.data.tracks[i]) < 0) {
                    //find track type
                    var type;
                    for (var j in tracks) {
                      if (msg.data.tracks[i] in tracks[j]) {
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
                      trackid: msg.data.tracks[i]
                    },MistVideo.video);
                  }
                }

                currenttracks = msg.data.tracks;
              }

              break;
            }
            case "tracks": {
              //check if all codecs are equal to the ones we were using before
              function checkEqual(arr1,arr2) {
                if (!arr2) { return false; }
                if (arr1.length != arr2.length) { return false; }
                for (var i in arr1) {
                  if (arr2.indexOf(arr1[i]) < 0) {
                    return false;
                  }
                }
                return true;
              }
              function setSeekingPosition(t) {
                var currPos = video.currentTime.toFixed(3);
                if (currPos > t) {
                  //don't seek backwards
                  t = currPos;
                }
                if (!video.buffered.length || (video.buffered.end(video.buffered.length-1) < t)) {
                  if (player.debugging) { console.log("Desired seeking position ("+MistUtil.format.time(t,{ms:true})+") not yet in buffer ("+(video.buffered.length ? MistUtil.format.time(video.buffered.end(video.buffered.length-1),{ms:true}) : "null")+")"); }
                  player.sb._doNext(function(){ setSeekingPosition(t); });
                  return;
                }
                video.currentTime = t;
                MistVideo.log("Setting playback position to "+MistUtil.format.time(t,{ms:true}));
                if (video.currentTime.toFixed(3) < t) {
                  player.sb._doNext(function(){ setSeekingPosition(t); });
                  if (player.debugging) { console.log("Could not set playback position"); }
                }
                else {
                  if (player.debugging) { console.log("Set playback position to "+MistUtil.format.time(t,{ms:true})); }
                  var p = function(){
                    player.sb._doNext(function(){
                      if (video.buffered.length) {
                        //if (player.debugging) { console.log(video.buffered.start(0),video.buffered.end(0),video.currentTime); }
                        if (video.buffered.start(0) > video.currentTime) { 
                          var b = video.buffered.start(0);
                          video.currentTime = b;
                          if (video.currentTime != b) {
                            p();
                          }
                        }
                      }
                      else {
                        p();
                      }
                    });
                  };
                  p();
                }
              }
              

              if (checkEqual(player.last_codecs ? player.last_codecs : player.sb._codecs,msg.data.codecs)) {
                MistVideo.log("Player switched tracks, keeping source buffer as codecs are the same as before.");
                if ((video.currentTime == 0) && (msg.data.current != 0)) {
                  setSeekingPosition((msg.data.current*1e-3).toFixed(3));
                }
              }
              else {
                if (player.debugging) {
                  console.warn("Different codecs!");
                  console.warn("video time",video.currentTime,"switch startpoint",msg.data.current*1e-3);
                }
                player.last_codecs = msg.data.codecs;
                //start gathering messages in a new msg queue. They won't be appended to the current source buffer
                if (player.msgqueue) {
                  player.msgqueue.push([]);
                }
                else {
                  player.msgqueue = [[]];
                }
                //play out buffer, then when we reach the starting timestamp of the new data, reset the source buffers
                var clear = function(){
                  //once the source buffer is done updating the current segment, clear the specified interval from the buffer
                  if (player && player.sb) {
                    player.sb._do(function(remaining_do_on_updateend){
                      if (!player.sb.updating) {
                        if (!isNaN(player.ms.duration)) player.sb.remove(0,Infinity);
                        player.sb.queue = [];
                        player.ms.removeSourceBuffer(player.sb);
                        player.sb = null;
                        video.src = "";
                        player.ms.onsourceclose = null;
                        player.ms.onsourceended = null;
                        //console.log("sb murdered");
                        if (player.debugging && remaining_do_on_updateend && remaining_do_on_updateend.length) {
                          console.warn("There are do_on_updateend functions queued, which I will re-apply after clearing the sb.");
                        }

                        player.msinit().then(function(){
                          player.sbinit(msg.data.codecs);
                          player.sb.do_on_updateend = remaining_do_on_updateend;

                          var e = MistUtil.event.addListener(video,"loadedmetadata",function(){
                            MistVideo.log("Buffer cleared");

                            setSeekingPosition((msg.data.current*1e-3).toFixed(3));

                            MistUtil.event.removeListener(e);
                          });
                        });
                      }
                      else {
                        clear();
                      }
                    });
                  }
                  else {
                    if (player.debugging) { console.warn("sb not available to do clear"); }
                    player.onsbinit.push(clear);
                  }
                };

                if (!msg.data.codecs || !msg.data.codecs.length) {
                  MistVideo.showError("Track switch does not contain any codecs, aborting.");
                  //reset setTracks to auto
                  MistVideo.options.setTracks = false;
                  clear();
                  break;
                }
                function reachedSwitchingPoint(msg) {
                  if (player.debugging) {
                    console.warn("reached switching point",msg.data.current*1e-3,MistUtil.format.time(msg.data.current*1e-3));
                  }
                  MistVideo.log("Track switch: reached switching point");
                  clear();
                }
                if (video.currentTime == 0) {
                  reachedSwitchingPoint(msg);
                }
                else {
                  if (msg.data.current >= video.currentTime*1e3) {
                    MistVideo.log("Track switch: waiting for playback to reach the switching point ("+MistUtil.format.time(msg.data.current*1e-3,{ms:true})+")");

                    //wait untill the video has reached the time of the newly received track or the end of our buffer
                    var ontime = MistUtil.event.addListener(video,"timeupdate",function(){
                      if (msg.data.current < video.currentTime * 1e3) {
                        if (player.debugging) { console.log("Track switch: video.currentTime has reached switching point."); }
                        reachedSwitchingPoint(msg);
                        MistUtil.event.removeListener(ontime);
                        MistUtil.event.removeListener(onwait);
                      }
                    });
                    var onwait = MistUtil.event.addListener(video,"waiting",function(){
                      if (player.debugging) { console.log("Track switch: video has reached end of buffer.","Gap:",Math.round(msg.data.current - video.currentTime * 1e3),"ms"); }
                      reachedSwitchingPoint(msg);
                      MistUtil.event.removeListener(ontime);
                      MistUtil.event.removeListener(onwait);
                    });
                  } 
                  else {
                    //subscribe to on_time, wait until we've received current playback point
                    //if we don't wait, the screen will go black until the buffer is full enough
                    MistVideo.log("Track switch: waiting for the received data to reach the current playback point");
                    var ontime = function(newmsg){
                      if (newmsg.data.current >= video.currentTime*1e3) {
                        reachedSwitchingPoint(newmsg);
                        player.ws.removeListener("on_time",ontime);
                      }
                    }
                    player.ws.addListener("on_time",ontime);
                  }
                }
              }
              break;
            }
            case "pause": {
              if (player.sb) { player.sb.paused = true; }
              break;
            }
          }
          if (msg.type in this.listeners) {
            for (var i = this.listeners[msg.type].length-1; i >= 0; i--) { //start at last in case the listeners remove themselves
              this.listeners[msg.type][i](msg);
            }
          }
          return;
        }
        var data = new Uint8Array(e.data);
        if (data) {
          if (player.monitor && player.monitor.bitCounter) {
            for (var i in player.monitor.bitCounter) {
              player.monitor.bitCounter[i] += e.data.byteLength*8;
            }
          }
          if ((player.sb) && (!player.msgqueue)) {
            if (player.sb.updating || player.sb.queue.length || player.sb._busy) {
              player.sb.queue.push(data);
            }
            else {
              //console.log("appending new data");
              player.sb._append(data);
            }
          }
          else {
            //There is no active source buffer or we're preparing for a track switch.
            //Any data is kept in a seperate buffer and won't be appended to the source buffer until it is reinitialised.
            if (!player.msgqueue) { player.msgqueue = [[]]; }
            //There may be more than one seperate buffer (in case of rapid track switches), always append to the last of the buffers
            player.msgqueue[player.msgqueue.length-1].push(data);
          }
        }
        else {
          //console.warn("no data, wut?",data,new Uint8Array(e.data));
          MistVideo.log("Expecting data from websocket, but received none?!");
        }
      }
      
      
      this.ws.serverDelay = {
        delays: [],
        log: function (type) {
          var responseType = false;
          switch (type) {
            case "seek":
            case "set_speed": {
              //wait for cmd.type
              responseType = type;
              break;
            }
            case "request_codec_data": {
              responseType = "codec_data";
              break;
            }
            default: {
              //do nothing
              return;
            }
          }
          if (responseType) {
            var starttime = new Date().getTime();
            function onResponse() {
              if (!player.ws || !player.ws.serverDelay) { return; }
              player.ws.serverDelay.add(new Date().getTime() - starttime);
              player.ws.removeListener(responseType,onResponse);
            }
            player.ws.addListener(responseType,onResponse);
          }
        },
        add: function(delay){
          this.delays.unshift(delay);
          if (this.delays.length > 5) {
            this.delays.splice(5);
          }
        },
        get: function(){
          if (this.delays.length) {
            //return average of the last 3 recorded delays
            let sum = 0;
            let i = 0;
            for (null; i < this.delays.length; i++){
              if (i >= 3) { break; }
              sum += this.delays[i];
            }
            return sum/i;
          }
          return 500;
        }
      };
    }.bind(this));
  };
  this.wsconnect().then(function(){
    //retrieve codec info
    var f = function(msg){
      //got codec data, set up source buffer
      if (player.ms && player.ms.readyState == "open") {
        player.sbinit(msg.data.codecs);
      }
      else {
        player.msoninit.push(function(){
          player.sbinit(msg.data.codecs);
        });
      }
      
      player.ws.removeListener("codec_data",f);
    };
    this.ws.addListener("codec_data",f);
    send({type:"request_codec_data",supported_codecs:MistVideo.source.supportedCodecs});
  }.bind(this));
  
  function send(cmd,retry){
    if (!player.ws) { throw "No websocket to send to"; }
    if (retry > 5) { throw "Too many retries, giving up"; }
    if (player.ws.readyState < player.ws.OPEN) {
      MistVideo.timers.start(function(){
        send(cmd,++retry);
      },500);
      return;
    }
    if (player.ws.readyState >= player.ws.CLOSING) {
      if (MistVideo.destroyed) { return; }
      //throw "WebSocket has been closed already.";
      MistVideo.log("MP4 over WS: reopening websocket");
      player.wsconnect().then(function(){
        if (!player.sb) {
          //retrieve codec info
          var f = function(msg){
            //got codec data, set up source buffer
            if (!player.sb) { player.sbinit(msg.data.codecs); }
            else { player.api.play().catch(function(){}); }

            player.ws.removeListener("codec_data",f);
          };
          player.ws.addListener("codec_data",f);
          send({type:"request_codec_data",supported_codecs:MistVideo.source.supportedCodecs});
        }
        else {
          player.api.play();
        }
        send(cmd);
      },function(){
        Mistvideo.error("Lost connection to the Media Server");
      });
      return;
    }
    if (player.debugging) { console.log("ws send",cmd); }
    player.ws.serverDelay.log(cmd.type);
    if (!player.ws.send(JSON.stringify(cmd))) {
      //not able to send, not sure why.. go back to retry
      return send(cmd,++retry);
    }
  }

  player.findBuffer = function (position) {
    var buffern = false;
    for (var i = 0; i < video.buffered.length; i++) {
      if ((video.buffered.start(i) <= position) && (video.buffered.end(i) >= position)) {
        buffern = i;
        break;
      }
    }
    return buffern;
  };
  
  this.api = {
    play: function(skipToLive){
      return new Promise(function(resolve,reject){
        if (!video.paused) { 
          //we're already playing, what are you doing?
          resolve();
          return;
        }

        if (("paused" in player.sb) && !player.sb.paused) {
          video.play().then(resolve).catch(reject);
          return;
        }


        var f = function(e){
          if (!player.sb) {
            MistVideo.log("Attempting to play, but the source buffer is being cleared. Waiting for next on_time.");
            return;
          }
          if (MistVideo.info.type == "live") {
            if (skipToLive || (video.currentTime == 0)) {
              var g = function(){
                if (video.buffered.length) {
                  //is data.current contained within a buffer? is video.currentTime also contained in that buffer? if not, seek the video
                  var buffern = player.findBuffer(e.data.current*1e-3);
                  if (buffern !== false) {
                    if ((video.buffered.start(buffern) > video.currentTime) || (video.buffered.end(buffern) < video.currentTime)) {
                      video.currentTime = e.data.current*1e-3;
                      MistVideo.log("Setting live playback position to "+MistUtil.format.time(video.currentTime));
                    }
                    video.play().then(resolve).catch(function(){
                      //could not play video, pause the download
                      return reject.apply(this,arguments);
                    });
                    player.sb.paused = false;                   
                    player.sb.removeEventListener("updateend",g);
                  }
                }
              };
              player.sb.addEventListener("updateend",g);
            }
            else {
              player.sb.paused = false;
              video.play().then(resolve).catch(function(){
                //could not play video, pause the download
                player.api.pause();
                return reject.apply(this,arguments);
              });
            }
            player.ws.removeListener("on_time",f);
          }
          else if (e.data.current > video.currentTime) {
            player.sb.paused = false;
            if (video.buffered.length && video.buffered.start(0) > video.currentTime) {
              video.currentTime = video.buffered.start(0);
            }
            video.play().then(resolve).catch(reject);
            player.ws.removeListener("on_time",f);
          }
        };
        player.ws.addListener("on_time",f);
        var cmd = {type:"play"};
        if (skipToLive) { cmd.seek_time = "live"; }
        send(cmd);
      });
    },
    pause: function(){
      video.pause();
      send({type: "hold"});
      if (player.sb) { player.sb.paused = true; }
    },
    setTracks: function(obj){
      if (!MistUtil.object.keys(obj).length) { return; }
      obj.type = "tracks";
      obj = MistUtil.object.extend({
        type: "tracks",
        //seek_time:  Math.round(Math.max(0,video.currentTime*1e3-(500+player.ws.serverDelay.get())))
      },obj);
      send(obj);
    },
    unload: function(){
      player.api.pause();
      player.sb._do(function(){
        player.sb.remove(0,Infinity);
        try {
          player.ms.endOfStream();
          
          //it's okay if it fails
        } catch (e) {  }
      });
      player.ws.close();
    },
    setSubtitle: function(trackmeta) {
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
      }
    }
  };
 
  //override seeking
  Object.defineProperty(this.api,"currentTime",{
    get: function(){
      return video.currentTime;
    },
    set: function(value){
      //console.warn("seek to",value);
      if (isNaN(value) || (value < 0)) {
        MistVideo.log("Ignoring seek to "+value+" because ewww.");
        return;
      }
      MistUtil.event.send("seeking",value,video);
      send({type: "seek", seek_time: Math.round(Math.max(0,value*1e3-(250+player.ws.serverDelay.get())))}); //safety margin for server latency
      //set listener "seek"
      var onseek = function(e){
        player.ws.removeListener("seek",onseek);
        var ontime = function(e){
          player.ws.removeListener("on_time",ontime);
          //in the first on_time, assume that the data were getting is where we want to be
          value = e.data.current * 1e-3;
          value = value.toFixed(3);
          //retry a max of 10 times
          var retries = 10;
          var f = function() {
            video.currentTime = value;
            if (video.currentTime.toFixed(3) < value) {
              MistVideo.log("Failed to seek, wanted: "+value+" got: "+video.currentTime.toFixed(3));
              if (retries >= 0) {
                retries--;
                player.sb._doNext(f);
              }
            }
          }
          f();
        };
        player.ws.addListener("on_time",ontime);
      }
      player.ws.addListener("seek",onseek);
      video.currentTime = value;
      MistVideo.log("Seeking to "+MistUtil.format.time(value,{ms:true})+" ("+value+")");
    }
  });
  //override duration
  var lastduration = Infinity;
  Object.defineProperty(this.api,"duration",{
    get: function(){
      return lastduration;
    }
  });
  Object.defineProperty(this.api,"playbackRate",{
    get: function(){
      return video.playbackRate;
    },
    set: function(value){
      var f = function(msg){
        video.playbackRate = msg.data.play_rate_curr;
      };
      player.ws.addListener("set_speed",f);
      send({type: "set_speed", play_rate: (value == 1 ? "auto" : value)});
    }
  });
  
  //redirect properties
  //using a function to make sure the "item" is in the correct scope
  function reroute(item) {
    Object.defineProperty(player.api,item,{
      get: function(){ return video[item]; },
      set: function(value){
        return video[item] = value;
      }
    });
  }
  var list = [
    "volume"
    ,"buffered"
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
  
  //loop
  MistUtil.event.addListener(video,"ended",function(){
    if (player.api.loop) {
      player.api.currentTime = 0;
      player.sb._do(function(){
        try {
          player.sb.remove(0,Infinity);
        } catch (e) {}
      });
    }
  });
  
  var seeking = false;
  MistUtil.event.addListener(video,"seeking",function(){
    seeking = true;
    var seeked = MistUtil.event.addListener(video,"seeked",function(){
      seeking = false;
      MistUtil.event.removeListener(seeked);
    });
  });
  MistUtil.event.addListener(video,"waiting",function(){
    //check if there is a gap in the buffers, and if so, jump it
    if (seeking) { return; }
    var buffern = player.findBuffer(video.currentTime);
    if (buffern !== false) {
      if ((buffern+1 < video.buffered.length) && (video.buffered.start(buffern+1) - video.currentTime < 10e3)) {
        MistVideo.log("Skipped over buffer gap (from "+MistUtil.format.time(video.currentTime)+" to "+MistUtil.format.time(video.buffered.start(buffern+1))+")");
        video.currentTime = video.buffered.start(buffern+1);
      }
    } 
  });
  MistUtil.event.addListener(video,"pause",function(){
    if (player.sb && !player.sb.paused) {
      MistVideo.log("The browser paused the vid - probably because it has no audio and the tab is no longer visible. Pausing download.");
      send({type:"hold"});
      player.sb.paused = true;
      var p = MistUtil.event.addListener(video,"play",function(){
        if (player.sb && player.sb.paused) {
          send({type:"play"});
        }
        MistUtil.event.removeListener(p);
      });
    }
  });

  if (player.debugging) {
    MistUtil.event.addListener(video,"waiting",function(){
      //check the buffer available
      var buffers = [];
      var contained = false;
      for (var i = 0; i < video.buffered.length; i++) {
        if ((video.currentTime >= video.buffered.start(i)) && (video.currentTime <= video.buffered.end(i))) {
          contained = true;
        }
        buffers.push([
          video.buffered.start(i),
          video.buffered.end(i),
        ]);
      }
      console.log("waiting","currentTime",video.currentTime,"buffers",buffers,contained ? "contained" : "outside of buffer","readystate",video.readyState,"networkstate",video.networkState);
      if ((video.readyState >= 2) && (video.networkState >= 2)) {
        console.error("Why am I waiting?!",video.currentTime);
      }
      
    });
  }

  this.ABR = {
    size: null,
    bitrate: null,
    generateString: function(type,raw){
      switch (type) {
        case "size": {
          return "~"+[raw.width,raw.height].join("x");
        }
        case "bitrate": {
          return "<"+Math.round(raw)+"bps,minbps";
        }
        default: {
          throw "Unknown ABR type";
        }
      }
    },
    request: function(type,value){
      this[type] = value;

      var request = [];
      if (this.bitrate !== null) {
        request.push(this.generateString("bitrate",this.bitrate));
      }
      if (this.size !== null) {
        request.push(this.generateString("size",this.size));
      }
      else {
        request.push("maxbps");
      }

      return player.api.setTracks({
        video: request.join(",|")
      });
    }
  }

  this.api.ABR_resize = function(size){
    MistVideo.log("Requesting the video track with the resolution that best matches the player size");
    player.ABR.request("size",size);
  };
  //ABR: monitor playback issues and switch to lower bitrate track if available
  //NB: this ABR requests a lower bitrate if needed, but it can never go back up
  this.monitor = {
    bitCounter: [],
    bitsSince: [],
    currentBps: null,
    nWaiting: 0,
    nWaitingThreshold: 3,
    listener: MistVideo.options.ABR_bitrate ? MistUtil.event.addListener(video,"waiting",function(){
      player.monitor.nWaiting++;

      if (player.monitor.nWaiting >= player.monitor.nWaitingThreshold) {
        player.monitor.nWaiting = 0;
        player.monitor.action();
      }
    }) : null,
    getBitRate: function(){
      if (player.sb && !player.sb.paused) {

        this.bitCounter.push(0);
        this.bitsSince.push(new Date().getTime());

        //calculate current bitrate
        var bits, since;
        if (this.bitCounter.length > 5) {
          bits = player.monitor.bitCounter.shift();
          since = this.bitsSince.shift();
        }
        else {
          bits = player.monitor.bitCounter[0];
          since = this.bitsSince[0];
        }
        var dt = new Date().getTime() - since;
        this.currentBps = bits / (dt*1e-3);

        //console.log(MistUtil.format.bits(this.currentBps)+"its/s");
      }
      MistVideo.timers.start(function(){
        player.monitor.getBitRate();
      },500);
    },
    action: function(){
      if (MistVideo.options.setTracks && MistVideo.options.setTracks.video) {
        //a video track was selected by the user, do not change it
        return;
      }
      MistVideo.log("ABR threshold triggered, requesting lower quality");
      player.ABR.request("bitrate",this.currentBps);
    }
  };
  this.monitor.getBitRate();
};
