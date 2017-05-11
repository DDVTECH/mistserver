mistplayers.theoplayer = {
  name: 'TheoPlayer',
  version: '0.2',
  mimes: ['html5/application/vnd.apple.mpegurl','dash/video/mp4'],
  priority: Object.keys(mistplayers).length + 1,
  isMimeSupported: function (mimetype) {
    return (this.mimes.indexOf(mimetype) == -1 ? false : true);
  },
  isBrowserSupported: function (mimetype) {
    //TODO like, actually check the browser or something?
    if (typeof theoplayer == 'function') {
      return true;
    }
    return false;
  },
  player: function(){}
};
var p = mistplayers.theoplayer.player;
p.prototype = new MistPlayer();
p.prototype.build = function (options,callback) {
  var ele = this.getElement('video');
  
  ele.src = options.src;
  ele.width = options.width;
  ele.height = options.height;
  
  if (options.controls) {
    ele.setAttribute('controls','');
  }
  if (options.autoplay) {
    ele.setAttribute('autoplay','');
  }
  if (options.loop) {
    ele.setAttribute('loop','');
  }
  if (options.poster) {
    ele.setAttribute('poster',options.poster);
  }
  
  this.theoplayer = theoplayer(ele);
  
  this.addlog('Built html');
  callback(ele);
}
p.prototype.play = function(){ return this.theoplayer.play(); };
p.prototype.pause = function(){ return this.theoplayer.pause(); };
p.prototype.volume = function(level){
  if (typeof level == 'undefined' ) { return this.theoplayer.volume; }
  return this.theoplayer.volume = level;
};
p.prototype.fullscreen = function(){
  return this.theoplayer.requestFullscreen();
};
