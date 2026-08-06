mistplayers.hlsjs = {
  name: "HLS.js player",
  mimes: ["html5/application/vnd.apple.mpegurl","html5/application/vnd.apple.mpegurl;version=7"],
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

    var supported = MistUtil.tracks.getSupported(MistVideo.info.meta.tracks,source);
    supported = MistUtil.shared.testMediaSource(supported);
    return MistUtil.tracks.tracktypes(supported);
  },
  player: function(){},
  scriptsrc: function(host) { return host+"/hlsjs.js"; },
  getScore: function(varname,source){
    switch (varname) {
      case "cpu_viewer": {
        switch (source.type) {
          case "html5/application/vnd.apple.mpegurl": return 5;
          case "html5/application/vnd.apple.mpegurl;version=7": return 9;
        }
      };
      case "recovery": return 0;
    }
  }
};
var p = mistplayers.hlsjs.player;
p.prototype = new MistPlayer();
p.prototype.build = function (MistVideo,callback) {
  var me = this;
  
  
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
  
  this.api = video;

  // HLS.js may expose a live playlist on a media timeline that starts at zero,
  // regardless of how long the Mist stream has already been running. Map that
  // timeline back to Mist packet time so the controls and unixoffset-based clock
  // use the same coordinate system.
  var timelineOffset = 0;
  var timelineOffsetSource = false;

  function getLastMs() {
    var lastms = -Infinity;
    if (MistVideo.info && MistVideo.info.meta && MistVideo.info.meta.tracks) {
      for (var i in MistVideo.info.meta.tracks) {
        var value = Number(MistVideo.info.meta.tracks[i].lastms);
        if (isFinite(value)) { lastms = Math.max(lastms,value); }
      }
    }
    if (isFinite(lastms)) { return lastms; }
    var infoLastMs = MistVideo.info ? Number(MistVideo.info.lastms) : NaN;
    if (isFinite(infoLastMs)) { return infoLastMs; }
    return lastms;
  }

  function updateTimelineOffset() {
    if (MistVideo.info.type != "live") { return 0; }

    var unixoffset = MistVideo.info ? Number(MistVideo.info.unixoffset) : NaN;
    var details = me.hls && me.hls.latestLevelDetails;
    var fragments = details && details.fragments;
    if (fragments && isFinite(unixoffset)) {
      for (var i = 0; i < fragments.length; i++) {
        var fragment = fragments[i];
        if (fragment && fragment.programDateTime != null && isFinite(fragment.programDateTime) && isFinite(fragment.start)) {
          timelineOffset = (fragment.programDateTime-unixoffset)*1e-3-fragment.start;
          timelineOffsetSource = "program-date-time";
          return timelineOffset;
        }
      }
    }

    // Keep the exact wall-clock mapping across transient playlist refreshes.
    if (timelineOffsetSource == "program-date-time") { return timelineOffset; }
    // The JSON metadata is the snapshot that started this player. Keep its
    // pairing with the first parsed playlist edge stable as that playlist slides.
    if (timelineOffsetSource == "api-live-edge") { return timelineOffset; }
    // Older HLS playlists may not carry PROGRAM-DATE-TIME. Pair Mist's live
    // edge from the JSON API with HLS.js' parsed playlist edge; neither value
    // can be inferred from a resource URL or from the DVR buffer's firstms.
    var lastms = getLastMs();
    if (details && isFinite(details.edge) && isFinite(lastms)) {
      timelineOffset = lastms*1e-3-details.edge;
      timelineOffsetSource = "api-live-edge";
    }
    return timelineOffset;
  }

  function mediaDuration() {
    if (MistVideo.info.type == "live") {
      var details = me.hls && me.hls.latestLevelDetails;
      if (details && isFinite(details.edge) && details.edge) { return details.edge; }
      if (video.seekable && video.seekable.length) {
        return video.seekable.end(video.seekable.length-1);
      }
    }
    return video.duration;
  }

  if ((MistVideo.info.type == "live") && ("Proxy" in window) && ("Reflect" in window)) {
    var overrides = {get: {}, set: {}};
    this.api = new Proxy(video,{
      get: function(target,key){
        if (key in overrides.get) { return overrides.get[key].call(target); }
        var value = target[key];
        if (typeof value == "function") {
          return function(){ return value.apply(target,arguments); };
        }
        return value;
      },
      set: function(target,key,value){
        if (key in overrides.set) { return overrides.set[key].call(target,value); }
        target[key] = value;
        return true;
      }
    });

    overrides.get.duration = function(){
      var duration = mediaDuration();
      return isFinite(duration) ? duration+updateTimelineOffset() : 0;
    };
    overrides.set.currentTime = function(value){
      MistVideo.log("Seeking to "+MistUtil.format.time(value)+" ("+Math.round((value-me.api.duration)*-10)/10+"s from live)");
      var target = value-updateTimelineOffset();
      if (video.seekable && video.seekable.length) {
        var range = video.seekable.length-1;
        target = Math.max(video.seekable.start(range),Math.min(video.seekable.end(range),target));
      }
      video.currentTime = target;
      return true;
    };
    overrides.get.currentTime = function(){
      return (isNaN(video.currentTime) ? 0 : video.currentTime)+updateTimelineOffset();
    };
    overrides.get.buffered = function(){
      var buffered = video.buffered;
      var offset = updateTimelineOffset();
      return {
        length: buffered.length,
        start: function(i){ return buffered.start(i)+offset; },
        end: function(i){ return buffered.end(i)+offset; }
      };
    };

    this.api.lastProgress = new Date();
    this.api.liveOffset = 0;
  }
  
  MistVideo.player.api.unload = function(){
    if (MistVideo.player.hls) {
      MistVideo.player.hls.destroy();
      MistVideo.player.hls = false;
      MistVideo.log("hls.js instance disposed");
    }
  };
  
  function init(url) {
    timelineOffset = 0;
    timelineOffsetSource = false;
    MistVideo.player.hls = new Hls({
      maxBufferLength: 15,
      maxMaxBufferLength: 60,
      manifestLoadingTimeOut: 60e3
    });
    MistVideo.player.hls.attachMedia(video);
    MistVideo.player.hls.on(Hls.Events.MEDIA_ATTACHED, function () {
      //console.log("video and hls.js are now bound together !");
      //hls.loadSource("https://cattop/mist/cmaf/live/v9.m3u8");
      //hls.loadSource("https://mira:4433/cmaf/live/v9.m3u8");
      MistVideo.player.hls.loadSource(url);
      /*MistVideo.player.hls.on(Hls.Events.MANIFEST_PARSED, function (event, data) {
        console.log("manifest loaded, found " + data.levels.length + " quality level");
      });*/
    });
  }

  function updateProgress(){
    MistVideo.player.api.lastProgress = new Date();
    if (MistVideo.info.type == "live" && video.seekable.length) {
      var i = video.seekable.length-1;
      var end = Math.max(video.seekable.end(i),mediaDuration());
      MistVideo.info.meta.buffer_window = (end-video.seekable.start(i))*1e3;
    }
  }
  MistUtil.event.addListener(video,"progress",updateProgress);
  MistUtil.event.addListener(video,"loadedmetadata",updateProgress);
  MistUtil.event.addListener(video,"durationchange",updateProgress);
  
  MistVideo.player.api.setSource = function(url) {
    if (!MistVideo.player.hls) { return; }
    if (MistVideo.player.hls.url != url) {
      MistVideo.player.hls.destroy();
      init(url);
    }
  };
  
  MistVideo.player.api.setSubtitle = function(trackmeta) {
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
  };
  
  function onHLSjsLoad(){
    init(MistVideo.source.url);
  }
  
  if ("Hls" in window) {
    onHLSjsLoad();
  }
  else {
    //load the videojs player
    
    var scripturl = MistVideo.urlappend(mistplayers.hlsjs.scriptsrc(MistVideo.options.host));
    MistUtil.scripts.insert(scripturl,{
      onerror: function(e){
        var msg = "Failed to load hlsjs.js";
        if (e.message) { msg += ": "+e.message; }
        MistVideo.showError(msg);
      },
      onload: onHLSjsLoad
    },MistVideo);
  }
  
  callback(video);
};
