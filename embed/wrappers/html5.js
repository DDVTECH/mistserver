mistplayers.html5 = {
  name: 'HTML5 video player',
  mimes: ['html5/application/vnd.apple.mpegurl','html5/video/mp4','html5/video/ogg','html5/video/webm','html5/audio/mp3','html5/audio/webm','html5/audio/ogg','html5/audio/wav'],
  priority: Object.keys(mistplayers).length + 1,
  isMimeSupported: function (mimetype) {
    return (this.mimes.indexOf(mimetype) == -1 ? false : true);
  },
  isBrowserSupported: function (mimetype) {
    if ((['iPad','iPhone','iPod','MacIntel'].indexOf(navigator.platform) != -1) && (mimetype == 'html5/video/mp4')) { return false; }
    var support = false;
    var shortmime = mimetype.split('/');
    shortmime.shift();
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
p.prototype.build = function (options,callback) {
  var cont = document.createElement('div');
  cont.className = 'mistplayer';
  var me = this; //to allow nested functions to access the player class itself
  
  var shortmime = options.source.type.split('/');
  shortmime.shift();
  
  var ele = this.element((shortmime[0] == 'audio' ? 'audio' : 'video'));
  ele.className = '';
  cont.appendChild(ele);
  ele.crossOrigin = 'anonymous';
  if (shortmime[0] == 'audio') {
    this.setTracks = false;
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
  ele.startTime = 0;
  
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
        ele.load();
        me.addlog('Decoding error: reloading..');
      }
    },true);
    
    var errorstate = false;
    function dced(e) {
      if (errorstate) { return; }
      
      errorstate = true;
      me.adderror('Connection lost..');
      
      var err = document.createElement('div');
      var msgnode = document.createTextNode('Connection lost..');
      err.appendChild(msgnode);
      err.className = 'error';
      var button = document.createElement('button');
      var t = document.createTextNode('Reload');
      button.appendChild(t);
      err.appendChild(button);
      button.onclick = function(){
        errorstate = false;
        ele.parentNode.removeChild(err);
        ele.load();
        ele.style.opacity = '';
      }
      err.style.position = 'absolute';
      err.style.top = 0;
      err.style.width = '100%';
      err.style['margin-left'] = 0;
      
      ele.parentNode.appendChild(err);
      ele.style.opacity = '0.2';
      
      function nolongerdced(){
        ele.removeEventListener('progress',nolongerdced);
        errorstate = false;
        ele.parentNode.removeChild(err);
        ele.style.opacity = '';
      }
      ele.addEventListener('progress',nolongerdced);
    }
    
    ele.addEventListener('stalled',dced,true);
    ele.addEventListener('ended',dced,true);
    ele.addEventListener('pause',dced,true);
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
  var events = ['abort','canplay','canplaythrough','durationchange','emptied','ended','interruptbegin','interruptend','loadeddata','loadedmetadata','loadstart','pause','play','playing','ratechange','seeked','seeking','stalled','volumechange','waiting'];
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
p.prototype.setTracks = function(usetracks){
  function urlAddParam(url,params) {
    var spliturl = url.split('?');
    var ret = [spliturl.shift()];
    var splitparams = [];
    if (spliturl.length) {
      splitparams = spliturl[0].split('&');
    }
    for (var i in params) {
      splitparams.push(i+'='+params[i]);
    }
    if (splitparams.length) { ret.push(splitparams.join('&')); }
    return ret.join('?');
  }
  
  if ('subtitle' in usetracks) {
    //remove previous subtitles
    var ts = this.element.getElementsByTagName('track');
    for (var i = ts.length - 1; i >= 0; i--) {
      this.element.removeChild(ts[i]);
    }
    var tracks = this.tracks.subtitle;
    for (var i in tracks) {
      if (tracks[i].trackid == usetracks.subtitle) {
        var t = document.createElement('track');
        this.element.appendChild(t);
        t.kind = 'subtitles';
        t.label = tracks[i].desc;
        t.srclang = tracks[i].lang;
        t.src = this.subtitle+'?track='+tracks[i].trackid;
        t.setAttribute('default','');
        break;
      }
    }
    delete usetracks.subtitle;
    if (Object.keys(usetracks).length == 0) { return true; }
  }
  
  var time = this.element.currentTime;
  this.source.setAttribute('src',urlAddParam(this.options.src,usetracks));
  if (this.element.readyState) {
    this.element.load();
  }
  
  this.element.currentTime = time;
  
  if ('trackselects' in this) {
    for (var i in usetracks) {
      if (i in this.trackselects) { this.trackselects[i].value = usetracks[i]; }
    }
  }
  
  return true;
}
p.prototype.resize = function(size){
  this.element.width = size.width;
  this.element.height = size.height;
};
