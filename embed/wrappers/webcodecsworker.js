if ((typeof WorkerGlobalScope == "undefined") || !(this instanceof WorkerGlobalScope)) {
  throw "This should be running in a WebWorker";
}
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
function FrameTiming() {
  this.tweakSpeed = function(tweak){
    this.setSpeed(this.speed.main,tweak);
  };
  this.setSpeed = function(speed,tweak) {
    if (!tweak) tweak = this.speed.tweak;

    if ((speed == this.speed.main) && (tweak == this.speed.tweak)) return; //nothing to do

    var combinedSpeed = speed*tweak;

    //a frames target output time is determined by adding its media timestamp (divided by the speed) to the basetime
    //if the desired speed changes, the basetime should be recalculated
    if (this.basetime && this.out) {
      this.basetime = this.basetime + this.out*1e-3 / this.speed.combined - this.out*1e-3 / combinedSpeed;
    }


    this.speed.main = speed;
    this.speed.tweak = tweak;
    this.speed.combined = combinedSpeed;

    self.postMessage({
      type: "setplaybackrate",
      speed: combinedSpeed,
      idx: null
    });
    for (var i in pipelines) {
      var p = pipelines[i];
      if (p.track.type == "audio") {
        p.reset();
      }
    }

    if (this.speed.main != speed) {
      self.postMessage({
        type:"sendevent",
        kind:"ratechange",
        idx: null
      });
    }
  };
  this.reset = function(){
    this.in = null;
    this.decoded = null;
    this.paused = false;
    this.speed = {
      main: 1,
      tweak: 1,
      combined: 1
    };
    if (!this.seeking) {
      this.out = null;
      this.basetime = null;
    }
  };
  this.seeking = false;
  this.reset();
}
var MistUtil = {
  format: {
    time: function(secs,options){
      if (isNaN(secs) || !isFinite(secs)) { return secs; }
      if (!options) { options = {}; }
      
      var ago = (secs < 0 ? " ago" : "");
      secs = Math.abs(secs);
      
      var days = Math.floor(secs / 86400)
      secs = secs - days * 86400;
      var hours = Math.floor(secs / 3600);
      secs = secs - hours * 3600;
      var mins  = Math.floor(secs / 60);
      var ms = Math.round((secs % 1)*1e3);
      secs = Math.floor(secs - mins * 60);
      var str = [];
      if (days) {
        days = days+" day"+(days > 1 ? "s" : "")+", ";
      }
      if ((hours) || (days)) {
        str.push(hours);
        str.push(("0"+mins).slice(-2));
      }
      else {
        str.push(mins); //don't use 0 padding if there are no hours in front
      }
      str.push(("0"+Math.floor(secs)).slice(-2));
      
      if (options.ms) {
        str[str.length-1] += "."+("000"+ms).slice(-3);
      }
      
      return (days ? days : "")+str.join(":")+ago;
    },
    ucFirst: function(string){
      return string.charAt(0).toUpperCase()+string.slice(1);
    }
  }
};

var self = this;
var debugging = false;
var pipelines = {};
//add shortcuts for convenience: during trackswitch when there are multiple audio/video tracks, this will probably return the lowest index
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
var frameTiming = new FrameTiming();

