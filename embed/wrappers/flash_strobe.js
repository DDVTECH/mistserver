mistplayers.flash_strobe = {
  name: "Strobe Flash media playback",
  mimes: ["flash/10","flash/11","flash/7"],
  priority: MistUtil.object.keys(mistplayers).length + 1,
  isMimeSupported: function (mimetype) {
    return (this.mimes.indexOf(mimetype) == -1 ? false : true);
  },
  isBrowserSupported: function (mimetype,source,MistVideo) {
    
    //check for http/https mismatch
    if ((MistUtil.http.url.split(source.url).protocol.slice(0,4) == "http") && (location.protocol != MistUtil.http.url.split(source.url).protocol)) {
      MistVideo.log("HTTP/HTTPS mismatch for this source");
      return false;
    }
    
    var version = 0;
    try {
      // check in the mimeTypes
      var plugin = navigator.mimeTypes["application/x-shockwave-flash"].enabledPlugin;
      if (plugin.version) { version = plugin.version.split(".")[0]; }
      else { version = plugin.description.replace(/([^0-9\.])/g, "").split(".")[0]; }
    } catch(e){}
    try {
      // for our special friend IE
      version = new ActiveXObject("ShockwaveFlash.ShockwaveFlash").GetVariable("$version").replace(/([^0-9\,])/g, "").split(",")[0];
    } catch(e){}
    
    if (!version) {
      //flash is not enabled? Might need to ask permission first.
      //TODO how? just let it build?
      return false;
    }
    
    var mimesplit = mimetype.split("/");
    
    return Number(version) >= Number(mimesplit[mimesplit.length-1]);
  },
  player: function(){this.onreadylist = [];}
};
var p = mistplayers.flash_strobe.player;
p.prototype = new MistPlayer();
p.prototype.build = function (MistVideo,callback) {
  
  var ele = document.createElement("object");
  var e = document.createElement("embed");
  ele.appendChild(e);
  
  function build(source) {
    var options = MistVideo.options;
    function createParam(name,value) {
      var p = document.createElement("param");
      p.setAttribute("name",name);
      p.setAttribute("value",value);
      return p;
    }
    
    MistUtil.empty(ele);
    ele.appendChild(createParam("movie",MistVideo.urlappend(options.host+MistVideo.source.player_url)));
    var flashvars = "src="+encodeURIComponent(source)+"&controlBarMode="+(options.controls ? "floating" : "none")+"&initialBufferTime=0.5&expandedBufferTime=5&minContinuousPlaybackTime=3"+(options.live ? "&streamType=live" : "")+(options.autoplay ? "&autoPlay=true" : "" )+(options.loop ? "&loop=true" : "" )+(options.poster ? "&poster="+options.poster : "" )+(options.muted ? "&muted=true" : "" );
    ele.appendChild(createParam("flashvars",flashvars));
    ele.appendChild(createParam("allowFullScreen","true"));
    ele.appendChild(createParam("wmode","direct"));
    if (options.autoplay) {
      ele.appendChild(createParam("autoPlay","true"));
    }
    if (options.loop) {
      ele.appendChild(createParam("loop","true"));
    }
    if (options.poster) {
      ele.appendChild(createParam("poster",options.poster));
    }
    if (options.muted) {
      ele.appendChild(createParam("muted","true"));
    }
    
    e.setAttribute("src",MistVideo.urlappend(MistVideo.source.player_url));
    e.setAttribute("type","application/x-shockwave-flash");
    e.setAttribute("allowfullscreen","true");
    e.setAttribute("flashvars",flashvars);
  }
  build(MistVideo.source.url);
  
  this.api = {};
  this.setSize = function(size){
    ele.setAttribute("width",size.width);
    ele.setAttribute("height",size.height);
    e.setAttribute("width",size.width);
    e.setAttribute("height",size.height);
  };
  this.setSize(MistVideo.calcSize());
  this.onready(function(){
    if (MistVideo.container) { MistVideo.container.removeAttribute("data-loading"); }
  });
  this.api.setSource = function(url){
    build(url);
  }
  
  MistVideo.log("Built html");
  callback(ele);
}
