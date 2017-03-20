mistplayers.myplayer = {
  name: 'My video player',
  version: '0.1',
  mimes: ['my/mime/types'],
  priority: Object.keys(mistplayers).length + 1,
  isMimeSupported: function (mimetype) {
    return (this.mimes.indexOf(mimetype) == -1 ? false : true);
  },
  isBrowserSupported: function (mimetype) {
    //TODO your code here
    return false;
  },
  player: function(){}
};
var p = mistplayers.myplayer.player;
p.prototype = new MistPlayer();
p.prototype.build = function (options,callback) {
  var ele = this.element('object');
  
  //TODO your code here
  
  this.addlog('Built html');
  callback(ele);
}
