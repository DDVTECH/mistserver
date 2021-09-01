mistplayers.flv = {
  name: "HTML5 FLV Player",
  mimes: ["flash/7"],
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
    
    if (!window.MediaSource) { return false; }
    
    try {
      function test(mime) {
        return window.MediaSource.isTypeSupported("video/mp4;codecs=\""+mime+"\"");
      }
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
          codecs[translateCodec(MistVideo.info.meta.tracks[i])] = MistVideo.info.meta.tracks[i].type;
        }
      }
      source.supportedCodecs = [];
      for (var i in codecs) {
        //i is the long name (like mp4a.40.2), codecs[i] is the type (audio/video)
        var s = test(i);
        if (s) {
          source.supportedCodecs.push(codecs[i]);
        }
      }
      if ((!MistVideo.options.forceType) && (!MistVideo.options.forcePlayer)) { //unless we force mews, skip this player if not both video and audio are supported
        if (source.supportedCodecs.length < source.simul_tracks) {
          MistVideo.log("Not enough playable tracks for this source");
          return false;
        }
      }
      return source.supportedCodecs.length > 0;
    } catch(e){}
    
    return false;
  },
  player: function(){
    this.onreadylist = [];
  },
  scriptsrc: function(host) { return host+"/flv.js"; }
};
var p = mistplayers.flv.player;
p.prototype = new MistPlayer();
p.prototype.build = function (MistVideo,callback) {
  
  this.onFLVLoad = function() {
    if (MistVideo.destroyed) { return; }
    
    MistVideo.log("Building flv.js player..");
  
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
      video.muted = true; //don't use attribute because of Chrome bug: https://stackoverflow.com/questions/14111917/html5-video-muted-but-stilly-playing?rq=1
    }
    if (MistVideo.options.controls == "stock") {
      video.setAttribute("controls","");
    }
    if (MistVideo.info.type == "live") {
      video.loop = false;
    }
    
    //send logging through our system
    flvjs.LoggingControl.applyConfig({
      enableVerbose: false
    });
    flvjs.LoggingControl.addLogListener(function(loglevel,message){
      MistVideo.log("[flvjs] "+message);
    });
    
    var opts = {
      type: "flv",
      url: MistVideo.source.url,
      //isLive: true, //not needed apparently
      hasAudio: false,
      hasVideo: false
    };
    //if for example audio is not supported, send hasAudio = false flag or you get a bunch of errors ^_^
    for (var i in MistVideo.source.supportedCodecs) {
      opts["has"+MistVideo.source.supportedCodecs[i].charAt(0).toUpperCase()+MistVideo.source.supportedCodecs[i].slice(1)] = true;
    }
    MistVideo.player.create = function(o){
      o = MistUtil.object.extend({},o); //create a copy to force flv.js to recreate the segments key
      MistVideo.player.flvPlayer = flvjs.createPlayer(o,{
        lazyLoad: false //if we let it lazyLoad, once it resumes, it will try to seek and fail miserably :)
      });
      MistVideo.player.flvPlayer.attachMediaElement(video);
      MistVideo.player.flvPlayer.load();
      MistVideo.player.flvPlayer.play();
      if (!MistVideo.options.autoplay) {
        video.pause();
      }
    }
    MistVideo.player.create(opts);
    
    MistVideo.player.api = {};
    
    //redirect properties
    //using a function to make sure the "item" is in the correct scope
    function reroute(item) {
      Object.defineProperty(MistVideo.player.api,item,{
        get: function(){ return video[item]; },
        set: function(value){
          return video[item] = value;
        }
      });
    }
    var list = [
      "volume"
      ,"buffered"
      ,"muted"
      ,"loop"
      ,"paused",
      ,"error"
      ,"textTracks"
      ,"webkitDroppedFrameCount"
      ,"webkitDecodedFrameCount"
    ];
    if (MistVideo.info.type != "live") {
      list.push("duration");
    }
    else {
      Object.defineProperty(MistVideo.player.api,"duration",{
        get: function(){
          if (!video.buffered.length) { return 0; }
          return video.buffered.end(video.buffered.length-1);
        },
      });
    }
    for (var i in list) {
      reroute(list[i]);
    }
    
    //redirect methods
    function redirect(item) {
      if (item in video) {
        MistVideo.player.api[item] = function(){
          return video[item].call(video,arguments);
        };
      }
    }
    var list = ["load","getVideoPlaybackQuality","play","pause"];
    for (var i in list) {
      redirect(list[i]);
    }
    MistVideo.player.api.setSource = function(url){
      if ((url != opts.url) && (url != "")) {
        MistVideo.player.flvPlayer.unload();
        MistVideo.player.flvPlayer.detachMediaElement();
        MistVideo.player.flvPlayer.destroy();
        opts.url = url;
        MistVideo.player.create(opts);
      }
    };
    MistVideo.player.api.unload = function(){
      MistVideo.player.flvPlayer.unload();
      MistVideo.player.flvPlayer.detachMediaElement();
      MistVideo.player.flvPlayer.destroy();
    }
    MistVideo.player.setSize = function(size){
      video.style.width = size.width+"px";
      video.style.height = size.height+"px";
    };
    
    //override seeking
    Object.defineProperty(MistVideo.player.api,"currentTime",{
      get: function(){ return video.currentTime; },
      set: function(value){
        var keepaway = 0.5; //don't go closer to buffer end than this value [seconds]
        
        //check if this time is in the buffer
        for (var i = 0; i < video.buffered.length; i++) {
          if ((value >= video.buffered.start(i)) && (value <= video.buffered.end(i)-keepaway)) {
            //the desired seek time is in the buffer, go to it
            return video.currentTime = value;
          }
        }
        MistVideo.log("Seek attempted outside of buffer, but MistServer does not support seeking in progressive flash. Setting to closest available instead");
        return video.currentTime = (video.buffered.length ? video.buffered.end(video.buffered.length-1)-keepaway : 0);
      }
    });
    
    callback(video);
  }
  
  if ("flvjs" in window) {
    this.onFLVLoad();
  }
  else {
    var scripttag = MistUtil.scripts.insert(MistVideo.urlappend(mistplayers.flv.scriptsrc(MistVideo.options.host)),{
      onerror: function(e){
        var msg = "Failed to load flv.js";
        if (e.message) { msg += ": "+e.message; }
        MistVideo.showError(msg);
      },
      onload: MistVideo.player.onFLVLoad
    },MistVideo);
  }
}
