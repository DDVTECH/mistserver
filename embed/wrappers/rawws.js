mistplayers.rawws = {
  name: "WebCodec Player (beta)",
  mimes: ["ws/video/raw"],
  priority: MistUtil.object.keys(mistplayers).length + 1,
  isMimeSupported: function (mimetype) {
    return (MistUtil.array.indexOf(this.mimes,mimetype) == -1 ? false : true);
  },
  isBrowserSupported: function (mimetype,source,MistVideo) {
    if (!("WebSocket" in window) || (MistVideo.info.capa && !MistVideo.info.capa.ssl)) {
      MistVideo.log("This player requires websocket support");
      return false;
    }
    if (!window.VideoDecoder || !window.AudioDecoder || (!window.MediaStreamTrackGenerator && !window.AudioWorkletNode)) {
      //unavailable at http
      return false; 
    }
    if (!window.Worker) return false;

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
    //we can actually test if track codecs will work.. but only in a promise
    //if we have them cached, good! we can return that. otherwise..
    //for now, assume all codecs are available
    //we will perform the actual testing in player buildup and nextCombo if it's bad
    var cache = {};
    if (this.cacheSupported && (MistVideo.stream in this.cacheSupported)) {
      cache = this.cacheSupported[MistVideo.stream];
    }
    else {
      this.cacheSupported[MistVideo.stream] = {};
    }
    if (MistVideo.info && MistVideo.info.meta && MistVideo.info.meta.tracks) {
      var playabletracks = {};
      for (var i in MistVideo.info.meta.tracks) {
        var track = MistVideo.info.meta.tracks[i];
        switch (track.type) {
          case "audio":
          case "video": {
            if (track.idx in cache) {
              if (cache[track.idx]) playabletracks[track.type] = true;
            }
            else {
              //we haven't actually tested yet if this track will play - but assume it will
              playabletracks[track.type] = "maybe";
            }
            break;
          }
          case "meta": {
            if (track.codec == "subtitle") playabletracks.subtitle = true;
            break;
          }
        }
      }
      this.cacheSupported[MistVideo.stream].last = playabletracks;
      return Object.keys(playabletracks);
    }
    return []; //there are no tracks in the metadata..
  },
  player: function(){
    this.onreadylist = [];
  },
  scriptsrc: function(host) { return host+"/webcodecsworker.js"; },
  cacheSupported: {
    //<streamname>: { last: <playabletracks>, trackId: true/false }
  },
  isTrackSupported: function(track){
    function str2bin(str) {
      //the init data received is a string, because it is read from the stream info json
      //it is raw asci, except where the value could not be encoded as asci, where it is the escaped char code instead
      var out = new Uint8Array(str.length);
      for (i = 0; i < str.length; i++) {
        out[i] = str.charCodeAt(i);
      }
      return out;
    }

    var decoder;
    var config = {
      codec: track.codecstring ? track.codecstring : (""+track.codec).toLowerCase()
    }
    if (track.init != "") {
      config.description = str2bin(track.init);
    }
    switch (track.type) {
      case "video": {
        switch (track.codec) {
          case "JPEG": {
            if (!window.ImageDecoder) {
              if (window.createImageBitmap) {
                return new Promise(function(resolve,reject){
                  resolve({
                    supported: true,
                    config: { codec: "image/jpeg" }
                  }); 
                });
              }
              else {
                return new Promise(function(resolve,reject){
                  resolve({
                    supported: false,
                    config: { codec: "image/jpeg" }
                  }); 
                });
              }
            }
            decoder = ImageDecoder;
            decoder.isConfigSupported = function(){
              return new Promise(function(resolve,reject){
                decoder.isTypeSupported("image/jpeg").then(function(isSupported){
                  resolve({
                    supported: isSupported,
                    config: { codec: "image/jpeg" }
                  });
                }).catch(reject);
              }) 
            };
            break;
          }
          case "UYVY":
          case "YUYV":
          case "NV12": {
            return new Promise(function(resolve,reject){
              function test(){
                try {
                  //attempt to create the smallest possible NV12 frame
                  new VideoFrame(new Uint8Array(6),{
                    format: "NV12",
                    codedWidth: 2,
                    codedHeight: 2,
                    timestamp: 0,
                    layout: [
                      { offset: 0, stride: 2 }, // Y: 2x2
                      { offset: 4, stride: 2 }  // UV: 2x2
                    ]
                  }).close();
                  return true;
                } catch (e) {
                  return false;
                }
              }

              resolve({
                supported: test(),
                config: { codec: "video/"+track.codec.toLowerCase() }
              })
            });
            break;

          }
          default: {
            decoder = VideoDecoder;
          }
        }
        break;
      }
      case "audio": {
        switch (track.codec) {
          case "PCM": {
            track.codecstring = "pcm";
            switch (track.size) {
              case 32: { track.codecstring += "-s32"; break; }
              case 24: { track.codecstring += "-s24"; break; }
              case 16: { track.codecstring += "-s16"; break; }
              case 8:  { track.codecstring += "u8";   break; }
            }
            config.codec = track.codecstring;
            //no break
          }
          default: {
            decoder = AudioDecoder;
            config.numberOfChannels = track.channels;
            config.sampleRate = track.rate;
          }
        }
        break;
      }
      default: {
        return new Promise(function(resolve,reject){ reject(); });;
      }
    }
    return new Promise(function(resolve,reject){
      decoder.isConfigSupported(config).then(function(result){
        //if (MistVideo.) console.warn("WebCodecs player: isConfigSupported?",result.supported,result,result.config.description,track);
        resolve(result);
      }).catch(reject);
    });
  }
};
var p = mistplayers.rawws.player;
p.prototype = new MistPlayer();
p.prototype.build = function (MistVideo,callback) {
  var main = this;

  var video = document.createElement("video");

  var debugging;
  //debugging = true;
  //debugging = "verbose"; //logs all parsed chunks

  Object.defineProperty(this,"debugging",{
    get: function(){ return debugging; },
    set: function(v){
      if (v !== debugging) {
        debugging = v;
        if (this.controller && this.controller.worker) {
          this.controller.worker.post({
            type: "debugging",
            value: v
          });
        }
      }
      return debugging;
    }
  });
  this.debugging = debugging;

  if (!window.MediaStreamTrackGenerator) {
    /* for Firefox, and Safari audio */
    var canvas = false;
    var PolyMediaStreamTrackGenerator = function(options) {
      var kind = options.kind;

      if (kind == "video") {
        if (canvas) {
          if (canvas.parentNode) canvas.parentNode.removeChild(canvas);
        }
        else {
          canvas = document.createElement("canvas");
          canvas.classList.add("MediaStreamTrackGenerator");
          canvas.style.imageRendering = "pixelated";
          canvas.style.position = "absolute";
          //canvas.style.filter = "grayscale(1)";
        }

        MistVideo.video.after(canvas);
        var ctx = canvas.getContext("2d", {desynchronized: true});
        var track = canvas.captureStream().getVideoTracks()[0];
        //console.warn("Track created!",track);
        //track.onmute = () => console.warn("Track went silent!");
        //track.onended = () => console.error("Track died!");
        var scale, x, y;
        track.writable = new WritableStream({
          write: function(frame) {

            if ((canvas.width != canvas.parentNode.clientWidth) || (canvas.height != canvas.parentNode.clientHeight) || (typeof scale == "undefined")) {
              canvas.width = canvas.parentNode.clientWidth;
              canvas.height = canvas.parentNode.clientHeight;

              scale = Math.min(canvas.width / frame.displayWidth, canvas.height / frame.displayHeight);
              x = (canvas.width - frame.displayWidth * scale) / 2;
              y = (canvas.height - frame.displayHeight * scale) / 2;
            }

            ctx.drawImage(frame, x, y, frame.displayWidth * scale, frame.displayHeight * scale);
            frame.close();

            return;
          }
        });
        return track;
      }
      else if (kind == "audio") {
        var ac = new AudioContext({
          latencyHint: "interactive",
          sampleRate: options.track.rate
        });

        var dest = ac.createMediaStreamDestination();
        var track = dest.stream.getAudioTracks()[0];

        function toArrayBuffer(audioData){
          var opts = {
            planeIndex: 0,
            format: audioData.format
          };
          var a =  new Float32Array(audioData.allocationSize(opts) / 4); // divide by 4 because allocationSize is in bytes (8 bit) and the Float32Array is 32 bit (32 / 8 = 4)
          audioData.copyTo(a,opts);

          if (options.track.codec == "PCM") {
            var int32View = new Int32Array(a.buffer);
            var normalized = new Float32Array(int32View.length);
            for (let i = 0; i < int32View.length; i++) {
              normalized[i] = int32View[i] / 2147483648;
            }
            return normalized;
          }
          return a;
        }

        track.writable = new WritableStream({
          start: function(controller) {
            var me = this;
            me.arrays = [];
            var workletCode = 'function worklet() {'+
            '  registerProcessor("mstg-shim", class Processor extends AudioWorkletProcessor {'+
            '    constructor() {'+
            '      super();'+
            '      this.arrays = [];'+
            '      this.arrayOffset = 0;'+
            '      this.port.onmessage = (e) => { this.arrays.push(new Float32Array(e.data)); };'+
            '      this.emptyArray = new Float32Array(0);'+
            '    }'+
            '    process(inputs, [outputs]) {'+
            '      for (let i = 0; i < outputs[0].length; i++) {'+
            '        for (let j = 0; j < outputs.length; j++) {'+
            '          if (!this.array || this.arrayOffset >= this.array.length) {'+
            '            this.array = this.arrays.shift() || this.emptyArray;'+
            '            this.arrayOffset = 0;'+
            '          }'+
            '          outputs[j][i] = this.array[this.arrayOffset++] || 0;'+
            '        }'+
            '      }'+
            '      return true;'+
            '    }'+
            '  });'+
            '}';
            return ac.audioWorklet.addModule("data:text/javascript,("+workletCode+")()").then(function() {
              me.node = new AudioWorkletNode(ac,"mstg-shim",{
                outputChannelCount: [options.track.channels]
              });
              me.node.connect(dest);
              return track;
            });
          },
          write: function(audioData) {
            if (audioData.numberOfFrames == 0) throw "AudioData is empty - 0 frames";
            var a = toArrayBuffer(audioData);
            this.node.port.postMessage(a, [a.buffer]);
            audioData.close();
          }
        });
        return track;
      }
    };
  }

  var supportedCodecs = [];

  function RawPlayer(url) {
    var controller = this;
    this.connecting = false;

    this.createControlChannel = function() {
      this.control = new MistUtil.shared.ControlChannel(function(){
        var ws = new WebSocket(url);
        ws.binaryType = "arraybuffer";
        return ws;
      },MistVideo);
      this.control.lock(); //lock control channel for api commands
      this.connection = this.control;

      this.control.addListener("channel_timeout").then(function(){
        MistVideo.log("Control channel timeout - try next combo","error");
        MistVideo.nextCombo(undefined,"control channel timeout");
      });
      this.control.addListener("channel_error").then(function(){
        if (controller.control.was_connected) {
          MistVideo.log("Attempting to reconnect control channel");
          controller.createControlChannel();
        }
      });
      //live passthrough of the debugging flag
      Object.defineProperty(this.control,"debugging",{
        get: function(){
          return main.debugging; 
        }
      });

      //add listeners
      var control = this.control;
      var requestingMoreBuffer = false;
      this.control.addListener("on_time",function(msg){
        //does the message mention any tracks we dont have a pipeline for?
        for (var i in msg.tracks) {
          var tid = msg.tracks[i];
          if (!(tid in controller.pipelines)) {
            //we don't have a pipeline for this track yet - set it up
            if (main.debugging) console.warn("▶️","Received on_time with track "+tid+" but there is no pipeline for it, attempting to create it");
            var p = new Pipeline(tid);
          }
        }
        //are there any pipelines that don't have a track?
        //close the pipeline when the display queue empties
        if (msg.tracks) {
          for (var tid in controller.pipelines) {
            if (msg.tracks.indexOf(Number(tid)) < 0) {
              controller.pipelines[tid].close(true);
            }
          }
        }

        //does the current time change, and do we have pipelines with a waiting timer? reset those
        var last = controller.frameTiming.server;
        if (last && (msg.current != last.current)) {
          for (var tid in controller.pipelines) {
            var pipeline = controller.pipelines[tid];
            if (pipeline.waitingTimer) {
              clearTimeout(pipeline.waitingTimer);
              pipeline.waitingTimer = false;
            }
          }
        }

        //buffer management
        /*var bounds = { // if the buffer <> desiredBuffer*bounds[low,high], take action
          low: 0.6,
          high: 2
        };
        var tweaks = { //action to take when the bounds are reached
          faster: 1.05,
          slower: 0.98
        }
        var buffer = null;
        var playbackPoint = controller.frameTiming.out;
        var receivePoint = controller.frameTiming.in;
        var decodePoint = controller.frameTiming.decoded;
        if (playbackPoint && decodePoint) { 
          buffer = Math.round(decodePoint*1e-3 - playbackPoint*1e-3); //in ms
          decodingTime = Math.round(receivePoint*1e-3 - decodePoint*1e-3); //in ms
        }

        if ((buffer !== null) && !controller.frameTiming.seeking && !requestingMoreBuffer) { //if the buffer is known, and we're not in the middle of a seek or additional data request
          var desiredBuffer = controller.desiredBuffer();

          if (main.debugging) {
            var args = [
              "▶️",
              "Buffer:",
              function(){
                if (buffer > desiredBuffer*bounds.high) { return "⬆️"; }
                if (buffer < desiredBuffer*bounds.low)  { return "⬇️"; }
                return "🟢";
              }(),
              buffer,"/",desiredBuffer,
              { 
                keepAway: keepAway,
                serverDelay: Math.round(controller.control.serverDelay.get()),
                jitter: Math.round(controller.jitter.get())
              },
              "Decoding time:",Math.round(Math.max.apply(null,Object.values(main.api.decodeTime))),
              "Decoding queues:",Math.max.apply(null,Object.values(main.api.decodeQueue)),
              "Earliness:",Math.round(Math.min.apply(null,Object.values(main.api.earliness))),
              "Server jitter:",msg.jitter,
              "Available:",msg.end - msg.current, 
              "Tweak:",
              function(t){
                if (t > 1) { return "↗️"; }
                if (t < 1) { return "↘️"; }
                return "🟢";
              }(controller.frameTiming.speed.tweak),
              controller.frameTiming.speed.tweak
            ];
            if (MistVideo.info.type == "live") {
              args.unshift("From live:",Math.round(msg.end*1e-2 - playbackPoint*1e-5)/10,"s");
            }
            console.log.apply(null,args);
          }

          if ((buffer < desiredBuffer*bounds.low) && (msg.play_rate_curr != "fast-forward") && (controller.frameTiming.speed.tweak >= 1)) {

            var underflow = 0;
            if (buffer < 10) {
              //if the buffer is empty, there may also be an underflow on the decoder output -> trackwriter timer
              //aka, the decoded packets are shown in the video element too late
              //add this lateness to the extra buffer we're requesting
              var early = main.api.earliness;
              if (early) underflow = -1*Math.min.apply(null,Object.values(early));
              underflow = Math.max(0,underflow);
            }

            if (msg.current < msg.end) {
              requestingMoreBuffer = true;
              //request more data
              control.send({
                type: "fast_forward",
                ff_add: desiredBuffer + underflow
              });
              if (controller.frameTiming.speed.tweak > 1) {
                controller.frameTiming.tweakSpeed(1);
              }
              MistVideo.log("Our buffer ("+buffer+"ms) is small (<"+Math.round(desiredBuffer*bounds.low)+"ms), requesting more data (+"+Math.round(desiredBuffer + underflow)+"ms)..");
              //test if we received enough data
              var gotsetspeed = false;
              control.addListener("set_speed").then(function(m){
                gotsetspeed = true;
                if (m.play_rate_prev == "fast-forward") {
                  control.addListener("on_time").then(function(m){
                    var newbuffer = 0;
                    var playbackPoint = controller.frameTiming.out;
                    var decodePoint = controller.frameTiming.in;
                    if (playbackPoint && decodePoint) { 
                      newbuffer = Math.round(decodePoint*1e-3 - playbackPoint*1e-3); //in ms
                    }
                    var increase = m.current - msg.current - (m._received - msg._received);
                    if (main.debugging) console.warn("▶️","Extra buffer received:",m.current - msg.current,"ms","Time taken:",m._received - msg._received,"ms","Increase:",increase,"ms");
                    if (buffer + increase < desiredBuffer*bounds.low) {
                      controller.frameTiming.tweakSpeed(tweaks.slower);
                      keepAway += 100;
                      MistVideo.log("Didn't receive enough extra data to increase our buffer ("+increase+"/"+Math.round(desiredBuffer*bounds.low - buffer)+"ms): slowing down..");
                      //once slowed down, the fast_forward request code will not trigger
                      //it may be tried again if the buffer shrinks again after playback speed returned to 1
                    }
                    else {
                      MistVideo.log("Received +"+increase+"ms extra data")
                    }
                    requestingMoreBuffer = false;
                  });
                }
                else {
                  //eh? reset
                  requestingMoreBuffer = false;
                }
              });
              //it's possible we don't receive a set_speed answer - in that case there is no extra data available
              control.addListener("on_time").then(function(m){
                if (gotsetspeed) return;

                if (requestingMoreBuffer && (m.play_rate_curr != "fast-forward")) {
                  requestingMoreBuffer = false;
                  controller.frameTiming.tweakSpeed(tweaks.slower);
                  keepAway += 100;
                  MistVideo.log("Didn't receive extra data: slowing down..");
                }
              });
            }
            else { //(msg.current >= msg.end)
              if (controller.frameTiming.speed.main > 1) {
                //if main playback speed is faster than real time, reset it to 1
                //controller.frameTiming.setSpeed(1,tweaks.slower);
                control.send({type:"set_speed",play_rate:"auto"});
              }
              controller.frameTiming.tweakSpeed(tweaks.slower);
              MistVideo.log("Our buffer ("+buffer+"ms) is small (<"+Math.round(desiredBuffer*bounds.low)+"ms), but can't request more data: slowing down..");
            }
          }
          else {
            if ((controller.frameTiming.speed.tweak < 1) && (buffer >= desiredBuffer)) {
              controller.frameTiming.tweakSpeed(1);
              MistVideo.log("Our buffer ("+buffer+"ms) is large enough (>"+Math.round(desiredBuffer)+"ms), so return to normal playback.");
            }
            else {
              if ((MistVideo.info.type == "live") && (MistVideo.options.liveCatchup)) { //in an else to prevent sending fast_forward more than once

                //if the buffer is large, tweak playback speed to catch up
                if ((msg.play_rate_curr == "auto") && controller.frameTiming) {

                  //do not try to buffer less than the maximum frame duration (e.g. for JPEG tracks): no need to speed up if there is no next frame
                  var max_frame_duration = 0; //in microseconds
                  for (var i in controller.pipelines) {
                    max_frame_duration = Math.max(max_frame_duration,controller.pipelines[i].stats.frame_duration);
                  }
                  desiredBuffer = Math.max(desiredBuffer,max_frame_duration*1e-3);


                  if ((controller.frameTiming.speed.tweak <= 1) && (buffer > desiredBuffer*bounds.high)) {
                    controller.frameTiming.tweakSpeed(tweaks.faster);
                    MistVideo.log("Our buffer ("+buffer+"ms) is big (>"+Math.round(desiredBuffer*bounds.high)+"ms), so tweak the playback speed to catch up.");

                  }
                  else if ((controller.frameTiming.speed.tweak > 1) && (buffer <= desiredBuffer)) {
                    controller.frameTiming.tweakSpeed(1);
                    MistVideo.log("Our buffer ("+buffer+"ms) is small enough (<"+Math.round(desiredBuffer)+"ms), so return to normal playback.");
                  }
                }

                //live catchup
                if (msg.play_rate_curr != "fast-forward") {
                  var distanceToLive = msg.end - msg.current;
                  if (
                    (distanceToLive < MistVideo.options.liveCatchup*1e3)  // we're within a minute of the live point
                    && (distanceToLive > Math.max(msg.jitter*1.1,msg.jitter+250)) // the current (download) timestamp is more than jitter*1.1 and jitter+250 away from the live point
                    && (buffer-desiredBuffer < 1e3) // our buffer is less than a second larger than the desired buffer size
                  ) {
                    control.send({
                      type: "fast_forward",
                      ff_add: 5e3 //request an additional 5 seconds of data
                    });
                    MistVideo.log("We're away ("+(distanceToLive)+"ms) from the live point, requesting more data..");
                  }
                }
              }
            }
          }
        }*/

        controller.frameTiming.server = msg;
      });
      this.control.addListener("set_speed",function(msg){
        var speed;
        switch (msg.play_rate_curr) {
          case "auto":
          case "fast-forward": {
            //MistServer is controlling the playback rate
            //fast forwards means it is speeding now but it will return to auto once the requested position is reached (after seeking)
            speed = 1;
            break;
          }
          default: {
            speed = msg.play_rate_curr;
            break;
          }
        }
        controller.frameTiming.setSpeed(speed);
        controller.jitter.setSpeed(msg.play_rate_curr);
      });
      this.control.addSendListener("seek",function(msg){ 
        var seekTo = msg.seek_time; //[ms]
        controller.worker.post({type:"seek",seek_time:seekTo});
        MistUtil.event.send("seeking",seekTo*1e-3,video);
        controller.jitter.reset();

        if (main.debugging) console.warn("▶️","Seeking to ["+MistUtil.format.time(seekTo*1e-3)+"]: Emptying decoding and display queues");

      });
      this.control.addListener("pause",function(msg){
        if (msg.paused) controller.frameTiming.paused = true;
      });
      this.control.addSendListener("play",function(){
        //cancel frame timing - start playing at normal speed from the first queued packet
        controller.frameTiming.reset();
      });
      return this.control;
    }
    this.control = this.createControlChannel();

    function Differentiate(get_func) {
      var lastval, lasttime;
      this.get = function(){
        var newval = get_func();
        var now = performance.now();
        var out;
        if (typeof lastval != "undefined") {
          var dx = newval - lastval;
          var dt = now - lasttime; //[ms]
          dt *= 1e-3; //[s] 
          out = dx/dt;
        }
        lastval = newval;
        lasttime = now;
        return out;
      }
    };
    function FrameTracker() {
      var pending = {};
      this.complete = [];
      this.last_in = undefined;
      this.last_out = undefined;
      this.shift_array = [];

      this.start = function(timestamp){
        this.last_in = timestamp;
        pending[timestamp] = performance.now();
      };
      this.end = function(timestamp){
        this.last_out = timestamp;
        var shift = 0;
        if (!(timestamp in pending)) {
          //the timestamp has changed: assume first in, first out
          timestamp = function(a){
            for (var i in a) return i;
            return undefined;
          }(pending);
          if (timestamp === undefined) return;
          shift = timestamp - this.last_out;
        }
        this.complete.push(performance.now() - pending[timestamp]);
        this.shift_array.push(shift);
        delete pending[timestamp];

        while (this.complete.length > 16) {
          this.complete.shift();
        }
        while (this.shift_array.length > 8) {
          this.shift_array.shift();
        }
      };
      this.getCopy = function(){
        return {
          complete: this.complete,
          last_in: this.last_in,
          last_out: this.last_out,
          shift_array: this.shift_array
        };
      }
      Object.defineProperty(this,"delay",{
        get: function(){
          if (this.complete.length) {
            return this.complete.reduce(function(partialsum,a){ return partialsum + a; }) / this.complete.length; //[ms]
          }
          return undefined;
        }
      });
      Object.defineProperty(this,"delta",{
        get: function(){
          if (this.last_out !== undefined) return this.last_in - this.last_out; //[microseconds]
          return undefined;
        }
      });
      Object.defineProperty(this,"shift",{
        get: function(){
          if (this.shift_array.length) {
            return this.shift_array.reduce(function(partialsum,a){ return partialsum + a; }) / this.shift_array.length; //[microseconds]
          }
          return undefined;
        }
      });
    }
    function RawChunk(data) {
      /* infoBytes:
       * start - length - meaning
       * 0       1        track index
       * 1       1        if == 1: keyframe 2: init data else (0): normal frame
       * 2       8        (uint) timestamp (when frame should be sent to decoder) [ms]
       * 10      2        (int)  offset (when frame should be outputted compared to timestamp) [ms]
       * */


      var l = 12; //length of the info bytes at the beginning of the data
      var info = new Uint8Array(data.slice(0,l)); //the info bytes

      this.data = new Uint8Array(data.slice(l,data.byteLength)); //the actual data

      this.track = info[0];
      this.type = function(b) {
        switch (b) {
          case 1: return "key";
          case 2: return "init";
          default: return "delta";
        }
      }(info[1]);
      this.timestamp = function(infoBytes) {
        var v = new DataView(infoBytes.slice(2,10).buffer);
        return Number(v.getBigUint64()); //getBigUint64 returns a big int, but while that is < 2^53 - 1, it can be converted to a "normal" Number
        //return v.getUint32(4); //64 bit hard, but we can settle for a 32bit integer that rolls over
      }(info);


      this.offset = function(infoBytes) {
        //return infoBytes[10]*256+infoBytes[11];
        return new DataView(infoBytes.slice(10,12).buffer).getInt16(); 
      }(info);
      
      if (main.debugging == "verbose") console.log("▶️","Parsed binary data received from WS:",MistUtil.format.time((this.timestamp + this.offset)*1e-3,{ms:true}),this);

    }
    function getTrack(trackIndex) {
      if (!MistVideo.info || !MistVideo.info.meta || !MistVideo.info.meta.tracks) return;
      for (var i in MistVideo.info.meta.tracks) {
        var track = MistVideo.info.meta.tracks[i];
        if (track.idx == trackIndex) {
          return track;
        }
      }
      throw "Missing metadata for track "+trackIndex;
    }

    this.data_queue = [];

    function JitterTracker(){
      let chunks = [];    //sliding window with data of 8 chunks (timestamp and received time only)
      let speed = 1;      //requested download speed
      let lastcalced = 0; //when the last peak was added to the log 
      let curJitter = 0;  //maximum jitter calculted during the last second
      let peaks = [];     //sliding window of previous 8 measurements of curJitter
      let maxJitter = 120; //intialize at 120ms, will change based on measured jitter (increase immediately, decrease slowly)
      this.addChunk = function(chunk) { //add all chunks
        chunks.push([
          performance.now(),
          chunk.timestamp
        ]);
        if (chunks.length > 8) chunks.shift();

        let jitter = this.calcJitter(); //calculate the jitter
        if (jitter > curJitter) curJitter = jitter; //curjitter is the max

        if (performance.now() > lastcalced + 1e3) { //every second, save the current peak jitter and reset
          peaks.push(curJitter);
          if (peaks.length > 8) peaks.shift();

          let maxPeak = Math.max.apply(null,peaks);
          let avgPeak = peaks.reduce(function(partialsum,a){ return partialsum + a; }) / peaks.length;
          let weighted = (avgPeak + maxPeak * 2) / 3 + 1;
          //limit lowering to max 250ms per step
          if (maxJitter > weighted + 500) { weighted = maxJitter - 500; }
          maxJitter = (maxJitter + weighted) / 2;

          curJitter = 0;
          lastcalced = performance.now();
        }
      }
      this.calcJitter = function(){ //calculate jitter based on the last 8 packets (if available)
        if (chunks.length <= 1) return 0;
        if (speed == "fast_forward") return 0;
        let oldest = chunks[0];
        let newest = chunks[chunks.length-1];
        let clock_time_passed = newest[0] - oldest[0]; //[ms]
        let media_time_passed = newest[1] - oldest[1]; //[ms]
        let jitter = media_time_passed/speed - clock_time_passed; //[ms]
        return Math.max(0,jitter);
      };
      this.get = function(){
        return maxJitter;
      }
      this.setSpeed = function(newspeed){
        if (speed != newspeed) {
          if (newspeed == "auto") newspeed = 1;
          speed = newspeed;
          this.reset();
        }
      };
      this.reset = function(){
        chunks = [];
      }
    }
    this.jitter = new JitterTracker();

    //handles incoming binary data (from the control channel) and inserts it into the appropriate pipeline
    this.receiver = function(data) {
      //var splitData = splitToInfoAndChunk(data);
      var chunk = new RawChunk(data)

      if (!(chunk.track in controller.pipelines)) {
        if (main.debugging) console.warn("▶️","Received data for track "+chunk.track+" but there is no pipeline for it, attempting to create it");
        new Pipeline(chunk.track);
      }

      var pipeline = controller.pipelines[chunk.track];


      if (chunk.type == "init") {
        pipeline.configure(chunk.data);
        return;
      }
      try {
        pipeline.receive({
          type: chunk.type,                                //Indicates if the chunk is a key chunk that does not rely on other frames for encoding. One of: "key" The data is a key chunk. "delta" The data is not a key chunk.
          timestamp: (chunk.timestamp + chunk.offset)*1e3, //An integer representing the timestamp of the video in microseconds.
          data: chunk.data                                 //An ArrayBuffer, a TypedArray, or a DataView containing the video data.
        });
        MistUtil.event.send("progress",null,video);
      }
      catch (err) {
        if (main.debugging) console.error("▶️","Error while decoding track "+chunk.track+" ("+pipeline.track.codec+" "+pipeline.track.type+")",chunk,err);
        MistVideo.log(err+" while decoding track "+chunk.track+" ("+pipeline.track.codec+" "+pipeline.track.type+")");
        pipeline.reset();
      }

      controller.jitter.addChunk(chunk);
    }

    this.pipelines = {}; //add a decoder pipeline for each track
    //add shortcuts for convenience: during trackswitch when there are multiple audio/video tracks, this will return the lowest index
    Object.defineProperty(this.pipelines,"audio",{
      enumerable: false,
      get: function(){
        for (var i in this) {
          if (this[i].track && this[i].track.type == "audio") { return this[i]; }
        }
      }
    });
    Object.defineProperty(this.pipelines,"video",{
      enumerable: false,
      get: function(){
        for (var i in this) {
          if (this[i].track && this[i].track.type == "video") { return this[i]; }
        }
      }
    });
    function WebCodecsWorker() {
      var self = this;

      function initWorker(){
        MistVideo.log("Loading WebCodecsWorker");
        return new Promise(function(resolve,reject){
          var url = mistplayers.rawws.scriptsrc(MistVideo.options.host);
          try {
            self.worker = new Worker(url);
            //console.warn("1: worker loaded");
          }
          catch(e) {
            //console.warn("2: worker failed to load",e);
            if (e.name == "SecurityError") {
              //SecurityError: Failed to construct 'Worker': Script at '<MistHost>/webcodecsworker.js' cannot be accessed from origin '<other domain or port>'.
              //The web worker will fail to load if the current webpage is at a different host than the web worker script. This can be mitigated by fetching the script and inserting the worker from a blob.

              //try fetching
              self.worker = "pending";
              //console.warn("3: fetching worker script");
              fetch(url).then(function(response){
                //console.warn("4: got worker script, reading");
                return response.text();
              }).then(function(text){
                //console.warn("5: injecting worker");
                var blob = new Blob([text],{type:"application/javascript"});
                try {
                  self.worker = new Worker(URL.createObjectURL(blob));
                  if (self.worker) resolve();
                  else (reject("Failed to load WebCodecsWorker after fetch"));
                }
                catch(e) { reject(e) }
              }).catch(reject);
            }
            else reject(e);
            return;
          }
          if (!self.worker) {
            //console.warn("no worker: rejecting");
            reject("Failed to load WebCodecsWorker");
            return;
          }
          resolve();
        });
      }
      var workerProm;
      this.workerReady = function(){
        return workerProm ? workerProm : new Promise(function(resolve,reject){
          if (self.worker) return resolve();
          reject();
        });
      };
      
      workerProm = initWorker().then(function(){
        workerProm = false;
        MistVideo.log("WebCodecsWorker was loaded succesfully");
        self.worker.onmessage = function(e){
          var msg = e.data;
          var key = [msg.type,msg.idx,msg.uid].join("_");

          if (msg.stats) {
            if (msg.stats.FrameTiming) {
              Object.assign(controller.frameTiming,msg.stats.FrameTiming);
            }
            if (msg.stats.pipelines) {
              for (var i in msg.stats.pipelines) {
                if (i in controller.pipelines) {
                  var p = controller.pipelines[i];
                  var stats = msg.stats.pipelines[i];
                  p.stats.early = stats.early;
                  p.stats.frame_duration = stats.frame_duration;
                  Object.assign(p.stats.frames,stats.frames);
                  Object.assign(p.stats.queues,stats.queues);
                  Object.assign(p.stats.timing.decoder,stats.timing.decoder);
                  Object.assign(p.stats.timing.writable,stats.timing.writable);
                }
              }
            }
          }

          var listened = false;
          if (key in self.listeners) {
            self.listeners[key](msg);
            if (msg.uid) delete self.listeners[key];
            listened = true;
          }
          key = [msg.type,msg.idx,""].join("_");
          if (key in self.listeners) {
            self.listeners[key](msg);
            listened = true
          }
          key = [msg.type,"",""].join("_");
          if (key in self.listeners) {
            self.listeners[key](msg);
            listened = true;
          }

          if (main.debugging) {
            switch (msg.type) {
              case "receive":
              case "writeframe":
              case "log": {
                break;
              }
              case "sendevent": {
                if (msg.kind == "timeupdate") break;
              }
              default: {
                console.log("💼","Worker sent:",msg);
              }
            }
          }
          if (!listened) console.warn("▶️","Received unhandled msg from worker",msg);
        };
        if (postqueue.length) {
          postqueue.unshift([
            [{
              type: "debugging",
              value: main.debugging
            }],
            function(){}, //resolve
            function(){}  //reject
          ]);
          while (postqueue.length) {
            var first = postqueue.shift();
            //                   arguments      resolve  reject  
            self.post.apply(self,first[0]).then(first[1],first[2]);
          }
          if (main.debugging) console.warn("Worker message queue cleared");
        }
        else {
          self.post({
            type: "debugging",
            value: main.debugging
          });
        }
      }).catch(function(e){ 
        workerProm = false;
        throw e;
      });

      this.listeners = {};
      var uid = 0;
      this.addListener = function(opts,callback){
        var key = [opts.type,opts.idx,opts.uid].join("_");
        if (callback) {
          this.listeners[key] = callback;
        }
        else {
          var self = this;
          return new Promise(function(resolve,reject){
            self.listeners[key] = function(msg){
              if (msg.status == "error") return reject(msg);
              resolve(msg);
            };
          });
        }
      };

      var postqueue = [];
      this.post = function(msg,opts){
        if (!this.worker || (this.worker == "pending")) {
          //worker is not there yet: queue
          if (main.debugging) console.warn("Worker is not ready yet - delaying command",msg);
          return new Promise(function(resolve,reject){
            postqueue.push([[msg,opts],resolve,reject]);
          });
        }

        if (!("uid" in msg)) {
          msg.uid = uid++;
        }
        if (opts && opts.transfer && !Array.isArray(opts.transfer)){
          opts.transfer = [opts.transfer];
        }
        this.worker.postMessage(msg,opts);
        return new Promise(function(resolve,reject){
          self.addListener(msg).then(resolve).catch(reject);
        });
      };
      this.terminate = function(){
        if (this.worker) this.worker.terminate();
      };
    }
    try {
      this.worker = new WebCodecsWorker();
    }
    catch (e) {
      mistplayers.rawws.isBrowserSupported = function(){ return false; };
      MistVideo.nextCombo(false,"failed to load webcodecs worker");
      throw e;
      return;
    }
    this.worker.addListener({type: "writeframe"},function(msg){
      //safari
      var pipeline = controller.pipelines[msg.idx];
      if (pipeline) {
        var uid = msg.frame.timestamp;
        pipeline.trackWriter.write(msg.frame).then(function(){
          controller.worker.post({
            type: "writeframe",
            idx: msg.idx,
            uid: uid,
            status: "ok"
          });
        }).catch(function(err){
          controller.worker.post({
            type: "writeframe",
            idx: msg.idx,
            uid: uid,
            status: "error",
            error: err
          });
        });
      }
      else {
        controller.worker.post({
          type: "writeframe",
          idx: msg.idx,
          uid: msg.frame.timestamp,
          status: "error",
          error: "Pipeline not active"
        });

      }
    });
    this.worker.addListener({type: "log"},function(msg){
      MistVideo.log("WebCodecsWorker: "+msg.msg);
    });
    this.worker.addListener({type: "sendevent"},function(msg){
      MistUtil.event.send(
        msg.kind,msg.message ? msg.message : ("idx" in msg ? "track "+msg.idx : null),
        video
      );
    });
    this.worker.addListener({type: "addtrack"},function(msg){
      var pipeline = controller.pipelines[msg.idx];
      if (pipeline) {
        if ("track" in msg) {
          //safari
          pipeline.trackGenerator = msg.track;
        }
        else {
          //webkit
          //the safari video pipeline will first send "creategenerator" through this path, which then sends another addtrack message with the track created. The generator will not be added after the first call, but after the second
          if (!pipeline.trackGenerator) {
            pipeline.createTrackGenerator();
          }
        }
        if (pipeline.trackGenerator) controller.stream.addTrack(pipeline.trackGenerator);
      }
    });
    this.worker.addListener({type: "removetrack"},function(msg){
      var pipeline = controller.pipelines[msg.idx];
      if (pipeline && pipeline.trackGenerator) {
        controller.stream.removeTrack(pipeline.trackGenerator);
        pipeline.trackGenerator = null;
      }
    });
    this.worker.addListener({type: "setplaybackrate"},function(msg){
      MistVideo.video.playbackRate = msg.speed;
    });
    this.worker.addListener({type: "closed"},function(msg){
      if (msg.idx in controller.pipelines) {
        delete controller.pipelines[msg.idx];
        //do not call .close: this message is sent when the worker pipeline has already done that
      }
    });
    this.worker.addListener({type: "haveVideoTrackGenerator"},function(msg){
      controller.worker.haveVideoTrackGenerator = msg.value;
    });
    function Pipeline(track) {
      if (!(typeof track == "object")) {
        track = getTrack(track);
      }
      var pipeline = this;

      //check cache if track is supported
      var cache = mistplayers.rawws.cacheSupported;
      if (cache) cache = cache[MistVideo.stream];
      if (cache && (track.idx in cache)) {
        if (!cache[track.idx]) {
          MistVideo.log("Cancelling pipeline "+track.idx+" because this codec ("+track.codec+") is not supported");
          return;
        }
      }
      else { 
        //we don't know yet if this track is supported
        mistplayers.rawws.isTrackSupported(track).then(function(result){
          theCache[i] = result.supported;
          if (!result.supported) {
            MistVideo.log("Cancelling pipeline "+track.idx+" because this codec ("+track.codec+") is not supported");
            pipeline.close();
          }
        });
      }
      
      
      let p = mistplayers.rawws.isTrackSupported(track);
      if (!p) return;
      if ("then" in p) {
        p.then(function(result){
          if (!result.supported){ 
            MistVideo.log("Cancelling pipeline "+track.idx+" because this codec ("+track.codec+") is not supported")
            pipeline.close();
          }
        }).catch(function(){
          pipeline.close();
        });
      }
        
      
      if ((track.type != "audio") && (track.type != "video")) {
        controller.control.send({type:"hold"})
        return; 
      }

      function post(obj,transfer){
        obj.idx = track.idx;
        if (transfer) return controller.worker.post(obj,{transfer:transfer});
        return controller.worker.post(obj);
      }

      pipeline.track = track;
      pipeline.stats = {
        early: null,
        frames: {
          in: 0,
          decoded: 0,
          out: 0
        },
        framerate: {
          in: new Differentiate(function(){ return pipeline.stats.frames.in; }),
          decoded: new Differentiate(function(){ return pipeline.stats.frames.decoded; }),
          out: new Differentiate(function(){ return pipeline.stats.frames.out; })
        },
        queues: {
          in: 0,
          decoder: 0,
          out: 0
        },
        timing: {
          decoder: new FrameTracker(),
          writable: new FrameTracker()
        }
      };

      pipeline.createTrackGenerator = function(){
        if (!window.MediaStreamTrackGenerator && controller.worker.haveVideoTrackGenerator) {
          //Safari: VideoTrackGenerator is available in the worker
          if (track.type == "video") {
            post({
              type: "creategenerator"
            });
          }
          else if (track.type == "audio") {
            //we should use the polyfill for audio, but the writable is not transferrable :(
            //instead, the worker will postMessage {type: "writeframe", frame: data} and the frames will be written here in the main thread
            pipeline.trackGenerator = new PolyMediaStreamTrackGenerator({kind: "audio", track: track});
            pipeline.trackWriter = pipeline.trackGenerator.writable.getWriter();
            post({ //will create a dummy trackwriter that postMessages the frames
              type: "creategenerator"
            });
          }
        }
        else {
          //otherwise: either use the native code or the polyfill
          pipeline.trackGenerator = typeof MediaStreamTrackGenerator !== "undefined" ? new MediaStreamTrackGenerator({kind: track.type}) : new PolyMediaStreamTrackGenerator({kind: track.type, track: track});
          post({
            type: "setwritable",
            writable: pipeline.trackGenerator.writable,
          },pipeline.trackGenerator.writable);
        }
      };
      pipeline.configure = function(header){
        post({
          type: "configure",
          header: header
        },"buffer" in header ? header.buffer : undefined);
      };
      pipeline.receive = function(chunkOpts){
        post({
          type: "receive",
          chunk: chunkOpts
        },chunkOpts.data.buffer);
      };
      pipeline.close = function(waitEmpty){
        post({
          type: "close",
          waitEmpty: waitEmpty
        });
      };

      MistVideo.log("Creating pipeline for track "+track.idx+" ("+track.codec+(track.codecstring ? " "+track.codecstring : "")+" "+track.type+")");


      post({
        type: "create",
        track: track,
        opts: { optimizeForLatency: MistVideo.info.type == "live" }
      });
      pipeline.createTrackGenerator();


      controller.pipelines[track.idx] = pipeline;

    } //end of function Pipeline

    //This class saves times chunks/frames were in various places in the pipelines.
    //This is used by the pipelines to determine when a frame should be outputted.
    function FrameTiming() {
      this.tweakSpeed = function(tweak){
        this.setSpeed(this.speed.main,tweak);
      };
      this.setSpeed = function(speed,tweak) {
        controller.worker.post({
          type: "frametiming",
          action: "setSpeed",
          speed: speed,
          tweak: tweak
        }); 
      };
      this.reset = function(){
        //this.server = false; //don't reset: keep last on_time message
        controller.worker.post({
          type: "frametiming",
          action: "reset"
        });
        this.in = null;
        this.decoded = null;
        this.out = null;
        this.paused = false;
        this.seeking = false;
        this.speed = {
          main: 1,
          tweak: 1,
          combined: 1
        };
      };
      this.reset();
    }
    this.frameTiming = new FrameTiming();

    var keepAway = MistVideo.info.type == "live" ? 100 : 500;
    this.desiredBuffer = function(){
      var out = keepAway + controller.control.serverDelay.get() + controller.jitter.get(); //in ms
      return Math.round(out);
    };

    //connects to the control websocket and requests codecs
    this.connect = function(){
      if (this.connecting) {
        return this.connecting;
      }

      this.connecting = new Promise(function(resolve,reject){
        MistVideo.container.setAttribute("data-loading","");

        //controller.createControlChannel();
        var control = controller.control;
        if ((control.connectionState == "closing") || (control.connectionState == "closed")) {
          control.reconnect();
        }

        controller.stream = new MediaStream();
        video.srcObject = controller.stream;
        controller.frameTiming.reset();
        controller.bm = new MistUtil.shared.BufferManager(controller.control,MistVideo,video,{
          //getter
          desiredBuffer: new MistUtil.shared.DesiredBuffer({ //add all these factors: all buffer components in ms
            base: MistVideo.info.type == "live" ? 100 : 500,           //never changes
            keepAway: 250,                                             //slowly decays by keepAwayDecay every on_time if buffer state is ok, increases when waiting event is triggered
            serverDelay: controller.control.serverDelay.get,
            underflow: function(){
              //if the buffer is empty, there may be an underflow on the decoder output -> trackwriter timer
              //aka, the decoded packets are shown in the video element too late
              //add this lateness (when the value for earliness is negative) to the extra buffer we're requesting
              var buffer = (controller.frameTiming.decoded - controller.frameTiming.out)*1e-3;
              if (buffer < 10) {
                var early = main.api.earliness;
                var out = -1*Math.min.apply(null,Object.values(early));
                return Math.max(0,out);
              }
              return 0;
            }
          },{ //clamp desired buffer according to these rules
            min: { //the desired buffer should be at least the frame duration (for sparse tracks eg JPEG)
              maxFrameDuration: function(){
                var max_frame_duration = 0; //in microseconds
                for (var i in controller.pipelines) {
                  max_frame_duration = Math.max(max_frame_duration,controller.pipelines[i].stats.frame_duration);
                }
                return max_frame_duration*1e-3;
              }
            }
          }),
          buffer: function(){
            return (controller.frameTiming.decoded - controller.frameTiming.out)*1e-3; //[ms]
          },
          keepAwayDecay: 0.25,
          setPlaybackRate: function(value){
            return controller.frameTiming.setSpeed(value);
          }
        });
        controller.bm.addListener("buffer_low",function(){
          main.ABR.badness++;
        });

        return controller.control.addListener("channel_open").then(function(){
          //now check if worker is ready
          return controller.worker.workerReady();
        }).then(function(){
          var codecs = [Object.values(supportedCodecs).filter(function(a){
            return a.length
          })/*.map(function(b){
            return b.map(function(a){
              if (a == "PCM") return a+"-little";
              return a;
            });
          })*/]; //request a combination of 1 audio track and 1 video track
          //codecs = [[["H264","JPEG"],["AAC"],["subtitle"]]];
          //codecs[0][1] = ["UYVY"];
          control.send({type:"request_codec_data",supported_combinations:codecs},true);
          //control.send({type:"request_codec_data",supportedCodecs:["H264"]});
          MistVideo.log("Requesting codecs: "+codecs.flat(2).join(", "));
          controller.connecting = false;


          return control.addListener("codec_data");
        }).then(function(msg){
          if (!msg.codecs || !msg.codecs.length) {
            throw("No playable codecs available");
          }

          control.addListener("binary",controller.receiver);
          control.unlock(); //unlock control channel for api commands

          //received codec data, set up track pipelines
          for (var i in msg.tracks) {
            var trackIndex = msg.tracks[i];
            if (!(i in msg.codecs)) throw "No codec data for track "+trackIndex;
            if (trackIndex in controller.pipelines) {
              if (main.debugging) console.warn("Pipeline for track "+trackIndex+" already exists, not creating it again");
            }
            else {
              controller.pipelines[trackIndex] = new Pipeline(trackIndex);
            }
          }
          
          MistVideo.log("Preparing to play codecs: "+Object.values(controller.pipelines).map(function(a){return a.track.codec}).join(", "));

          control.send({type:"play"});
          
          if (!MistVideo.options.autoplay) {
            try { video.play() } catch(e){}
            setTimeout(function(){
              control.send({type:"hold"});
            },100);
          }

          resolve();

        }).catch(function(err){
          MistVideo.showError(err);
          if (main.debugging) {
            console.error("▶️",err);
          }
        });
      });

      return this.connecting;
    }
    this.close = function(){
      this.control.removeListener("binary",controller.receiver);
      this.control.close();
      if (controller.stream) {
        for (var i in controller.pipelines) {
          controller.pipelines[i].close();
        }
      }
    }

    var supportedCodecs = { audio: [], video: [] };
    function checkTracksAndConnect(){
      //check supported codecs if unknown, then connect
      var promises = [], thePlayer = mistplayers.rawws;
      if (!(MistVideo.stream in thePlayer.cacheSupported)) {
        thePlayer.cacheSupported[MistVideo.stream] = {}; 
      }
      var theCache = thePlayer.cacheSupported[MistVideo.stream];
      function checkTrack(i){
        function pushIfUnique(array,entry) {
          if (array.indexOf(entry) < 0) array.push(entry);
        }
        var track = MistVideo.info.meta.tracks[i];
        var idx = track.idx;
        if ((track.type != "audio") && (track.type != "video")) { return; }
        if (idx in theCache) {
          if (theCache[idx]) {
            pushIfUnique(supportedCodecs[track.type],track.codec);
          }
        }
        else {
          promises.push(mistplayers.rawws.isTrackSupported(track).then(function(result){
            theCache[idx] = result.supported;
            if (result.supported) {
              pushIfUnique(supportedCodecs[track.type],track.codec);
            }
          }));
        }
      }
      for (var i in MistVideo.info.meta.tracks) {
        checkTrack(i);
      }
      if (promises.length) {
        Promise.all(promises).then(connectIfEnough).catch(console.err);
      }
      else {
        connectIfEnough();
      }
    }
    function connectIfEnough() {
      var skip = false;
      //skip = true; //skip codec support step (for debugging)

      if (skip) {

        supportedCodecs = { audio: [], video: [] };
        for (var i in MistVideo.info.meta.tracks) {
          var track = MistVideo.info.meta.tracks[i];
          switch (track.type){
            case "audio":
            case "video": {
              if (supportedCodecs[track.type].indexOf(track.codec) < 0) supportedCodecs[track.type].push(track.codec);
            }
          }
        }

      }
      else {
        //calculate current player score
        var oldtracks = mistplayers.rawws.cacheSupported[MistVideo.stream].last;
        var myscore = MistVideo.scoreCombo(mistplayers.rawws.isBrowserSupported(MistVideo.source.type,MistVideo.source,MistVideo));
        if (myscore < 1) {
          //nothing (or only subtitles) can play, uhoh!
          MistVideo.log("After testing decoders, "+mistplayers[MistVideo.playerName].name+" cannot play any tracks after all: trying next combo..");
          MistVideo.nextCombo(false,"rawws cannot play any tracks");
          return;
        }

        var newtracks = mistplayers.rawws.cacheSupported[MistVideo.stream].last;
        for (var kind in oldtracks) {
          if (!(kind in newtracks)){
            //our score has decreased..

            //if not enough playable tracks, try to fall back to another player
            var firstCombo = MistVideo.checkCombo({startCombo:false},true);
            if (firstCombo.score > myscore && firstCombo.player != MistVideo.playerName) {
              MistVideo.log("After testing decoders, "+mistplayers[MistVideo.playerName].name+" is no longer the best scoring player, trying next combo..");
              MistVideo.nextCombo(false,"after testing, rawws is no longer the best player");
              return;
            }

            //our score is still best though
            break; 
          }
        }

        //this player is good - carry on
        MistVideo.log("Confirmed "+mistplayers[MistVideo.playerName].name+" is still the best scoring player, connecting..");
      }

      controller.connect();
    }

    checkTracksAndConnect();
  }
  try {
    this.controller = new RawPlayer(MistVideo.source.url);
  }
  catch(e) {
    MistVideo.nextCombo(false,e);
    return;
  }

  /////////////////////////////////////
  // WebCodec-specific api overrides //
  /////////////////////////////////////
  var custom_funcs = {};

  custom_funcs.currentTime = {
    get: function(){
      var frameTiming = main.controller.frameTiming;
      if (frameTiming) {
        var ts = frameTiming.out; //contains the timestamp of the frame that has last passed to one of the track writers
        return ts ? ts*1e-6 : 0;
      }
      else return 0;
    },
    set: function(value){
      //send seek command
      //  MistServer will send new data:
      //  - starting with the first key frame before the specified seek_time
      //  - fast forward data to seek_time, plus additional ff_add
      main.controller.control.send({
        type: "seek",
        seek_time: (value == "live" ? "live" : Math.round(value*1e3)),
        ff_add: main.controller.desiredBuffer()
      });
    }
  };

  //the displayed buffer represents the frames that have been received from MistServer through the control channel, but have not yet been presented to the track writer
  //index 0 is What has been decoded but not yet passed to the track writer
  //index 1 is What was received but not yet decoded
  custom_funcs.buffered = {
    get: function(){
     return new function TimeRanges() {
       var frameTiming = main.controller.frameTiming;
       Object.defineProperty(this,"length",{
         get: function() { 
           if (frameTiming && frameTiming.in && frameTiming.out) return 2;
           return 0;
         }
       });
       this.start = function(index){
         if (frameTiming) {
           var info;
           switch (index) {
             case 0: info = frameTiming.out; break;
             case 1: info = frameTiming.decoded; break;
             default: throw new Error("Index out of bounds");
           }
           return info ? info*1e-6 : 0;
         }
       };
       this.end = function(index){
         if (frameTiming) {
           var info;
           switch (index) {
             case 0: info = frameTiming.decoded; break;
             case 1: info = frameTiming.in; break;
             default: throw new Error("Index out of bounds");
           }
           return info ? info*1e-6 : 0;
         }
       };
     }
    }
  };

  custom_funcs.duration = {
    get: function(){
      var frameTiming = main.controller.frameTiming;
      if (frameTiming) {
        var info = frameTiming.server;
        if (info && ("end" in info)) {
          if (MistVideo.info && (MistVideo.info.type == "live")) {
            return (info.end + new Date().getTime() - info._received.getTime())*1e-3;
          }
          else return info.end*1e-3;
        }
      }
      return 0;
    }
  };

  custom_funcs.stream_end = function(){
    //wait for buffers to complete, then send ended event
    var promises = [];
    for (var tid in main.controller.pipelines) {
      promises.push(main.controller.pipelines[tid].close(true));
    }
    Promise.all(promises).then(function(){
      MistUtil.event.send("ended",null,video);
    });
  };
  var looping = false;
  custom_funcs.restart = function(){
    if (looping) return;
    looping = true;
    //wait for buffers to complete, then loop
    var promises = [];
    for (var tid in main.controller.pipelines) {
      promises.push(main.controller.pipelines[tid].close(true));
    }
    Promise.all(promises).then(function(){
      MistVideo.log("Looping..");
      var result = main.controller.close();
      if (result instanceof Promise) {
        result.then(function(){
          //console.warn("closed, connecting");
          main.controller.connect().then(function(){
            looping = false;
            //console.warn("Looping complete");
          });
        });
      }
      else {
        main.controller.connect().then(function(){
          looping = false;
          //console.warn("Looping complete");
        });
      }
    });
  }


  /////////////////////////////
  // Expose additional stats //
  /////////////////////////////
  custom_funcs.receiveQueue = { //queue of chunks received over the control channel, but not yet passed to the decoder (because it is not yet configured) -> should be 0
    get: function(){
      var out = {};
      for (var i in main.controller.pipelines) {
        var pipeline = main.controller.pipelines[i];
        if (pipeline.track) {
          out[pipeline.track.type] = pipeline.stats.queues.in;
        }
      }
      return out;
    }
  };
  custom_funcs.decodeQueue = { //browsers internal decoder queue -> should be 0
    get: function(){
      var out = {};
      for (var i in main.controller.pipelines) {
        var pipeline = main.controller.pipelines[i];
        if (pipeline.track) {
          out[pipeline.track.type] = pipeline.stats.queues.decoder;
        }
      }
      return out;
    }
  };
  custom_funcs.displayQueue = { //queue of frames that have been decoded, but are not yet passed to the video element (track writer) -> should be > 0
    get: function(){
      var out = {};
      for (var i in main.controller.pipelines) {
        var pipeline = main.controller.pipelines[i];
        if (pipeline.track) {
          out[pipeline.track.type] = pipeline.stats.queues.out;
        }
      }
      return out;
    }
  };
  custom_funcs.displayBuffer = { //duration of frames that have been decoded, but are not yet passed to the video element (track writer) -> should be desired buffer size
    get: function(){
      var out = {};
      for (var i in main.controller.pipelines) {
        var pipeline = main.controller.pipelines[i];
        if (pipeline.track) {
          out[pipeline.track.type] = (pipeline.stats.timing.decoder.last_out - pipeline.stats.timing.writable.last_in)*1e-3;
        }
        else {
          out[pipeline.track.type] = 0;
        }
      }
      return out;
    }
  };
  custom_funcs.earliness = { 
    get: function(){
      var out = {};
      for (var i in main.controller.pipelines) {
        var pipeline = main.controller.pipelines[i];
        if (pipeline.track) {
          if ("early" in pipeline.stats) {
            out[pipeline.track.type] = pipeline.stats.early;
          }
          else {
            out[pipeline.track.type] = 0;
          }
        }
      }
      return out;
    }
  };
  custom_funcs.sync = { //difference in output timestamps between video and audio tracks, in ms
    get: function(){
      var audio;
      var video;

      for (var i in main.controller.pipelines) {
        var pipeline = main.controller.pipelines[i];
        if (pipeline.track) {
          if (pipeline.closeWhenEmpty) continue;
          switch (pipeline.track.type) {
            case "audio": {
              audio = pipeline; break;
            }
            case "video": {
              video = pipeline; break;
            }
          }
        }
      }
      function get(p) {
        return p.stats.timing.writable.last_out;
      }
      try {
        return Math.abs(get(audio) - get(video))*1e-3;
      }
      catch {
        return 0;
      }
    }
  };

  custom_funcs.framerate_in = {
    get: function() {
      return function(){
        var out = {};
        for (var i in main.controller.pipelines) {
          var pipeline = main.controller.pipelines[i];
          if (pipeline.track) {
            if ("in" in pipeline.stats.framerate) {
              out[pipeline.track.type] = pipeline.stats.framerate.in.get();
            }
            else {
              out[pipeline.track.type] = 0;
            }
          }
        }
        return out;
      }
    }
  };
  custom_funcs.framerate_decoder = {
    get: function() {
      return function(){
        var out = {};
        for (var i in main.controller.pipelines) {
          var pipeline = main.controller.pipelines[i];
          if (pipeline.track) {
            if ("decoded" in pipeline.stats.framerate) {
              out[pipeline.track.type] = pipeline.stats.framerate.decoded.get();
            }
            else {
              out[pipeline.track.type] = 0;
            }
          }
        }
        return out;
      }
    }
  };
  custom_funcs.framerate_out = {
    get: function() {
      return function(){
        var out = {};
        for (var i in main.controller.pipelines) {
          var pipeline = main.controller.pipelines[i];
          if (pipeline.track) {
            if ("out" in pipeline.stats.framerate) {
              out[pipeline.track.type] = pipeline.stats.framerate.out.get();
            }
            else {
              out[pipeline.track.type] = 0;
            }
          }
        }
        return out;
      }
    }
  };
  custom_funcs.local_jitter = {
    get: function() {
      return main.controller.jitter.get();
    }
  };
  custom_funcs.decodeTime = { //averaged time it takes for frames to decode [ms]
    get: function(){ 
      var out = {};
      for (var i in main.controller.pipelines) {
        var pipeline = main.controller.pipelines[i];
        if (pipeline.track) {
          if (pipeline.stats.timing.decoder) {
            out[pipeline.track.type] = pipeline.stats.timing.decoder.delay; 
          }
          else {
            out[pipeline.track.type] = 0;
          }
        }
      }
      return out;
    }
  };
  custom_funcs.displayTime = { //averaged time it takes for frames to pass the track writer [ms]
    get: function(){ 
      var out = {};
      for (var i in main.controller.pipelines) {
        var pipeline = main.controller.pipelines[i];
        if (pipeline.track) {
          if (pipeline.stats.timing.writable) {
            out[pipeline.track.type] = pipeline.stats.timing.writable.delay; 
          }
          else {
            out[pipeline.track.type] = 0;
          }
        }
      }
      return out;
    }
  };
  custom_funcs.shift = { //timestamp difference after exiting the decoder
    get: function(){ 
      var out = {};
      for (var i in main.controller.pipelines) {
        var pipeline = main.controller.pipelines[i];
        if (pipeline.track) {
          if (pipeline.stats.timing.decoder) {
            out[pipeline.track.type] = pipeline.stats.timing.decoder.shift; 
          }
          else {
            out[pipeline.track.type] = 0;
          }
        }
      }
      return out;
    }
  };

  this.api = new MistUtil.shared.ControlChannelAPI(main.controller,MistVideo,video,custom_funcs);
  Object.defineProperty(this.api,"buffer_manager",{
    get: function(){
      if (main.controller && main.controller.bm) return main.controller.bm;
      return null;
    }
  });
  this.ABR = new MistUtil.shared.ABRController(MistVideo,{
    bitCounter: function(){ return main.controller.control.bitCounter; }
  });

  this.api.unload = function(){
    main.controller.worker.terminate();
    main.controller.close();
  };

  callback(video);
}
