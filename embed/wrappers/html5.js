mistplayers.html5 = {
  name: 'HTML5 video player',
  mimes: ['html5/application/vnd.apple.mpegurl','html5/video/mp4','html5/video/ogg','html5/video/webm','html5/audio/mp3','html5/audio/webm','html5/audio/ogg','html5/audio/wav'],
  priority: Object.keys(mistplayers).length + 1,
  isMimeSupported: function (mimetype) {
    return (this.mimes.indexOf(mimetype) == -1 ? false : true);
  },
  isBrowserSupported: function (mimetype,source,options,streaminfo) {
    if ((['iPad','iPhone','iPod','MacIntel'].indexOf(navigator.platform) != -1) && (mimetype == 'html5/video/mp4')) { return false; }
    
    var support = false;
    var shortmime = mimetype.split('/');
    shortmime.shift();
    
    if ((shortmime[0] == 'audio') && (streaminfo.height) && (!options.forceType) && (!options.forceSource)) {
      //claim you don't support audio-only playback if there is video data, unless this mime is being forced
      return false;
    }
    
    try {
      var v = document.createElement((shortmime[0] == 'audio' ? 'audio' : 'video'));
      shortmime = shortmime.join('/')
      if ((v) && (v.canPlayType(shortmime) != "")) {
        support = v.canPlayType(shortmime);
      }
    } catch(e){}
    return support;
  },
  player: function(){},
  mistControls: true
};
var p = mistplayers.html5.player;
p.prototype = new MistPlayer();
p.prototype.build = function (options) {
  var cont = document.createElement('div');
  cont.className = 'mistplayer';
  var me = this; //to allow nested functions to access the player class itself
  
  var shortmime = options.source.type.split('/');
  shortmime.shift();
  
  var ele = this.getElement((shortmime[0] == 'audio' ? 'audio' : 'video'));
  ele.className = '';
  cont.appendChild(ele);
  ele.crossOrigin = 'anonymous'; //required for subtitles
  if (shortmime[0] == 'audio') {
    this.setTracks = function() { return false; }
    this.fullscreen = false;
    cont.className += ' audio';
  }
  
  this.addlog('Building HTML5 player..');
  
  var source = document.createElement('source');
  source.setAttribute('src',options.src);
  this.source = source;
  ele.appendChild(source);
  source.type = shortmime.join('/');
  this.addlog('Adding '+source.type+' source @ '+options.src);
  
  if ((this.tracks.subtitle.length) && (this.subtitle)) {
    for (var i in this.tracks.subtitle) {
      var t = document.createElement('track');
      ele.appendChild(t);
      t.kind = 'subtitles';
      t.label = this.tracks.subtitle[i].desc;
      t.srclang = this.tracks.subtitle[i].lang;
      t.src = this.subtitle+'?track='+this.tracks.subtitle[i].trackid;
    }
  }
  
  ele.width = options.width;
  ele.height = options.height;
  ele.style.width = options.width+'px';
  ele.style.height = options.height+'px';
  
  if (options.autoplay) {
    ele.setAttribute('autoplay','');
  }
  if (options.loop) {
    ele.setAttribute('loop','');
  }
  if (options.poster) {
    ele.setAttribute('poster',options.poster);
  }
  if (options.controls) {
    if ((options.controls == 'stock') || (!this.buildMistControls())) {
      //MistControls have failed to build in the if condition
      ele.setAttribute('controls','');
    }
  }
  
  cont.onclick = function(){
    if (ele.paused) { ele.play(); }
    else { ele.pause(); }
  };
  
  if (options.live) {
    ele.addEventListener('error',function(e){
      if ((ele.error) && (ele.error.code == 3)) {
        e.stopPropagation();
        ele.load();
        me.cancelAskNextCombo();
        e.message = 'Handled decoding error';
        me.addlog('Decoding error: reloading..');
        me.report({
          type: 'playback',
          warn: 'A decoding error was encountered, but handled'
        });
      }
    },true);
  }
  
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
    me.adderror(msg);
  },true);
  var events = ['abort','canplay','canplaythrough','durationchange','emptied','ended','interruptbegin','interruptend','loadeddata','loadedmetadata','loadstart','pause','play','playing','ratechange','seeked','seeking','stalled','volumechange','waiting','progress'];
  for (var i in events) {
    ele.addEventListener(events[i],function(e){
      me.addlog('Player event fired: '+e.type);
    },true);
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
  this.source.setAttribute('src',src);
  return true;
};
p.prototype.resize = function(size){
  this.element.width = size.width;
  this.element.height = size.height;
};
