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

    return true;
  },
  player: function(){},
  scriptsrc: function(host) { return host+"/hlsjs.js"; }
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
  
  MistVideo.player.api.unload = function(){
    if (MistVideo.player.hls) {
      MistVideo.player.hls.destroy();
      MistVideo.player.hls = false;
      MistVideo.log("hls.js instance disposed");
    }
  };
  
  function init(url) {
    MistVideo.player.hls = new Hls({
      maxBufferLength: 15,
      maxMaxBufferLength: 60
    });
    MistVideo.player.hls.attachMedia(video);
    MistVideo.player.hls.on(Hls.Events.MEDIA_ATTACHED, function () {
      console.log("video and hls.js are now bound together !");
      //hls.loadSource("https://cattop/mist/cmaf/live/v9.m3u8");
      //hls.loadSource("https://mira:4433/cmaf/live/v9.m3u8");
      MistVideo.player.hls.loadSource(url);
      MistVideo.player.hls.on(Hls.Events.MANIFEST_PARSED, function (event, data) {
        console.log("manifest loaded, found " + data.levels.length + " quality level");
      });
    });
  }
  
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
