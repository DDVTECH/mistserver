var mistplayers = {}; /*TODO move this*/
function MistPlayer() {};
function mistPlay(streamName,options) {
  return new MistVideo(streamName,options);
}
function MistVideo(streamName,options) {
  var MistVideo = this;
  
  if (!options) { options = {}; }
  if (typeof mistoptions != "undefined") {
    options = MistUtil.object.extend(MistUtil.object.extend({},mistoptions),options);
  }
  options = MistUtil.object.extend({
    host: null,           //override mistserver host (default is the host that player.js is loaded from)
    autoplay: true,       //start playing when loaded
    controls: true,       //show controls (MistControls when available)
    loop: false,          //don't loop when the stream has finished
    poster: false,        //don't show an image before the stream has started
    muted: false,         //don't start muted
    callback: false,      //don't call a function when the player has finished building
    streaminfo: false,    //don't use this streaminfo but collect it from the mistserverhost
    startCombo: false,    //start looking for a player/source match at the start
    forceType: false,     //don't force a mimetype
    forcePlayer: false,   //don't force a player
    forceSource: false,   //don't force a source
    forcePriority: false, //no custom priority sorting
    monitor: false,       //no custom monitoring
    reloadDelay: false,   //don't override default reload delay
    urlappend: false,     //don't add this to urls
    setTracks: false,     //don't set tracks
    fillSpace: false,     //don't fill parent container
    width: false,         //no set width
    height: false,        //no set height
    maxwidth: false,      //no max width (apart from targets dimensions)
    maxheight: false,     //no max height (apart from targets dimensions)
    ABR_resize: true,     //for supporting wrappers: when the player resizes, request a video track that matches the resolution best
    ABR_bitrate: true,    //for supporting wrappers: when there are playback issues, request a lower bitrate video track
    MistVideoObject: false//no reference object is passed
  },options);
  if (options.host) { options.host = MistUtil.http.url.sanitizeHost(options.host); }
  
  this.options = options;
  this.stream = streamName;
  this.info = false;
  if (!window.MistInstances) { window.MistInstances = 0;}
  window.MistInstances++;
  this.n = window.MistInstances;
  this.logs = [];
  this.log = function(message,type){
    if (!type) { type = "log"; }
    var event = MistUtil.event.send(type,message,options.target);
    var data = {
      type: type
    };
    this.logs.push({
      time: new Date(),
      message: message,
      data: data
    });
    if (this.options.skin == "dev") {
      try {
        var msg = "["+(type ? type :"log")+"] "+(MistVideo.destroyed ? "[DESTROYED] " : "")+"[#"+(MistVideo.n)+"] "+(this.player && this.player.api ? MistUtil.format.time(this.player.api.currentTime,{ms:true})+" " : "")+message;
        if (type && (type != "log")) { console.warn(msg); }
        else { console.log(msg); }
      } catch(e){}
    }
    return event;
  };
  this.log("Initializing..");
  this.bootMs = new Date().getTime();
  
  this.timers = {
    list: {}, //will contain the timeouts, format timeOutIndex: endTime
    start: function(callback,delay){
      var i = setTimeout(function(){
        delete MistVideo.timers.list[i];
        if (MistVideo.destroyed) return;
        callback();
      },delay);
      this.list[i] = new Date(new Date().getTime() + delay);
      return i;
    },
    stop: function(which){
      var list;
      if (which == "all") {
        list = this.list;
      }
      else {
        list = {};
        list[which] = 1;
      }
      
      for (var i in list) {
        //MistVideo.log("Stopping timer "+i);
        clearTimeout(i);
        delete this.list[i];
      }
    }
  }
  this.errorListeners = [];
  this.resumeTime = false;
  
  this.urlappend = function(url){
    if (this.options.urlappend) {
      url += this.options.urlappend;
    }
    return url;
  }

  if (options.reloadDelay && (options.reloadDelay > 3600)) {
    options.reloadDelay /= 1000;
    this.log("A reloadDelay of more than an hour was set: assuming milliseconds were intended. ReloadDelay is now "+options.reloadDelay+"s");
  }

  
  new MistSkin(this);
  
  this.checkCombo = function(options,quiet) {
    if (!options) {
      options = {};
    }
    options = MistUtil.object.extend(MistUtil.object.extend({},this.options),options)
    
    var source = false;
    var mistPlayer = false;
        
    //retrieve the sources we can loop over
    var sources;
    if (options.forceSource) {
      sources = [MistVideo.info.source[options.forceSource]];
      MistVideo.log("Forcing source "+options.forceSource+": "+sources[0].type+" @ "+sources[0].url);
    }
    else if (options.forceType) {
      sources = MistVideo.info.source.filter(function(d){ return (d.type == options.forceType); });
      MistVideo.log("Forcing type "+options.forceType);
    }
    else {
      sources = MistVideo.info.source; 
    }
    
    //retrieve and sort the players we can loop over
    var players;
    //make sure all players have the shortname param
    for (var i in mistplayers) {
      mistplayers[i].shortname = i;
    }
    if (options.forcePlayer) {
      players = [mistplayers[options.forcePlayer]];
      MistVideo.log("Forcing player "+options.forcePlayer);
    }
    else {
      players = MistUtil.object.values(mistplayers);
    }
    
    
    //create a copy to not mess with the sorting of the original sourced array
    sources = [].concat(sources);
    
    var sortoptions = {
      first: "source",
      source: [function(a){
        if ("origIndex" in a) { return a.origIndex; }
        
        //use original sorting -> retrieve index in original array
        a.origIndex = MistVideo.info.source.indexOf(a)
        return a.origIndex;
      }],
      player: [{priority:1}]
    };
    var map = {
      inner: "player",
      outer: "source"
    };
    if (options.forcePriority) {
      if ("source" in options.forcePriority) {
        if (!(options.forcePriority.source instanceof Array)) { throw "forcePriority.source is not an array."; }
        sortoptions.source = options.forcePriority.source.concat(sortoptions.source); //prepend
        MistUtil.array.multiSort(sources,sortoptions.source);
      }
      if ("player" in options.forcePriority) {
        if (!(options.forcePriority.player instanceof Array)) { throw "forcePriority.player is not an array."; }
        sortoptions.player = options.forcePriority.player.concat(sortoptions.player); //prepend
        MistUtil.array.multiSort(players,sortoptions.player);
      }
      if ("first" in options.forcePriority) {
        sortoptions.first = options.forcePriority.first; //overwrite
      }
      
      
      //define inner and outer loops
      if (sortoptions.first == "player") {
        map.outer = "player";
        map.inner = "source";
      }
    }
    
    var variables = {
      player: {
        list: players,
        current: false
      },
      source: {
        list: sources,
        current: false
      }
    };

    if (options.startCombo) {
      options.startCombo.started = {
        player: false,
        source: false
      };
      for (var i = 0; i < players.length; i++) {
        if (players[i].shortname == options.startCombo.player) {
          options.startCombo.player = i;
          break;
        }
      }
    }

    
    function checkStartCombo(which) {
      if ((options.startCombo) && (!options.startCombo.started[which])) {
        //if we have a starting point for the loops, skip testing until we are at the correct point
        if ((options.startCombo[which] == variables[which].current) || (options.startCombo[which] == variables[which].list[variables[which].current])) {
          //we're here!
          options.startCombo.started[which] = true;
          return 1; //issue continue statement in inner loop
        }
        return 2; //always issue continue statement
      }
      return 0; //carry on!
    }
    
    outerloop:
    for (var n in variables[map.outer].list) {
      variables[map.outer].current = n;
      
      //loop over the sources (prioritized by MistServer)
      
      if (checkStartCombo(map.outer) >= 2) { continue; }
      
      innerloop:
      for (var m in variables[map.inner].list) {
        variables[map.inner].current = m;
        
        if (checkStartCombo(map.inner) >= 1) { continue; }
        
        var source = variables.source.list[variables.source.current];
        var p_shortname = variables.player.list[variables.player.current].shortname;
        var player = mistplayers[p_shortname];
        
        if (player.isMimeSupported(source.type)) {
          //this player supports this mime
          if (player.isBrowserSupported(source.type,source,MistVideo)) {
            //this browser is supported
            return {
              player: p_shortname,
              source: source,
              source_index: variables.source.current
            };
          }
        }
        if (!quiet) { MistVideo.log("Checking "+player.name+" with "+source.type+".. Nope."); }
      }
    }
    
    return false;
  }
  
  this.choosePlayer = function() {
    MistVideo.log("Checking available players..");
    
    var result = this.checkCombo();
    if (!result) { return false; }
    
    var player = mistplayers[result.player];
    var source = result.source;
    
    MistVideo.log("Found a working combo: "+player.name+" with "+source.type+" @ "+source.url);
    MistVideo.playerName = result.player;
    source = MistUtil.object.extend({},source);
    source.index = result.source_index;
    source.url = MistVideo.urlappend(source.url);
    MistVideo.source = source;
    
    MistUtil.event.send("comboChosen","Player/source combination selected",MistVideo.options.target);
    
    return true;
  }
  
  function hasVideo(d){
    if (("meta" in d) && ("tracks" in d.meta)) {
      //check if this stream has video
      var tracks = d.meta.tracks;
      var hasVideo = false;
      for (var i in tracks) {
        if (tracks[i].type == "video") {
          return true;
        }
      }
    }
    return false;
  }
  
  function onStreamInfo(d) {
    
    if ((MistVideo.player) && (MistVideo.player.api) && (MistVideo.player.api.unload)) {
      MistVideo.log("Received new stream info while a player was already loaded: unloading player");
      MistVideo.player.api.unload();
    }
    
    MistVideo.info = d;
    MistVideo.info.updated = new Date();
    MistUtil.event.send("haveStreamInfo",d,MistVideo.options.target);
    MistVideo.log("Stream info was loaded succesfully.");
    
    if ("error" in d) {
      var e = d.error;
      if ("on_error" in d) {
        MistVideo.log(e);
        e = d.on_error;
      }
      MistVideo.showError(e,{reload:true,hideTitle:true});
      return;
    }
    
    //pre-show poster or other loading image
    MistVideo.calcSize = function(size) {
      if (!size) { size = { width:false, height:false }; }
      
      var fw = size.width  || ('width'  in options && options.width  ? options.width   : false ); //force this width
      var fh = size.height || ('height' in options && options.height ? options.height  : false ); //force this height
      
      if ((!this.info) || !("source" in this.info)) {
        fw = 640;
        fh = 480;
      }
      else if ((!this.info.hasVideo) || (this.source.type.split("/")[1] == "audio")){
        if (!fw) { fw = 480; }
        if (!fh) { fh = 42; }
      }
      else {
        //calculate desired width and height
        if (!(fw && fh)) {
          var ratio = MistVideo.info.width / MistVideo.info.height;
          if (fw || fh) {
            if (fw) {
              fh = fw/ratio;
            }
            else {
              fw = fh*ratio;
            }
          }
          else {
            
            //neither width or height are being forced. Set them to the minimum of video and target size
            var cw = ('maxwidth' in options && options.maxwidth ? options.maxwidth : window.innerWidth);
            var ch = ('maxheight' in options && options.maxheight ? options.maxheight : window.innerHeight);
            var fw = MistVideo.info.width;
            var fh = MistVideo.info.height;
            
            function rescale(factor){
              fw /= factor;
              fh /= factor;
            };
            
            if (fw < 426) { //rescale if video width is smaller than 240p
              rescale(fw / 426);
            }
            if (fh < 240) { //rescale if video height is smaller than 240p
              rescale(fh / 240);
            }
            
            if (cw) {
              if (fw > cw) { //rescale if video width is larger than the target
                rescale(fw / cw);
              }
            }
            if (ch) {
              if (fh > ch) { //rescale if video height is (still?) larger than the target
                rescale(fh / ch);
              }
            }
          }
        }
      }
      this.size = {
        width: Math.round(fw),
        height: Math.round(fh)
      };
      return this.size;
    };
    
    d.hasVideo = hasVideo(d);
    
    
    if (d.type == "live") {
      //calculate duration so far
      var maxms = 0;
      for (var i in MistVideo.info.meta.tracks) {
        maxms = Math.max(maxms,MistVideo.info.meta.tracks[i].lastms);
      }
      d.lastms = maxms;
    }
    else {
      //If this is VoD and was already playing, return to the previous time
      //this is triggered when the MistInput is killed/crashes during playback
      
      var time = MistVideo.resumeTime;
      if (time) {
        var f = function(){
          if (MistVideo.player && MistVideo.player.api) {
            MistVideo.player.api.currentTime = time;
          }
          this.removeEventListener("initialized",f);
        };
        MistUtil.event.addListener(MistVideo.options.target,"initialized",f);
      }
    }
    
    //check MistServer version if combined track selection is supported
    if (MistVideo.options.ABR_bitrate && MistVideo.options.ABR_resize && (MistVideo.info && !MistVideo.info.selver)) {
      //having both won't work, disable bitrate based ABR
      MistVideo.options.ABR_bitrate = false;
    }
    
    if (MistVideo.choosePlayer()) {
     
      if (MistVideo.reporting) {
        MistVideo.reporting.report({
          player: MistVideo.playerName,
          sourceType: MistVideo.source.type,
          sourceUrl: MistVideo.source.url,
          pageUrl: location.href
        });
      }

      //build player
      MistVideo.player = new mistplayers[MistVideo.playerName].player();
      
      MistVideo.player.onreadylist = [];
      MistVideo.player.onready = function(dothis){
        this.onreadylist.push(dothis);
      };
      
      
      MistVideo.player.build(MistVideo,function(video){
        MistVideo.log("Building new player");
        
        MistVideo.container.removeAttribute("data-loading");
        MistVideo.video = video;

        if (MistVideo.reporting) {
          MistVideo.reporting.init();
        }
        
        if ("api" in MistVideo.player) {
          
          //add monitoring
          MistVideo.monitor = {
            MistVideo: MistVideo,         //added here so that the other functions can use it. Do not override it.
            delay: 1,                     //the amount of seconds between measurements.
            averagingSteps: 20,           //the amount of measurements that are saved.
            threshold: function(){        //returns the score threshold below which the "action" should be taken
              if (this.MistVideo.source.type == "webrtc") {
                return 0.95;
              }
              return 0.75;
            },
            init: function(){             //starts the monitor and defines the basic shape of the procedure it follows. This is called when the stream should begin playback.
              
              if ((this.vars) && (this.vars.active)) { return; } //it's already running, don't bother
              this.MistVideo.log("Enabling monitor");
              
              this.vars = {
                values: [],
                score: false,
                active: true
              };
              
              var monitor = this;
              //the procedure to follow
              function repeat(){
                if ((monitor.vars) && (monitor.vars.active)) {
                  monitor.vars.timer = monitor.MistVideo.timers.start(function(){
                    
                    var score = monitor.calcScore();
                    if (score !== false) {
                      if (monitor.check(score)) {
                        monitor.action();
                      }
                    }
                    repeat();
                  },monitor.delay*1e3);
                }
              }
              repeat();
              
            },
            destroy: function(){          //stops the monitor. This is called when the stream has ended or has been paused by the viewer.
              
              if ((!this.vars) || (!this.vars.active)) { return; } //it's not running, don't bother]
              
              this.MistVideo.log("Disabling monitor");
              this.MistVideo.timers.stop(this.vars.timer);
              delete this.vars;
            },
            reset: function(){            //clears the monitor’s history. This is called when the history becomes invalid because of a seek or change in the playback rate.
              
              if ((!this.vars) || (!this.vars.active)) {
                //it's not running, start it up
                this.init();
                return;
              }
              
              this.MistVideo.log("Resetting monitor");
              this.vars.values = [];
            },
            calcScore: function(){        //calculate and save the current score
              
              var list = this.vars.values;
              list.push(this.getValue()); //add the current value to the history
              
              if (list.length <= 1) { return false; } //no history yet, can't calculate a score
              
              var score = this.valueToScore(list[0],list[list.length-1]); //should be 1, decreases if bad
              
              //kick the oldest value from the array
              if (list.length > this.averagingSteps) { list.shift(); }
              
              //the final score is the maximum of the averaged and the current value
              score = Math.max(score,list[list.length-1].score);
              
              this.vars.score = score;

              if (MistVideo.reporting) {
                MistVideo.reporting.stats.set("playbackScore",Math.round(score*10)/10);
              }

              return score;
            },
            valueToScore: function(a,b){  //calculate the moving average
              //if this returns > 1, the video played faster than the clock
              //if this returns < 0, the video time went backwards
              var rate = 1;
              if (("player" in this.MistVideo) && ("api" in this.MistVideo.player) && ("playbackRate" in this.MistVideo.player.api)) {
                rate = this.MistVideo.player.api.playbackRate;
              }
              return (b.video - a.video) / (b.clock - a.clock) / rate;
            },
            getValue: function(){         //save the current testing value and time
              // If the video plays, this should keep a constant value. If the video is stalled, it will go up with 1sec/sec. If the video is playing faster, it will go down.
              //      current clock time         - current playback time
              var result = {
                clock: (new Date()).getTime()*1e-3,
                video: this.MistVideo.player.api.currentTime,
              };
              if (this.vars.values.length) {
                result.score =  this.valueToScore(this.vars.values[this.vars.values.length-1],result);
              }
              
              return result;
            },
            check: function(score){       //determine if the current score is good enough. It must return true if the score fails.
              
              if (this.vars.values.length < this.averagingSteps * 0.5) { return false; } //gather enough values first
              
              if (score < this.threshold()) {
                return true;
              }
            },
            action: function(){           //what to do when the check is failed
              var score = this.vars.score;
              
              //passive: only if nothing is already showing
              this.MistVideo.showError("Poor playback: "+Math.max(0,Math.round(score*100))+"%",{
                passive: true,
                reload: true,
                nextCombo: true,
                ignore: true,
                type: "poor_playback"
              });
            }
          };
          
          //overwrite (some?) monitoring functions/values with custom ones if specified
          if ("monitor" in MistVideo.options) {
            MistVideo.monitor.default = MistUtil.object.extend({},MistVideo.monitor);
            MistUtil.object.extend(MistVideo.monitor,MistVideo.options.monitor);
          }
          
          //enable
          var events = ["loadstart","play","playing"];
          for (var i in events) {
            MistUtil.event.addListener(MistVideo.video,events[i],function(){MistVideo.monitor.init()});
          }
          
          //disable
          var events = ["loadeddata","pause","abort","emptied","ended"];
          for (var i in events) {
            MistUtil.event.addListener(MistVideo.video,events[i],function(){
              if (MistVideo.monitor) { MistVideo.monitor.destroy(); }
            });
          }
          
          //reset
          var events = ["seeking","seeked",/*"canplay","playing",*/"ratechange"];
          for (var i in events) {
            MistUtil.event.addListener(MistVideo.video,events[i],function(){
              if (MistVideo.monitor) { MistVideo.monitor.reset(); }
            });
          }          
        }
        
        //remove placeholder and add UI structure
        
        MistUtil.empty(MistVideo.options.target);
        new MistSkin(MistVideo);
        MistVideo.container = new MistUI(MistVideo);
        MistVideo.options.target.appendChild(MistVideo.container);
        MistVideo.container.setAttribute("data-loading",""); //will be removed automatically when video loads
        
        MistVideo.video.p = MistVideo.player;
        
        //add event logging
        var events = [
        "abort","canplay","canplaythrough",/*"durationchange"*/,"emptied","ended","loadeddata","loadedmetadata","loadstart","pause","play","playing","ratechange","seeked","seeking","stalled","volumechange","waiting","metaUpdate_tracks","resizing"
        //,"timeupdate"
        ];
        for (var i in events) {
          MistUtil.event.addListener(MistVideo.video,events[i],function(e){
            MistVideo.log("Player event fired: "+e.type);
          });
        }
        MistUtil.event.addListener(MistVideo.video,"error",function(e){
          var msg;
          if (
            ("player" in MistVideo) && ("api" in MistVideo.player)
            && ("error" in MistVideo.player.api) && (MistVideo.player.api.error)
          ) {
            if ("message" in MistVideo.player.api.error) {
              msg = MistVideo.player.api.error.message;
            }
            else if (("code" in MistVideo.player.api.error) && (MistVideo.player.api.error instanceof MediaError)) {
              var human = {
                1: "MEDIA_ERR_ABORTED: The fetching of the associated resource was aborted by the user's request.",
                2: "MEDIA_ERR_NETWORK: Some kind of network error occurred which prevented the media from being successfully fetched, despite having previously been available.",
                3: "MEDIA_ERR_DECODE: Despite having previously been determined to be usable, an error occurred while trying to decode the media resource, resulting in an error.",
                4: "MEDIA_ERR_SRC_NOT_SUPPORTED: The associated resource or media provider object (such as a MediaStream) has been found to be unsuitable."
              };
              if (MistVideo.player.api.error.code in human) {
                msg = human[MistVideo.player.api.error.code];
              }
              else {
                msg = "MediaError code "+MistVideo.player.api.error.code;
              }
            }
            else {
              msg = MistVideo.player.api.error;
              if (typeof msg != "string") {
                msg = JSON.stringify(msg);
              }
            }
          }
          else {
            msg = "An error was encountered.";
            //console.log("Err:",e);
          }
          if (MistVideo.state == "Stream is online") {
            MistVideo.showError(msg);
          }
          else {
            //it was probaby an error like "PIPELINE_ERROR_READ: FFmpegDemuxer: data source error" because the live stream has ended. Print it in the log, but display the stream state instead.
            MistVideo.log(msg,"error");
            MistVideo.showError(MistVideo.state,{polling:true});
          }
          
        });
        
        //add general resize function
        if ("setSize" in MistVideo.player) {
          MistVideo.player.videocontainer = MistVideo.video.parentNode;
          MistVideo.video.currentTarget = MistVideo.options.target;
          if (!MistUtil.class.has(MistVideo.options.target,"mistvideo-secondaryVideo")) {
            //this is the main MistVideo
            MistVideo.player.resizeAll = function(){
              function findVideo(startAt,matchTarget) {
                if (startAt.video.currentTarget == matchTarget) {
                  return startAt.video;
                }
                if (startAt.secondary) {
                  for (var i = 0; i < startAt.secondary.length; i++) {
                    var result = findVideo(startAt.secondary[i].MistVideo,matchTarget);
                    if (result) { return result; }
                  }
                }
                return false;
              }
              
              //find the video that is in the main container, and resize that one
              var main = findVideo(MistVideo,MistVideo.options.target);
              if (!main) { throw "Main video not found"; }
              main.p.resize();
              
              //then, resize the secondaries
              if ("secondary" in MistVideo) {
                function tryResize(mv){
                  if (mv.MistVideo) {
                    if ("player" in mv.MistVideo) {
                      var sec = findVideo(MistVideo,mv.MistVideo.options.target);
                      if (!sec) { throw "Secondary video not found"; }
                      sec.p.resize();
                    }
                  }
                  else {
                    //player is not loaded yet, try again later
                    MistVideo.timers.start(function(){
                      tryResize(mv);
                    },0.1e3);
                  }
                }
                for (var i in MistVideo.secondary) {
                  tryResize(MistVideo.secondary[i]);
                }
              }
            };
            
          }
          MistVideo.player.resize = function(options,oldsize){
            var container = MistVideo.video.currentTarget.querySelector(".mistvideo");
            if (!oldsize) {
              oldsize = {
                width: MistVideo.video.clientWidth,
                height: MistVideo.video.clientHeight
              };
            }
            if (!container.hasAttribute("data-fullscreen")) {
              //if ((!document.fullscreenElement) || (document.fullscreenElement.parentElement != MistVideo.video.currentTarget)) {
              //first, base the size on the video dimensions
              size = MistVideo.calcSize(options);
              this.setSize(size);
              container.style.width = size.width+"px";
              container.style.height = size.height+"px";
              
              if ((MistVideo.options.fillSpace) && (!options || !options.reiterating)) {
                //if this container is set to fill the available space
                //start by fitting the video to the window size, then iterate until the container is not smaller than the video
                return this.resize({
                  width:window.innerWidth,
                  height: false,
                  reiterating: true
                },oldsize);
              }
              
              //check if the container is smaller than the video, if so, set the max size to the current container dimensions and reiterate
              if ((MistVideo.video.currentTarget.clientHeight) && (MistVideo.video.currentTarget.clientHeight < size.height)) {
                //console.log("current h:",size.height,"target h:",MistVideo.video.currentTarget.clientHeight);
                return this.resize({
                  width: false,
                  height: MistVideo.video.currentTarget.clientHeight,
                  reiterating: true
                },oldsize);
              }
              if ((MistVideo.video.currentTarget.clientWidth) && (MistVideo.video.currentTarget.clientWidth < size.width)) {
                //console.log("current w:",size.width,"target w:",MistVideo.video.currentTarget.clientWidth);
                return this.resize({
                  width: MistVideo.video.currentTarget.clientWidth,
                  height: false,
                  reiterating: true
                },oldsize);
              }

            }
            else {
              //this is the video that is in the main container, and resize this one to the screen dimensions
              size = {
                width: window.innerWidth,
                height: window.innerHeight
              }
              this.setSize(size);
            }
            if ((size.width != oldsize.width) || (size.height != oldsize.height)) {
              MistVideo.log("Player size calculated: "+size.width+" x "+size.height+" px"); 
              MistUtil.event.send("player_resize",size,MistVideo.video);
            }
            return true;
          };
          
          //if this is the main video
          if (!MistUtil.class.has(MistVideo.options.target,"mistvideo-secondaryVideo")) {
            MistUtil.event.addListener(window,"resize",function(){
              if (MistVideo.destroyed) { return; }
              MistVideo.player.resizeAll();
            },MistVideo.video);
            MistUtil.event.addListener(MistVideo.options.target,"resize",function(){
              MistVideo.player.resizeAll();
            },MistVideo.video);
            MistVideo.player.resizeAll();
          }
        }
        
        if (MistVideo.player.api) {
          //add general setSource function
          if ("setSource" in MistVideo.player.api) {
            MistVideo.sourceParams = {};
            MistVideo.player.api.setSourceParams = function(url,params){
              //append these params to the current source, overwrite if they already exist
              MistUtil.object.extend(MistVideo.sourceParams,params);
              
              MistVideo.player.api.setSource(MistUtil.http.url.addParam(url,params));
            };
            
            //add track selection function
            if (!("setTracks" in MistVideo.player.api)) {
              MistVideo.player.api.setTracks = function(usetracks){
                
                //check tracks exist
                var meta = MistUtil.tracks.parse(MistVideo.info.meta.tracks);
                for (var i in usetracks) {
                  if ((i in meta) && ((usetracks[i] in meta[i]) || (usetracks[i] == "none"))) { continue; }
                  MistVideo.log("Skipping trackselection of "+i+" track "+usetracks[i]+" because it does not exist");
                  delete usetracks[i];
                }
                //if (!MistUtil.object.keys(usetracks).length) { return; } //don't do this; allow switching back to auto
                
                //create source url
                var newurl = MistVideo.source.url;                
                var time = MistVideo.player.api.currentTime;
                
                //actually switch to the new source url
                this.setSourceParams(newurl,usetracks);
                
                //restore video position
                if (MistVideo.info.type != "live") {
                  var f = function(){
                    MistVideo.player.api.currentTime = time;
                    this.removeEventListener("loadedmetadata",f);
                  };
                  MistUtil.event.addListener(MistVideo.video,"loadedmetadata",f);
                }
                
              }
              
            }
            
            
          }
          //add general setTracks function if setTrack exists
          if (!("setTracks" in MistVideo.player.api) && ("setTrack" in MistVideo.player.api)) {
            MistVideo.player.api.setTracks = function(usetracks){
              for (var i in usetracks) {
                MistVideo.player.api.setTrack(i,usetracks[i]);
              }
            };
          }
          
          if (options.setTracks) {
            var setTracks = MistUtil.object.extend({},options.setTracks);
            if (("subtitle" in options.setTracks) && ("setSubtitle" in MistVideo.player.api)) {
              MistVideo.player.onready(function(){
                
                //find the source for subtitles
                var subtitleSource = false;
                for (var i in MistVideo.info.source) {
                  var source = MistVideo.info.source[i];
                  //this is a subtitle source, and it's the same protocol (HTTP/HTTPS) as the video source
                  if ((source.type == "html5/text/vtt") && (MistUtil.http.url.split(source.url).protocol == MistUtil.http.url.split(MistVideo.source.url).protocol)) {
                    subtitleSource = source.url.replace(/.srt$/,".vtt");
                    break;
                  }
                }
                if (!subtitleSource) { return; }
                
                //find the track meta information
                var tracks = MistUtil.tracks.parse(MistVideo.info.meta.tracks);
                if (!("subtitle" in tracks) || !(setTracks.subtitle in tracks.subtitle)) { return; }
                meta = tracks.subtitle[setTracks.subtitle];
                
                //add source to the meta
                meta.src = MistUtil.http.url.addParam(subtitleSource,{track:setTracks.subtitle});
                
                meta.label = "automatic";
                meta.lang = "unknown";
                
                MistVideo.player.api.setSubtitle(meta);
                MistUtil.event.send("playerUpdate_trackChanged",{
                  type: "subtitle",
                  trackid: setTracks.subtitle
                }, MistVideo.video);
                
                delete setTracks.subtitle;
              });
            }
            
            if ("setTrack" in MistVideo.player.api) {
              MistVideo.player.onready(function(){
                for (var i in setTracks) {
                  MistVideo.player.api.setTrack(i,setTracks[i]);
                  MistUtil.event.send("playerUpdate_trackChanged",{
                    type: i,
                    trackid: setTracks[i]
                  }, MistVideo.video);
                }
              });
            }
            else if ("setTracks" in MistVideo.player.api) {
              MistVideo.player.onready(function(){
                MistVideo.player.api.setTracks(setTracks);
              });
              for (var i in setTracks) {
                MistUtil.event.send("playerUpdate_trackChanged",{
                  type: i,
                  trackid: setTracks[i]
                }, MistVideo.video);
              }
            }
          }
          
          if (MistVideo.player.api.ABR_resize && MistVideo.options.ABR_resize) {
            var resizeratelimiter = false;
            var newsize = false;
            MistUtil.event.addListener(MistVideo.video,"player_resize",function(e){
              if (MistVideo.options.setTracks && MistVideo.options.setTracks.video) {
                //trackselection is not set to 'automatic'
                return; 
              }

              //Whenever the player resizes, start a timer. When the timer ends, request the correct video track. When the player resizes before the timer ends, stop it: track request is sent 1s after the player has the new size

              if (resizeratelimiter) {
                MistVideo.timers.stop(resizeratelimiter);
              }
              resizeratelimiter = MistVideo.timers.start(function(){
                MistVideo.player.api.ABR_resize(e.message);
                resizeratelimiter = false;
              },1e3);
              
            });

            MistUtil.event.addListener(MistVideo.video,"trackSetToAuto",function(e){
              //the user selected automatic track selection, update the track resolution
              if (e.message == "video") {
                MistVideo.player.api.ABR_resize({
                  width: MistVideo.video.clientWidth,
                  height: MistVideo.video.clientHeight
                });
              }
            });
            //initialize
            MistVideo.player.api.ABR_resize({
              width: MistVideo.video.clientWidth,
              height: MistVideo.video.clientHeight
            });

          }
        }
        
        for (var i in MistVideo.player.onreadylist) {
          MistVideo.player.onreadylist[i]();
        }
        
        MistUtil.event.send("initialized",null,options.target);
        MistVideo.log("Initialized");

        if (MistVideo.options.callback) { options.callback(MistVideo); }
        
      });
    }
    else if (MistVideo.options.startCombo) {
      //try again without a startCombo
      delete MistVideo.options.startCombo;
      MistVideo.unload("No compatible players found - retrying without startCombo.");
      mistPlay(MistVideo.stream,MistVideo.options);
    }
    else {
      MistVideo.showError("No compatible player/source combo found.",{reload:true});
      MistUtil.event.send("initializeFailed",null,options.target);
      MistVideo.log("Initialization failed");
    }
  }
  
  MistVideo.calcSize = function(){
    return {
      width: 640,
      height: 480
    };
  };
  
  //load placeholder
  MistUtil.empty(MistVideo.options.target);
  new MistSkin(MistVideo);
  MistVideo.container = new MistUI(MistVideo,MistVideo.skin.structure.placeholder);
  MistVideo.options.target.appendChild(MistVideo.container);
  MistVideo.container.setAttribute("data-loading","");
  
  //listen for changes to the srteam status
  //switch to polling-mode if websockets are not supported
  
  function openWithGet() {
    var url = MistVideo.urlappend(options.host+"/json_"+encodeURIComponent(MistVideo.stream)+".js");
    MistVideo.log("Requesting stream info from "+url);
    MistUtil.http.get(url,function(d){
      if (MistVideo.destroyed) { return; }
      onStreamInfo(JSON.parse(d));
    },function(xhr){
      var msg = "Connection failed: the media server may be offline";
      MistVideo.showError(msg,{reload:30});
      if (!MistVideo.info) {
        MistUtil.event.send("initializeFailed",null,options.target);
        MistVideo.log("Initialization failed");
      }
    });
  }
  
  if ("WebSocket" in window) {
    function openSocket() {
      MistVideo.log("Opening stream status stream..");
      var url = MistVideo.options.host.replace(/^http/i,"ws");
      var socket = new WebSocket(MistVideo.urlappend(url+"/json_"+encodeURIComponent(MistVideo.stream)+".js"));
      MistVideo.socket = socket;
      socket.die = false;
      socket.destroy = function(){
        this.die = true;
        if (MistVideo.reporting) {
          MistVideo.reporting.reportStats();
          MistVideo.reporting = false;
        }
        this.onclose = function(){};
        this.close();
      };
      //add a timeout: if the websocket does not connect, switch to http polling
      socket.timeOut = MistVideo.timers.start(function(){
        if (socket.readyState <= 1) {
          //either it hasn't opened yet, or it is open but we've not received a message so this timer hasn't been removed yet
          socket.destroy();
          openWithGet();
        }
      },5e3);
      socket.onopen = function(e){
        this.wasConnected = true;

        //report player status to MistServer
        if (!MistVideo.reporting) {
          MistVideo.reporting = {
            stats: {
              set: function(key,value){
                this.d[key] = value;
              },
              add: function(key,add){
                if (typeof add == "undefined") { add = 1; }
                this.d[key] += add;
              },
              d: {
                nWaiting: 0,
                timeWaiting: 0,
                nStalled: 0,
                timeStalled: 0,
                timeUnpaused: 0,
                nError: 0,
                nLog: 0,
                videoHeight: null,
                videoWidth: null,
                playerHeight: null,
                playerWidth: null
              },
              last: {
                firstPlayback: null,
                nWaiting: 0,
                timeWaiting: 0,
                nStalled: 0,
                timeStalled: 0,
                timeUnpaused: 0,
                nError: 0,
                lastError: null,
                playbackScore: 1,
                nLog: 0,
                autoplay: null,
                videoHeight: null,
                videoWidth: null,
                playerHeight: null,
                playerWidth: null
              }
            },
            report: function(d){
              if (MistVideo.socket.readyState == 1) {
                MistVideo.socket.send(JSON.stringify(d));
              }
            },
            reportStats: function(){
              var d = {};
              var report = false;
              var newlogs = MistVideo.logs.slice(this.stats.last.nLog);
              for (var i in this.stats.d) {
                if (this.stats.d[i] != this.stats.last[i]) {
                  d[i] = this.stats.d[i];
                  this.stats.last[i] = d[i];
                  report = true;
                }
              }
              if (report) {
                if (newlogs.length) {
                  d.logs = [];
                  for (var i in newlogs) {
                    d.logs.push(newlogs[i].message);
                  }
                }
                this.report(d);
              }
              MistVideo.timers.start(function(){
                if (MistVideo.reporting) { MistVideo.reporting.reportStats(); }
              },5e3);
            },
            init: function(){
              var video = MistVideo.video;

              var firstPlay = MistUtil.event.addListener(video,"playing",function(){
                MistVideo.reporting.stats.set("firstPlayback",new Date().getTime() - MistVideo.bootMs);
                MistUtil.event.removeListener(firstPlay);
              });

              //set listeners for player reporting
              MistUtil.event.addListener(video,"waiting",function(){
                MistVideo.reporting.stats.add("nWaiting");
              });
              MistUtil.event.addListener(video,"stalled",function(){
                MistVideo.reporting.stats.add("nStalled");
              });
              MistUtil.event.addListener(MistVideo.options.target,"error",function(e){
                MistVideo.reporting.stats.add("nError");
                MistVideo.reporting.stats.set("lastError",e.message);
              },video); //remove event listener when the player is removed

              if (Object && Object.defineProperty) {
                var timeWaiting = 0;
                var waitingSince = false;
                var timeStalled = 0;
                var stalledSince = false;
                var timeUnpaused = 0;
                var unpausedSince = false;
                var d = MistVideo.reporting.stats.d;
                Object.defineProperty(d,"timeWaiting",{
                  get: function(){
                    return timeWaiting + (waitingSince ? (new Date()).getTime() - waitingSince : 0);
                  }
                });
                Object.defineProperty(d,"timeStalled",{
                  get: function(){
                    return timeStalled + (stalledSince ? (new Date()).getTime() - stalledSince : 0);
                  }
                });
                Object.defineProperty(d,"timeUnpaused",{
                  get: function(){
                    return timeUnpaused + (unpausedSince ? (new Date()).getTime() - unpausedSince : 0);
                  }
                });
                Object.defineProperty(d,"nLog",{
                  get: function(){
                    return MistVideo.logs.length;
                  }
                });
                Object.defineProperty(d,"videoHeight",{
                  get: function(){
                    return MistVideo.video ? MistVideo.video.videoHeight : null;
                  }
                });
                Object.defineProperty(d,"videoWidth",{
                  get: function(){
                    return MistVideo.video ? MistVideo.video.videoWidth : null;
                  }
                });
                Object.defineProperty(d,"playerHeight",{
                  get: function(){
                    return MistVideo.video ? MistVideo.video.clientHeight : null;
                  }
                });
                Object.defineProperty(d,"playerWidth",{
                  get: function(){
                    return MistVideo.video ? MistVideo.video.clientWidth : null;
                  }
                });

                MistUtil.event.addListener(video,"waiting",function(){
                  timeWaiting = d.timeWaiting; //in case we get waiting several times in a row
                  waitingSince = (new Date()).getTime();
                });
                MistUtil.event.addListener(video,"stalled",function(){
                  timeStalled = d.timeStalled; //in case we get stalled several times in a row
                  stalledSince = (new Date()).getTime();
                });
                var events = ["playing","pause"];
                for (var i in events) {
                  MistUtil.event.addListener(video,events[i],function(){
                    timeWaiting = d.timeWaiting;
                    timeStalled = d.timeStalled;
                    waitingSince = false;
                    stalledSince = false;
                  });
                }
                MistUtil.event.addListener(video,"playing",function(){
                  timeUnpaused = d.timeUnpaused; //in case we get playing several times in a row
                  unpausedSince = (new Date()).getTime();
                });
                MistUtil.event.addListener(video,"pause",function(){
                  timeUnpaused = d.timeUnpaused;
                  unpausedSince = false;
                });



              }


              //periodically send the gathered stats
              this.reportStats();

            }
          };
        }

      };
      socket.onclose = function(e){
        if (this.die) {
          //it's supposed to go down
          return;
        }
        if (this.wasConnected) {
          MistVideo.log("Reopening websocket..");
          openSocket();
          return;
        }
        
        openWithGet();
        
      };
      var on_ended_show_state = false;
      var on_waiting_show_state = false;
      socket.addEventListener("message",function(e){
        if (socket.timeOut) {
          MistVideo.timers.stop(socket.timeOut);
          socket.timeOut = false;
        }

        var data = JSON.parse(e.data);
        if (!data) { MistVideo.showError("Error while parsing stream status stream. Obtained: "+e.data.toString(),{reload:true}); }
        
        
        if ("error" in data) {
          var e = data.error;
          if ("on_error" in data) {
            MistVideo.log(e);
            e = data.on_error;
          }
          MistVideo.state = data.error;
          var buttons;
          switch (data.error) {
            case "Stream is offline":
              MistVideo.info = false;
              if (MistVideo.player && MistVideo.player.api && MistVideo.player.api.currentTime) {
                MistVideo.resumeTime = MistVideo.player.api.currentTime;
              }
            case "Stream is initializing":
            case "Stream is booting":
            case "Stream is waiting for data":
            case "Stream is shutting down":
            case "Stream status is invalid?!":
              if ((MistVideo.player) && (MistVideo.player.api) && (!MistVideo.player.api.paused)) {
                //something is (still) playing
                MistVideo.log(data.error,"error");
                
                //on ended, show state
                if (!on_ended_show_state) {
                  on_ended_show_state = MistUtil.event.addListener(MistVideo.video,"ended",function(){
                    MistVideo.showError(data.error,{polling:true});
                  });
                }
                if (!on_waiting_show_state) {
                  on_ended_show_state = MistUtil.event.addListener(MistVideo.video,"waiting",function(){
                    MistVideo.showError(data.error,{polling:true});
                  });
                }
                
                return;
              }
              buttons = {polling:true};
              break;
            default:
              buttons = {reload:true};
          }
          
          MistVideo.showError(e,buttons);
        }
        else {
          //new metadata object!
          //console.log("stream status stream said",data);
          MistVideo.state = "Stream is online";
          MistVideo.clearError();
          if (on_ended_show_state) { MistUtil.event.removeListener(on_ended_show_state); }
          if (on_waiting_show_state) { MistUtil.event.removeListener(on_waiting_show_state); }
          
          if (!MistVideo.info) {
            onStreamInfo(data);
            return;
          }
          
          //figure out what changed
          
          //calculate the changes. note: ignores missing keys in the new data
          function difference(a,b) {
            if (a == b) { return false; }
            if ((typeof a == "object") && (typeof b != "undefined")) {
              var results = {};
              for (var i in a) {
                
                //ignore certain keys for which we don't care about changes
                if (MistUtil.array.indexOf(["lastms","hasVideo"],i) >= 0) { continue; }
                
                var d = difference(a[i],b[i]);
                //console.log(i,a[i],b[i],d);
                if (d) {
                  if (d === true) {
                    results[i] = [a[i],b[i]];
                  }
                  else {
                    results[i] = d;
                  }
                }
              }
              //also show keys in b that are not in a
              for (var i in b) {
                
                //ignore certain keys for which we don't care about changes
                if (MistUtil.array.indexOf(["lastms","hasVideo"],i) >= 0) { continue; }
                
                if (!(i in a)) {
                  results[i] = [a[i],b[i]];
                }
              }
              
              //add this check: [1,2] == [1,2] -> false
              if (MistUtil.object.keys(results).length) { return results; }
              return false;
            }
            return true;
          }
          var diff = difference(data,MistVideo.info);
          if (diff) {
            //console.log("Difference",diff,data,MistVideo.info);
            
            if ("source" in diff) {
              if ("error" in MistVideo.info) {
                MistVideo.reload("Reloading, stream info has error");
              }
              return;
            }
            
            MistVideo.info = MistUtil.object.extend(MistVideo.info,data);
            MistVideo.info.updated = new Date();
            
            var resized = false;
            
            for (var i in diff) {
              switch (i) {
                case "meta": {
                  for (var j in diff[i]) {
                    switch (j) {
                      case "tracks":
                        //if difference in tracks, recalculate info.hasVideo
                        MistVideo.info.hasVideo = hasVideo(MistVideo.info);
                        
                        //signal track selector to refresh
                        MistUtil.event.send("metaUpdate_tracks",data,MistVideo.video);
                        
                        break;
                    }
                  }
                  break;
                }
                case "width":
                case "height": {
                  resized = true;
                  break;
                }
              }
            }
            
            if (resized) {
              //call resize function
              MistVideo.player.resize();
            }
            
          }
          else {
            MistVideo.log("Metachange: no differences detected");
          }
          
        }
        
      });
      

    }
    openSocket();
  }
  else {
    openWithGet();
  }
  
  this.unload = function(reason){
    if (this.destroyed) { return; }
    
    this.log("Unloading..");
    this.destroyed = true;
    
    this.timers.stop("all");
    for (var i in this.errorListeners) {
      var listener = this.errorListeners[i];
      if (listener.src in MistUtil.scripts.list) {
        var index = MistUtil.array.indexOf(MistUtil.scripts.list[listener.src].subscribers);
        if (index >= 0) {
          MistUtil.scripts.list[listener.src].subscribers.splice(index,1);
        }
      }
    }
    if (("monitor" in MistVideo) && ("destroy" in MistVideo.monitor)) {
      MistVideo.monitor.destroy();
    }
    if (this.socket) {
      if (this.reporting) {
        this.reporting.reportStats();
        this.reporting.report({unload:reason ? reason : null});
      }
      this.socket.destroy();
    }
    if ((this.player) && (this.player.api)) {
      if ("pause" in this.player.api) { this.player.api.pause(); }
      if ("setSource" in this.player.api) {
        this.player.api.setSource("");
        //this.element.load(); //don't use this.load() to avoid interrupting play/pause
      }
      if ("unload" in this.player.api) { 
        try { this.player.api.unload(); }
        catch (e) {
          MistVideo.log("Error while unloading player: "+e.message);
        }
      }
    }
    if ((this.UI) && (this.UI.elements)) {
      for (var i in this.UI.elements) {
        var e = this.UI.elements[i];
        if ("attachedListeners" in e) {
          //remove attached event listeners
          for (var i in e.attachedListeners) {
            MistUtil.event.removeListener(e.attachedListeners[i]);
          }
        }
        if (e.parentNode) {
          e.parentNode.removeChild(e);
        }
      }
    }
    if (this.video) { MistUtil.empty(this.video); }
    if ("container" in this) { MistUtil.empty(this.container); delete this.container; }
    MistUtil.empty(this.options.target);
    delete this.video;
    
  };
  this.reload = function(reason){
    var time = ("player" in this && "api" in this.player ? this.player.api.currentTime : false);
    
    this.unload(reason);
    var NewMistVideo = mistPlay(this.stream,this.options);
    
    if ((time) && (this.info.type != "live")) {
      //after load, try to restore the video position
      var f = function(){
        if (NewMistVideo.player && NewMistVideo.player.api) {
          NewMistVideo.player.api.currentTime = time;
        }
        this.removeEventListener("initialized",f);
      };
      MistUtil.event.addListener(this.options.target,"initialized",f);
    }
    
    return MistVideo;
  };
  this.nextCombo = function(){
    
    var time = false;
    if (("player" in this) && ("api" in this.player)) { time = this.player.api.currentTime; }
    
    var startCombo = {
      source: this.source.index,
      player: this.playerName
    };
    if (!this.checkCombo({startCombo:startCombo},true)) {
      //the nextCombo won't yield a result
      if (this.checkCombo({startCombo: false},true)) {
        //..but resetting the startcombo would
        startCombo = false;
      }
      else {
        return;
      }
    }
    
    this.unload("nextCombo");
    var opts = this.options;
    opts.startCombo = startCombo;
    MistVideo = mistPlay(this.stream,opts);
    
    if ((time) && (isFinite(time) && (this.info.type != "live"))) {
      //after load, try to restore the video position
      var f = function(){
        if (("player" in MistVideo) && ("api" in MistVideo.player)) { MistVideo.player.api.currentTime = time; }
        this.removeEventListener("initialized",f);
      };
      MistUtil.event.addListener(opts.target,"initialized",f);
    }
    
  };
  this.onPlayerBuilt = function(){};
  
  if (options.MistVideoObject) {
    options.MistVideoObject.reference = this;
  }
  
  return this;
}
