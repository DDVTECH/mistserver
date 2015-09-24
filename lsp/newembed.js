if (typeof mistplayers == 'undefined') {
  //no need to define this if it's already there

  var mistplayers = {};

  //create prototype with empty functions the player should have, so that any that have not been defined return false
  function MistPlayer() {
    
    //return true if this player is to be used for this mimetype
    this.formimetype = function (fullmimetype){ return false; };
    
    this.name = 'Generic player';
    
    //mimetype: e.g. 'video/mp4'; that is withouth the html5, flash, or whatever first part.
    this.supported = function(mimetype){ return false; };
    
    /*
    shape of build options: 
      {
        container: html DOM element,
        width: (int) video width,
        height: (int) video height,
        name: (string) the stream name,
        src: (string) the video source string,
        mimetype: (string) the mimetype,
        islive: (boolean) whether or not this is a live video,
        autoplay: (boolean) whether or not to enable autoplay
      }
    */
    this.build = function(options){ return false; };
    
    //creates the player element, including custom functions
    this.element = function(tag){
      var ele = document.createElement(tag);
      ele.mistplay = function(){ return false; };
      ele.mistpause = function(){ return false; };
      
      //(double) value: 0 for muted, 1 for max
      ele.mistsetvolume = function(value){ return false; };
      //return the current position, in seconds
      ele.mistposition = function(){ return false; };
      
      return ele;
    };
  }

  ///////////////////////////////////////////////////
  //ADD AVAILABLE PLAYERS TO THE MISTPLAYERS OBJECT//
  ///////////////////////////////////////////////////

  ////HTML5////////////////////////////////////////////
  var html5 = new MistPlayer();
  mistplayers.html5 = html5;
  html5.name = 'HTML5 video element';
  html5.formimetype = function(fullmimetype){
    var t = fullmimetype.split('/');
    return ((t[0] == 'html5') && (t[1] == 'video'));
  };
  html5.supported = function(mimetype) {
    var support = false;
    try {
      var v = document.createElement('video');
      if ((v) && (v.canPlayType(mimetype) != "")) {
        support = v.canPlayType(mimetype);
      }
    } catch(e){}
    return support;
  }
  html5.build = function(options){
    var ele = this.element('video');
    
    ele.setAttribute('width',options.width);
    ele.setAttribute('height',options.height);
    ele.setAttribute('src',encodeURI(options.src));
    ele.setAttribute('controls','controls');
    
    if (options.autoplay) {
      ele.setAttribute('autoplay','controls');
    }
    
    ele.mistplay = function(){
      this.play();
    };
    ele.mistpause = function(){
      this.pause();
    };
    ele.mistsetvolume = function(value){
      this.volume = value;
    };
    ele.mistposition = function(){
      return this.currentTime;
    }
    
    options.container.appendChild(ele);
    return ele;
  }

  ////FLASH////////////////////////////////////////////
  var flash = new MistPlayer();
  mistplayers.flash = flash;
  flash.name = 'Flash object';
  flash.formimetype = function(fullmimetype){
    var t = fullmimetype.split('/');
    return (t[0] == 'flash');
  };
  flash.supported = function(mimetype) {
    
    var version = 0;
    try {
      // check in the mimeTypes
      version = navigator.mimeTypes['application/x-shockwave-flash'].enabledPlugin.description.replace(/([^0-9\.])/g, '').split('.')[0];
    } catch(e){}
    try {
      // for our special friend IE
      version = new ActiveXObject('ShockwaveFlash.ShockwaveFlash').GetVariable("$version").replace(/([^0-9\,])/g, '').split(',')[0];
    } catch(e){}
    version = parseInt(version);
    
    return version >= parseInt(mimetype);
  }
  flash.build = function(options){
    var ele = this.element('object');
    ele.setAttribute('id',options.name);
    ele.setAttribute('width',options.width);
    ele.setAttribute('height',options.height);
    
    //set flashvars
    var flashvars = {
      src: encodeURIComponent(options.src),
      controlBarMode: 'floating',
      initialBufferTime: 5,
      expandedBufferTime: 5,
      minContinuousPlaybackTime: 3
    };
    //set param elements
    var params = {
      movie: 'http://fpdownload.adobe.com/strobe/FlashMediaPlayback.swf',
      flashvars: [],
      allowFullScreen: 'true',
      allowscriptaccess: 'always',
      wmode: 'direct'
    };
    if (options.autoplay) {
      params.autoPlay = 'true';
      flashvars.autoPlay = 'true';
    }
    if (options.islive) {
      flashvars.streamType = 'live';
    }
    if (parseInt(options.mimetype) >= 10) {
      params.movie = 'http://fpdownload.adobe.com/strobe/FlashMediaPlayback_101.swf';
    }
    for (var i in flashvars) {
      params.flashvars.push(i+'='+flashvars[i]);
    }
    params.flashvars = params.flashvars.join('&');
    for (var i in params) {
      var param = document.createElement('param');
      ele.appendChild(param);
      param.setAttribute('name',i);
      param.setAttribute('value',params[i]);
    }
    
    var embed = document.createElement('embed');
    embed.setAttribute('name',options.name);
    embed.setAttribute('src',params.movie);
    embed.setAttribute('type','application/x-shockwave-flash');
    embed.setAttribute('allowscriptaccess','always');
    embed.setAttribute('allowfullscreen','true');
    embed.setAttribute('width',options.width);
    embed.setAttribute('height',options.height);
    embed.setAttribute('flashvars',params.flashvars);
    ele.appendChild(embed);
    
    options.container.appendChild(ele);
    return ele;
  }
  
  ////SILVERLIGHT//////////////////////////////////////
  var silverlight = new MistPlayer();
  mistplayers.silverlight = silverlight;
  silverlight.name = 'Silverlight';
  silverlight.formimetype = function(fullmimetype){
    return (fullmimetype == 'silverlight');
  };
  silverlight.supported = function(mimetype) {
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
  }
  silverlight.build = function(options){
    var ele = this.element('object');
    
    ele.setAttribute('data','data:application/x-silverlight,'); //yes that comma needs to be there
    ele.setAttribute('type','application/x-silverlight');
    ele.setAttribute('width',options.width);
    ele.setAttribute('height',options.height);
    
    var params = {
      source: encodeURI(options.src),
      onerror: 'onSilverlightError',
      autoUpgrade: 'true',
      background: 'black',
      enableHtmlAccess: 'true',
      minRuntimeVersion: '3.0.40624.0',
      initparams: []
    };
    var initparams = {
      autoload: 'false',
      enablecaptions: 'true',
      joinLive: 'true',
      muted: 'false',
      playlist: document.createElement('playList')
    };
    if (options.autoplay) {
      initparams.autoplay = 'true';
    }
    var playitems = document.createElement('playListItems');
    initparams.playlist.appendChild(playitems);
    var playitem = document.createElement('playListItem');
    playitems.appendChild(playitem);
    playitems.setAttribute('mediaSource',encodeURI(options.src));
    playitems.setAttribute('adaptiveStreaming','true');
    initparams.playlist = initparams.playlist.outerHTML;
    
    for (var i in initparams) {
      params.initparams.push(i+'='+initparams[i]);
    }
    params.initparams = params.initparams.join(',');
    for (var i in params) {
      var param = document.createElement('param');
      ele.appendChild(param);
      param.setAttribute('name',i);
      param.setAttribute('value',params[i]);
    }
    
    ele.innerHTML += '<a href="http://go.microsoft.com/fwlink/?LinkID=124807" style="text-decoration: none;"><img src="http://go.microsoft.com/fwlink/?LinkId=108181" alt="Get Microsoft Silverlight" style="border-style: none" /></a>';
    
    options.container.appendChild(ele);
    return ele;
  }
  
} //end of player definitions

