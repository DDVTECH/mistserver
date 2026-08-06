mistplayers.videojs = {
  name: "VideoJS player",
  mimes: ["html5/application/vnd.apple.mpegurl","html5/application/vnd.apple.mpegurl;version=7","dash/video/mp4"],
  priority: MistUtil.object.keys(mistplayers).length + 1,
  isMimeSupported: function (mimetype) {
    return (MistUtil.array.indexOf(this.mimes,mimetype) == -1 ? false : true);
  },
  isBrowserSupported: function (mimetype,source,MistVideo) {
    if (location.protocol != MistUtil.http.url.split(source.url).protocol) {
      MistVideo.log("HTTP/HTTPS mismatch for this source");
      return false;
    }

    if (location.protocol == "file:") {
      MistVideo.log("This source ("+mimetype+") won't load if the page is run via file://");
      return false;
    }

    if (!("customElements" in window)) {
      MistVideo.log("Video.js 10 requires browser support for custom elements");
      return false;
    }

    var supported = MistUtil.tracks.getSupported(MistVideo.info.meta.tracks,source);
    supported = MistUtil.shared.testMediaSource(supported);
    return MistUtil.tracks.tracktypes(supported);
  },
  player: function(){},
  scriptsrc: function(host) { return host+"/videojs.js"; },
  getScore: function(varname,source){
    switch (varname) {
      case "cpu_viewer": {
        switch (source.type) {
          case "html5/application/vnd.apple.mpegurl": return 5;
          case "html5/application/vnd.apple.mpegurl;version=7": return 9;
          case "dash/video/mp4": return 9;
        }
      };
      case "recovery": return 0;
    }
  }
};
var p = mistplayers.videojs.player;
p.prototype = new MistPlayer();
p.prototype.build = function (MistVideo,callback) {
  var me = this;
  var ele;
  var isDash = MistVideo.source.type == "dash/video/mp4";
  var isLive = MistVideo.info.type == "live";

  function hasVideoJS() {
    return window.customElements
      && customElements.get("video-player")
      && customElements.get("live-video-player")
      && customElements.get("video-skin")
      && customElements.get("live-video-skin")
      && customElements.get("media-container")
      && customElements.get(isDash ? "dash-video" : "hlsjs-video");
  }

  function onVideoJSLoad() {
    if (MistVideo.destroyed) { return; }
    if (!hasVideoJS()) {
      MistVideo.showError("Video.js 10 loaded without registering the required player elements");
      return;
    }

    MistVideo.log("Building Video.js 10 player..");

    ele = document.createElement(isDash ? "dash-video" : "hlsjs-video");
    ele.setAttribute("crossorigin","anonymous");
    ele.setAttribute("playsinline","");
    ele.setAttribute("src",MistVideo.source.url);
    me.source = ele;
    me.mediaElement = ele.target;
    me.displayElement = ele;

    if (!me.mediaElement) {
      MistVideo.showError("Video.js 10 loaded without creating its native video element");
      return;
    }

    if (MistVideo.options.autoplay) { ele.setAttribute("autoplay",""); }
    if (MistVideo.options.loop && MistVideo.info.type != "live") { ele.setAttribute("loop",""); }
    if (MistVideo.options.muted) { ele.muted = true; }
    if (MistVideo.options.poster) { ele.setAttribute("poster",MistVideo.options.poster); }

    var playerElement = false;
    var mediaContainer = false;

    if (MistVideo.options.controls == "stock") {
      playerElement = document.createElement(isLive ? "live-video-player" : "video-player");
      mediaContainer = document.createElement(isLive ? "live-video-skin" : "video-skin");
      mediaContainer.appendChild(ele);
      if (MistVideo.options.poster) {
        var poster = document.createElement("img");
        poster.setAttribute("slot","poster");
        poster.setAttribute("src",MistVideo.options.poster);
        poster.setAttribute("alt","");
        mediaContainer.appendChild(poster);
      }
      playerElement.appendChild(mediaContainer);
      playerElement.style.display = "block";
      mediaContainer.style.display = "block";
      mediaContainer.style.position = "relative";
      me.displayElement = playerElement;
    }
    ele.style.display = "block";

    function getFirstMs() {
      var firstms = Infinity;
      if (!MistVideo.info || !MistVideo.info.meta || !MistVideo.info.meta.tracks) { return firstms; }
      for (var i in MistVideo.info.meta.tracks) {
        var value = Number(MistVideo.info.meta.tracks[i].firstms);
        if (isFinite(value)) { firstms = Math.min(firstms,value); }
      }
      return firstms;
    }

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

    // DASH already exposes Mist's media timeline. HLS.js starts its own media
    // timeline at the beginning of the first playlist it sees, so live HLS needs
    // a mapping back to Mist packet time. PROGRAM-DATE-TIME and unixoffset describe
    // the same wall clock and make that mapping exact, including after a rendition
    // switch. Older playlists without PROGRAM-DATE-TIME are aligned by pairing
    // Mist's JSON live edge with HLS.js' parsed playlist edge.
    var timelineOffset = 0;
    var timelineOffsetSource = isDash ? "native" : (isLive ? false : "firstms");
    if (!isDash && !isLive) {
      var firstms = getFirstMs();
      timelineOffset = isFinite(firstms) ? firstms*1e-3 : 0;
    }

    function updateTimelineOffset() {
      if (isDash || !isLive) { return timelineOffset; }

      var unixoffset = MistVideo.info ? Number(MistVideo.info.unixoffset) : NaN;
      var details = ele.engine && ele.engine.latestLevelDetails;
      var fragments = details && details.fragments;
      if (isFinite(unixoffset) && fragments) {
        for (var i = 0; i < fragments.length; i++) {
          var fragment = fragments[i];
          if (fragment && fragment.programDateTime != null && isFinite(fragment.programDateTime) && isFinite(fragment.start)) {
            timelineOffset = (fragment.programDateTime-unixoffset)*1e-3 - fragment.start;
            timelineOffsetSource = "program-date-time";
            return timelineOffset;
          }
        }
      }

      // Do not replace an exact mapping during a transient playlist refresh.
      if (timelineOffsetSource == "program-date-time") { return timelineOffset; }
      // Keep the initial JSON/playlist-edge pairing stable while the playlist slides.
      if (timelineOffsetSource == "api-live-edge") { return timelineOffset; }
      var lastms = getLastMs();
      if (details && isFinite(details.edge) && isFinite(lastms)) {
        timelineOffset = lastms*1e-3 - details.edge;
        timelineOffsetSource = "api-live-edge";
      }
      return timelineOffset;
    }

    function mediaDuration(){
      if (isLive) {
        // Video.js/VHS exposed the HLS playlist edge as the media duration. The
        // v10 hls.js component uses an infinite media duration instead, so read
        // the equivalent edge directly from its parsed playlist (including parts).
        if (!isDash) {
          var details = ele.engine && ele.engine.latestLevelDetails;
          if (details && isFinite(details.edge) && details.edge) { return details.edge; }
        }
        if (ele.seekable && ele.seekable.length) {
          return ele.seekable.end(ele.seekable.length-1);
        }
      }
      return ele.duration;
    }

    var overrides = {get: {}, set: {}};
    if (("Proxy" in window) && ("Reflect" in window)) {
      me.api = new Proxy(ele,{
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
        if (!MistVideo.info) { return 0; }
        var duration = mediaDuration();
        return isFinite(duration) ? duration+updateTimelineOffset() : 0;
      };
      overrides.set.currentTime = function(value){
        MistVideo.log("Seeking to "+MistUtil.format.time(value)+" ("+Math.round((value-me.api.duration)*-10)/10+"s from live)");
        var target = value-updateTimelineOffset();
        if (isLive && ele.seekable && ele.seekable.length) {
          var range = ele.seekable.length-1;
          target = Math.max(ele.seekable.start(range),Math.min(ele.seekable.end(range),target));
        }
        ele.currentTime = target;
        return true;
      };
      overrides.get.currentTime = function(){
        var time = ele.currentTime;
        return isNaN(time) ? 0 : time+updateTimelineOffset();
      };
      overrides.get.buffered = function(){
        var buffered = ele.buffered;
        var offset = updateTimelineOffset();
        return {
          length: buffered.length,
          start: function(i){ return buffered.start(i)+offset; },
          end: function(i){ return buffered.end(i)+offset; }
        };
      };
    }
    else {
      me.api = ele;
    }

    me.api.setSource = function(url){
      if (url) {
        if (ele.src != url) {
          if (!isDash && isLive) {
            timelineOffset = 0;
            timelineOffsetSource = false;
          }
          ele.src = url;
        }
      }
      else {
        ele.removeAttribute("src");
      }
    };
    me.api.setSubtitle = function(trackmeta){
      var tracks = ele.getElementsByTagName("track");
      for (var i = tracks.length-1; i >= 0; i--) { ele.removeChild(tracks[i]); }
      if (trackmeta) {
        var track = document.createElement("track");
        track.kind = "subtitles";
        track.label = trackmeta.label;
        track.srclang = trackmeta.lang;
        track.src = trackmeta.src;
        track.setAttribute("default","");
        ele.appendChild(track);
      }
    };
    me.api.unload = function(){
      ele.pause();
      ele.removeAttribute("src");
      if (playerElement && playerElement.destroy) { playerElement.destroy(); }
      MistVideo.log("Video.js 10 instance disposed");
    };

    function updateProgress(){
      me.api.lastProgress = new Date();
      if (isLive && ele.seekable.length) {
        var i = ele.seekable.length-1;
        // Match the old wrapper: the DVR window ends at the playlist edge even
        // when the media element has not appended that final advertised part yet.
        var end = Math.max(ele.seekable.end(i),mediaDuration());
        MistVideo.info.meta.buffer_window = (end-ele.seekable.start(i))*1e3;
      }
    }
    MistUtil.event.addListener(ele,"progress",updateProgress);
    MistUtil.event.addListener(ele,"loadedmetadata",updateProgress);
    MistUtil.event.addListener(ele,"durationchange",updateProgress);

    MistUtil.event.addListener(ele,"error",function(e){
      var message = e && e.target && e.target.error && e.target.error.message;
      if (message && message.indexOf("NS_ERROR_DOM_MEDIA_OVERFLOW_ERR") >= 0) {
        MistVideo.timers.start(function(){
          MistVideo.log("Reloading player because of NS_ERROR_DOM_MEDIA_OVERFLOW_ERR");
          MistVideo.reload();
        },1e3);
      }
    });

    me.setSize = function(size){
      var width = size.width+"px";
      var height = size.height+"px";
      if (playerElement) {
        playerElement.style.width = width;
        playerElement.style.height = height;
        mediaContainer.style.width = width;
        mediaContainer.style.height = height;
      }
      ele.style.width = width;
      ele.style.height = height;
    };

    if (isLive) {
      me.api.lastProgress = new Date();
      me.api.liveOffset = 0;

      var loadstart = MistUtil.event.addListener(ele,"loadstart",function(){
        MistUtil.event.removeListener(loadstart);
        MistUtil.event.send("canplay",false,this);
      });
      var canplay = MistUtil.event.addListener(ele,"canplay",function(){
        if (loadstart) { MistUtil.event.removeListener(loadstart); }
        MistUtil.event.removeListener(canplay);
      });
    }

    MistVideo.log("Built Video.js 10 elements");
    // Keep MistVideo.video compatible with the other wrappers: callers use it
    // as a real canvas image source and attach native media event listeners.
    // The Video.js custom element remains the element inserted into the skin.
    callback(me.mediaElement);
  }

  if (hasVideoJS()) {
    onVideoJSLoad();
    return;
  }

  var scripturl = MistVideo.urlappend(mistplayers.videojs.scriptsrc(MistVideo.options.host));
  MistUtil.scripts.insert(scripturl,{
    type: "module",
    onerror: function(e){
      var msg = "Failed to load Video.js 10";
      if (e && e.message) { msg += ": "+e.message; }
      MistVideo.showError(msg);
    },
    onload: onVideoJSLoad
  },MistVideo);
};
