mistplayers.videojs = {
  name: 'VideoJS player',
  mimes: ['html5/video/mp4','html5/application/vnd.apple.mpegurl','html5/video/ogg','html5/video/webm'],
  priority: Object.keys(mistplayers).length + 1,
  isMimeSupported: function (mimetype) {
    return (this.mimes.indexOf(mimetype) == -1 ? false : true);
  },
  isBrowserSupported: function (mimetype,source,options,streaminfo,logfunc) {
    
    //dont use https if the player is loaded over http
    if ((options.host.substr(0,7) == 'http://') && (source.url.substr(0,8) == 'https://')) {
      if (logfunc) { logfunc('HTTP/HTTPS mismatch for this source'); }
      return false;
    }
    
    //dont use videojs if this location is loaded over file://
    if ((location.protocol == 'file:') && (mimetype == 'html5/application/vnd.apple.mpegurl')) {
      if (logfunc) { logfunc('This source ('+mimetype+') won\'t work if the page is run via file://'); }
      return false;
    }
    
    //dont use HLS if there is an MP3 audio track, unless we're on apple or edge
    if ((mimetype == 'html5/application/vnd.apple.mpegurl') && (['iPad','iPhone','iPod','MacIntel'].indexOf(navigator.platform) == -1) && (navigator.userAgent.indexOf('Edge') == -1)) {
      for (var i in streaminfo.meta.tracks) {
        var t = streaminfo.meta.tracks[i];
        if (t.codec == 'MP3') {
          return false;
        }
      }
    }
    
    
    return ('MediaSource' in window);
  },
  player: function(){},
};
var p = mistplayers.videojs.player;
p.prototype = new MistPlayer();
p.prototype.build = function (options) {
  var cont = document.createElement('div');
  cont.className = 'mistplayer';
  var me = this; //to allow nested functions to access the player class itself
  
  this.addlog('Building VideoJS player..');
  
  var ele = this.getElement('video');
  cont.appendChild(ele);
  ele.className = '';
  ele.crossOrigin = 'anonymous'; //required for subtitles
  
  var shortmime = options.source.type.split('/');
  shortmime.shift();
  
  var source = document.createElement('source');
  source.setAttribute('src',options.src);
  this.source = source;
  ele.appendChild(source);
  source.type = shortmime.join('/');
  this.addlog('Adding '+source.type+' source @ '+options.src);
  if (source.type == 'application/vnd.apple.mpegurl') { source.type = 'application/x-mpegURL'; }
  
  ele.className += ' video-js';
  ele.width = options.width;
  ele.height = options.height;
  ele.style.width = options.width+'px';
  ele.style.height = options.height+'px';
  
  var vjsopts = {
    preload: 'auto'
  };
  
  if (options.autoplay) { vjsopts.autoplay = true; }
  if (options.loop) { 
    vjsopts.loop = true;
    ele.loop = true;
  }
  if (options.poster) { vjsopts.poster = options.poster; }
  if (options.controls) {
    if ((options.controls == 'stock') || (!this.buildMistControls())) {
      //MistControls have failed to build in the if condition
      ele.setAttribute('controls',true);
    }
  }
  
  
  me.onready(function(){
    me.videojs = videojs(ele,vjsopts,function(){
      me.addlog('Videojs initialized');
    });
  });
  
  this.addlog('Built html');
  
  //forward events
  ele.addEventListener('error',function(e){
    var msg;
    if ('message' in e) {
      msg = e.message;
    }
    else {
      msg = 'readyState: ';
      switch (me.element.readyState) {
        case 0:
          msg += 'HAVE_NOTHING';
          break;
        case 1:
          msg += 'HAVE_METADATA';
          break;
        case 2:
          msg += 'HAVE_CURRENT_DATA';
          break;
        case 3:
          msg += 'HAVE_FUTURE_DATA';
          break;
        case 4:
          msg += 'HAVE_ENOUGH_DATA';
          break;
      }
      msg += ' networkState: ';
      switch (me.element.networkState) {
        case 0:
          msg += 'NETWORK_EMPTY';
          break;
        case 1:
          msg += 'NETWORK_IDLE';
          break;
        case 2:
          msg += 'NETWORK_LOADING';
          break;
        case 3:
          msg += 'NETWORK_NO_SOURCE';
          break;
      }
    }
    //prevent onerror loops
    if (e.target == me.element) {
      e.message = msg;
    }
    else {
      me.adderror(msg);
    }
  });
  var events = ['abort','canplay','canplaythrough','durationchange','emptied','ended','interruptbegin','interruptend','loadeddata','loadedmetadata','loadstart','pause','play','playing','ratechange','seeked','seeking','stalled','volumechange','waiting','progress'];
  for (var i in events) {
    ele.addEventListener(events[i],function(e){
      me.addlog('Player event fired: '+e.type);
    });
  }
  
  return cont;
}
p.prototype.play = function(){ return this.element.play(); };
p.prototype.pause = function(){ return this.element.pause(); };
p.prototype.volume = function(level){
  if (typeof level == 'undefined' ) { return this.element.volume; }
  return this.element.volume = level;
};
p.prototype.loop = function(bool){ 
  if (typeof bool == 'undefined') {
    return this.element.loop;
  }
  return this.element.loop = bool;
};
p.prototype.load = function(){ return this.element.load(); };
if (document.fullscreenEnabled || document.webkitFullscreenEnabled || document.mozFullScreenEnabled || document.msFullscreenEnabled) {
  p.prototype.fullscreen = function(){
    if(this.element.requestFullscreen) {
      return this.element.requestFullscreen();
    } else if(this.element.mozRequestFullScreen) {
      return this.element.mozRequestFullScreen();
    } else if(this.element.webkitRequestFullscreen) {
      return this.element.webkitRequestFullscreen();
    } else if(this.element.msRequestFullscreen) {
      return this.element.msRequestFullscreen();
    }
  };
}
p.prototype.updateSrc = function(src){
  if (src == '') {
    this.videojs.dispose();
    return;
  }
  this.videojs.src({
    src: src,
    type: this.source.type
  });
  return true;
};
