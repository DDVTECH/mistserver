mistplayers.videojs = {
  name: "VideoJS player",
  mimes: ["html5/application/vnd.apple.mpegurl","html5/application/vnd.apple.mpegurl;version=7"],
  priority: MistUtil.object.keys(mistplayers).length + 1,
  isMimeSupported: function (mimetype) {
    return (MistUtil.array.indexOf(this.mimes,mimetype) == -1 ? false : true);
  },
  isBrowserSupported: function (mimetype,source,MistVideo) {
    
    //check for http/https mismatch
    if (location.protocol != MistUtil.http.url.split(source.url).protocol) {
      MistVideo.log("HTTP/HTTPS mismatch for this source");
      return false;
    }
    
    //don't use videojs if this location is loaded over file://
    if ((location.protocol == "file:") && (mimetype == "html5/application/vnd.apple")) {
      MistVideo.log("This source ("+mimetype+") won't load if the page is run via file://");
      return false;
    }

    function checkPlaybackOfTrackTypes(mime) {
      if (!MediaSource.isTypeSupported) { return true; } //we can't ask, but let's assume something will work

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
        playabletracks[MistVideo.info.meta.tracks[i].type][MistUtil.tracks.translateCodec(MistVideo.info.meta.tracks[i])] = 1;
      }

      var tracktypes = [];
      for (var type in playabletracks) {
        var playable = false;

        for (var codec in playabletracks[type]) {
          if (MediaSource.isTypeSupported(mime+";codecs=\""+codec+"\"")) {
            playable = true;
            break;
          }
        }
        if (playable) {
          tracktypes.push(type);
        }
      }
      if (hassubtitles) {
        //there is a subtitle track, check if there is a webvtt source
        for (var i in MistVideo.info.source) {
          if (MistVideo.info.source[i].type == "html5/text/vtt") {
            tracktypes.push("subtitle");
            break;
          }
        }
      }

      return tracktypes.length ? tracktypes : false;
    }

    //can this browser play this natively?
    if (document.createElement("video").canPlayType(mimetype.replace("html5/",""))) {

      //we can't ask, but let's assume something will work
      if (!("MediaSource" in window)) { return true; }
      if (!MediaSource.isTypeSupported) { return true; }

      return checkPlaybackOfTrackTypes(mimetype.replace("html5/","")); 
    }

    if (!("MediaSource" in window)) { return false; }
    return checkPlaybackOfTrackTypes("video/mp4");
  },
  player: function(){},
  scriptsrc: function(host) { return host+"/videojs.js"; }
};
var p = mistplayers.videojs.player;
p.prototype = new MistPlayer();
p.prototype.build = function (MistVideo,callback) {
  var me = this; //to allow nested functions to access the player class itself
  
  var ele;
  function onVideoJSLoad () {
    if (MistVideo.destroyed) { return;}
    
    MistVideo.log("Building VideoJS player..");
    
    ele = document.createElement("video");
    if (MistVideo.source.type != "html5/video/ogg") {
      ele.crossOrigin = "anonymous"; //required for subtitles, but if ogg, the video won"t load
    }
    ele.setAttribute("playsinline",""); //for apple
    
    var shortmime = MistVideo.source.type.split("/");
    if (shortmime[0] == "html5") {
      shortmime.shift();
    }
    
    var source = document.createElement("source");
    source.setAttribute("src",MistVideo.source.url);
    me.source = source;
    ele.appendChild(source);
    source.type = shortmime.join("/");
    MistVideo.log("Adding "+source.type+" source @ "+MistVideo.source.url);
    //if (source.type.indexOf("application/vnd.apple.mpegurl") >= 0) { source.type = "application/x-mpegURL"; }
    //source.type = "application/vnd.apple.mpegurl";
    
    MistUtil.class.add(ele,"video-js");
    
    var vjsopts = {};
    
    if (MistVideo.options.autoplay) { vjsopts.autoplay = true; }
    if ((MistVideo.options.loop) && (MistVideo.info.type != "live")) {
      //vjsopts.loop = true;
      ele.setAttribute("loop","");
    }
    if (MistVideo.options.muted) {
      //vjsopts.muted = true;
      ele.setAttribute("muted","");
    }
    if (MistVideo.options.poster) { vjsopts.poster = MistVideo.options.poster; }
    if (MistVideo.options.controls == "stock") {
      ele.setAttribute("controls","");
      if (!document.getElementById("videojs-css")) {
        var style = document.createElement("link");
        style.rel = "stylesheet";
        style.href = MistVideo.options.host+"/skins/videojs.css";
        style.id = "videojs-css";
        document.head.appendChild(style);
      }
    }
    else {
      vjsopts.controls = false;
    }

    //capture any errors generated before videojs is initialized and ignore them
    var captureErrors = MistUtil.event.addListener(ele,"error",function(e){
      e.stopImmediatePropagation();
      
      var msg = e.message;
      if (!msg && ele.error) {
        if (("code" in ele.error) && (ele.error.code)) {
          msg = "Code "+ele.error.code;
          for (var i in ele.error) {
            if (i == "code") { continue; }
            if (ele.error[i] == ele.error.code) {
              msg = i;
              break;
            }
          }
        }
        else {
          msg = JSON.stringify(ele.error);
        }
      }
      MistVideo.log("Error captured and stopped because videojs has not yet loaded: "+msg);  
    });
    
    //for android < 7, enable override native
    function androidVersion(){
      var match = navigator.userAgent.toLowerCase().match(/android\s([\d\.]*)/i);
      return match ? match[1] : false;
    }
    var android = MistUtil.getAndroid();
    if (android && (parseFloat(android) < 7)) {
      MistVideo.log("Detected android < 7: instructing videojs to override native playback");
      vjsopts.html5 = {hls: {overrideNative: true}};
      vjsopts.nativeAudioTracks = false;
      vjsopts.nativeVideoTracks = false;
    }
    
    me.onready(function(){
      MistVideo.log("Building videojs");
      me.videojs = videojs(ele,vjsopts,function(){
        //remove error grabbing
        MistUtil.event.removeListener(captureErrors);

        MistVideo.log("Videojs initialized");
        
        if (MistVideo.info.type == "live") {
          //overwrite the stream info's buffer window to the seekable range as indicated by the m3u8
          MistUtil.event.addListener(ele,"progress",function(e){
            var i = MistVideo.player.videojs.seekable().length-1;
            MistVideo.info.meta.buffer_window = (Math.max(MistVideo.player.videojs.seekable().end(i),ele.duration) - MistVideo.player.videojs.seekable().start(i))*1e3;
          });
        }
      });
      
      MistUtil.event.addListener(ele,"error",function(e){
        if (e && e.target && e.target.error && e.target.error.message && (MistUtil.array.indexOf(e.target.error.message,"NS_ERROR_DOM_MEDIA_OVERFLOW_ERR") >= 0)) {
          //there is a problem with a certain segment, try reloading
          MistVideo.timers.start(function(){
            MistVideo.log("Reloading player because of NS_ERROR_DOM_MEDIA_OVERFLOW_ERR");
            MistVideo.reload();
          },1e3);
        }
      });
      
      me.api.unload = function(){
        if (me.videojs) {
          me.videojs.autoplay(false); //don't play again ffs
          me.videojs.pause(); //pause goddamn
          me.videojs.dispose(); //and now die, bitch
          me.videojs = false;
          MistVideo.log("Videojs instance disposed");
        }
      };
      
    });
    
    MistVideo.log("Built html");
    
    if (("Proxy" in window) && ("Reflect" in window)) {
      var overrides = {
        get: {},
        set: {}
      };
      
      MistVideo.player.api = new Proxy(ele,{
        get: function(target, key, receiver){
          if (key in overrides.get) {
            return overrides.get[key].apply(target, arguments);
          }
          var method = target[key];
          if (typeof method === "function"){
            return function () {
              return method.apply(target, arguments);
            }
          }
          return method;
        },
        set: function(target, key, value) {
          if (key in overrides.set) {
            return overrides.set[key].call(target,value);
          }
          return target[key] = value;
        }
      });
      MistVideo.player.api.load = function(){};
      
      overrides.set.currentTime = function(value){
        MistVideo.player.videojs.currentTime(value); //seeking backwards does not work if we set it on the video directly
        //MistVideo.video.currentTime = value;
      };
      
      //get first and lastms
      var lastms = 0;
      var firstms = Infinity;
      for (var i in MistVideo.info.meta.tracks) {
        lastms = Math.max(lastms,MistVideo.info.meta.tracks[i].lastms);
        firstms = Math.min(firstms,MistVideo.info.meta.tracks[i].firstms);
      }
      //correct the currentTime timestamp
      var correction = firstms*1e-3;

      overrides.get.duration = function(){
        if (MistVideo.info) {
          var duration = ele.duration;
          return duration + correction;
        }
        return 0;
      };
      
      MistUtil.event.addListener(ele,"progress",function(){
        MistVideo.player.api.lastProgress = new Date();
      });
      overrides.set.currentTime = function(value){
        var diff = MistVideo.player.api.currentTime - value;
        var offset = value - MistVideo.player.api.duration;
        
        MistVideo.log("Seeking to "+MistUtil.format.time(value)+" ("+Math.round(offset*-10)/10+"s from live)");
        MistVideo.player.videojs.currentTime(MistVideo.video.currentTime - diff);
      }
      overrides.get.currentTime = function(){
        var time = MistVideo.player.videojs ? MistVideo.player.videojs.currentTime() : ele.currentTime;
        if (isNaN(time)) { return 0; }
        return time + correction;
      }
      overrides.get.buffered = function(){
        var buffered = MistVideo.player.videojs ? MistVideo.player.videojs.buffered() : ele.buffered;
        return {
          length: buffered.length,
          start: function(i) { return buffered.start(i) + correction; },
          end: function(i) { return buffered.end(i) + correction; }
        }
      };

      if (MistVideo.info.type == "live") {

        MistVideo.player.api.lastProgress = new Date();
        MistVideo.player.api.liveOffset = 0;

      }
    }
    else {
      me.api = ele;
    }
    
    MistVideo.player.setSize = function(size){
      if ("videojs" in MistVideo.player) {
        MistVideo.player.videojs.dimensions(size.width,size.height);
        
        //for some reason, the videojs' container won't be resized with the method above.
        //so let's cheat and do it ourselves
        ele.parentNode.style.width = size.width+"px";
        ele.parentNode.style.height = size.height+"px";
      }
      this.api.style.width = size.width+"px";
      this.api.style.height = size.height+"px";
    };
    MistVideo.player.api.setSource = function(url) {
      if (!MistVideo.player.videojs) { return; }
      if (MistVideo.player.videojs.src() != url) {
        MistVideo.player.videojs.src({
          type: MistVideo.player.videojs.currentSource().type,
          src: url
        });
      }
    };
    MistVideo.player.api.setSubtitle = function(trackmeta) {
      //remove previous subtitles
      var tracks = ele.getElementsByTagName("track");
      for (var i = tracks.length - 1; i >= 0; i--) {
        ele.removeChild(tracks[i]);
      }
      if (trackmeta) { //if the chosen track exists
        //add the new one
        var track = document.createElement("track");
        ele.appendChild(track);
        track.kind = "subtitles";
        track.label = trackmeta.label;
        track.srclang = trackmeta.lang;
        track.src = trackmeta.src;
        track.setAttribute("default","");
      }
    };
    
    if (MistVideo.info.type == "live") {
      
      //for some reason, videojs doesn't always fire the canplay event ???
      //mitigate by sending one when durationchange follows loadstart
      
      var loadstart = MistUtil.event.addListener(ele,"loadstart",function(e){
        MistUtil.event.removeListener(loadstart);
        MistUtil.event.send("canplay",false,this);
      });
      var canplay = MistUtil.event.addListener(ele,"canplay",function(e){
        //remove the listener
        if (loadstart) { MistUtil.event.removeListener(loadstart); }
        MistUtil.event.removeListener(canplay);
      });
      
    }
    
    callback(ele);
  }
  
  if ("videojs" in window) {
    onVideoJSLoad();
  }
  else {
    //load the videojs player
    
    var timer = false;
    function reloadVJSrateLimited(){
      
      try {
        MistVideo.video.pause();
      } catch (e) {}
      MistVideo.showError("Error in videojs player");
      
      //rate limit the reload
      if (!window.mistplayer_videojs_failures) {
        window.mistplayer_videojs_failures = 1;
        MistVideo.reload();
      }
      else {
        if (!timer) { 
          var delay = 0.05*Math.pow(2,window.mistplayer_videojs_failures)
          MistVideo.log("Rate limiter activated: MistPlayer reload delayed by "+Math.round(delay*10)/10+" seconds.","error");
          timer = MistVideo.timers.start(function(){
            timer = false;
            delete window.videojs;
            MistVideo.reload();
          },delay*1e3);
          window.mistplayer_videojs_failures++;
        }
      }
    }
    
    var scripturl = MistVideo.urlappend(mistplayers.videojs.scriptsrc(MistVideo.options.host));
    var scripttag;
    var f = function (msg, url, lineNo, columnNo, error) {
      if (!scripttag) { return; }
      
      if (url == scripttag.src) {
        //error in internal videojs code
        //console.error(me.videojs,MistVideo.video,ele,arguments);
        window.removeEventListener("error",f);
        reloadVJSrateLimited();
      }
      
      return false;
    };
    window.addEventListener("error",f);
    
    //disabled for now because it seemed to cause more issues than it solved
    /*var old_console_error = console.error;
    console.error = function(){
      if (arguments[0] == "VIDEOJS:") {
        if ((arguments.length > 3) && arguments[4] && (arguments[4].code == 3)) { return; } //it's a decoding  error, nothing in videojs itself
        //videojs reports an error
        console.error = old_console_error;
        reloadVJSrateLimited();
      }
      return old_console_error.apply(this,arguments);
    };*/
    
    scripttag = MistUtil.scripts.insert(scripturl,{
      onerror: function(e){
        var msg = "Failed to load videojs.js";
        if (e.message) { msg += ": "+e.message; }
        MistVideo.showError(msg);
      },
      onload: onVideoJSLoad
    },MistVideo);
    
  }
}
