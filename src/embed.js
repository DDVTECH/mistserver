function mistembed(streamname) {
  // return the current flash version
  function flash_version() {
    var version = 0;

    try {
      // check in the mimeTypes
      version = navigator.mimeTypes['application/x-shockwave-flash'].enabledPlugin.description.replace(/([^0-9\.])/g, '').split('.')[0];
    } catch(e){}
    try {
      // for our special friend IE
      version = new ActiveXObject('ShockwaveFlash.ShockwaveFlash').GetVariable("$version").replace(/([^0-9\,])/g, '').split(',')[0];
    } catch(e){}

    return parseInt(version, 10);
  };

  // return true if silverlight is installed
  function silverlight_installed() {
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
  };

  // return true if the browser thinks it can play the mimetype
  function html5_video_type(type) {
    var support = false;

    
    if (type == 'video/mp4') {
      if (navigator.userAgent.indexOf('Firefox') > -1) {
        //firefox claims to support MP4 but doesn't
        return false;
      }
      else if ((navigator.userAgent.indexOf('MSIE') > -1) && (parseInt(navigator.userAgent.split('MSIE')[1]) <= 9)) {
        //IE <= 9 doesn't either
        return false;
      }
    }
    
    
    try {
      var v = document.createElement('video');

      if( v && v.canPlayType(type) != "" )
      {
        support = true; // true-ish, anyway
      }
    } catch(e){}

    return support;
  }
  
  //return true if rtsp is supported
  function rtsp_support() {
    var plugin;
    
    try {
      // check in the mimeTypes
      plugin = navigator.mimeTypes["application/x-google-vlc-plugin"];
      return !!plugin;
    } catch(e){}
    try {
      // for our special friend IE
      plugin = new ActiveXObject('VideoLAN.Vlcplugin.1');
      return true;
    } catch(e){}
    
    return false;
  }

  // parse a "type" string from the controller. Format:
  // xxx/# (e.g. flash/3) or xxx/xxx/xxx (e.g. html5/application/ogg)
  function parseType(type) {
    var split = type.split('/');
    
    if( split.length > 2 ) {
      split[1] += '/' + split[2];
    }
    
    return split;
  }
  
  // return true if a type is supported
  function hasSupport(type) {
    var typemime = parseType(type);
    
    switch(typemime[0]) {
      case 'flash':             return flash_version() >= parseInt(typemime[1], 10);            break;
      case 'html5':             return html5_video_type(typemime[1]);                           break;
      case 'rtsp':              return rtsp_support();                                          break;
      case 'silverlight':       return silverlight_installed();                                 break;
      default:                  return false;                                                   break;
    }
  }

  
  // build HTML for certain kinds of types
  function buildPlayer(src, container, videowidth, videoheight, vtype) {
    // used to recalculate the width/height
    var ratio;

    // get the container's width/height
    var containerwidth = parseInt(container.scrollWidth, 10);
    var containerheight = parseInt(container.scrollHeight, 10);

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

    var maintype = parseType(src.type);
    mistvideo[streamname].embedded = src;
    
    switch(maintype[0]) {
      case 'flash':
        // maintype[1] is already checked (i.e. user has version > maintype[1])
        var flashplayer,
        url = encodeURIComponent(src.url) + '&controlBarMode=floating&initialBufferTime=0.5&expandedBufferTime=5&minContinuousPlaybackTime=3' + (vtype == 'live' ? "&streamType=live" : "") + (autoplay ? '&autoPlay=true' : '');
        
        if( parseInt(maintype[1], 10) >= 10 ) {
          flashplayer = 'http://fpdownload.adobe.com/strobe/FlashMediaPlayback_101.swf';
        }
        else {
          flashplayer = 'http://fpdownload.adobe.com/strobe/FlashMediaPlayback.swf';
        }
        
        container.innerHTML += '<object width="' + videowidth + '" height="' + videoheight + '">' +
                                '<param name="movie" value="' + flashplayer + '"></param>' + 
                                '<param name="flashvars" value="src=' + url + '"></param>' +
                                '<param name="allowFullScreen" value="true"></param>' +
                                '<param name="allowscriptaccess" value="always"></param>' + 
                                '<param name="wmode" value="direct"></param>' +
                                (autoplay ? '<param name="autoPlay" value="true">' : '') +
                                '<embed src="' + flashplayer + '" type="application/x-shockwave-flash" allowscriptaccess="always" allowfullscreen="true" width="' + videowidth + '" height="' + videoheight + '" flashvars="src=' + url + '"></embed>' + 
                              '</object>';
      break;

      case 'html5':
        container.innerHTML += '<video width="' + videowidth + '" height="' + videoheight + '" src="' + encodeURI(src.url) + '" controls="controls" '+(autoplay ? 'autoplay="autoplay"' : '')+'><strong>No HTML5 video support</strong></video>';
        break;
        
      case 'rtsp':
        /*container.innerHTML += '<object classid="clsid:CFCDAA03-8BE4-11cf-B84B-0020AFBBCCFA" width="'+videowidth+'" height="'+videoheight+'">'+
                                  '<param name="src" value="'+encodeURI(src.url)+'">'+
                                  '<param name="console" value="video1">'+
                                  '<param name="controls" value="All">'+
                                  '<param name="autostart" value="false">'+
                                  '<param name="loop" value="false">'+
                                  '<embed name="myMovie" src="'+encodeURI(src.url)+'" width="'+videowidth+'" height="'+videoheight+'" autostart="false" loop="false" nojava="true" console="video1" controls="All"></embed>'+
                                  '<noembed>Something went wrong.</noembed>'+
                                '</object>'; //realplayer, doesnt work */
        container.innerHTML +=  '<embed type="application/x-google-vlc-plugin"'+
                                  'pluginspage="http://www.videolan.org"'+
                                  'width="'+videowidth+'"'+
                                  'height="'+videoheight+'"'+
                                  'target="'+encodeURI(src.url)+'"'+
                                  'autoplay="'+(autoplay ? 'yes' : 'no')+'"'+
                                '>'+
                                '</embed>'+
                                '<object classid="clsid:9BE31822-FDAD-461B-AD51-BE1D1C159921" codebase="http://downloads.videolan.org/pub/videolan/vlc/latest/win32/axvlc.cab">'+
                                '</object>'; //vlc, seems to work, sort of. it's trying anyway
      break;
        
      case 'silverlight':
        container.innerHTML +=  '<object data="data:application/x-silverlight," type="application/x-silverlight" width="' + videowidth + '" height="' + videoheight + '">'+
                                  '<param name="source" value="' + encodeURI(src.url) + '/player.xap"/>'+
                                  '<param name="onerror" value="onSilverlightError" />'+
                                  '<param name="autoUpgrade" value="true" />'+
                                  '<param name="background" value="white" />'+
                                  '<param name="enableHtmlAccess" value="true" />'+
                                  '<param name="minRuntimeVersion" value="3.0.40624.0" />'+
                                  '<param name="initparams" value =\'autoload=false,'+(autoplay ? 'autoplay=true' : 'autoplay=false')+',displaytimecode=false,enablecaptions=true,joinLive=true,muted=false,playlist=<playList><playListItems><playListItem title="Test" description="testing" mediaSource="' + encodeURI(src.url) + '" adaptiveStreaming="true" thumbSource="" frameRate="25.0" width="" height=""></playListItem></playListItems></playList>\' />'+
                                  '<a href="http://go.microsoft.com/fwlink/?LinkID=124807" style="text-decoration: none;"> <img src="http://go.microsoft.com/fwlink/?LinkId=108181" alt="Get Microsoft Silverlight" style="border-style: none" /></a>'+
                                '</object>';
      break;
      default:
        container.innerHTML += '<strong>Missing embed code for output type "'+src.type+'"</strong>';
        video.error = 'Missing embed code for output type "'+src.type;
    }
  }
  
  var video = mistvideo[streamname],
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
  
  if (video.width == 0) { video.width = 250; }
  if (video.height == 0) { video.height = 250; }
  
  // create the container
  me.parentNode.insertBefore(container, me);
  // set the class to 'mistvideo'
  container.setAttribute('class', 'mistvideo');
  // remove script tag
  me.parentNode.removeChild(me);

  if(video.error) {
    // there was an error; display it
    if (video.on_error){
      container.innerHTML = video.on_error;
    }else{
      container.innerHTML = ['<strong>Error: ', video.error, '</strong>'].join('');
    }
  }
  else if ((typeof video.source == 'undefined') || (video.source.length < 1)) {
    // no stream sources
    if (video.on_error){
      container.innerHTML = video.on_error;
    }else{
      container.innerHTML = '<strong>Error: no protocols found</strong>';
    }
  }
  else {
    // no error, and sources found. Check the video types and output the best
    // available video player.
    var i,
      vtype = (video.type ? video.type : 'unknown'),
      foundPlayer = false,
      len = video.source.length;
      
    for (var i in video.source) {
      var support = hasSupport(video.source[i].type);
      video.source[i].browser_support = support;
      if ((support) || (forceType)) {
        if ((!forceType) || ((forceType) && (video.source[i].type.indexOf(forceType) >= 0))) {
          if (foundPlayer === false) { 
            foundPlayer = i;
            if (!forceSupportCheck) {
              break;
            }
          }
        }
      }
    }
    if (foundPlayer === false) {
      // of all the streams given, none was supported (eg. no flash and HTML5 video). Display error
      container.innerHTML = '<strong>No support for any player found</strong>';
    }
    else {
      // we support this kind of video, so build it.
      buildPlayer(video.source[foundPlayer], container, video.width, video.height, vtype);
    }
  }
  
  return (mistvideo[streamname].embedded ? mistvideo[streamname].embedded.type : false);
}