function GenericPipeline(ref,track,opts){
  if (typeof opts == "undefined") opts = {};

  /*

    When a new pipeline is created, the following are prepared by the main thread:
    A track Generator
    A track Writable

    This worker configures a decoder.

    The decoder must first be "configured" (possibly requiring init data) using Pipeline.configure(initdata), after that it can process chunks.
    Pipeline.configureDecoder(init)

    Data travels as follows:

    - Pipeline.receive(chunkOpts): the chunk is either queued in the input queue or sent to decode
    - Pipeline.decode(chunkOpts): the chunk is converted to an EncodedVideoChunk or EncodedAudioChunk and sent to the decoder
    - Pipeline.decoder.decode(chunk): the chunk is decoded and the frame is sent to decoderOpts.output (or triggers decoderOpts.error)
    - Pipeline.onDecoderOutput: the frame is added to the output queue
    - processOutputQueue: when the targetTime is reached, the frame is sent to pipeline.trackWriter, which adds it to the media track

*/

  var pipeline = ref;
  pipeline.track = track;
  pipeline.codec = track.codecstring || (""+track.codec).toLowerCase();
  pipeline.trackWriter = null;
  pipeline.lastReset = null;
  pipeline.closeWhenEmpty = false;
  pipeline.decoderConfigOpts = {
    codec: pipeline.codec,
    optimizeForLatency: opts.optimizeForLatency // sent from main thread if MistVideo.info.type == "live"
  };
  pipeline.queues = {
    in: [],
    out: []
  };
  pipeline.stats = {
    frames: { //total frames in/out
      in: 0,
      decoded: 0,
      out: 0
    },
    early: 0, //the idle time in ms before the first frame in the queue should be outputted - if < 0 the frame is late
    framerate: {
      in: new Differentiate(function(){ return pipeline.stats.frames.in; }),
      out: new Differentiate(function(){ return pipeline.stats.frames.out; }),
      effective: {
        log: [],
        add: function(timestamp){ 
          this.log.push(timestamp);
          if (this.log.length > 16) { this.log.shift(); }
        },
        frame_duration: function(){
          if (this.log.length < 2) return 0; //not enough data
          var dt = this.log[this.log.length-1] - this.log[0]; //[microseconds]
          var frames = this.log.length-1; //-1 because dt is the time between frames, not the duration of the frames
          return dt / frames; //in microseconds
        },
        fps: function(){
          return 1e6 / this.frame_duration;
        }
      }
    },
    timing: {
      decoder: new FrameTracker(),
      writable: new FrameTracker()
    }
  };

  var outputTimer = {
    tid: false,
    targetTime: false
  };
  var waitingTimer = false;
  var decoderOpts;
  var processingReceiveQueue = false;
  var flushing = false;
  var resetting = false;
  var closingPromise = false;

  function post(obj){
    obj.idx = track.idx;
    obj.stats = gatherStats();
    self.postMessage(obj);
  }
  function log(msg,isError){
    post({
      type: "log",
      msg: msg
    });
  }

  function processOutputQueue(calledFromTimer){ //calledFromTimer should be true when processOutputQueue is being called from the outputTimer - to indicate it is not late; this is the expected time to process the next frame
    if (frameTiming.paused) return;

    if (!pipeline.queues.out.length) {
      if (pipeline.closeWhenEmpty) {
        pipeline.close(true);
      }
      else {
        var newBaseTime = performance.now() - frameTiming.out*1e-3 / frameTiming.speed.combined;
        var inc = newBaseTime - frameTiming.basetime;
        if (isNaN(inc) || inc > 1e3 || inc < 50) {
          inc = 50;
          newBaseTime = frameTiming.basetime + inc;
        }
        if (debugging) {
          console.log(
            "💼",
            "Uhoh: output queue for "+function(t){
              switch (t) {
                case "video": return "🎞️";
                case "audio": return "🎵";
                default: return t;
              }
            }(track.type)+" track",track.idx,"is empty!",
            "Increased playback timer with",Math.round(inc),"ms"
          );
        }
        frameTiming.basetime = newBaseTime;
        if (!pipeline.waitingTimer) {
          pipeline.waitingTimer = setTimeout(function(){
            //add some time to the playback timer
            post({type:"sendevent",kind:"waiting"});
          },1e3);
          //the timer will be canceled
          //- when something enters the queue
        }
      }
      return;
    }

    if (outputTimer.tid) {
      if (debugging && (new Date().getTime() - outputTimer.targetTime > 1e3)) {
        console.log(
          "💼",
          "⚠️",
          function(t){
            switch (t) {
              case "video": return "🎞️";
              case "audio": return "🎵";
              default: return t;
            }
          }(track.type),
          "processOutputQueue() outputTimer already active:",
          "ending at",new Date(outputTimer.targetTime).toLocaleTimeString(),
          "seeking to",frameTiming.seeking ? MistUtil.format.time(frameTiming.seeking*1e-6) : "not seeking",
          "first timestamp",MistUtil.format.time(pipeline.queues.out[0].timestamp*1e-6)
        );
      }
      return;
    }
    if (waitingTimer) {
      clearTimeout(waitingTimer);
      waitingTimer = false;
    }

    var frame = pipeline.queues.out[0]; //inspect the first frame, but do not remove it from the queue yet

    if ((pipeline.queues.out.length > 1) && (pipeline.queues.out[1].timestamp < frame.timestamp)) {
      //the frames are received from the decoder out of order (bframes)
      if (debugging) {
        console.warn(
          "💼",
          "⚠️",
          function(t){
            switch (t) {
              case "video": return "🎞️";
              case "audio": return "🎵";
              default: return t;
            }
          }(track.type),
          "Frames out of order!",
          "diff:",(pipeline.queues.out[1].timestamp - frame.timestamp)*1e-3,"ms",
          "this frame:",frame,
          "next frame:",pipeline.queues.out[1]
        );
      }
      //put frame at index 0 at index 1
      var next = pipeline.queues.out.splice(1,1)[0]; //remove and save next frame
      pipeline.queues.out.unshift(next); //put it at the front of the queue

      return processOutputQueue(calledFromTimer); //call self again
    }

    if (frameTiming.basetime !== null) {
      var targetTime = frameTiming.basetime + frame.timestamp*1e-3 / frameTiming.speed.combined; //in ms
      if (isNaN(targetTime)) throw "Invalid target time: "+targetTime;

      var delay = targetTime - performance.now();
      if (!calledFromTimer) pipeline.stats.early = delay;
      else {
        if (debugging && (Math.abs(delay) > 10)) {
          console.log(
            "💼",
            "⚠️",
            function(t){
              switch (t) {
                case "video": return "🎞️";
                case "audio": return "🎵";
                default: return t;
              }
            }(track.type),
            "processOutputQueue() timer end reached, but off by more than 10ms;",delay > 0 ? "early:" : "late:",Math.round(Math.abs(delay)*100)/100,"ms",
            "original delay:",Math.round(calledFromTimer),"ms"
          );
        }
      }
      if (delay <= 2) { //technically delay should be 0 but if we set another timer it's going to be more inaccurate
        return writeFrame();
      }
      else {

        outputTimer.tid = setTimeout(function(){
          outputTimer.tid = false;
          outputTimer.targetTime = false;
          processOutputQueue(delay);
        },delay);
        outputTimer.targetTime = new Date().getTime()+delay;
        if (debugging && (delay > 1e3) && (delay > pipeline.stats.frame_duration*1e-3)) {
          console.warn(
            "💼",
            "⚠️",
            function(t){
              switch (t) {
                case "video": return "🎞️";
                case "audio": return "🎵";
                default: return t;
              }
            }(track.type),
            "processOutputQueue() delay high:",
            Math.round(delay),"ms",
            "ending at:",new Date(outputTimer.targetTime).toLocaleTimeString(),
            frameTiming.seeking ? "seeking to:" : "not seeking",frameTiming.seeking ? MistUtil.format.time(frameTiming.seeking*1e-6) : "",
            "first timestamp:",MistUtil.format.time(pipeline.queues.out[0].timestamp*1e-6)
          );
        }
        if (calledFromTimer && debugging) {
          console.warn(
            "💼",
            "⚠️",
            function(t){
              switch (t) {
                case "video": return "🎞️";
                case "audio": return "🎵";
                default: return t;
              }
            }(track.type),
            "processOutputQueue() timer re-timered, delay was",Math.round(calledFromTimer),"needs another ",Math.round(delay),"ms"
          );
        }
      }

    }
    else {
      frameTiming.basetime = performance.now() - frame.timestamp*1e-3 / frameTiming.speed.combined
      return writeFrame();
    }
  } //end of function processOutputQueue

  //outputs a decoded frame to the media track
  function writeFrame() {
    var frame = pipeline.queues.out.shift();
    if (frameTiming.seeking) {
      if (frame.timestamp < frameTiming.seeking) {
        //skip
        frame.close();
        if (debugging) {
          console.log(
            "💼",
            "‼️",
            function(t){
              switch (t) {
                case "video": return "🎞️";
                case "audio": return "🎵";
                default: return t;
              }
            }(track.type),
            "Skipped display of "+track.type+" frame because it is before the seek time",
            frame.timestamp,"<",frameTiming.seeking
          );
        }
        return;
      }
      else {
        if (debugging) console.warn("💼","Reached seek target of ["+MistUtil.format.time(frameTiming.seeking*1e-6)+"]: returning to normal playback");
        frameTiming.seeking = undefined;
        post({type:"sendevent",kind:"seeked"});
      }
    }

    pipeline.stats.frames.out++;
    pipeline.stats.timing.writable.start(frame.timestamp);
    var timestamp = frame.timestamp; //cache this as frame data stops being available if frame is postMessage'd to main thread (audio on Safari)
    frameTiming.written = timestamp;
    var promise = pipeline.trackWriter.write(frame).then(function(){
      //frame.close(); shouldnt be needed
      frameTiming.out = Math.max(frameTiming.out,timestamp); //TODO overwrite only if > current .out?
      //console.warn(".out",timestamp*1e-6);
      pipeline.stats.timing.writable.end(timestamp);
      post({type:"sendevent",kind:"timeupdate"});
    }).catch(function(err){
      if (debugging) console.warn("💼","Trackwriter.write failed for",frame,err);
      frame.close();
      pipeline.stats.timing.writable.end(timestamp);
    });
    setTimeout(processOutputQueue,0); //asyncify
  } //end of function writeFrame

  pipeline.setWritable = function(writable){
    pipeline.trackWriter = writable.getWriter();
  };
  pipeline.decode = function(){
    throw "Should be set in the Audio or VideoPipeline"
  };
  pipeline.decoder = null; //Should be set in the Audio or VideoPipeline

  pipeline.configureDecoder = function(header){
    if (pipeline.decoder.state != "unconfigured") {
      if (
        //check if the new header is equal to the previous one: using == will only return true if the reference is equal
        function ArrayBuffersAreEqual(a,b){
          return a === b || indexedDB.cmp(a,b) === 0;
        }(header,pipeline.decoderConfigOpts.description)
      ){
        //nothing to do, carry on
        log("Received equal init data for track "+track.idx+" ("+track.codec+" "+track.type+") that was already configured: not resetting");
        post({ type: "addtrack" });
        return;
      }
      log("Received new init data for track "+track.idx+" ("+track.codec+" "+track.type+") that was already configured: resetting");
      pipeline.decoder.reset();
    }
    if (debugging) console.log("💼","Configuring decoder for track",track.idx,"("+track.codec+" "+track.type+")",pipeline.decoderConfigOpts,header ? "Header size:" : "(No header)",header ? header.byteLength+"B" : "");

    post({ type: "addtrack" });
    pipeline.decoderConfigOpts.description = header;
    pipeline.decoder.configure(pipeline.decoderConfigOpts);
    if (debugging) console.log("💼","Configure track "+track.idx+" complete");
    return true; //indicate a keyframe may be required 
  };

  pipeline.onDecoderOutput = function(frame){
    //It's better to make sure that the decoder output callback quickly returns (https://developer.chrome.com/docs/web-platform/best-practices/webcodecs)
    
    if (("numberOfFrames" in frame) && (frame.numberOfFrames == 0)) {
      //happens in Firefox for surround sound
      //pipeline.onDecoderError("AudioData is empty: 0 frames");
      //other frames seem to be fine - so ignore this output I suppose
      frame.close();
      return;
    }

    pipeline.stats.timing.decoder.end(frame.timestamp);
    pipeline.stats.frames.decoded++;
    if (flushing) { frame.close(); return; }

    //add to out queue
    pipeline.queues.out.push(frame);

    if (debugging == "verbose") console.log("💼","New frame exited "+track.type+" decoder:",frame);

    //asyncify other tasks
    setTimeout(function(){
      frameTiming.decoded = frame.timestamp;
      processOutputQueue();
    },0);
  };
  pipeline.onDecoderError = function(err){
    if (debugging) console.error("💼",track.type+" decoder error:",err,"Config:",pipeline.decoderConfigOpts);
    log(MistUtil.format.ucFirst(track.type)+" decoder error: "+err+" (Track "+track.idx+")");
    pipeline.reset();
  };

  //pipeline input
  pipeline.receive = function(chunkOpts){

    if (resetting || flushing) {
      pipeline.queues.in.push(chunkOpts);
      if (debugging == "verbose") {
        console.log(
          "💼",
          "Queued",
          function(t){
            switch (t) {
              case "video": return "🎞️";
              case "audio": return "🎵";
              default: return t;
            }
          }(track.type),
          "frame because "+(resetting ? "resetting" : "flushing"),
          "Queue length:",pipeline.queues.in.length,
          chunkOpts
        );
      }
      return;
    }

    frameTiming.in = chunkOpts.timestamp;
    pipeline.stats.frames.in++;

    switch (pipeline.decoder.state) {
      case "configured": {
        pipeline.queues.in.push(chunkOpts);
        if (!processingReceiveQueue) {
          processingReceiveQueue = true;

          while (pipeline.queues.in.length) {
            var first = pipeline.queues.in.shift();
            pipeline.decode(first);
            pipeline.stats.framerate.effective.add(first.timestamp);
          }

          processingReceiveQueue = false;
        }

        return;
      }
      case "closed": { return; }
      case "unconfigured": {
        pipeline.queues.in.push(chunkOpts);
        return;
      }
      default: {
        console.log("💼","Unhandled decoder state for ",track.codec,":",pipeline.decoder.state);
        return;
      }
    }
  };

  //resets the decoder and reconfigures it
  pipeline.reset = function(){
    if (resetting) return resetting;
    resetting = new Promise(function(resolve,reject){
      if (debugging) console.log("💼","Resetting track "+track.idx+" ("+track.codec+" "+track.type+")");
      if (pipeline.decoder.state == "closed") {
        if ((pipeline.lastReset === null) || (new Date().getTime() - pipeline.lastReset.getTime() > 1e3)) {
          //return;
          if (debugging) console.warn("💼","Recreating pipeline");
          if (pipeline.decoder.decodeQueueSize) pipeline.decoder.flush();
          var oldpipe = pipeline;
          pipeline = new Pipeline(track,opts); //does not seem to  break references
          pipeline.closeWhenEmpty = this.closeWhenEmpty;
          pipeline.lastReset = new Date();
          pipeline.configureDecoder(oldpipe.decoderConfigOpts.description);
          pipeline.trackWriter = oldpipe.trackWriter;
          pipeline.queues.out = oldpipe.queues.out;
          resolve();
        }
        else {
          setTimeout(function(){
            pipeline.reset().then(resolve);
          },1e3);
        }
      }
      else {
        //"soft" reset
        pipeline.lastReset = new Date();
        if (flushing) {
          flushing.finally(function(){
            post({type: "removetrack"});
            pipeline.decoder.reset();
            pipeline.configureDecoder(pipeline.decoderConfigOpts.description);
            resolve();
          });
        }
        else {
          post({type: "removetrack"});
          pipeline.decoder.reset();
          pipeline.configureDecoder(pipeline.decoderConfigOpts.description);
          resolve();
        }
      }
    });
    resetting.then(function(){
      resetting = false;
    });
    return resetting;
  };

  //clear all queues
  pipeline.empty = function(){
    return new Promise(function(resolve,reject){
      pipeline.queues.in = [];
      if (outputTimer.tid) {
        clearTimeout(outputTimer.tid);
        outputTimer.tid = false;
      }
      if (debugging) {
        console.log(
          "💼",
          "Clearing input queue for "+function(t){
            switch (t) {
              case "video": return "🎞️";
              case "audio": return "🎵";
              default: return t;
            }
          }(track.type)+" pipeline"
        );
      }
      if (flushing) return flushing;
      if (pipeline.decoder.state == "closed") {
        var frames = pipeline.queues.out;
        pipeline.queues.out = [];
        while (frames.length) {
          var first = frames.shift();
          if (first) first.close();
        }
        resolve();
      }
      else {
        flushing = pipeline.decoder.flush().catch(function(e){
          if (debugging) {
            console.error("💼","‼️",function(t){
              switch (t) {
                case "video": return "🎞️";
                case "audio": return "🎵";
                default: return t;
              }
            }(track.type),"Error while flushing:",e);
          }
        }).finally(function(){
          var frames = pipeline.queues.out;
          pipeline.queues.out = [];
          flushing = false;
          while (frames.length) {
            var first = frames.shift();
            if (first) first.close();
          }
          resolve();
        });
      }
    });
  }

  pipeline.close = function(waitEmpty){
    if (!closingPromise) closingPromise = Promise.withResolvers();
    function isEmpty() {
      if (pipeline.queues.out.length) return false;
      if (pipeline.queues.in.length) return false;
      if (pipeline.decoder.decodeQueueSize) return false;
      return true;
    }
    if (waitEmpty && !isEmpty()) {
      if (!pipeline.closeWhenEmpty) {
        pipeline.closeWhenEmpty = true;
        log("The pipeline for track "+track.idx+" will close once empty");
      }
    }
    else {
      post({ type: "removetrack" });
      if (pipeline.decoder.state != "closed") pipeline.decoder.close();
      pipeline.empty();
      delete pipelines[track.idx];
      log("The pipeline for track "+track.idx+" has now been closed");
      closingPromise.resolve();
    }
    return closingPromise.promise;
  };

  log("Creating pipeline for track "+track.idx+" ("+track.codec+(track.codecstring ? " "+track.codecstring : "")+" "+track.type+")");

  pipelines[track.idx] = pipeline;

  return pipeline;
} //end of function GenericPipeline;
function RawCoder(convert_func) {
  this.decode = convert_func;
  this.configure = function(){
    this.state = "configured";
  };
  this.close = function(){};
  this.flush = function(){
    return new Promise(function(resolve){ resolve(); });
  };
  this.reset = function(){};
  this.state = "unconfigured";
}
function AudioPipeline(track,opts){
  var pipeline = GenericPipeline(this,track,opts);

  pipeline.decoderConfigOpts.numberOfChannels = track.channels;
  pipeline.decoderConfigOpts.sampleRate = track.rate;

  pipeline.decoder = new AudioDecoder({
    output: pipeline.onDecoderOutput,
    error: pipeline.onDecoderError
  });

  pipeline.decode = function (chunkOpts){
    chunkOpts.type = "key";
    pipeline.stats.timing.decoder.start(chunkOpts.timestamp);
    return pipeline.decoder.decode(new EncodedAudioChunk(chunkOpts));
  }
  if (track.codec == "PCM") {
    //swap big endian to little endian
    var convert_func = function(){};
    var decode_func = pipeline.decode;
    switch (track.size){
      case 32: {
        //converts source data in big endian to Int32Array with little endian
        convert_func = function(ab){
          const view = new DataView(ab);
          for (let i = 0; i < view.byteLength; i += 4) {
            const val = view.getInt32(i,false); //false: big endian
            view.setInt32(i,val,true);          //true:  little endian
          }
        };
        break;
      }
      case 16: {
        convert_func = function(ab){
          const view = new DataView(ab);
          for (let i = 0; i < view.byteLength; i += 2) {
            const val = view.getInt16(i,false); //false: big endian
            view.setInt16(i,val,true);          //true:  little endian
          }
        };
        break;
      }
      case 24: {
        convert_func = function(ab){
          const view = new DataView(ab);
          for (let i = 0; i < view.byteLength; i += 3) {
            //swap the first and last byte of the 24bit/3byte section
            const firstByte = view.getUint8(i);
            view.setUint8(i,view.getUint8(i+2));
            view.setUint8(i+2,firstByte);
          }
        };
      }
    }
    pipeline.decode = function(chunkOpts){
      convert_func(chunkOpts.data.buffer);
      decode_func.call(this,chunkOpts);
    };
  }
  pipeline.createGenerator = function(){
    if (typeof VideoTrackGenerator != "undefined") { //yes - this is the Audio pipeline, but still
      //safari only
      pipeline.setWritable({
        getWriter: function(){
          return {
            write: function(data){
              return new Promise(function(resolve,reject){
                self.addListener({
                  type: "writeframe",
                  idx: track.idx,
                  uid: data.timestamp
                }).then(function(){
                  resolve();
                }).catch(reject);

                self.postMessage({
                  type: "writeframe",
                  idx: track.idx,
                  frame: data
                },[data]);
              });
            }
          }
        }
      });
    }
  }

  if (track.init == "") {
    //this codec does not use init data, so we can configure it right away
    pipeline.configureDecoder();
  }

  return pipeline;
}
function VideoPipeline(track,opts){
  var pipeline = GenericPipeline(this,track,opts);

  var wantKey = false;
  switch (track.codec) {
    case "UYVY":
    case "YUYV": 
    case "NV12": {
      pipeline.decoder = new RawCoder(function(chunk){
        if (chunk.byteLength == 0) return;
        var a = new Uint8Array(chunk.byteLength);
        chunk.copyTo(a);
        var frame = new VideoFrame(a,{
          format: "NV12",
          timestamp: chunk.timestamp,
          codedWidth: track.width,
          codedHeight: track.height
        });
        return pipeline.onDecoderOutput(frame);
      });
      break;
    }
    default: {
      pipeline.decoder = new VideoDecoder({
        output: pipeline.onDecoderOutput,
        error: pipeline.onDecoderError
      });
    }
  }

  pipeline.decode = function (chunkOpts){
    if (wantKey) {
      if (chunkOpts.type == "key") {
        if (debugging) {
          console.log("💼","Got a "+function(t){
            switch (t) {
              case "video": return "🎞️";
              case "audio": return "🎵";
              default: return t;
            }
          }(track.type)+" key!",chunkOpts);
        }
        wantKey = false;
      }
      else {
        if (debugging) console.log("💼","Skipped "+function(t){
          switch (t) {
            case "video": return "🎞️";
            case "audio": return "🎵";
            default: return t;
          }
        }(track.type)+" frame because waiting for a key",chunkOpts);
        return;
      }
    }
    pipeline.stats.timing.decoder.start(chunkOpts.timestamp);
    return pipeline.decoder.decode(new EncodedVideoChunk(chunkOpts));
  }

  var cd = pipeline.configureDecoder;
  pipeline.configureDecoder = function(){
    var output = cd.apply(pipeline,arguments);
    if (output) wantKey = true;
    return output;
  }
  pipeline.createGenerator = function(){
    if (VideoTrackGenerator) {
      //safari only
      pipeline.trackGenerator = new VideoTrackGenerator();
      pipeline.setWritable(pipeline.trackGenerator.writable);
      //return track
      self.postMessage({
        type: "addtrack",
        track: pipeline.trackGenerator.track,
        idx: track.idx
      },pipeline.trackGenerator.track);
    }
  };

  if (track.init == "") {
    //this codec does not use init data, so we can configure it right away
    pipeline.configureDecoder();
  }

  return pipeline;
}
function ImagePipeline(track,opts) {
  var pipeline = GenericPipeline(this,track,opts);

  if (!("ImageDecoder" in self)) {
    if ("createImageBitmap" in self) {
      self.ImageDecoder = function(opts){
        this.decode = function(){
          return new Promise(function(resolve,reject){
            var blob = new Blob([opts.data.buffer],{type:opts.type});
            var url = URL.createObjectURL(blob);
            createImageBitmap(blob).then(function(bitmap){
              resolve({
                complete: true,
                image: bitmap
              });
            }).catch(reject);
          });
        }
      };
    }
    else throw "ImageDecoder is not supported in this browser";
  }

  pipeline.decode = function(chunkOpts){
    return new Promise(function(){
      var decoder = new ImageDecoder({
        type: "image/jpeg",
        data: chunkOpts.data
      });
      pipeline.stats.timing.decoder.start(chunkOpts.timestamp);
      decoder.decode().then(function(output){
        if (output.complete) {
          //create a new VideoFrame from this one, that has the correct timestamp
          var frame = new VideoFrame(output.image,{
            timestamp: chunkOpts.timestamp,
            //duration: 0
          });
          pipeline.onDecoderOutput(frame);
        }
      });
    });
  };

  pipeline.configureDecoder = function(){
    self.postMessage({ type: "addtrack", idx: track.idx });
    pipeline.decoder.state = "configured";
  };
  pipeline.decoder = {
    state: "unconfigured",
    decodeQueueSize: null,
    flush: function(){ return new Promise(function(resolve,reject){resolve();}) },
    reset: function(){},
    close: function(){}
  };
  pipeline.createGenerator = function(){
    if (VideoTrackGenerator) {
      //safari only
      pipeline.trackGenerator = new VideoTrackGenerator();
      pipeline.setWritable(pipeline.trackGenerator.writable);
      //return track
      self.postMessage({
        type: "addtrack",
        track: pipeline.trackGenerator.track,
        idx: track.idx
      },pipeline.trackGenerator.track);
    }
  };

  if (track.init == "") {
    //this codec does not use init data, so we can configure it right away
    pipeline.configureDecoder();
  }

  return pipeline;
}
function Pipeline(track,opts) {
  if (track.type == "audio") return new AudioPipeline(track,opts);
  if (track.type == "video") {
    if (track.codec == "JPEG") return new ImagePipeline(track,opts);
    return new VideoPipeline(track,opts);
  }
}

