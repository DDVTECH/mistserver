mistplayers.html5 = {
  name: "HTML5 video player",
  mimes: ["html5/application/vnd.apple.mpegurl","html5/video/mp4","html5/video/ogg","html5/video/webm","html5/audio/mp3","html5/audio/webm","html5/audio/ogg","html5/audio/wav"],
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
    
    
    var support = false;
    var shortmime = mimetype.split("/");
    shortmime.shift();
    
    try {
      shortmime = shortmime.join("/");
      
      function test(mime) {
        var v = document.createElement("video");
        if ((v) && (v.canPlayType(mime) != "")) {
          support = v.canPlayType(mime);
        }
        return support;
      }
      
      if (shortmime == "video/mp4") {
        function translateCodec(track) {
          
          function bin2hex(index) {
            return ("0"+track.init.charCodeAt(index).toString(16)).slice(-2);
          }
          
          switch (track.codec) {
            case "AAC":
              return "mp4a.40.2";
            case "MP3":
              return "mp3";
              //return "mp4a.40.34";
            case "AC3":
              return "ec-3";
            case "H264":
              return "avc1."+bin2hex(1)+bin2hex(2)+bin2hex(3);
            case "HEVC":
              return "hev1."+bin2hex(1)+bin2hex(6)+bin2hex(7)+bin2hex(8)+bin2hex(9)+bin2hex(10)+bin2hex(11)+bin2hex(12);
            default:
              return track.codec.toLowerCase();
          }
          
        }
        var codecs = {};
        for (var i in MistVideo.info.meta.tracks) {
          if (MistVideo.info.meta.tracks[i].type != "meta") {
            codecs[translateCodec(MistVideo.info.meta.tracks[i])] = 1;
          }
        }
        codecs = MistUtil.object.keys(codecs);
        if (codecs.length) {
          if (codecs.length > source.simul_tracks) {
            //not all of the tracks have to work
            var working = 0;
            for (var i in codecs) {
              var s = test(shortmime+";codecs=\""+codecs[i]+"\"");
              if (s) {
                working++;
              }
            }
            return (working >= source.simul_tracks);
          }
          shortmime += ";codecs=\""+codecs.join(",")+"\"";
        }
      }
      
      support = test(shortmime);
    } catch(e){}
    return support;
  },
  player: function(){
    this.onreadylist = [];
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
    if (MistVideo.info.type == "live") {
      
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
      
      if (MistVideo.source.type == "html5/video/mp4") {
        overrides.get.currentTime = function(){
          return this.currentTime - MistVideo.player.api.liveOffset + MistVideo.info.lastms * 1e-3;
        }
      }
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
  MistVideo.player.api.setSource = function(url) {
    if (url != this.source.src) {
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
