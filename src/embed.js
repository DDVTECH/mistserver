function mistembed(streamname)
{
	// return the current flash version
	function flashVersion()
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

	// what does the browser support - used in hasSupport()
	supports =
	{
		flashversion: flashVersion()
	};

	// return true if a type is supported
	function hasSupport(type)
	{
		switch(type)
		{
			case 'f4v':		return supports.flashversion >= 11;		break;
			case 'rtmp':	return supports.flashversion >= 10;		break;
			case 'flv':		return supports.flashversion >= 7;			break;

			default:			return false;
		}
	};

	// build HTML for certain kinds of types
	function buildPlayer(src, container, width, height)
	{
		// get the container's width/height
		cwidth = parseInt(container.style.width, 10);
		cheight = parseInt(container.style.height, 10);

		// video's max width/height is either the containers width/height (if it's smaller then the video) or the video's size
		width = cwidth < width ? cwidth : width;
		height = cheight < height ? cheight : height;

		switch(src.type)
		{
			case 'f4v':
			case 'rtmp':
			case 'flv':
				container.innerHTML = '<object width="' + width + '" height="' + height + '"><param name="movie" value="http://fpdownload.adobe.com/strobe/FlashMediaPlayback.swf"></param><param name="flashvars" value="src=' + encodeURI(src.url) + '&controlBarMode=floating"></param><param name="allowFullScreen" value="true"></param><param name="allowscriptaccess" value="always"></param><embed src="http://fpdownload.adobe.com/strobe/FlashMediaPlayback.swf" type="application/x-shockwave-flash" allowscriptaccess="always" allowfullscreen="true" width="' + width + '" height="' + height + '" flashvars="src=' + encodeURI(src.url) + '&controlBarMode=floating"></embed></object>';
			break;
		}
	};




	var video = mistvideo[streamname],
	    container = document.createElement('div'),
		 scripts = document.getElementsByTagName('script'),
		 me = scripts[scripts.length - 1];

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
		var i, video
			 foundPlayer = false,
			 len = video.source.length;

		for(i = 0; i < len; i++)
		{
			if( hasSupport( video.source[i].type ) )
			{
				// we support this kind of video, so build it.
				buildPlayer(video.source[i], container, video.width, video.height);

				// we've build a player, so we're done here
				foundPlayer = true;
				break;   // break for() loop
			}
		}

		if(!foundPlayer)
		{
			// of all the streams given, none was supported (eg. no flash and HTML5 video). Fall back.
			container.innerHTML = 'fallback here';
		}
	}

}