var frameTiming = new FrameTiming();
this.postMessage({
  type: "haveVideoTrackGenerator",
  value: typeof VideoTrackGenerator !== "undefined"
}); //report if VideoTrackGenerator exists in the worker scope, so that we can use it if it is available (Safari)
this.listeners = {};
this.addListener = function(opts){
  var key = [opts.type,opts.idx,opts.uid].join("_");
  return new Promise(function(resolve,reject){
    self.listeners[key] = function(msg){
      if (msg.status == "error") return reject(msg);
      resolve(msg);
      delete self.listeners[key];
    };
  });
};
function gatherStats(){
  var out = {};

  out.FrameTiming = {
    in: frameTiming.in,
    decoded: frameTiming.decoded,
    out: frameTiming.out,
    paused: frameTiming.paused,
    speed: frameTiming.speed
  };

  out.pipelines = {};
  for (var i in pipelines) {
    var p = pipelines[i];
    out.pipelines[i] = {
      early: p.stats.early,
      frame_duration: p.stats.framerate.effective.frame_duration(),
      frames: p.stats.frames,
      queues: {
        in: p.queues.in.length,
        decoder: p.decoder.decodeQueueSize || 0,
        out: p.queues.out.length
      },
      timing: {
        decoder: p.stats.timing.decoder.getCopy(),
        writable: p.stats.timing.writable.getCopy()
      }
    };
  }

  return out;
}

