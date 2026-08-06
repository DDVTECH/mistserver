mistplayers.html5 = {
  name: "HTML5 video player",
  mimes: ["html5/application/vnd.apple.mpegurl","html5/application/vnd.apple.mpegurl;version=7","html5/video/mp4","html5/video/ogg","html5/video/webm","html5/audio/mp3","html5/audio/webm","html5/audio/ogg","html5/audio/wav"],
  priority: MistUtil.object.keys(mistplayers).length + 1,
  isMimeSupported: function (mimetype) {
    return (MistUtil.array.indexOf(this.mimes,mimetype) == -1 ? false : true);
  },
  isBrowserSupported: function (mimetype,source,MistVideo) {
    
    //check for http/https mismatch
    if (location.protocol != MistUtil.http.url.split(source.url).protocol) {
      if ((location.protocol == "file:") && (MistUtil.http.url.split(source.url).protocol == "http:")) {
        MistVideo.log("This page was loaded over file://, the player might not behave as intended.");
      }
      else {
        MistVideo.log("HTTP/HTTPS mismatch for this source");
        return false;
      }
    }
    
    if (mimetype == "html5/application/vnd.apple.mpegurl") {
      var android = MistUtil.getAndroid();
      if (android && (parseFloat(android) < 7)) { 
        MistVideo.log("Skipping native HLS as videojs will do better");
        return false;
      }
    }
    if ((mimetype == "html5/video/webm") && (MistUtil.getBrowser() == "safari")) {
      MistVideo.log("Skipping html5/webm, as safari and webm are not friends");
      return false;
    }
    
    
    var support = false;
    var shortmime = mimetype.split("/");
    shortmime.shift();
    
    try {
      shortmime = shortmime.join("/");

      var codecs = {};
      var playabletracks = {};
      var hassubtitles = false;
      var supported = MistUtil.tracks.getSupported(MistVideo.info.meta.tracks,source);
      for (var i in supported) {
        codecs[MistUtil.tracks.translateCodec(supported[i])] = supported[i];
      }
      var container = mimetype.split("/")[2];

      source.supportedCodecs = [];
      for (var i in codecs) {
        //i is the long name (like mp4a.40.2), codecs[i] is the track meta, codecs[i].codec is the short name (like AAC)
        var s = test(i);
        if (s) {
          source.supportedCodecs.push(codecs[i].codec);
          playabletracks[codecs[i].type] = 1;
        }
      }
      function test(codecs) {
        var v = document.createElement("video");
        if ((v) && (typeof v.canPlayType == "function")) {
          var result;
          switch (shortmime) {
            case "video/webm": {
              //if codecs are included here, at least chrome reports it as not working, even though it does. So we'll just assume it will play if webm returns maybe.
              result = v.canPlayType(shortmime);
              break;
            }
            case "video/mp4":
            case "html5/application/vnd.apple.mpegurl":
            default: {
              // Safari requires codecs before additional MIME parameters such as version=7.
              var mimeparts = shortmime.split(";");
              var testmime = mimeparts.shift()+";codecs=\""+codecs+"\"";
              if (mimeparts.length) { testmime += ";"+mimeparts.join(";"); }
              result = v.canPlayType(testmime);
              break;
            }
          }
          if (result != "") {
            return result;
          }
        }
        return false;
      }
      
      support = MistUtil.object.keys(playabletracks);
    } catch(e){}
    return support;
  },
  player: function(){
    this.onreadylist = [];
  },
  getScore: function(varname,source){
    var nativeHls = source.type.indexOf("html5/application/vnd.apple.mpegurl") > -1;
    switch (varname) {
      case "cpu_viewer": return nativeHls ? 4 : 10;
      case "recovery": {
        // Keep native HLS as a fallback, but prefer wrappers that expose and manage
        // the complete HLS timeline instead of the browser's opaque native window.
        return nativeHls ? 0 : 5;
      }
    }
  },
  mistControls: true
};
var p = mistplayers.html5.player;
p.prototype = new MistPlayer();
p.prototype.build = function (MistVideo,callback) {
  var shortmime = MistVideo.source.type.split("/");
  shortmime.shift();
  var video = document.createElement("video");
  
  //TODO verify: not required if player is loaded from same domain as it should always be when not in dev mode?
  video.setAttribute("crossorigin","anonymous");//required for subs, breaks ogg?
  
  video.setAttribute("playsinline",""); //iphones. effin' iphones.
  
  var source = document.createElement("source");
  source.setAttribute("src",MistVideo.source.url);
  video.source = source;
  video.appendChild(source);
  source.type = shortmime.join("/");
  
  //apply options
  var attrs = ["autoplay","loop","poster"];
  for (var i in attrs) {
    var attr = attrs[i];
    if (MistVideo.options[attr]) {
      video.setAttribute(attr,(MistVideo.options[attr] === true ? "" : MistVideo.options[attr]));
    }
  }
  if (MistVideo.options.muted) {
    video.muted = true; //don't use attribute because of Chrome bug: https://stackoverflow.com/questions/14111917/html5-video-muted-but-stilly-playing?rq=1
  }
  if (MistVideo.options.controls == "stock") {
    video.setAttribute("controls","");
  }
  if (MistVideo.info.type == "live") {
    video.loop = false;
  }
  
  if (("Proxy" in window) && ("Reflect" in window)) {
    var overrides = {
      get: {},
      set: {}
    };
    var isLiveMp4 = (MistVideo.info.type == "live") && (MistVideo.source.type == "html5/video/mp4");
    var isNativeHls = (MistVideo.info.type == "live") &&
      (MistVideo.source.type.indexOf("html5/application/vnd.apple.mpegurl") == 0);
    
    MistVideo.player.api = new Proxy(video,{
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
    
    if (MistVideo.source.type == "html5/audio/mp3") {
      overrides.set.currentTime = function(){
        MistVideo.log("Seek attempted, but MistServer does not currently support seeking in MP3.");
        return false;
      }
      
    }

    if (MistVideo.info.type == "live" && (isLiveMp4 || isNativeHls)) {
      // The player controls use Mist packet time, while native media timelines may start at
      // zero. Keep an explicit mapping between those two timelines. The previous liveOffset
      // calculation applied the seek offset twice, which made the displayed duration jump
      // into the future after seeking backwards.
      var timelineOffset = 0;
      var timelineOffsetSource = false;
      var mp4SourceLiveOffset = 0;

      function getInfoLastMs() {
        var lastms = Number(MistVideo.info.lastms);
        if (isFinite(lastms)) { return lastms; }

        lastms = -Infinity;
        for (var i in MistVideo.info.meta.tracks) {
          var value = Number(MistVideo.info.meta.tracks[i].lastms);
          if (isFinite(value)) { lastms = Math.max(lastms,value); }
        }
        return lastms;
      }

      function getLiveEdge() {
        var lastms = getInfoLastMs();
        if (!isFinite(lastms)) { return 0; }

        var updated = MistVideo.info.updated;
        var updatedAt = updated && (typeof updated.getTime == "function") ? updated.getTime() : NaN;
        var age = isFinite(updatedAt) ? Math.max(0,Date.now()-updatedAt) : 0;
        return (lastms+age)*1e-3;
      }

      function getTimelineOffset() {
        if (!isNativeHls) { return timelineOffset; }

        // Native HLS players may expose the media timeline's wall-clock origin when the playlist
        // has EXT-X-PROGRAM-DATE-TIME. unixoffset converts it back to Mist packet time.
        if ((typeof video.getStartDate == "function") && isFinite(Number(MistVideo.info.unixoffset))) {
          try {
            var startDate = video.getStartDate();
            if (startDate && isFinite(startDate.getTime())) {
              timelineOffset = (startDate.getTime()-Number(MistVideo.info.unixoffset))*1e-3;
              timelineOffsetSource = "program-date-time";
              return timelineOffset;
            }
          }
          catch(e) {}
        }

        // Older/non-dated playlists have no exact wall-clock mapping. Pair the API edge with
        // the first native seekable edge once, then keep that mapping stable as the window slides.
        if (!timelineOffsetSource && video.seekable.length) {
          timelineOffset = getLiveEdge()-video.seekable.end(video.seekable.length-1);
          timelineOffsetSource = "api-live-edge";
        }
        return timelineOffset;
      }

      timelineOffset = getLiveEdge();

      overrides.get.duration = function(){
        return getLiveEdge();
      };
      overrides.set.currentTime = function(value){
        var liveEdge = getLiveEdge();
        var target = Math.min(Number(value),liveEdge);
        if (!isFinite(target)) { return false; }

        MistVideo.log("Seeking to "+MistUtil.format.time(target)+" ("+Math.round((liveEdge-target)*10)/10+"s from live)");

        if (isNativeHls) {
          var offset = getTimelineOffset();
          var nativeTarget = target-offset;
          if (video.seekable.length) {
            var seekableStart = video.seekable.start(0);
            var seekableEnd = video.seekable.end(video.seekable.length-1);
            if (nativeTarget >= seekableStart && nativeTarget <= seekableEnd) {
              video.currentTime = nativeTarget;
              return true;
            }
          }

          // A position outside the native window needs a new response. The controls already use
          // Mist packet time, so pass that timestamp directly instead of converting it through
          // wall-clock-relative startunix.
          var params = {start:Math.round(target*1e3)};
          MistVideo.player.api.setSource(MistUtil.http.url.addParam(MistVideo.source.url,params),true);
          return true;
        }

        mp4SourceLiveOffset = target-liveEdge;
        timelineOffset = target;
        var params = {start:Math.round(target*1e3)};
        MistVideo.player.api.setSource(MistUtil.http.url.addParam(MistVideo.source.url,params),true);
        return true;
      };
      overrides.get.currentTime = function(){
        return (isNaN(this.currentTime) ? 0 : this.currentTime)+getTimelineOffset();
      };
      overrides.get.buffered = function(){
        var buffered = this.buffered;
        var offset = getTimelineOffset();
        return {
          length: buffered.length,
          start: function(i) { return buffered.start(i)+offset; },
          end: function(i) { return buffered.end(i)+offset; }
        };
      };

      MistUtil.event.addListener(video,"pause",function(){
        MistVideo.player.api.pausedAt = new Date();
      });
      overrides.get.play = function(){
        return function(){
          if ((MistVideo.player.api.paused) && (MistVideo.player.api.pausedAt) && ((new Date()) - MistVideo.player.api.pausedAt > 5e3)) {
            if (isLiveMp4) { timelineOffset = getLiveEdge()+mp4SourceLiveOffset; }
            video.load();
            MistVideo.log("Reloading source..");
          }
          return video.play.apply(video,arguments);
        };
      };
    }
    else if (MistVideo.info.type == "live") {
      overrides.get.duration = function(){
        //this should indicate the end of Mist's buffer
        var buffer_end = 0;
        if (this.buffered.length) {
          buffer_end = this.buffered.end(this.buffered.length-1)
        }
        var time_since_buffer = (new Date().getTime() - MistVideo.player.api.lastProgress.getTime())*1e-3;
        return buffer_end + time_since_buffer - MistVideo.player.api.liveOffset;
      };
      overrides.set.currentTime = function(value){
        var offset = value - MistVideo.player.api.duration;
        
        if (offset > 0) {offset = 0;} //don't allow positive numbers, as Mist will interpret them as unix timestamps
        
        MistVideo.player.api.liveOffset = offset;

        MistVideo.log("Seeking to "+MistUtil.format.time(value)+" ("+Math.round(offset*-10)/10+"s from live)");
        
        var params = {startunix:offset};
        if (offset == 0) {
          params = {};
        }
        
        MistVideo.player.api.setSource(MistUtil.http.url.addParam(MistVideo.source.url,params));
      }

      MistUtil.event.addListener(video,"progress",function(){
        MistVideo.player.api.lastProgress = new Date();
      });
      MistVideo.player.api.lastProgress = new Date();
      MistVideo.player.api.liveOffset = 0;
      
      
      MistUtil.event.addListener(video,"pause",function(){
        MistVideo.player.api.pausedAt = new Date();
      });
      overrides.get.play = function(){
        return function(){
          if ((MistVideo.player.api.paused) && (MistVideo.player.api.pausedAt) && ((new Date()) - MistVideo.player.api.pausedAt > 5e3)) {
            video.load();
            MistVideo.log("Reloading source..");
          }
          
          return video.play.apply(video, arguments);
        }
      };
      
      var otherdurationoverride = overrides.get.duration;
      overrides.get.duration = function(){
        return otherdurationoverride.apply(this,arguments) - MistVideo.player.api.liveOffset + MistVideo.info.lastms * 1e-3;
      }
      overrides.get.currentTime = function(){
        return this.currentTime - MistVideo.player.api.liveOffset + MistVideo.info.lastms * 1e-3;
      }
      overrides.get.buffered = function(){
        var video = this;
        return {
          length: video.buffered.length,
          start: function(i) { return video.buffered.start(i) - MistVideo.player.api.liveOffset + MistVideo.info.lastms * 1e-3; },
          end: function(i) { return video.buffered.end(i) - MistVideo.player.api.liveOffset + MistVideo.info.lastms * 1e-3; }
        }
      };
    }
    else {
      if (!isFinite(video.duration)) {
        var duration = 0;
        for (var i in MistVideo.info.meta.tracks) {
          duration = Math.max(duration,MistVideo.info.meta.tracks[i].lastms);
        }
        overrides.get.duration = function(){
          if (isFinite(this.duration)) { return this.duration; }
          return duration * 1e-3;
        }
      }
    }
    
  }
  else {
    MistVideo.player.api = video;
  }
  MistVideo.player.api.setSource = function(url,force) {
    if ((url != this.source.src) || force) {
      this.source.src = url;
      this.load();
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
  MistVideo.player.setSize = function(size){
    this.api.style.width = size.width+"px";
    this.api.style.height = size.height+"px";
  };
  
  callback(video);
}
