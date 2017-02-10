/////////////////////////////////////
//  DECLARE MISTPLAYER BASE CLASS  //
/////////////////////////////////////

var mistplayers = {};
var mistplayer_session_id = Math.round(Math.random()*1e12);

function MistPlayer() {};
MistPlayer.prototype.sendEvent = function(type,message,target) {
  try {
    var event = new Event(type,{
      bubbles: true,
      cancelable: true
    });
    event.message = message;
    target.dispatchEvent(event);
  }
  catch (e) {
    try {
      var event = document.createEvent('Event');
      event.initEvent(type,true,true);
      event.message = message;
      target.dispatchEvent(event);
    }
    catch (e) { return false; }
  }
  return true;
}
MistPlayer.prototype.addlog = function(msg) {
  this.sendEvent('log',msg,this.target);
}
MistPlayer.prototype.adderror = function(msg) {
  this.sendEvent('error',msg,this.target);
}
MistPlayer.prototype.build = function () {
  this.addlog('Error in player implementation');
  var err = document.createElement('div');
  var msgnode = document.createTextNode(msg);
  err.appendChild(msgnode);
  err.className = 'error';
  return err;
}
//creates the player element, including custom functions
MistPlayer.prototype.getElement = function(tag){
  var ele = document.createElement(tag);
  ele.className = 'mistplayer';
  this.element = ele;
  return ele;
};
MistPlayer.prototype.onreadylist = [];
MistPlayer.prototype.onready = function(dothis){
  this.onreadylist.push(dothis);
};
MistPlayer.prototype.play = false;
MistPlayer.prototype.pause = false;
MistPlayer.prototype.paused = false;
MistPlayer.prototype.volume = false;
MistPlayer.prototype.loop = false;
MistPlayer.prototype.fullscreen = false;
MistPlayer.prototype.setTracks = function(usetracks){
  if (usetracks == false) {
    if (!('updateSrc' in this)) { return false; }
    return true;
  }
  
  function urlAddParam(url,params) {
    var spliturl = url.split('?');
    var ret = [spliturl.shift()];
    var splitparams = [];
    if (spliturl.length) {
      splitparams = spliturl[0].split('&');
    }
    for (var i in params) {
      splitparams.push(i+'='+params[i]);
    }
    if (splitparams.length) { ret.push(splitparams.join('&')); }
    return ret.join('?');
  }
  
  if ('subtitle' in usetracks) {
    //remove previous subtitles
    var ts = this.element.getElementsByTagName('track');
    for (var i = ts.length - 1; i >= 0; i--) {
      this.element.removeChild(ts[i]);
    }
    var tracks = this.tracks.subtitle;
    for (var i in tracks) {
      if (tracks[i].trackid == usetracks.subtitle) {
        var t = document.createElement('track');
        this.element.appendChild(t);
        t.kind = 'subtitles';
        t.label = tracks[i].desc;
        t.srclang = tracks[i].lang;
        t.src = this.subtitle+'?track='+tracks[i].trackid;
        t.setAttribute('default','');
        break;
      }
    }
    delete usetracks.subtitle;
    if (Object.keys(usetracks).length == 0) { return true; }
  }
  
  var time = this.element.currentTime;
  var newurl;
  if (this.options.source.type == 'html5/application/vnd.apple.mpegurl') { //for HLS, use a different format for track selection
    newurl = this.options.src.split('/');
    var m3u8 = newurl.pop(); //take this off now, it will be added back later
    var hlstracks = [];
    for (var i in usetracks) {
      //for audio or video tracks, just add the tracknumber between slashes
      switch (i) {
        case 'audio':
        case 'video':
          if (usetracks[i] == 0) { continue; }
          hlstracks.push(usetracks[i]);
          break;
      }
    }
    if (hlstracks.length) { newurl.push(hlstracks.join('_')); }
    newurl.push(m3u8); //put back index.m3u8
    newurl = newurl.join('/');
  }
  else {
    newurl = urlAddParam(this.options.src,usetracks);
  }
  this.updateSrc(newurl);
  if (this.element.readyState) {
    this.element.load();
  }
  
  this.element.currentTime = time;
  
  if ('trackselects' in this) {
    for (var i in usetracks) {
      if (i in this.trackselects) { this.trackselects[i].value = usetracks[i]; }
    }
  }
  
  return true;
};
MistPlayer.prototype.resize = false;
MistPlayer.prototype.buildMistControls = function(){
  if (!('flex' in document.head.style) || (['iPad','iPod','iPhone'].indexOf(navigator.platform) != -1)) {
    //this browser does not support MistControls
    this.addlog('Mist controls are not supported');
    return false;
  }
  this.addlog('Building Mist controls..');
  
  var ele = this.element;
  var options = this.options;
  var me = this; //to allow nested functions to access the player class itself
  
  function formatTime(secs) {
    var hours = Math.floor(secs / 3600);
    secs = secs - hours * 3600;
    var mins  = Math.floor(secs / 60);
    secs = Math.floor(secs - mins * 60);
    var str = [];
    if (hours) {
      str.push(hours);
    }
    str.push(('0'+mins).slice(-2));
    str.push(('0'+secs).slice(-2));
    return str.join(':');
  }
  function whilePlaying() {
    timestampValue.nodeValue = formatTime(ele.currentTime);
    bar.style.width = ((ele.currentTime-ele.startTime)/ele.duration*100)+'%';
    setTimeout(function(){
      if (!ele.paused) {
        whilePlaying();
      }
    },0.1e3);
  };
  function whileLivePlaying(track) {
    
    //var playtime = (new Date()) - options.initTime;
    var playtime = ele.currentTime*1e3;
    timestampValue.nodeValue = formatTime((playtime + track.lastms)/1e3);

    
    setTimeout(function(){
      if (!ele.paused) {
        whileLivePlaying(track);
      }
    },0.1e3);
  };
  

  var controls = document.createElement('div');
  ele.parentNode.appendChild(controls);
  controls.className = 'controls';
  controls.onclick = function(e){ e.stopPropagation(); };
  
  //if the video is very small, zoom the controls
  var zoom = options.width/480;
  if (zoom < 1) {
    zoom = Math.max(zoom,0.5);
    if ('zoom' in controls.style) { controls.style.zoom = zoom; }
    else {
      controls.className += ' smaller'; //if css doesn't support zoom, apply smaller class to use smaller controls
    }
  }
  else { zoom = 1; }
  ele.style['min-width'] = zoom*400+'px';
  ele.style['min-height'] = zoom*160+'px';

  var play = document.createElement('div');
  controls.appendChild(play);
  play.className = 'button play';
  play.title = 'Play / Pause';
  play.setAttribute('data-state','paused');
  play.onclick = function(){
    if (ele.paused) {
      if (options.live) {
        me.load();
      }
      me.play();
    }
    else {
      me.pause();
    }
  };

  var progressCont = document.createElement('div');
  controls.appendChild(progressCont);
  progressCont.className = 'progress_container';
  ele.startTime = 0;
  if (!options.live) {
    var progress = document.createElement('div');
    progressCont.appendChild(progress);
    progress.className = 'button progress';
    progress.getPos = function(e){
      if (!isFinite(ele.duration)) { return 0; }
      var style = progress.currentStyle || window.getComputedStyle(progress, null);
      var zoom = Number(!('zoom' in controls.style) || controls.style.zoom == '' ? 1 : controls.style.zoom);
      
      var pos0 = progress.getBoundingClientRect().left - parseInt(style.borderLeftWidth,10);
      var perc = (e.clientX - pos0 * zoom) / progress.offsetWidth / zoom;
      var secs = Math.max(0,perc) * ele.duration;
      return secs;
    }
    progress.onmousemove = function(e) {
      if (ele.duration) {
        var pos = this.getPos(e);
        hintValue.nodeValue = formatTime(pos);
        hint.style.display = 'block';
        hint.style.left = (pos / ele.duration)*100+'%';
      }
    };
    progress.onmouseout = function() {
      hint.style.display = 'none';
    };
    progress.onmouseup = function(e) {
      ele.currentTime = this.getPos(e);
      ele.startTime = ele.currentTime;
      bar.style.left = (ele.startTime/ele.duration*100)+'%';
    };
    progress.ondragstart = function() { return false; };
    var bar = document.createElement('div');
    progress.appendChild(bar);
    bar.className = 'bar';
    var buffers = [];
    var hint = document.createElement('div');
    progressCont.appendChild(hint);
    hint.className = 'hint';
    var hintValue = document.createTextNode('-:--');
    hint.appendChild(hintValue);
    
    ele.addEventListener('seeking',function(){
      me.target.setAttribute('data-loading','');
    });
    ele.addEventListener('canplay',function(){
      me.target.removeAttribute('data-loading');
    });
    ele.addEventListener('playing',function(){
      me.target.removeAttribute('data-loading');
    });
    ele.addEventListener('progress',function(){
      me.target.removeAttribute('data-loading');
    });
  }

  var timestamp = document.createElement('div');
  controls.appendChild(timestamp);
  timestamp.className = 'button timestamp';
  var timestampValue = document.createTextNode('-:--');
  timestamp.title = 'Time';
  timestamp.appendChild(timestampValue);

  var sound = document.createElement('div');
  controls.appendChild(sound);
  sound.className = 'button sound';
  var volume = document.createElement('div');
  sound.appendChild(volume);
  sound.getPos = function(ypos){
    var style = this.currentStyle || window.getComputedStyle(this, null);
    
    var zoom = Number(!('zoom' in controls.style) || controls.style.zoom == '' ? 1 : controls.style.zoom);
    
    var pos0 = sound.getBoundingClientRect().top - parseInt(style.borderTopWidth,10);
    var perc = (ypos - pos0 * zoom) / sound.offsetHeight / zoom;
    
    perc = 1 - Math.min(1,Math.max(0,perc)); //linear range between 0 and 1
    perc = 1 - Math.pow((1-perc),1/2);       //transform to quadratic range between 0    and 1
    
    return perc;
  }
  volume.className = 'volume';
  sound.title = 'Volume';
  if (('localStorage' in window) && (localStorage != null) && ('mistVolume' in localStorage)) {
    ele.volume = localStorage['mistVolume'];
    volume.style.height = ele.volume*100+'%';
  }
  var mousedown = function(e){
    var mousemove = function(e){
      ele.volume = sound.getPos(e.clientY);
    };
    var mouseup = function(e){
      document.removeEventListener('mousemove',mousemove);
      controls.removeEventListener('mousemove',mousemove);
      document.removeEventListener('touchmove',mousemove);
      document.removeEventListener('mouseup',mouseup);
      document.removeEventListener('touchend',mouseup);
      try {
        localStorage['mistVolume'] = ele.volume;
      }
      catch (e) {}
    };
    document.addEventListener('mousemove',mousemove);
    controls.addEventListener('mousemove',mousemove); //this one is added because the controls hiding mechanism stops propagation to the document
    document.addEventListener('touchmove',mousemove);
    document.addEventListener('mouseup',mouseup);
    document.addEventListener('touchend',mouseup);
    ele.volume = sound.getPos(e.clientY);
  };
  sound.onmousedown = mousedown;
  sound.ontouchstart = mousedown;
  sound.ondragstart = function() { return false; };
  sound.onclick = function(e){
    ele.volume = sound.getPos(e.clientY);
    try {
      localStorage['mistVolume'] = ele.volume;
    }
    catch (e) {}
  };
  var speaker = document.createElement('div');
  speaker.title = 'Mute / Unmute';
  sound.appendChild(speaker);
  speaker.className = 'button speaker';
  speaker.onclick = function(e) {
    if (ele.volume) {
      lastvolume = ele.volume;
      ele.volume = 0;
    }
    else {
      ele.volume = lastvolume;
    }
    e.stopPropagation();
  };
  speaker.onmousedown = function(e){
    e.stopPropagation();
  };

  var buttons = document.createElement('div');
  buttons.className = 'column';
  controls.appendChild(buttons);
  
  if (
    (this.setTracks(false))
    && (this.tracks.video.length + this.tracks.audio.length + this.tracks.subtitle.length > 1)
    && (this.options.source.type != 'html5/video/ogg')
  ) {
    
    /*
      - the player supports setting tracks;
      - there is something to choose
      - it's not OGG, which doesn't have track selection yet
    */
    
    //prepare the html stuff
    var tracks = this.tracks;
    var tracksc = document.createElement('div');
    tracksc.innerHTML = 'Tracks';
    tracksc.className = 'button tracks';
    buttons.appendChild(tracksc);
    
    var settings = document.createElement('div');
    tracksc.appendChild(settings);
    settings.className = 'settings';
    
    me.trackselects = {};
    for (var i in tracks) { //for each track type (video, audio, subtitle..)
      if (tracks[i].length) {
        var l = document.createElement('label');
        settings.appendChild(l);
        var p = document.createElement('span');
        l.appendChild(p);
        var t = document.createTextNode(i+':');
        p.appendChild(t);
        var s = document.createElement('select');
        l.appendChild(s);
        me.trackselects[i] = s;
        s.setAttribute('data-type',i);
        for (var j in tracks[i]) { //for each track
          var o = document.createElement('option');
          s.appendChild(o);
          o.value = tracks[i][j].trackid;
          
          //make up something logical for the track name
          var name;
          if ('name' in tracks[i][j]) {
            name = tracks[i][j].name;
          }
          else if ('lang' in tracks[i][j]) {
            name = tracks[i][j].lang;
            o.setAttribute('data-lang',tracks[i][j].lang);
          }
          else {
            name = 'Track '+(Number(j)+1);
          }
          o.appendChild(document.createTextNode(name));
        }
        var o = document.createElement('option');
        s.appendChild(o);
        o.value = 0;
        var t = document.createTextNode('No '+i);
        o.appendChild(t);
        s.onchange = function(){
          var usetracks = {};
          if (this.getAttribute('data-type') == 'subtitle') {
            usetracks.subtitle = this.value;
            try {
              localStorage['mistSubtitle'] = this.querySelector('[value="'+this.value+'"]').getAttribute('data-lang');
            }
            catch (e) {}
          }
          else {
            for (var i in me.trackselects) {
              usetracks[me.trackselects[i].getAttribute('data-type')] = me.trackselects[i].value
            }
            if (this.value == 0) {
              //if we are disabling a video or audio track, dont allow us to disable another video or audio track because there will be no data
              var optiontags = this.parentNode.parentNode.querySelectorAll('select:not([data-type="subtitle"]) option[value="0"]');
              for (var i = 0; i < optiontags.length; i++) {
                optiontags[i].setAttribute('disabled','');
              }
            }
            else {
              //put back the disabled track options
              var optiontags = this.parentNode.parentNode.querySelectorAll('option[value="0"][disabled]');
              for (var i = 0; i < optiontags.length; i++) {
                optiontags[i].removeAttribute('disabled');
              }
            }
          }
          me.setTracks(usetracks);

        }
        if (i == 'subtitle')  {
          s.value = 0;
          if (('localStorage' in window) && (localStorage != null) && ('mistSubtitle' in localStorage)) {
            var option = s.querySelector('[data-lang="'+localStorage['mistSubtitle']+'"]');
            if (option) {
              s.value = option.value;
              s.onchange();
            } 
          }
        }
      }
    }
      
    var l = document
  }
  
  var buttons2 = document.createElement('div');
  buttons2.className = 'row';
  buttons.appendChild(buttons2);

  if ((me.loop) && (!options.live)) {
    var loop = document.createElement('div');
    buttons2.appendChild(loop);
    loop.className = 'button loop';
    loop.title = 'Loop';
    if (me.loop()) { loop.setAttribute('data-on',''); }
    loop.onclick = function(){
      if (me.loop()) {
        this.removeAttribute('data-on');
        me.loop(false);
      }
      else {
        this.setAttribute('data-on','');
        me.loop(true);
        if ((me.element.paused) && (me.element.currentTime == me.element.duration)) {
          me.play();
        }
      }
    };
  }
  if (me.fullscreen) {
    var fullscreen = document.createElement('div');
    buttons2.appendChild(fullscreen);
    fullscreen.className = 'button fullscreen';
    fullscreen.title = 'Fullscreen'
    fullscreen.onclick = function(){
      me.fullscreen();
    };
  }

  ele.addEventListener('play',function(){
    play.setAttribute('data-state','playing');
  });
  ele.addEventListener('pause',function(){
    play.setAttribute('data-state','paused');
  });
  ele.addEventListener('ended',function(){
    play.setAttribute('data-state','paused');
  });
  ele.addEventListener('volumechange',function(){
    var vol = 1 - Math.pow(1-ele.volume,2); //transform back from quadratic
    volume.style.height = vol*100+'%';
    if (ele.volume == 0) {
      speaker.setAttribute('data-muted','');
    }
    else {
      speaker.removeAttribute('data-muted','');
    }
  });
  if (options.live) {
    ele.addEventListener('playing',function(){
      var track = {
        firstms: 0,
        lastms: 0
      };
      for (var i in options.meta.tracks) {
        track = options.meta.tracks[i];
        break;
      }
      whileLivePlaying(track);
    });
  }
  else {
    ele.addEventListener('playing',function(){
      whilePlaying();
    });
    ele.addEventListener('durationchange',function(){
      timestampValue.nodeValue = formatTime(ele.duration);
    });
    ele.addEventListener('progress',function(){
      for (var i in buffers) {
        progress.removeChild(buffers[i]);
      }
      buffers = [];
      
      var start,end;
      for (var i = 0; i < ele.buffered.length; i++) {
        var b = document.createElement('div');
        b.className = 'buffer';
        progress.appendChild(b);
        buffers.push(b);
        start = ele.buffered.start(i);
        end = ele.buffered.end(i);
        b.setAttribute('style','left:'+(start/ele.duration*100)+'%; width:'+((end-start)/ele.duration*100 )+'%');
      }
    });
  }
  
  //hide the cursor and controls if it is over the video (not the controls) and hasn't moved for 5 secs
  var position = {
    x: 0,
    y: 0,
    lastpos: { x: 0, y: 0 },
    counter: 0,
    element: false,
    interval: setInterval(function(){
      if (position.element == 'video') {
        if ((position.x == position.lastpos.x) && (position.y == position.lastpos.y)) {
          position.counter++;
          if (position.counter >= 5) {
            position.element = false;
            ele.parentNode.setAttribute('data-hide','');
            return;
          }
        }
        position.lastpos.x = position.x;
        position.lastpos.y = position.y;
        return;
      }
      position.counter = 0;
    },1e3)
  };
  ele.addEventListener('mousemove',function(e){
    position.x = e.pageX;
    position.y = e.pageY;
    position.element = 'video';
    ele.parentNode.removeAttribute('data-hide');
  });
  controls.addEventListener('mousemove',function(e){
    position.element = 'controls';
    e.stopPropagation();
  });
  ele.addEventListener('mouseout',function(e){
    position.element = false;
  });
  
  return true;
}
MistPlayer.prototype.askNextCombo = function(msg){
  var me = this;
  if (me.errorstate) { return; }
  me.errorstate = true;
  me.addlog('Showing error window');
  
  //show the error
  var err = document.createElement('div');
  var msgnode = document.createTextNode(msg ? msg : 'Player or stream error detected');
  err.appendChild(msgnode);
  err.className = 'error';
  err.style.position = 'absolute';
  err.style.top = 0;
  err.style.width = '100%';
  err.style['margin-left'] = 0;
  this.target.appendChild(err);
  this.element.style.opacity = '0.2';
  
  //if there is a next source/player, show a button to activate it
  var opts = this.mistplaySettings.options;
  opts.startCombo = this.mistplaySettings.startCombo;
  if (mistCheck(mistvideo[this.mistplaySettings.streamname],opts)) {
    var button = document.createElement('button');
    var t = document.createTextNode('Try next source/player');
    button.appendChild(t);
    err.appendChild(button);
    button.onclick = function(){
      me.nextCombo();
    }
  }
  
  //show a button to reload with the current settings
  var button = document.createElement('button');
  var i = document.createElement('div'); //a css countdown clock for 10sec
  i.className = 'countdown10';
  button.appendChild(i);
  var t = document.createTextNode('Reload this player');
  button.appendChild(t);
  err.appendChild(button);
  button.onclick = function(){
    me.reload();
  }
  
  //after 10 seconds, reload the player
  err.timeOut = setTimeout(function(){
    button.click();
  },10e3);
  
};
MistPlayer.prototype.cancelAskNextCombo = function(){
  if (this.errorstate) {
    this.errorstate = false;
    this.addlog('Removing error window');
    this.element.style.opacity = 1;
    var err = this.target.querySelector('.error');
    if (err) {
      this.target.removeChild(err);
      if (err.timeOut) { clearTimeout(err.timeOut); }
    }
  }
};
MistPlayer.prototype.reload = function(){
  this.unload();
  mistPlay(this.mistplaySettings.streamname,this.mistplaySettings.options);
};
MistPlayer.prototype.nextCombo = function(){
  this.unload();
  var opts = this.mistplaySettings.options;
  opts.startCombo = this.mistplaySettings.startCombo;
  mistPlay(this.mistplaySettings.streamname,opts);
};
///send information back to mistserver
///\param msg object containing the information to report
MistPlayer.prototype.report = function(msg) {
  return false; ///\todo Remove this when the backend reporting function has been coded
  
  
  ///send a http post request
  ///\param url (string) url to send to
  ///\param params object containing post parameters
  function httpPost(url,params) {
    var http = new XMLHttpRequest();
    
    var postdata = [];
    for (var i in params) {
      postdata.push(i+'='+params[i]);
    }
    
    http.open("POST", url, true);
    http.send(postdata.join('&'));
  }
  
  //add some extra information
  msg.userinfo = {
    page: location.href,
    stream: this.streamname,
    session: mistplayer_session_id
  };
  if ('index' in this) { msg.userinfo.playerindex = this.index; }
  if ('playername' in this) { msg.userinfo.player = this.playername; }
  if ('options' in this) {
    if ('source' in this.options) {
      msg.userinfo.source = {
        src: this.options.source.url,
        type: this.options.source.type
      };
    }
    msg.userinfo.resolution = this.options.width+'x'+this.options.height;
    msg.userinfo.time = Math.round(((new Date) - this.options.initTime)/1e3); //seconds since the info js was loaded
  }
  
  try {
    httpPost(this.options.host+'/report',{
      report: JSON.stringify(msg)
    });
  }
  catch (e) { }
}
MistPlayer.prototype.unload = function(){
  if (('pause' in this) && (this.pause)) { this.pause(); }
  if ('updateSrc' in this) { this.updateSrc(''); }
  //delete this.element;
  this.target.innerHTML = '';
};

