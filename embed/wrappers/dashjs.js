mistplayers.dashjs = {
  name: 'Dash.js Player',
  mimes: ['dash/video/mp4'],
  priority: Object.keys(mistplayers).length + 1,
  isMimeSupported: function (mimetype) {
    return (this.mimes.indexOf(mimetype) == -1 ? false : true);
  },
  isBrowserSupported: function (mimetype,source,options) {
    if ((options.host.substr(0,7) == 'http://') && (source.url.substr(0,8) == 'https://')) { return false; }
    return (('dashjs' in window) && ('MediaSource' in window) && (location.protocol != 'file:'));
  },
  player: function(){}
};
var p = mistplayers.dashjs.player;
p.prototype = new MistPlayer();
p.prototype.build = function (options,callback) {
  var cont = document.createElement('div');
  cont.className = 'mistplayer';
  var me = this;
  
  var ele = this.getElement('video');
  ele.className = '';
  cont.appendChild(ele);
  ele.width = options.width;
  ele.height = options.height;
  
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
    if (options.controls == 'stock') {
      ele.setAttribute('controls','');
    }
    else {
      this.buildMistControls();
    }
  }
  
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
  
  var player = dashjs.MediaPlayer().create();
  player.getDebug().setLogToBrowserConsole(false);
  player.initialize(ele,options.src,true);
  this.dash = player;
  this.src = options.src;
  
  this.addlog('Built html');
  return cont;
}
p.prototype.play = function(){ return this.element.play(); };
p.prototype.pause = function(){ return this.element.pause(); };
p.prototype.volume = function(level){
  if (typeof level == 'undefined' ) { return this.element.volume; }
  return this.element.volume = level;
};
p.prototype.play = function(){ return this.element.play(); };
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
p.prototype.resize = function(size){
  this.element.width = size.width;
  this.element.height = size.height;
};
