mistplayers.jwplayer = {
  name: 'JWPlayer',
  version: '0.1',
  mimes: ['html5/video/mp4','html5/video/webm','dash/video/mp4','flash/10','flash/7','html5/application/vnd.apple.mpegurl','html5/audio/mp3','html5/audio/aac'],
  priority: Object.keys(mistplayers).length + 1,
  isMimeSupported: function (mimetype) {
    return (this.mimes.indexOf(mimetype) == -1 ? false : true);
  },
  isBrowserSupported: function (mimetype) {
    //TODO like, actually check the browser or something?
    if (typeof jwplayer == 'function') {
      return true;
    }
    return false;
  },
  player: function(){}
};
var p = mistplayers.jwplayer.player;
p.prototype = new MistPlayer();
p.prototype.build = function (options) {
  var ele = this.getElement('div');
  
  this.jw = jwplayer(ele).setup({
    file: options.src,
    width: options.width,
    height: options.height,
    autostart: options.autoplay,
    image: options.poster,
    controls: options.controls
  });
  
  this.addlog('Built html');
  return ele;
}
p.prototype.play = function(){ return this.jw.play(); };
p.prototype.pause = function(){ return this.jw.pause(); };
p.prototype.volume = function(level){
  if (typeof level == 'undefined' ) { return this.jw.getVolume/100; }
  return this.jw.setVolume(level*100);
};
