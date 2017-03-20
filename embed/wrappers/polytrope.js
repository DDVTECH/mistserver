mistplayers.polytrope = {
  name: 'Polytrope Flash Player',
  version: '0.2',
  mimes: ['flash/11','flash/10','flash/7'],
  priority: Object.keys(mistplayers).length + 1,
  isMimeSupported: function (mimetype) {
    return (this.mimes.indexOf(mimetype) == -1 ? false : true);
  },
  isBrowserSupported: function (mimetype) {
    return false;
    
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
  player: function(){}
};
var p = mistplayers.polytrope.player;
p.prototype = new MistPlayer();
p.prototype.build = function (options,callback) {
  function createParam(name,value) {
    var p = document.createElement('param');
    p.setAttribute('name',name);
    p.setAttribute('value',value);
    return p;
  }
  
  //TODO its not working.
  
  /*
    this.swf = this.video_instance_el.flash({
      swf: "/shared/swf/videoplayer.swf?" + (new Date).getTime(),
      width: "100%",
      height: parseInt(this.options.element.height()),
      wmode: "opaque",
      menu: "false",
      allowFullScreen: "true",
      allowFullScreenInteractive: "true",
      allowScriptAccess: "always",
      id: "cucumbertv-swf-" + this.guid,
      expressInstall: "/shared/swf/expressInstall.swf",
      flashvars: {
        rtmp_url: "rtmp://" + this.options.stream_host + "/play/",
        stream_name: this.options.stream_name,
        poster: this.options.poster,
        autoplay: this.options.autoplay,
        color_1: "0x1d1d1d",
        color_2: "0xffffff",
        buffer_time: .1,
        is_streaming_url: "/api/user/is_streaming",
        username: this.options.username,
        mode: "v" == this.options.type ? "archive" : "live",
        guid: this.guid
      }
    })
    
    <div>
      <object data="/shared/swf/videoplayer.swf?1468312898591" type="application/x-shockwave-flash" id="cucumbertv-swf-4dc64c18-59af-91a2-d0c5-ab8df4f45c65" width="100%" height="660">
        <param name="wmode" value="opaque">
        <param name="menu" value="false">
        <param name="allowFullScreen" value="true">
        <param name="allowFullScreenInteractive" value="true">
        <param name="allowScriptAccess" value="always">
        <param name="expressInstall" value="/shared/swf/expressInstall.swf">
        <param name="flashvars" value="rtmp_url=rtmp://www.stickystage.com/play/&amp;stream_name=stickystage_archive+SrA-2016.07.08.23.54.08&amp;poster=/stickystage/users/SrA/archive/SrA-2016.07.08.23.54.08.jpg&amp;autoplay=true&amp;color_1=0x1d1d1d&amp;color_2=0xffffff&amp;buffer_time=0.1&amp;is_streaming_url=/api/user/is_streaming&amp;username=SrA&amp;mode=archive&amp;guid=4dc64c18-59af-91a2-d0c5-ab8df4f45c65">
        <param name="movie" value="/shared/swf/videoplayer.swf?1468312898591">
      </object>
    </div>
  */
  
  
  
  var ele = this.element('object');
  ele.data = 'players/polytrope.swf';
  ele.type = 'application/x-shockwave-flash';
  ele.width = options.width;
  ele.height = options.height;
  
  /*
  ele.appendChild(createParam('allowFullScreen','true'));
  ele.appendChild(createParam('allowScriptAccess','always'));
  var flashvars = 'rtmp_url=rtmp://www.stickystage.com/play/&amp;stream_name=stickystage_archive+SrA-2016.07.08.23.54.08&amp;poster=/stickystage/users/SrA/archive/SrA-2016.07.08.23.54.08.jpg&amp;autoplay=true&amp;color_1=0x1d1d1d&amp;color_2=0xffffff&amp;buffer_time=0.1&amp;is_streaming_url=/api/user/is_streaming&amp;username=SrA&amp;mode=archive&amp;guid=4dc64c18-59af-91a2-d0c5-ab8df4f45c65';
  ele.appendChild(createParam('flashvars',flashvars));
  ele.appendChild(createParam('movie','players/polytrope.swf'));
  */
  
  ele.innerHTML =         '<param name="wmode" value="opaque">       <param name="menu" value="false">        <param name="allowFullScreen" value="true">        <param name="allowFullScreenInteractive" value="true">        <param name="allowScriptAccess" value="always">        <param name="expressInstall" value="/shared/swf/expressInstall.swf">        <param name="flashvars" value="rtmp_url=rtmp://www.stickystage.com/play/&amp;stream_name=stickystage_archive+SrA-2016.07.08.23.54.08&amp;poster=http://stickystage.com/stickystage/users/SrA/archive/SrA-2016.07.08.23.54.08.jpg&amp;autoplay=true&amp;color_1=0x1d1d1d&amp;color_2=0xffffff&amp;buffer_time=0.1&amp;is_streaming_url=/api/user/is_streaming&amp;username=SrA&amp;mode=archive&amp;guid=4dc64c18-59af-91a2-d0c5-ab8df4f45c65">        <param name="movie" value="players/polytrope.swf">';
  
  this.addlog('Built html');
  callback(ele);
}