function mistembed(streamname) {
  
  function findPlayer(fullmimetype) {
    for (var i in mistplayers) {
      if (mistplayers[i].formimetype(fullmimetype)) {
        return mistplayers[i];
      }
    }
    return false;
  }
  
  var video = mistvideo[streamname];
  container = document.createElement('div'),
  scripts = document.getElementsByTagName('script'),
  me = scripts[scripts.length - 1];
  
  if (me.parentNode.hasAttribute('data-forcetype')) {
    var forceType = me.parentNode.getAttribute('data-forcetype');
  }
  if (me.parentNode.hasAttribute('data-forcesupportcheck')) {
    var forceSupportCheck = true;
  }
  if (me.parentNode.hasAttribute('data-autoplay')) {
    var autoplay = true;
  }
  
  // create the container
  me.parentNode.insertBefore(container, me);
  // set the class to 'mistvideo'
  container.setAttribute('class', 'mistvideo');
  // remove script tag
  me.parentNode.removeChild(me);

  if(video.error) {
    // there was an error; display it
    container.innerHTML = '<strong>Error: '+video.error+'</strong>';
  }
  else if ((typeof video.source == 'undefined') || (video.source.length < 1)) {
    // no stream sources
    container.innerHTML = '<strong>Error: no protocols found</strong>';
  }
  else {
    // no error, and sources found. Check the video types and output the best
    // available video player.
    
    var foundPlayer = false;
      
    for (var i in video.source) {
      if ((forceType) && (video.source[i].type.indexOf(forceType) < 0)) {
        video.source[i].rejected = 'This source type is not the one being forced.';
        continue;
      }
      
      var player = findPlayer(video.source[i].type);
      var shortmime = video.source[i].type.split('/');
      shortmime.shift();
      shortmime = shortmime.join('/');
      video.source[i].browser_support = false;
      
      if (player) {
        var support = player.supported(shortmime);
        if ((support) || (forceType)) {
          //either the player is supported by the browser, or this source type is being enforced
          
          video.source[i].browser_support = Boolean(support);
          if (foundPlayer === false) {
            foundPlayer = {
              protocol: video.source[i],
              player: player,
              shortmime: shortmime
            };
          }
          if (!forceSupportCheck) {
            break;
          }
        }
        else {
          video.source[i].rejected = 'The player for this source type ('+player.name+') is not supported by your browser.';
        }
      }
      else {
        video.source[i].rejected = 'No compatible player found for this source type.';
      }
    }
  }
  
    if (foundPlayer) {
      // we support this kind of video, so build it.
      
      //calculations for the size
      videowidth = video.width || 250;
      videoheight = video.height || 250;
      var ratio;
      var containerwidth = parseInt(container.scrollWidth);
      var containerheight = parseInt(container.scrollHeight);
      if(videowidth > containerwidth && containerwidth > 0) {
        ratio = videowidth / containerwidth;
        videowidth /= ratio;
        videoheight /= ratio;
      }
      if(videoheight > containerheight && containerheight > 0) {
        ratio = videoheight / containerheight;
        videowidth /= ratio;
        videoheight /= ratio;
      }
      
      video.embedded = foundPlayer.player.build({
        container: container,
        width: videowidth,
        height: videoheight,
        src: foundPlayer.protocol.url,
        name: streamname,
        mimetype: foundPlayer.shortmime,
        islive: (video.type == 'live'),
        autoplay: autoplay
      });
      
      return foundPlayer.protocol.type;
    }
    else {
      // of all the source types given, none was supported (eg. no flash and HTML5 video). Display error
      container.innerHTML = '<strong>No support for any player found</strong>';
    }
  
  return false;
}
