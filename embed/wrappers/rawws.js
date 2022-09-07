mistplayers.rawws = {
  name: "RAW to Canvas",
  mimes: ["ws/video/raw"],
  priority: MistUtil.object.keys(mistplayers).length + 1,
  isMimeSupported: function (mimetype) {
    return (MistUtil.array.indexOf(this.mimes,mimetype) == -1 ? false : true);
  },
  isBrowserSupported: function (mimetype,source,MistVideo) {
    
    //check for http/https mismatch
    if (location.protocol != MistUtil.http.url.split(source.url.replace(/^ws/,"http")).protocol) {
      if ((location.protocol == "file:") && (MistUtil.http.url.split(source.url.replace(/^ws/,"http")).protocol == "http:")) {
        MistVideo.log("This page was loaded over file://, the player might not behave as intended.");
      }
      else {
        MistVideo.log("HTTP/HTTPS mismatch for this source");
        return false;
      }
    }
        
    for (var i in MistVideo.info.meta.tracks) {
      if (MistVideo.info.meta.tracks[i].codec == "HEVC") { return ["video"]; }
    }
    
    return false;
  },
  player: function(){
    this.onreadylist = [];
  },
  scriptsrc: function(host) { return host+"/libde265.js"; }
};
var p = mistplayers.rawws.player;
p.prototype = new MistPlayer();
p.prototype.build = function (MistVideo,callback) {

  var player = this;
  
  player.onDecoderLoad = function() {
    if (MistVideo.destroyed) { return; }
    
    MistVideo.log("Building rawws player..");
    
    var api = {};
    MistVideo.player.api = api;

    var ele = document.createElement("canvas");
    var ctx = ele.getContext("2d");

    ele.style.objectFit = "contain";

    player.vars = {}; //will contain data like currentTime
    if (MistVideo.options.autoplay) {
      //if wantToPlay is false, playback will be paused after the first frame
      player.vars.wantToPlay = true;
    }
    player.dropping = false;
    player.frames = { //contains helper functions and statistics
      received: 0,
      bitsReceived: 0,
      decoded: 0,
      dropped: 0,
      behind: function(){
        return this.received - this.decoded - this.dropped;
      },
      timestamps: {},
      frame2time: function(frame,clean){
        if (frame in this.timestamps) {
          if (clean) {
            //clear any entries before the entry we're actually using
            for (var i in this.timestamps) {
              if (i == frame) { break; }
              delete this.timestamps[i];
            }
          }
          return this.timestamps[frame]*1e-3;
        }
        return 0;


        //get the closest known timestamp for the frame, then correct for the offset using the framerate
        var last = 0;
        var last_time = 0;
        for (var i in this.timestamps) {
          last = i;
          last_time = this.timestamps[i];
          if (i >= frame) {
            break;
          }
        }

        if (clean) {
          //clear any entries before the entry we're actually using
          for (var i in this.timestamps) {
            if (i == last) { break; }
            delete this.timestamps[i];
          }
        }


        var framerate = this.framerate();
        if ((typeof framerate != "undefined") && (framerate > 0)) {
          return last_time + (frame - last) / framerate;
        }
        else {
          return last_time;
        }
        
      },
      history: {
        log: [],
        add: function() {
          this.log.unshift({
            time: new Date().getTime(),
            received: player.frames.received,
            bitsReceived: player.frames.bitsReceived,
            decoded: player.frames.decoded
          });
          if (this.log.length > 3) { this.log.splice(3); }
        }
      },
      framerate_in: function(){
        var l = this.history.log.length -1;
        if (l < 1) { return 0; }
        var dframe = this.history.log[0].received - this.history.log[l].received;
        var dt = (this.history.log[0].time - this.history.log[l].time) * 1e-3;
        return dframe / dt;
      },
      bitrate_in: function(){
        var l = this.history.log.length -1;
        if (l < 1) { return 0; }
        var dbits = this.history.log[0].bitsReceived - this.history.log[l].bitsReceived;
        var dt = (this.history.log[0].time - this.history.log[l].time) * 1e-3;
        return dbits / dt;
      },
      framerate_out: function(){
        var l = this.history.log.length -1;
        if (l < 1) { return 0; }
        var dframe = this.history.log[0].decoded - this.history.log[l].decoded;
        var dt = (this.history.log[0].time - this.history.log[l].time) * 1e-3;
        return dframe / dt;
      },
      framerate: function(){
        if ("rate_theoretical" in this) { return this.rate_theoretical; }
        return this.framerate_in();
        //return undefined;
      },
      keepingUp: function(){
        var l = this.history.log.length -1;
        if (l < 1) { return 0; }
        var dBehind = this.history.log[l].received - this.history.log[l].decoded   -   (this.history.log[0].received - this.history.log[0].decoded);
        var dt = (this.history.log[0].time - this.history.log[l].time) * 1e-3;
        var keepingUp_frames = dBehind / dt; //amount of frames falling behind (negative) or catching up (positive) per second

        return keepingUp_frames / this.framerate(); //in seconds per seconds
      }
    };
    api.framerate_in = function () { return player.frames.framerate_in(); }
    api.framerate_out = function() { return player.frames.framerate_out(); }
    api.currentBps = function () { return player.frames.bitrate_in(); }
    api.loop = MistVideo.options.loop;
    
    //TODO define these if we're adding audio capabilities
    /*Object.defineProperty(MistVideo.player.api,"volume",{
      get: function(){ return 0; }
    });
    Object.defineProperty(MistVideo.player.api,"muted",{
      get: function(){ return true; }
    });*/

    Object.defineProperty(MistVideo.player.api,"webkitDecodedFrameCount",{
      get: function(){ return player.frames.decoded; }
    });
    Object.defineProperty(MistVideo.player.api,"webkitDroppedFrameCount",{
      get: function(){ return player.frames.dropped; }
    });


    var decoder;
    this.decoder = null;

    //shorter code to fake an event from the "video" (== canvas) element
    function emitEvent(type) {
      //console.log(type);
      MistUtil.event.send(type,undefined,ele);
    }

    function init() {

      function init_decoder() {
        decoder = new libde265.Decoder();
        MistVideo.player.decoder = decoder;
        
        var onDecode = [];
        decoder.addListener = function(func){
          onDecode.push(func);
        };
        decoder.removeListener = function(func){
          var i = onDecode.indexOf(func);
          if (i < 0) { return; }
          onDecode.splice(i,1);
          return true;
        };
        
        //pull requestAnimationFrame-if outside of display_image callback function so it only gets called once
        var displayImage;
        if (window.requestAnimationFrame) {
          displayImage = function(display_image_data){
            decoder.pending_image_data = display_image_data;
            window.requestAnimationFrame(function() {
              if (decoder.pending_image_data) {
                ctx.putImageData(decoder.pending_image_data, 0, 0);
                decoder.pending_image_data = null;
              }
            });
          };
        }
        else {
          displayImage = function(display_image_data){
            ctx.putImageData(display_image_data, 0, 0);
          };
        }


        decoder.set_image_callback(function(image) {
          player.frames.decoded++;
          if (player.vars.wantToPlay && (player.state != "seeking")) {
            emitEvent("timeupdate");
          }

          //image.display() wants a starting image, create it if it doesn't exist yet
          if (!decoder.image_data) {
            var w = image.get_width();
            var h = image.get_height();
            if (w != ele.width || h != ele.height || !this.image_data) {
              ele.width = w;
              ele.height = h;

              var img = ctx.createImageData(w, h);
              decoder.image_data = img;
            }
          }

          if (player.state != "seeking") { 
            //when seeking, do not display the new frame if we're not yet at the appropriate timestamp

            image.display(this.image_data, function(display_image_data) {
              decoder.decoding = false;
              displayImage(display_image_data);
            }); 
          }
          image.free();

          //we've decoded and displayed a frame: change player state and emit events if required
          switch (player.state) {
            case "play":
            case "waiting": {
              if (!player.dropping) {
                emitEvent("canplay");
                emitEvent("playing");
                player.state = "playing";
                if (!player.vars.wantToPlay) {
                  MistVideo.player.send({type:"hold"});
                }
              }
              break;
            }
            case "seeking": {
              var t = player.frames.frame2time(player.frames.decoded + player.frames.dropped);
              if (t >= player.vars.seekTo) {
                emitEvent("seeked");
                player.vars.seekTo = null;
                player.state = "playing";
                if (!player.vars.wantToPlay) {
                  emitEvent("timeupdate");
                  MistVideo.player.send({type:"hold"});
                }
              }
              break;
            }
            default: {
              player.state = "playing";
            }
          }
 
          //console.log("pending",player.frames.behind());
          
          for (var i in onDecode) {
            onDecode[i]();
          }

        });
      }
      init_decoder();

      /*
       * infoBytes:
       * start - length - meaning
       * 0       1        track index
       * 1       1        if == 1 ? keyframe : normal frame
       * 2       8        timestamp (when frame should be sent to decoder) [ms]
       * 9       2        offset (when frame should be outputted compared to timestamp) [ms]
       * */
      function isKeyFrame(infoBytes) {
        return !!infoBytes[1];
      }
      function toTimestamp(infoBytes) { //returns timestamp in ms
        var v = new DataView(new ArrayBuffer(8));
        for (var i = 0; i < 8; i++) {
          v.setUint8(i,infoBytes[i+2]);
        }
        //return v.getBigInt64(); 
        return v.getInt32(4); //64 bit is an issue in browsers apparently, but we can settle for a 32bit integer that rolls over
      }

      function connect(){
        emitEvent("loadstart");

        //?buffer=0 ensures real time sending
        //?video=hevc,|minbps selects the lowest bitrate hevc track
        var url = MistUtil.http.url.addParam(MistVideo.source.url,{buffer:0,video:"hevc,|minbps"}); 

        var socket = new WebSocket(url);
        MistVideo.player.ws = socket;
        socket.binaryType = "arraybuffer";
        function send(obj) {
          if (!MistVideo.player.ws) { throw "No websocket to send to"; }
          if (socket.readyState == 1) {
            socket.send(JSON.stringify(obj));
          }
          return false;
        }
        MistVideo.player.send = send;
        socket.wasConnected = false;
        socket.onopen = function(){
          if (!MistVideo.player.built) {
            MistVideo.player.built = true;
            callback(ele); 
          }
          send({type:"request_codec_data",supported_codecs:["HEVC"]});
          socket.wasConnected = true;
        }
        socket.onclose = function(){
          if (this.wasConnected && (!MistVideo.destroyed) && (MistVideo.state == "Stream is online")) {
            MistVideo.log("Raw over WS: reopening websocket");
            connect(url);
          }
          else {
            MistVideo.showError("Raw over WS: websocket closed");
          }
        }
        socket.onerror = function(e){
          MistVideo.showError("Raw over WS: websocket error");
        };
        socket.onmessage = function(e){
          //console.log(new Uint8Array(e.data));
          if (typeof e.data == "string") {
            var d = JSON.parse(e.data);
            switch (d.type) {
              case "on_time": {
                //console.log("received",MistUtil.format.time(d.data.current*1e-3));

                player.vars.paused = false;

                player.frames.history.add();
                
                if (player.vars.duration != d.data.end*1e-3) {
                  player.vars.duration = d.data.end*1e-3;
                  emitEvent("durationchange");
                }

                break;
              }
              case "seek": {
                //MistVideo.player.decoder.reset(); //should be used when seeking, but makes things worse, honestly
                MistVideo.player.frames.timestamps = {};
                if (MistVideo.player.dropping) { 
                  MistVideo.log("Emptying drop queue for seek");
                  MistVideo.player.frames.dropped += MistVideo.player.dropping.length;
                  MistVideo.player.dropping = []; 
                }
                break;
              }
              case "codec_data": {
                emitEvent("loadedmetadata");
                send({type:"play"});
                player.state = "play";
                break;
              }
              case "info": {
                var tracks = MistVideo.info.meta.tracks;
                var track;
                for (var i in tracks) {
                  if (tracks[i].idx == d.data.tracks[0]) {
                    track = tracks[i];
                    break;
                  }
                }
                if ((typeof track != undefined) && (track.fpks > 0)) {
                  player.frames.rate_theoretical = track.fpks*1e-3;
                }
                break;
              }
              case "pause": {
                player.vars.paused = d.paused;
                if (d.paused) {
                  player.decoder.flush(); //push last 6 frames through
                  emitEvent("pause");
                }
                break;
              }
              case "on_stop": {
                if (player.state == "ended") { return; }
                player.state = "ended";
                player.vars.paused = true;
                socket.onclose = function(){}; //don't reopen websocket, just close, it's okay, rly
                socket.close();
                player.decoder.flush(); //push last 6 frames through
                emitEvent("ended");

                break;
              }
              default: {
                //console.log("ws message",d.type,d.data);
              }
            }
          }
          else {

            player.frames.received++;
            player.frames.bitsReceived += e.data.byteLength*8;

            var l = 12;
            var infoBytes = new Uint8Array(e.data.slice(0,l));
            //console.log(infoBytes);

            var data = new Uint8Array(e.data.slice(l,e.data.byteLength)); //actual raw h265 frame

            player.frames.timestamps[player.frames.received] = toTimestamp(infoBytes);

            function prepare(data,infoBytes) {

              //to avoid clogging the websocket onmessage, process the frame asynchronously
              setTimeout(function(){

                if (player.dropping) {
                  //console.log(player.frames.behind(),player.dropping.length);
                  if (player.state != "waiting") {                  
                    emitEvent("waiting");
                    player.state = "waiting";
                  }

                  if (isKeyFrame(infoBytes)) {
                    if (player.dropping.length) {
                      player.frames.dropped += player.dropping.length;
                      MistVideo.log("Dropped "+player.dropping.length+" frames");
                      player.dropping = [];
                    }
                    else {
                      MistVideo.log("Caught up! no longer dropping");
                      player.dropping = false;

                    }
                  }
                  else {
                    player.dropping.push([infoBytes,data]);
                    if (!decoder.decoding) {
                      var d = player.dropping.shift();
                      MistVideo.player.process(d[1],d[0]);
                    }
                    return;
                  }
                }
                else {
                  if (player.frames.behind() > 20) {
                    //enable dropping
                    player.dropping = [];
                    MistVideo.log("Falling behind, dropping files..");
                  }
                }

                MistVideo.player.process(data,infoBytes);

              },0);
            }

            prepare(data,infoBytes);
          }
        }

        socket.listeners = {}; //kind of event listener list for websocket messages
        socket.addListener = function(type,f){
          if (!(type in this.listeners)) { this.listeners[type] = []; }
          this.listeners[type].push(f);
        };
        socket.removeListener = function(type,f) {
          if (!(type in this.listeners)) { return; }
          var i = this.listeners[type].indexOf(f);
          if (i < 0) { return; }
          this.listeners[type].splice(i,1);
          return true;
        }

      }
      MistVideo.player.connect = connect;
      MistVideo.player.process = function(data,infoBytes){

        //add to the decoding queue
        decoder.decoding = true;
        var err = decoder.push_data(data);

        if (player.state == "play") {
          emitEvent("loadeddata");
          player.state = "waiting";
        }

        if (player.vars.wantToPlay && (player.state != "seeking")) {
          emitEvent("progress");
        }

        function onerror(err) {
          if (err == 0) { return; }
          if (err == libde265.DE265_ERROR_WAITING_FOR_INPUT_DATA) {
            //emitEvent("waiting");
            player.state = "waiting";
            //do nothing, we'll decode again when we get the next data message
            return;
          }
          if (!libde265.de265_isOK(err)) {
            //console.warn("decode",libde265.de265_get_error_text(err));
            ele.error = "Decode error: "+libde265.de265_get_error_text(err);
            emitEvent("error");
            return true; //don't call decoder.decode();
          }
        }

        if (!onerror(err)) {
          decoder.decode(onerror);
        }
        else {
          decoder.free();
        }

      }

      connect(); 
    }

    init();
    
    //redirect properties
    //using a function to make sure the "item" is in the correct scope
    function reroute(item) {
      Object.defineProperty(MistVideo.player.api,item,{
        get: function(){ return player.vars[item]; },
        set: function(value){
          return player.vars[item] = value;
        }
      });
    }
    var list = [
      "duration"
      ,"paused"
      ,"error"
    ];
    for (var i in list) {
      reroute(list[i]);
    }
    
    api.play = function(){
      return new Promise(function(resolve,reject){
        player.vars.wantToPlay = true;
        var f = function(){
          resolve();
          MistVideo.player.decoder.removeListener(f);
        };
        MistVideo.player.decoder.addListener(f);

        if (MistVideo.player.ws.readyState > MistVideo.player.ws.OPEN) {
          //websocket has closed
          MistVideo.player.connect();
          MistVideo.log("Websocket was closed: reconnecting to resume playback");
          return;
        }
        if (api.paused) MistVideo.player.send({type:"play"});
        player.state = "play";
      });
    };
    api.pause = function(){
      player.vars.wantToPlay = false;
      MistVideo.player.send({type:"hold"});
    };

    MistVideo.player.api.unload = function(){
      //close socket
      if (MistVideo.player.ws) {
        MistVideo.player.ws.onclose = function(){};
        MistVideo.player.ws.close();
      }

      if (MistVideo.player.decoder) {
        //prevent adding of new data
        MistVideo.player.decoder.push_data = function(){};
        //free decoder
        MistVideo.player.decoder.flush();
        MistVideo.player.decoder.free();
      }
    }
    MistVideo.player.setSize = function(size){
      ele.style.width = size.width+"px";
      ele.style.height = size.height+"px";
    };
    
    //override seeking
    Object.defineProperty(MistVideo.player.api,"currentTime",{
      get: function(){
        var n = player.frames.decoded + player.frames.dropped;
        if (player.state == "seeking") { return player.vars.seekTo; } 
        if (n in player.frames.timestamps) { return player.frames.frame2time(n); }
        return 0;
      },
      set: function(value){
        emitEvent("seeking");
        player.state = "seeking";
        player.vars.seekTo = value;
        MistVideo.player.send({type:"seek", seek_time: value*1e3});
        //player.frames.timestamps[player.frames.received] = value; //set currentTime to value
        return value;
      }
    });

    //show the difference between decoded frames and received frames as the buffer
    Object.defineProperty(MistVideo.player.api,"buffered",{
      get: function(){
        return {
          start: function(i){
            if (this.length && i == 0) {
              return player.frames.frame2time(player.frames.decoded + player.frames.dropped); 
            }
          },
          end: function(i){
            if (this.length && i == 0) {
              return player.frames.frame2time(player.frames.received);
            }
          },
          length: player.frames.received - player.frames.decoded > 0 ? 1 : 0
        };
      }
    });

    //loop
    if (MistVideo.info.type != "live") {
      MistUtil.event.addListener(ele,"ended",function(){
        if (player.api.loop) {
          player.api.play();
          player.api.currentTime = 0;
        }
      });
    }
    
  }
  
  if ("libde265" in window) {
    this.onDecoderLoad();
  }
  else {
    var scripttag = MistUtil.scripts.insert(MistVideo.urlappend(mistplayers.rawws.scriptsrc(MistVideo.options.host)),{
      onerror: function(e){
        var msg = "Failed to load H265 decoder";
        if (e.message) { msg += ": "+e.message; }
        MistVideo.showError(msg);
      },
      onload: MistVideo.player.onDecoderLoad
    },MistVideo);
  }
}
