mistplayers.flash_strobe = {
  name: 'Strobe Flash Media Playback',
  version: '1.1',
  mimes: ['flash/10','flash/11','flash/7'],
  priority: Object.keys(mistplayers).length + 1,
  isMimeSupported: function (mimetype) {
    return (this.mimes.indexOf(mimetype) == -1 ? false : true);
  },
  isBrowserSupported: function (mimetype,source,options) {
    //check for http / https crossovers
    if ((options.host.substr(0,7) == 'http://') && (source.url.substr(0,8) == 'https://')) { return false; }
    
    var version = 0;
    try {
      // check in the mimeTypes
      version = navigator.mimeTypes['application/x-shockwave-flash'].enabledPlugin.description.replace(/([^0-9\.])/g, '').split('.')[0];
    } catch(e){}
    try {
      // for our special friend IE
      version = new ActiveXObject('ShockwaveFlash.ShockwaveFlash').GetVariable("$version").replace(/([^0-9\,])/g, '').split(',')[0];
    } catch(e){}
    
    var mimesplit = mimetype.split('/');
    
    return Number(version) >= Number(mimesplit[mimesplit.length-1]);
  },
  player: function(){this.onreadylist = [];}
};
var p = mistplayers.flash_strobe.player;
p.prototype = new MistPlayer();
p.prototype.build = function (options,callback) {
  function createParam(name,value) {
    var p = document.createElement('param');
    p.setAttribute('name',name);
    p.setAttribute('value',value);
    return p;
  }
  
  
  var ele = this.getElement('object');
  
  ele.setAttribute('width',options.width);
  ele.setAttribute('height',options.height);
  
  ele.appendChild(createParam('movie',options.host+options.source.player_url));
  var flashvars = 'src='+encodeURIComponent(options.src)+'&controlBarMode='+(options.controls ? 'floating' : 'none')+'&initialBufferTime=0.5&expandedBufferTime=5&minContinuousPlaybackTime=3'+(options.live ? '&streamType=live' : '')+(options.autoplay ? '&autoPlay=true' : '' );
  ele.appendChild(createParam('flashvars',flashvars));
  ele.appendChild(createParam('allowFullScreen','true'));
  ele.appendChild(createParam('wmode','direct'));
  if (options.autoplay) {
    ele.appendChild(createParam('autoPlay','true'));
  }
  
  var e = document.createElement('embed');
  ele.appendChild(e);
  e.setAttribute('src',options.source.player_url);
  e.setAttribute('type','application/x-shockwave-flash');
  e.setAttribute('allowfullscreen','true');
  e.setAttribute('width',options.width);
  e.setAttribute('height',options.height);
  e.setAttribute('flashvars',flashvars);
  
  
  this.addlog('Built html');
  callback(ele);
}
