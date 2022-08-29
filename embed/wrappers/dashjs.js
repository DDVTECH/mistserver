mistplayers.dashjs = {
  name: "Dash.js player",
  mimes: ["dash/video/mp4"/*,"html5/application/vnd.ms-ss"*/],
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
    
    //don't use dashjs if this location is loaded over file://
    if (location.protocol == "file:") {
      MistVideo.log("This source ("+mimetype+") won't load if the page is run via file://");
      return false;
    }

    var codecs = {};
    for (var i in MistVideo.info.meta.tracks) {
      if (MistVideo.info.meta.tracks[i].type != "meta") {
        codecs[MistVideo.info.meta.tracks[i].codec] = 1;
      }
    }
    codecs = MistUtil.object.keys(codecs);
    //if there's a h265 track, remove it from the list of codecs
    for (var i = codecs.length-1; i >= 0; i--) {
      if (codecs[i].substr(0,4) == "HEVC") {
        codecs.splice(i,1);
      }
    }
    if (codecs.length < source.simul_tracks) { return false; } //if there's no longer enough playable tracks, skip this player
    
    return ("MediaSource" in window);
  },
  player: function(){this.onreadylist = [];},
  scriptsrc: function(host) { return host+"/dashjs.js"; }
};
var p = mistplayers.dashjs.player;
p.prototype = new MistPlayer();
p.prototype.build = function (MistVideo,callback) {
  var me = this;
  
  this.onDashLoad = function() {
    if (MistVideo.destroyed) { return; }
    
    MistVideo.log("Building DashJS player..");
    
    var ele = document.createElement("video");
    
    if ("Proxy" in window) {
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
        overrides.get.duration = function(){
          //this should indicate the end of Mist's buffer
          var buffer_end = 0;
          if (this.buffered.length) {
            buffer_end = this.buffered.end(this.buffered.length-1)
          }
          var time_since_buffer = (new Date().getTime() - MistVideo.player.api.lastProgress.getTime())*1e-3;
          return buffer_end + time_since_buffer + -1*MistVideo.player.api.liveOffset + 45;
        };
        overrides.set.currentTime = function(value){
          var offset = value - MistVideo.player.api.duration;
          //MistVideo.player.api.liveOffset = offset;
          
          MistVideo.log("Seeking to "+MistUtil.format.time(value)+" ("+Math.round(offset*-10)/10+"s from live)");
          
          MistVideo.video.currentTime = value;
          //.player.api.setSource(MistUtil.http.url.addParam(MistVideo.source.url,{startunix:offset}));
        }
        MistUtil.event.addListener(ele,"progress",function(){
          MistVideo.player.api.lastProgress = new Date();
        });
        MistVideo.player.api.lastProgress = new Date();
        MistVideo.player.api.liveOffset = 0;
      }
      
    }
    else {
      me.api = ele;
    }
    
    if (MistVideo.options.autoplay) {
      ele.setAttribute("autoplay","");
    }
    if ((MistVideo.options.loop) && (MistVideo.info.type != "live")) {
      ele.setAttribute("loop","");
    }
    if (MistVideo.options.poster) {
      ele.setAttribute("poster",MistVideo.options.poster);
    }
    if (MistVideo.options.muted) {
      ele.muted = true;
    }
    if (MistVideo.options.controls == "stock") {
      ele.setAttribute("controls","");
    }
    
    var player = dashjs.MediaPlayer().create();
    //player.getDebug().setLogToBrowserConsole(false);
    player.initialize(ele,MistVideo.source.url,MistVideo.options.autoplay);
    
    
    me.dash = player;
    
    //add listeners for events that we can log
    var skipEvents = ["METRIC_ADDED","METRIC_UPDATED","METRIC_CHANGED","METRICS_CHANGED","FRAGMENT_LOADING_STARTED","FRAGMENT_LOADING_COMPLETED","LOG","PLAYBACK_TIME_UPDATED","PLAYBACK_PROGRESS"];
    for (var i in dashjs.MediaPlayer.events) {
      if (skipEvents.indexOf(i) < 0) {
        me.dash.on(dashjs.MediaPlayer.events[i],function(e){
          MistVideo.log("Player event fired: "+e.type);
        });
      }
    }
    
    MistVideo.player.setSize = function(size){
      this.api.style.width = size.width+"px";
      this.api.style.height = size.height+"px";
    };
    MistVideo.player.api.setSource = function(url) {
      MistVideo.player.dash.attachSource(url);
    };
    
    var subsloaded = false;
    me.dash.on("allTextTracksAdded",function(){
      subsloaded = true;
    });
    
    MistVideo.player.api.setSubtitle = function(trackmeta) {

      if (!subsloaded) {
        var f = function(){
          MistVideo.player.api.setSubtitle(trackmeta);
          me.dash.off("allTextTracksAdded",f);
        };
        me.dash.on("allTextTracksAdded",f);
        return;
      }
      if (!trackmeta) {
        me.dash.enableText(false);
        return;
      }
      
      var dashsubs = me.dash.getTracksFor("text");
      for (var i in dashsubs) {
        var trackid = ("idx" in trackmeta ? trackmeta.idx : trackmeta.trackid);
        if (dashsubs[i].id == trackid) {
          me.dash.setTextTrack(i);
          if (!me.dash.isTextEnabled()) { me.dash.enableText(); }
          return true;
        }
      }
      
      return false; //failed to find subtitle
    };
    
    //dashjs keeps on spamming the stalled icon >_>
    MistUtil.event.addListener(ele,"progress",function(e){
      if (MistVideo.container.getAttribute("data-loading") == "stalled") {
        MistVideo.container.removeAttribute("data-loading");
      }
    });
    
    me.api.unload = function(){
      me.dash.reset();
    };
    
    MistVideo.log("Built html");
    callback(ele);
  }
  
  if ("dashjs" in window) {
    this.onDashLoad();
  }
  else {
    var scripttag = MistUtil.scripts.insert(MistVideo.urlappend(mistplayers.dashjs.scriptsrc(MistVideo.options.host)),{
      onerror: function(e){
        var msg = "Failed to load dashjs.js";
        if (e.message) { msg += ": "+e.message; }
        MistVideo.showError(msg);
      },
      onload: me.onDashLoad
    },MistVideo);
  }
}