function mistCheck(streaminfo,options,embedLog) {
  if (typeof embedLog != 'function') { embedLog = function(){}; }
  
  embedLog('Checking available players..');
  
  var source = false;
  var mistPlayer = false;
  
  if (options.startCombo) {
    options.startCombo.started = {
      player: false,
      source: false
    };
  }
  
  function checkPlayer(p_shortname) {
    if ((options.startCombo) && (!options.startCombo.started.player)) {
      if (p_shortname != options.startCombo.player) { return false; }
      else {
        options.startCombo.started.player = true;
      }
    }
    
    embedLog('Checking '+mistplayers[p_shortname].name+' (priority: '+mistplayers[p_shortname].priority+') ..');
    
    //loop over the available sources and check if this player can play it
    var loop;
    if (options.forceSource) {
      loop = [streaminfo.source[options.forceSource]];
    }
    else {
      loop = streaminfo.source;
    }
    for (var s in loop) {
      if ((options.startCombo) && (!options.startCombo.started.source)) {
        if (s == options.startCombo.source) {
          options.startCombo.started.source = true;
        }
        continue;
      }
      if ((options.forceType) && (loop[s].type != options.forceType)) {
        continue;
      }
      
      if (mistplayers[p_shortname].isMimeSupported(loop[s].type)) {
        //this player supports this mime
        if (mistplayers[p_shortname].isBrowserSupported(loop[s].type,loop[s],options,streaminfo,embedLog)) {
          //this browser is supported
          embedLog('Found a working combo: '+mistplayers[p_shortname].name+' with '+loop[s].type+' @ '+loop[s].url);
          mistPlayer = p_shortname;
          source = loop[s];
          source.index = s;
          return p_shortname;
        }
        else {
          embedLog('This browser does not support '+loop[s].type);
        }
      }
    }
    
    return false;
  }
  
  if (options.forcePlayer) {
    checkPlayer(options.forcePlayer);
  }
  else {
    //sort the players
    var sorted = Object.keys(mistplayers);
    sorted.sort(function(a,b){
      return mistplayers[a].priority - mistplayers[b].priority;
    });
    for (var n in sorted) {
      var p_shortname = sorted[n];
      if (checkPlayer(p_shortname)) { break; }
    }
  }
  
  return ((source && mistPlayer) ? {
    source: source,
    mistPlayer: mistPlayer
  } : false);
}

