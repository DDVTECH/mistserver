mistplayers.img = {
  name: 'HTML img tag',
  version: '1.1',
  mimes: ['html5/image/jpeg'],
  priority: Object.keys(mistplayers).length + 1,
  isMimeSupported: function (mimetype) {
    return (this.mimes.indexOf(mimetype) == -1 ? false : true);
  },
  isBrowserSupported: function (mimetype,source,options,streaminfo) {
    //only use this if we are sure we just want an image
    if ((options.forceType) || (options.forceSource) || (options.forcePlayer)) { return true; }
    return false;
  },
  player: function(){this.onreadylist = [];}
};
var p = mistplayers.img.player;
p.prototype = new MistPlayer();
p.prototype.build = function (options,callback) {
  var ele = this.getElement('img');
  ele.src = options.src;
  ele.style.display = 'block';
  callback(ele);
}
