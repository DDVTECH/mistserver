mistplayers.img = {
  name: 'HTML img tag',
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
  player: function(){}
};
var p = mistplayers.img.player;
p.prototype = new MistPlayer();
p.prototype.build = function (options) {
  var ele = this.element('img');
  ele.src = options.src;
  ele.style.display = 'block';
  return ele;
}
