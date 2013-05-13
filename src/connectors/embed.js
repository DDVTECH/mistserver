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

  function html5_video_type(type)
  {
    var support = false;

    try
    {
      var v = document.createElement('video');

      if( v && v.canPlayType(type) != "" )
      {
        support = true;
      }
    }catch(e){}

    return support;
  }

  // what does the browser support - used in hasSupport()
  supports =
  {
    flashversion:	flash_version(),
    hls:				html5_video_type('application/vnd.apple.mpegurl'),
    ism:				html5_video_type('application/vnd.ms-ss')
  };

  // return true if a type is supported
  function hasSupport(type)
  {
    switch(type)
    {
      case 'f4v':		return supports.flashversion >= 11;		break;
      case 'rtmp':	return supports.flashversion >= 10;		break;
      case 'flv':		return supports.flashversion >= 7;		break;

      case 'hls':		return supports.hls;							break;
      case 'ism':		return supports.ism;							break;

      default:			return false;
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

    // if the video type is 'live', 
    lappend = vtype == 'live' ? "&streamType=live" : "";

    switch(src.type)
    {
      case 'f4v':
      case 'rtmp':
      case 'flv':
        container.innerHTML = '<object width="' + videowidth + '" height="' + videoheight + '"><param name="movie" value="http://fpdownload.adobe.com/strobe/FlashMediaPlayback.swf"></param><param name="flashvars" value="src=' + encodeURI(src.url) + '&controlBarMode=floating&expandedBufferTime=4&minContinuousPlaybackTime=10' + lappend + '"></param><param name="allowFullScreen" value="true"></param><param name="allowscriptaccess" value="always"></param><embed src="http://fpdownload.adobe.com/strobe/FlashMediaPlayback.swf" type="application/x-shockwave-flash" allowscriptaccess="always" allowfullscreen="true" width="' + videowidth + '" height="' + videoheight + '" flashvars="src=' + encodeURI(src.url) + '&controlBarMode=floating&expandedBufferTime=4&minContinuousPlaybackTime=10' + lappend + '"></embed></object>';
      break;


      case 'hls':
      case 'ism':
        container.innerHTML = '<video width="' + videowidth + '" height="' + videoheight + '" src="' + encodeURI(src.url) + '" controls="controls" ><strong>No HTML5 video support</strong></video>';
      break;


      case 'fallback':
        container.innerHTML = '<strong>No support for any player found</strong>';
      break;
    }

  };



  var video = mistvideo[streamname],
  container = document.createElement('div'),
  scripts = document.getElementsByTagName('script'),
  me = scripts[scripts.length - 1];
  
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
    var i, video, 
          vtype = video.type ? video.type : 'unknown';
       foundPlayer = false,
       len = video.source.length;

    for(i = 0; i < len; i++)
    {
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