/////////////////////////////////////////////////
// SELECT AND ADD A VIDEO PLAYER TO THE TARGET //
/////////////////////////////////////////////////

function mistPlay(streamName,options) {
  
  var protoplay = new MistPlayer();
  protoplay.streamname = streamName;
  var embedLog = function(msg) {
    protoplay.sendEvent('log',msg,options.target);
  };
  function mistError(msg) {
    var info = {};
    if ((typeof mistvideo != 'undefined') && (streamName in mistvideo)) { info = mistvideo[streamName]; }
    var displaymsg = msg;
    if ('on_error' in info) { displaymsg = info.on_error; }
    
    var err = document.createElement('div');
    err.innerHTML = displaymsg.replace(new RegExp("\n",'g'),'<br>')+'<br>';
    err.className = 'error';
    var button = document.createElement('button');
    var t = document.createTextNode('Reload');
    button.appendChild(t);
    err.appendChild(button);
    button.onclick = function(){
      options.target.removeChild(err);
      delete options.startCombo;
      mistPlay(streamName,options);
    }
    
    options.target.appendChild(err);
    
    protoplay.sendEvent('error',msg,options.target);
    
    return err;
  }
  
  //merge local and global options
  var local = options;
  var global = (typeof mistoptions == 'undefined' ? {} : mistoptions);
  var options = {
    host: null,         //override mistserver host (default is the host that player.js is loaded from)
    autoplay: true,     //start playing when loaded
    controls: true,     //show controls (MistControls when available)
    loop: false,        //don't loop when the stream has finished
    poster: null,       //don't show an image before the stream has started
    callback: false,    //don't call a function when the player has finished building
    streaminfo: false,   //don't use this streaminfo but collect it from the mistserverhost
    startCombo: false,
    forceType: false,
    forcePlayer: false,
    forceSource: false
  };
  for (var i in global) {
    options[i] = global[i];
  }
  for (var i in local) {
    options[i] = local[i];
  }
  
  if (!options.host) {
    mistError('MistServer host undefined.');
    return;
  }
  
  options.target.setAttribute('data-loading','');
  
  //check if the css is loaded
  if (!document.getElementById('mist_player_css')) {
    var css = document.createElement('link');
    css.rel = 'stylesheet';
    css.href = options.host+'/player.css';
    css.id = 'mist_player_css';
    //prepend it to the head: don't use append, because the customer might want to override our default css
    if (document.head.children.length) {
      document.head.insertBefore(css,document.head.firstChild);
    }
    else {
      document.head.appendChild(css);
    }
  }
  
  function onstreaminfo() {
    options.target.innerHTML = '';
    options.target.removeAttribute('data-loading');
    embedLog('Stream info was loaded succesfully');
    
    //get streaminfo data
    var streaminfo = mistvideo[streamName];
    //embedLog('Stream info contents: '+JSON.stringify(streaminfo));
    streaminfo.initTime = new Date();
    
    if (!('source' in streaminfo)) {
      mistError('Error while loading stream info.');
      protoplay.report({
        type: 'init',
        error: 'No sources'
      });
      return;
    }
    
    if (('forceType' in options) && (options.forceType)) {
      embedLog('Forcing '+options.forceType);
    }
    if (('forceSource' in options) && (options.forceSource)) {
      options.forceType = streaminfo.source[options.forceSource].type;
      embedLog('Forcing source '+options.forceSource+': '+options.forceType+' @ '+streaminfo.source[options.forceSource].url);
    }
    if (('forcePlayer' in options) && (options.forcePlayer)) {
      if (options.forcePlayer in mistplayers) {
        embedLog('Forcing '+mistplayers[options.forcePlayer].name);
      }
      else {
        embedLog('The forced player ('+options.forcePlayer+') isn\'t known, ignoring. Possible values are: '+Object.keys(mistplayers).join(', '));
        options.forcePlayer = false;
      }
    }
    if (('startCombo' in options) && (options.startCombo)) {
      embedLog('Selecting a new player/source combo, starting after '+mistplayers[options.startCombo.player].name+' with '+streaminfo.source[options.startCombo.source].type+' @ '+streaminfo.source[options.startCombo.source].url);
    }
    
    //sort the sources by simultracks, priority and mime, but prefer HTTPS
    streaminfo.source.sort(function(a,b){
      return (b.simul_tracks - a.simul_tracks) || (b.priority - a.priority) || a.type.localeCompare(b.type) || b.url.localeCompare(a.url);
    });
    
    var r = mistCheck(streaminfo,options,embedLog);
    var mistPlayer = r.mistPlayer;
    var source = r.source;
    
    options.target.innerHTML = '';
    if (mistPlayer) {
      
      //create the options to send to the player
      var playerOpts = {
        src: source.url+(('urlappend' in options) && (options.urlappend) ? options.urlappend : '' ),
        live: (streaminfo.type == 'live' ? true : false),
        initTime: streaminfo.initTime,
        meta: streaminfo.meta,
        source: source,
        host: options.host
      };
      //pass player options and handle defaults
      playerOpts.autoplay = options.autoplay;
      playerOpts.controls = options.controls;
      playerOpts.loop     = (playerOpts.live ? false : options.loop);
      playerOpts.poster   = options.poster;
      
      function calcSize() {
        //calculate desired width and height
        var fw = ('width' in options && options.width ? options.width  : false );    //force this width
        var fh = ('height' in options && options.height ? options.height  : false ); //force this height
        if (!(fw && fh)) {
          var ratio = streaminfo.width / streaminfo.height;
          if (fw || fh) {
            if (fw) {
              fh = fw/ratio;
            }
            else {
              fw = fh*ratio;
            }
          }
          else {
            //neither width or height are being forced. Set them to the minimum of video and target size
            var cw = ('maxwidth' in options && options.maxwidth ? options.maxwidth : options.target.clientWidth || window.innerWidth);
            var ch = ('maxheight' in options && options.maxheight ? options.maxheight : options.target.clientHeight || window.innerHeight);
            var fw = streaminfo.width;
            var fh = streaminfo.height;
            
            var factor; //resize factor
            if ((cw) && (fw > cw)) { //rescale if video width is larger than the target
              factor = fw / cw;
              fw /= factor;
              fh /= factor;
            }
            if ((ch) && (fh > ch)) { //rescale if video height is (still?) larger than the target
              factor = fh / ch;
              fw /= factor;
              fh /= factor;
            }
          }
        }
        return {
          width: fw,
          height: fh
        };
      }
      
      var lastsize = calcSize();
      playerOpts.width = lastsize.width;
      playerOpts.height = lastsize.height;
      
      //save the objects for future reference
      var player = new mistplayers[mistPlayer].player();
      player.playername = mistPlayer;
      player.target = options.target;
      if (!('embedded' in streaminfo)) { streaminfo.embedded = []; }
      streaminfo.embedded.push({
        options: options,
        selectedPlayer: mistPlayer,
        player: player,
        playerOptions: playerOpts
      });
      player.index = streaminfo.embedded.length-1;
      
      if (player.setTracks(false)) {
        //gather track info
        //tracks
        var tracks = {
          video: [],
          audio: [],
          subtitle: []
        };
        for (var i in streaminfo.meta.tracks) {
          var t = streaminfo.meta.tracks[i];
          var skip = false;
          switch (t.type) {
            case 'video':
              t.desc = ['['+t.codec+']',t.width+'x'+t.height,Math.round(t.bps/1024)+'kbps',t.fpks/1e3+'fps',t.lang];
              break;
            case 'audio':
              t.desc = ['['+t.codec+']',t.channels+' channels',Math.round(t.bps/1024)+'kbps',t.rate+'Hz',t.lang];
              break;
            case 'subtitle':
              t.desc = ['['+t.codec+']',t.lang];
              break;
            default:
              skip = true;
              break;
          }
          if (skip) { continue; }
          t.desc = t.desc.join(', ');
          tracks[t.type].push(t);
        }
        player.tracks = tracks;
        if (tracks.subtitle.length) {
          var vttsrc = false;
          player.subtitle = false;
          for (var i in streaminfo.source) {
            if (streaminfo.source[i].type == 'html5/text/vtt') {
              vttsrc = streaminfo.source[i].url;
              break;
            }
          }
          if (vttsrc) {
            player.subtitle = vttsrc.replace(/.srt$/,'.vtt');
          }
        }
        var usetracks = {};
        for (var i in tracks) {
          if (i == 'subtitle') { continue; }
          if (tracks[i].length) {
            tracks[i].sort(function(a,b){
              return a.trackid - b.trackid;
            });
            usetracks[i] = tracks[i][0].trackid;
          }
        }
      }
      
      //build the player
      player.mistplaySettings = {
        streamname: streamName,
        options: options,
        startCombo: {
          player: mistPlayer,
          source: source.index
        }
      };
      player.options = playerOpts;
      try {
        var element = player.build(playerOpts);
      }
      catch (e) {
        //show the next player/reload buttons if there is an error in the player build code
        options.target.appendChild(player.element);
        player.askNextCombo('Error while building player: '+e.stack);
        throw e;
        player.report({
          type: 'init',
          error: 'Error while building player'
        });
        return;
      }
      options.target.appendChild(element);
      element.setAttribute('data-player',mistPlayer);
      element.setAttribute('data-mime',source.type);
      player.report({
        type: 'init',
        info: 'Player built'
      });
      
      if (player.setTracks(false)) {
        player.onready(function(){
          //player.setTracks(usetracks);
          if ('setTracks' in options) { player.setTracks(options.setTracks); }
        });
      }
      
      //monitor for errors
      element.checkStalledTimeout = false;
      element.checkProgressTimeout = false;
      element.sendPingTimeout = setInterval(function(){
        if (player.paused) { return; }
        player.report({
          type: 'playback',
          info: 'ping'
        });
      },150e3);
      element.addEventListener('error',function(e){
        player.askNextCombo('The player has thrown an error');
        var r = {
          type: 'playback',
          error: 'The player has thrown an error',
          origin: e.target.outerHTML.slice(0,e.target.outerHTML.indexOf('>')+1),
        };
        if ('readyState' in player.element) {
          r.readyState = player.element.readyState;
        }
        if ('networkState' in player.element) {
          r.networkState = player.element.networkState;
        }
        if (('error' in player.element) && ('code' in player.element.error)) {
          r.code = player.element.error.code;
        }
        player.report(r);
      });
      var stalled = function(e){
        if (element.checkStalledTimeout) { return; }
        element.checkStalledTimeout = setTimeout(function(){
          if (player.paused) { return; }
          player.askNextCombo('Playback has stalled');
          player.report({
            'type': 'playback',
            'warn': 'Playback was stalled for > 8 sec'
          });
        },10e3);
      };
      element.addEventListener('stalled',stalled,true);
      element.addEventListener('waiting',stalled,true);
      var progress = function(e){
        if (element.checkStalledTimeout) {
          clearTimeout(element.checkStalledTimeout);
          element.checkStalledTimeout = false;
          player.cancelAskNextCombo();
        }
        if (element.checkStalledTimeout) {
          clearTimeout(element.checkStalledTimeout);
          element.checkStalledTimeout = false;
          player.cancelAskNextCombo();
        }
      };
      element.addEventListener('progress',progress,true);
      element.addEventListener('playing',progress,true);
      element.addEventListener('play',function(){
        player.paused = false;
        if ((!element.checkProgressTimeout) && (player.element) && ('currentTime' in player.element)) {
          //check if the progress made is equal to the time spent
          var lasttime = player.element.currentTime;
          element.checkProgressTimeout = setInterval(function(){
            var newtime = player.element.currentTime;
            if (newtime == 0) { return; }
            var progress = newtime - lasttime;
            lasttime = newtime;
            if (progress < 0) { return; }
            if (progress == 0) {
              var msg = 'There should be playback but nothing was played';
              var r = {
                type: 'playback',
                warn: msg
              };
              player.addlog(msg);
              if ('readyState' in player.element) {
                r.readyState = player.element.readyState;
              }
              if ('networkState' in player.element) {
                r.networkState = player.element.networkState;
              }
              if (('error' in player.element) && (player.element.error) && ('code' in player.element.error)) {
                r.code = player.element.error.code;
              }
              player.report(r);
              player.askNextCombo('No playback');
              if ('load' in player.element) { player.element.load(); }
              return;
            }
            player.cancelAskNextCombo();
            if (progress < 4) {
              var msg = 'It seems playback is lagging (progressed '+Math.round(progress*100)/100+'/8s)'
              player.addlog(msg);
              player.report({
                type: 'playback',
                warn: msg
              });
              return;
            }
          },8e3);
        }
      },true);
      element.addEventListener('pause',function(){
        player.paused = true;
        if (element.checkProgressTimeout) {
          clearInterval(element.checkProgressTimeout);
          element.checkProgressTimeout = false;
        }
      },true);
      
      if (player.resize) {
        //monitor for resizes and fire if needed 
        window.addEventListener('resize',function(){
          player.resize(calcSize());
        });
      }
      
      for (var i in player.onreadylist) {
        player.onreadylist[i]();
      }
      
      protoplay.sendEvent('initialized','',options.target);
      if (options.callback) { options.callback(player); }
    }
    else {
      if (streaminfo.error) {
        var str = streaminfo.error;
      }
      else if (('source' in streaminfo) && (streaminfo.source.length)) {
        var str = 'Could not find a compatible player and protocol combination for this stream and browser. ';
        if (options.forceType) { str += "\n"+'The mimetype '+options.forceType+' was enforced. '; }
        if (options.forcePlayer) { str += "\n"+'The player '+options.forcePlayer+' was enforced. '; }
      }
      else {
        var str = 'Stream not found.';
      }
      protoplay.report({
        type: 'init',
        error: str
      });
      mistError(str);
    }
  }
  if ((options.streaminfo) && (typeof options.streaminfo == 'object') && ('type' in options.streaminfo)
    && ('source' in options.streaminfo) && (options.streaminfo.source.length)
    && ('meta' in options.streaminfo) && ('tracks' in options.streaminfo.meta)) { //catch some of the most problematic stuff
    if (typeof mistvideo == 'undefined') { mistvideo = {}; }
    mistvideo[streamName] = options.streaminfo;
    onstreaminfo();
  }
  else {
    //get info js
    var info = document.createElement('script');
    info.src = options.host+'/info_'+encodeURIComponent(streamName)+'.js';
    embedLog('Retrieving stream info from '+info.src);
    document.head.appendChild(info);
    info.onerror = function(){
      options.target.innerHTML = '';
      options.target.removeAttribute('data-loading');
      mistError('Error while loading stream info.');
      protoplay.report({
        type: 'init',
        error: 'Failed to load '+info.src
      });
    }
    info.onload = function(){
      //clean up info script
      document.head.removeChild(info);
      
      onstreaminfo();
    }
  }
}