this.onmessage = function(e) {
  var msg = e.data;
  var answer = {
    type: msg.type,
    status: "ok",
    idx: msg.idx,
    uid: msg.uid
  };
  try {

    var key = [msg.type,msg.idx,msg.uid].join("_");
    if (key in self.listeners) {
      self.listeners[key](msg);
    }

    var pipeline;
    if ("idx" in msg) {
      pipeline = pipelines[msg.idx];
    }

    switch (msg.type) {
      case "receive": {
        if (!pipeline) throw "Invalid pipeline: "+msg.idx;
        pipeline.receive(msg.chunk);
        break;
      }
      case "writeframe": {
        //status response from main thread - do not reply to this
        return;
      }
      case "configure": {
        pipeline.configureDecoder(msg.header);
        break;
      }
      case "seek": {
        var seekTo = msg.seek_time; //[ms]

        //flush frame queue and reset decoders
        for (var i in pipelines) {
          pipelines[i].empty();
          pipelines[i].reset();
        }

        //set timestamp to target
        frameTiming.seeking = seekTo*1e3; //[microseconds]
        frameTiming.out = seekTo*1e3; //[microseconds]
        frameTiming.basetime = performance.now() - seekTo / frameTiming.speed.combined; //[milliseconds]
        if (debugging) console.log("💼","Frame timing was set to seek mode",{
          seeking: frameTiming.seeking,
          out: frameTiming.out
        });
        this.postMessage({
          type: "sendevent",
          kind: "timeupdate",
          idx: null,
          message: seekTo*1e-3 //[seconds]
        });

        break;
      }
      case "frametiming": {
        switch (msg.action) {
          case "setSpeed": {
            frameTiming.setSpeed(msg.speed,msg.tweak);
            break;
          }
          case "reset": {
            frameTiming.reset();
            break;
          }
          default: {
            console.warn("💼","Worker received unhandled message:",msg);
          }
        }
        break;
      }
      case "create": {
        if (!pipeline) pipeline = new Pipeline(msg.track,msg.opts);
        else if (debugging) {
          console.warn("💼","Pipeline for track "+msg.idx+" already exists, not creating it again");
        }
        break;
      }
      case "setwritable": {
        //webkit
        pipeline.setWritable(msg.writable);
        break;
      }
      case "creategenerator": {
        //not webkit
        if (pipeline.createGenerator) {
          pipeline.createGenerator();
        }
        else {
          throw "pipeline.createGenerator function not available"
        }
        break;
      }
      case "close": {
        if (!pipeline) {
          self.postMessage({
            type: "closed",
            idx: msg.idx
          });
        }
        else {
          pipeline.close(msg.waitEmpty).then(function(){
            //message main thread that their pipeline tracker may also be removed
            self.postMessage({
              type: "closed",
              idx: msg.idx
            });
          });
        }
        break;
      }
      case "debugging": {
        debugging = msg.value;
        break;
      }
      default: {
        console.warn("💼","Worker received unhandled message:",msg);
      }
    }

    if (debugging) {
      switch (msg.type) {
        case "receive": {
          break;
        }
        default: {
          console.log("💼","Worker received:",msg);
        }
      }
    }

  }
  catch(err) {
    answer.status = "error";
    answer.error = err;
    if (debugging) console.error("💼",err,msg);
  }

  //gather stats
  answer.stats = gatherStats();

  this.postMessage(answer);
}
