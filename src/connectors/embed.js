function mistembed(streamname)
{
  // return the current flash version
  function flash_version()
  {
    var version = 0;

    try
    {
      // check in the mimeTypes
      version = navigator.mimeTypes['application/x-shockwave-flash'].enabledPlugin.description.replace(/([^0-9\.])/g, '').split('.')[0];
    }catch(e){}
    try
    {
      // for our special friend IE
      version = new ActiveXObject('ShockwaveFlash.ShockwaveFlash').GetVariable("$version").replace(/([^0-9\,])/g, '').split(',')[0];
    }catch(e){}

    return parseInt(version, 10);
  };

  // return true if silverlight is installed
  function silverlight_installed()
  {
    var plugin;
    
    try
    {
      // check in the mimeTypes
      plugin = navigator.plugins["Silverlight Plug-In"];
      return !!plugin;
    }catch(e){}
    try
    {
      // for our special friend IE
      plugin = new ActiveXObject('AgControl.AgControl');
      return true;
    }catch(e){}

    return false;
  };

  // return true if the browser thinks it can play the mimetype
  function html5_video_type(type)
  {
    var support = false;

    try
    {
      var v = document.createElement('video');

      if( v && v.canPlayType(type) != "" )
      {
        support = true; // true-ish, anyway
      }
    }catch(e){}

    return support;
  }

  // parse a "type" string from the controller. Format:
  // xxx/# (e.g. flash/3) or xxx/xxx/xxx (e.g. html5/application/ogg)
  function parseType(type)
  {
    var split = type.split('/');
    
    if( split.length > 2 )
    {
      split[1] += '/' + split[2];
    }
    
    return split;
  }
  
  // return true if a type is supported
  function hasSupport(type)
  {
    var typemime = parseType(type);
    
    switch(typemime[0])
    {
      case 'flash':             return flash_version() >= parseInt(typemime[1], 10);            break;
      case 'html5':             return html5_video_type(typemime[1]);                           break;
      case 'silverlight':	return silverlight_installed();                                 break;

      default:                  return false;                                                   break;
    }
  };

  
  // build HTML for certain kinds of types
  function buildPlayer(src, container, videowidth, videoheight, vtype)
  {
    // used to recalculate the width/height
    var ratio;

    // get the container's width/height
    var containerwidth = parseInt(container.scrollWidth, 10);
    var containerheight = parseInt(container.scrollHeight, 10);

    if(videowidth > containerwidth && containerwidth > 0)
    {
      ratio = videowidth / containerwidth;

      videowidth /= ratio;
      videoheight /= ratio;
    }

    if(videoheight > containerheight && containerheight > 0)
    {
      ratio = videoheight / containerheight;

      videowidth /= ratio;
      videoheight /= ratio;
    }

    var maintype = parseType(src.type);
    mistvideo[streamname].embedded_type = src.type;
    
    switch(maintype[0])
    {
      case 'flash':
        // maintype[1] is already checked (i.e. user has version > maintype[1])
        var flashplayer,
            url = encodeURIComponent(src.url) + '&controlBarMode=floating&initialBufferTime=0.5&expandedBufferTime=5&minContinuousPlaybackTime=3' + (vtype == 'live' ? "&streamType=live" : "");
        
        if( parseInt(maintype[1], 10) >= 10 )
        {
          flashplayer = 'http://fpdownload.adobe.com/strobe/FlashMediaPlayback_101.swf';
        }else{
          flashplayer = 'http://fpdownload.adobe.com/strobe/FlashMediaPlayback.swf';
        }
        
        container.innerHTML += '<object width="' + videowidth + '" height="' + videoheight + '">' +
                                '<param name="movie" value="' + flashplayer + '"></param>' + 
                                '<param name="flashvars" value="src=' + url + '"></param>' +
                                '<param name="allowFullScreen" value="true"></param>' +
                                '<param name="allowscriptaccess" value="always"></param>' + 
                                '<param name="wmode" value="direct"></param>' +
                                '<embed src="' + flashplayer + '" type="application/x-shockwave-flash" allowscriptaccess="always" allowfullscreen="true" width="' + videowidth + '" height="' + videoheight + '" flashvars="src=' + url + '"></embed>' + 
                              '</object>';
      break;

      case 'html5':
        container.innerHTML += '<video width="' + videowidth + '" height="' + videoheight + '" src="' + encodeURI(src.url) + '" controls="controls" ><strong>No HTML5 video support</strong></video>';
        break;
        
      case 'silverlight':
        container.innerHTML += '<object data="data:application/x-silverlight," type="application/x-silverlight" width="' + videowidth + '" height="' + videoheight + '"><param name="source" value="' + encodeURI(src.url) + '/player.xap"/><param name="onerror" value="onSilverlightError" /><param name="autoUpgrade" value="true" /><param name="background" value="white" /><param name="enableHtmlAccess" value="true" /><param name="minRuntimeVersion" value="3.0.40624.0" /><param name="initparams" value =\'autoload=false,autoplay=true,displaytimecode=false,enablecaptions=true,joinLive=true,muted=false,playlist=<playList><playListItems><playListItem title="Test" description="testing" mediaSource="' + encodeURI(src.url) + '" adaptiveStreaming="true" thumbSource="" frameRate="25.0" width="" height=""></playListItem></playListItems></playList>\' /><a href="http://go.microsoft.com/fwlink/?LinkID=124807" style="text-decoration: none;"> <img src="http://go.microsoft.com/fwlink/?LinkId=108181" alt="Get Microsoft Silverlight" style="border-style: none" /></a></object>';
      break;


      case 'fallback':
        container.innerHTML += '<strong>No support for any player found</strong>';
      break;         
    }

  };
  
  var video = mistvideo[streamname],
  container = document.createElement('div'),
  scripts = document.getElementsByTagName('script'),
  me = scripts[scripts.length - 1];
  
  if (me.parentNode.hasAttribute('data-forcetype')) {
    var forceType = me.parentNode.getAttribute('data-forcetype');
  }
  
  if (video.width == 0) { video.width = 250; }
  if (video.height == 0) { video.height = 250; }
  
  // create the container
  me.parentNode.insertBefore(container, me);
  // set the class to 'mistvideo'
  container.setAttribute('class', 'mistvideo');
  // remove script tag
  me.parentNode.removeChild(me);

  if(video.error)
  {
    // there was an error; display it
    container.innerHTML = ['<strong>Error: ', video.error, '</strong>'].join('');
  }else if(video.source.length < 1)
  {
    // no stream sources
    container.innerHTML = '<strong>Error: no streams found</strong>';
  }else{
    // no error, and sources found. Check the video types and output the best
    // available video player.
    var i,
        vtype = (video.type ? video.type : 'unknown'),
        foundPlayer = false,
        len = video.source.length;
        
    if (typeof forceType != 'undefined') {
      i = forceType;
      if (typeof video.source[i] == 'undefined') {
        container.innerHTML = '<strong>Invalid force integer ('+i+').</strong>';
      }
      else {
        if ( hasSupport(video.source[i].type) ) {
          buildPlayer(video.source[i], container.parentNode, video.width, video.height, vtype);
        }
        else {
          container.innerHTML = '<strong>Your browser does not support the type "'+video.source[i].type+'".</strong>';
        }
      }
    }
    else {
    
      for(i = 0; i < len; i++)
      {
        //console.log("trying support for type " + video.source[i].type + " (" + parseType(video.source[i].type)[0] + "  -  " + parseType(video.source[i].type)[1] + ")");
        if( hasSupport( video.source[i].type ) )
        {
          // we support this kind of video, so build it.
          buildPlayer(video.source[i], container.parentNode, video.width, video.height, vtype);

          // we've build a player, so we're done here
          foundPlayer = true;
          break;   // break for() loop
        }
      }

      if(!foundPlayer)
      {
        // of all the streams given, none was supported (eg. no flash and HTML5 video). Display error
        buildPlayer({type: 'fallback'}, container.parentNode, video.width, video.height);
      }
    }
  }
  
  return (typeof mistvideo[streamname].embedded_type != 'undefined' ? mistvideo[streamname].embedded_type : false);
}
