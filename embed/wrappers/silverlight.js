mistplayers.silverlight = {
  name: 'Silverlight',
  version: '1.1',
  mimes: ['silverlight'],
  priority: Object.keys(mistplayers).length + 1,
  isMimeSupported: function (mimetype) {
    return (this.mimes.indexOf(mimetype) == -1 ? false : true);
  },
  isBrowserSupported: function (mimetype) {
    var plugin;
    try {
      // check in the mimeTypes
      plugin = navigator.plugins["Silverlight Plug-In"];
      return !!plugin;
    } catch(e){}
    try {
      // for our special friend IE
      plugin = new ActiveXObject('AgControl.AgControl');
      return true;
    } catch(e){}
    
    return false;
  },
  player: function(){}
};
var p = mistplayers.silverlight.player;
p.prototype = new MistPlayer();
p.prototype.build = function (options,callback) {
  function createParam(name,value) {
    var p = document.createElement('param');
    p.setAttribute('name',name);
    p.setAttribute('value',value);
    return p;
  }
  
  var ele = this.getElement('object');
  ele.setAttribute('data','data:application/x-silverlight,');
  ele.setAttribute('type','application/x-silverlight');
  ele.setAttribute('width',options.width);
  ele.setAttribute('height',options.height);
  ele.appendChild(createParam('source',encodeURI(options.src)+'/player.xap'));
  ele.appendChild(createParam('initparams','autoload=false,'+(options.autoplay ? 'autoplay=true' : 'autoplay=false')+',displaytimecode=false,enablecaptions=true,joinLive=true,muted=false'));
  
  var a = document.createElement('a');
  ele.appendChild(a);
  a.setAttribute('href','http://go.microsoft.com/fwlink/?LinkID=124807');
  a.setAttribute('style','text-decoration: none;');
  var img = document.createElement('img');
  a.appendChild(img);
  img.setAttribute('src','http://go.microsoft.com/fwlink/?LinkId=108181');
  img.setAttribute('alt','Get Microsoft Silverlight');
  img.setAttribute('style','border-style: none;')
  
  this.addlog('Built html');
  callback(ele);
}
