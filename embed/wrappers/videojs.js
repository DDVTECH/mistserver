mistplayers.videojs = {
  name: "VideoJS player",
  mimes: ["html5/application/vnd.apple.mpegurl"],
  priority: MistUtil.object.keys(mistplayers).length + 1,
  isMimeSupported: function (mimetype) {
    return (this.mimes.indexOf(mimetype) == -1 ? false : true);
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
    
    return ("MediaSource" in window);
  },
  player: function(){},
  scriptsrc: function(host) { return host+"/videojs.js"; }
};
var p = mistplayers.videojs.player;
p.prototype = new MistPlayer();
p.prototype.build = function (MistVideo,callback) {
  var me = this; //to allow nested functions to access the player class itself
  
  function onVideoJSLoad () {
    if (MistVideo.destroyed) { return;}
    
    MistVideo.log("Building VideoJS player..");
    
    var ele = document.createElement("video");
    if (MistVideo.source.type != "html5/video/ogg") {
      ele.crossOrigin = "anonymous"; //required for subtitles, but if ogg, the video won"t load
    }
    
    var shortmime = MistVideo.source.type.split("/");
    shortmime.shift();
    
    var source = document.createElement("source");
    source.setAttribute("src",MistVideo.source.url);
    me.source = source;
    ele.appendChild(source);
    source.type = shortmime.join("/");
    MistVideo.log("Adding "+source.type+" source @ "+MistVideo.source.url);
    if (source.type == "application/vnd.apple.mpegurl") { source.type = "application/x-mpegURL"; }
    
    MistUtil.class.add(ele,"video-js");
    
    var vjsopts = {};
    
    if (MistVideo.options.autoplay) { vjsopts.autoplay = true; }
    if ((MistVideo.options.loop) && (MistVideo.info.type != "live")) {
      vjsopts.loop = true;
      ele.loop = true;
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
    
    
    me.onready(function(){
      me.videojs = videojs(ele,vjsopts,function(){
        MistVideo.log("Videojs initialized");
      });
      
      me.api.unload = function(){
        videojs(ele).dispose();
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
      
      if (MistVideo.info.type == "live") {
        function getLastBuffer(video) {
          var buffer_end = 0;
          if (video.buffered.length) {
            buffer_end = video.buffered.end(video.buffered.length-1)
          }
          return buffer_end;
        }
        var HLSlatency = 90; //best guess..
        
        overrides.get.duration = function(){
          return (MistVideo.info.lastms + (new Date()).getTime() - MistVideo.info.updated.getTime())*1e-3;
        };
        MistVideo.player.api.lastProgress = new Date();
        MistVideo.player.api.liveOffset = 0;
        
        MistUtil.event.addListener(ele,"progress",function(){
          MistVideo.player.api.lastProgress = new Date();
        });
        overrides.set.currentTime = function(value){
          var diff = MistVideo.player.api.currentTime - value;
          var offset = value - MistVideo.player.api.duration;
          //MistVideo.player.api.liveOffset = offset;
          
          MistVideo.log("Seeking to "+MistUtil.format.time(value)+" ("+Math.round(offset*-10)/10+"s from live)");
          MistVideo.video.currentTime -= diff;
        }
        overrides.get.currentTime = function(){
          return this.currentTime + MistVideo.info.lastms*1e-3 - MistVideo.player.api.liveOffset - HLSlatency;
        }
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
    
    callback(ele);
  }
  
  if ("videojs" in window) {
    onVideoJSLoad();
  }
  else {
    //load the videojs player
    
    var scripttag = MistUtil.scripts.insert(MistVideo.urlappend(mistplayers.videojs.scriptsrc(MistVideo.options.host)),{
      onerror: function(e){
        var msg = "Failed to load videojs.js";
        if (e.message) { msg += ": "+e.message; }
        MistVideo.showError(msg);
      },
      onload: onVideoJSLoad
    },MistVideo);
    
  }
}
