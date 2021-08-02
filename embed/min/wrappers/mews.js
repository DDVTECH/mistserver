mistplayers.mews={name:"MSE websocket player",mimes:["ws/video/mp4","ws/video/webm"],priority:MistUtil.object.keys(mistplayers).length+1,isMimeSupported:function(e){return this.mimes.indexOf(e)==-1?false:true},isBrowserSupported:function(e,t,i){if(!("WebSocket"in window)||!("MediaSource"in window)){return false}if(location.protocol.replace(/^http/,"ws")!=MistUtil.http.url.split(t.url.replace(/^http/,"ws")).protocol){i.log("HTTP/HTTPS mismatch for this source");return false}if(navigator.platform.toUpperCase().indexOf("MAC")>=0){return false}function s(e){function t(t){return("0"+e.init.charCodeAt(t).toString(16)).slice(-2)}switch(e.codec){case"AAC":return"mp4a.40.2";case"MP3":return"mp4a.40.34";case"AC3":return"ec-3";case"H264":return"avc1."+t(1)+t(2)+t(3);case"HEVC":return"hev1."+t(1)+t(6)+t(7)+t(8)+t(9)+t(10)+t(11)+t(12);default:return e.codec.toLowerCase()}}var n={};for(var r in i.info.meta.tracks){if(i.info.meta.tracks[r].type!="meta"){n[s(i.info.meta.tracks[r])]=i.info.meta.tracks[r].codec}}var a=e.split("/")[2];function o(e){return MediaSource.isTypeSupported("video/"+a+';codecs="'+e+'"')}t.supportedCodecs=[];for(var r in n){var u=o(r);if(u){t.supportedCodecs.push(n[r])}}if(!i.options.forceType&&!i.options.forcePlayer){if(t.supportedCodecs.length<t.simul_tracks){i.log("Not enough playable tracks for this source");return false}}return t.supportedCodecs.length>0},player:function(){}};var p=mistplayers.mews.player;p.prototype=new MistPlayer;p.prototype.build=function(e,t){var i=document.createElement("video");i.setAttribute("playsinline","");var s=["autoplay","loop","poster"];for(var n in s){var r=s[n];if(e.options[r]){i.setAttribute(r,e.options[r]===true?"":e.options[r])}}if(e.options.muted){i.muted=true}if(e.info.type=="live"){i.loop=false}if(e.options.controls=="stock"){i.setAttribute("controls","")}i.setAttribute("crossorigin","anonymous");this.setSize=function(e){i.style.width=e.width+"px";i.style.height=e.height+"px"};var a=this;function o(){if(a.ws.readyState==a.ws.OPEN&&a.ms.readyState=="open"&&a.sb){t(i);if(e.options.autoplay){a.api.play()}return true}}this.msinit=function(){return new Promise(function(e,t){a.ms=new MediaSource;i.src=URL.createObjectURL(a.ms);a.ms.onsourceopen=function(){e()};a.ms.onsourceclose=function(e){console.error("ms close",e);u({type:"stop"})};a.ms.onsourceended=function(e){console.error("ms ended",e);function t(e,t,s){var n,r;n=new Blob([e],{type:s});r=window.URL.createObjectURL(n);i(r,t);setTimeout(function(){return window.URL.revokeObjectURL(r)},1e3)}function i(e,t){var i;i=document.createElement("a");i.href=e;i.download=t;document.body.appendChild(i);i.style="display: none";i.click();i.remove()}if(a.debugging){var s=0;for(var n=0;n<a.sb.appended.length;n++){s+=a.sb.appended[n].length}var r=new Uint8Array(s);var s=0;for(var n=0;n<a.sb.appended.length;n++){r.set(a.sb.appended[n],s);s+=a.sb.appended[n].length}t(r,"appended.mp4.bin","application/octet-stream")}u({type:"stop"})}})};this.msinit().then(function(){if(a.sb){e.log("Not creating source buffer as one already exists.");return}o()});this.onsbinit=[];this.sbinit=function(t){if(!t){e.showError("Did not receive any codec: nothing to initialize.");return}a.sb=a.ms.addSourceBuffer("video/"+e.source.type.split("/")[2]+';codecs="'+t.join(",")+'"');a.sb.mode="segments";a.sb._codecs=t;a.sb._duration=1;a.sb._size=0;a.sb.queue=[];var s=[];a.sb.do_on_updateend=s;a.sb.appending=null;a.sb.appended=[];var n=0;a.sb.addEventListener("updateend",function(){if(!a.sb){e.log("Reached updateend but the source buffer is "+JSON.stringify(a.sb)+". ");return}if(a.debugging){if(a.sb.appending)a.sb.appended.push(a.sb.appending);a.sb.appending=null}if(n>=500){n=0;a.sb._clean(10)}else{n++}var t=s.slice();s=[];for(var r in t){if(!a.sb){if(a.debugging){console.warn("I was doing on_updateend but the sb was reset")}break}if(a.sb.updating){s.concat(t.slice(r));if(a.debugging){console.warn("I was doing on_updateend but was interrupted")}break}t[r](r<t.length-1?t.slice(r):[])}if(!a.sb){return}a.sb._busy=false;if(a.sb&&a.sb.queue.length>0&&!a.sb.updating&&!i.error){a.sb._append(this.queue.shift())}});a.sb.error=function(e){console.error("sb error",e)};a.sb.abort=function(e){console.error("sb abort",e)};a.sb._doNext=function(e){s.push(e)};a.sb._do=function(e){if(this.updating||this._busy){this._doNext(e)}else{e()}};a.sb._append=function(e){if(!e){return}if(!e.buffer){return}if(a.debugging){a.sb.appending=new Uint8Array(e)}if(a.sb._busy){if(a.debugging)console.warn("I wanted to append data, but now I won't because the thingy was still busy. Putting it back in the queue.");a.sb.queue.unshift(e);return}a.sb._busy=true;a.sb.appendBuffer(e)};if(a.msgqueue){if(a.msgqueue[0]){var r=false;if(a.msgqueue[0].length){for(var o in a.msgqueue[0]){if(a.sb.updating||a.sb.queue.length||a.sb._busy){a.sb.queue.push(a.msgqueue[0][o])}else{a.sb._append(a.msgqueue[0][o])}}}else{r=true}a.msgqueue.shift();if(a.msgqueue.length==0){a.msgqueue=false}e.log("The newly initialized source buffer was filled with data from a seperate message queue."+(a.msgqueue?" "+a.msgqueue.length+" more message queue(s) remain.":""));if(r){e.log("The seperate message queue was empty; manually triggering any onupdateend functions");a.sb.dispatchEvent(new Event("updateend"))}}}a.sb._clean=function(e){if(!e)e=180;if(i.currentTime>e){a.sb._do(function(){a.sb.remove(0,Math.max(.1,i.currentTime-e))})}};if(a.onsbinit.length){a.onsbinit.shift()()}};this.wsconnect=function(){return new Promise(function(t,s){this.ws=new WebSocket(e.source.url);this.ws.binaryType="arraybuffer";this.ws.s=this.ws.send;this.ws.send=function(){if(this.readyState==1){return this.s.apply(this,arguments)}return false};this.ws.onopen=function(){this.wasConnected=true;t()};this.ws.onerror=function(t){e.showError("MP4 over WS: websocket error")};this.ws.onclose=function(t){e.log("MP4 over WS: websocket closed");if(this.wasConnected&&!e.destroyed){e.log("MP4 over WS: reopening websocket");a.wsconnect().then(function(){if(!a.sb){var t=function(e){if(!a.sb){a.sbinit(e.data.codecs)}else{a.api.play()}a.ws.removeListener("codec_data",t)};a.ws.addListener("codec_data",t);u({type:"request_codec_data",supported_codecs:e.source.supportedCodecs})}else{a.api.play()}},function(){Mistvideo.error("Lost connection to the Media Server")})}};this.ws.listeners={};this.ws.addListener=function(e,t){if(!(e in this.listeners)){this.listeners[e]=[]}this.listeners[e].push(t)};this.ws.removeListener=function(e,t){if(!(e in this.listeners)){return}var i=this.listeners[e].indexOf(t);if(i<0){return}this.listeners[e].splice(i,1);return true};a.msgqueue=false;var n=1;var r=[];this.ws.onmessage=function(t){if(!t.data){throw"Received invalid data"}if(typeof t.data=="string"){var s=JSON.parse(t.data);if(a.debugging&&s.type!="on_time"){console.log("ws message",s)}switch(s.type){case"on_stop":{var r;r=MistUtil.event.addListener(i,"waiting",function(e){a.sb.paused=true;MistUtil.event.send("ended",null,i);MistUtil.event.removeListener(r)});break}case"on_time":{var o=s.data.current-i.currentTime*1e3;var d=a.ws.serverDelay.get();var f=Math.max(500+d,d*2);if(e.info.type!="live"){f+=2e3}if(a.sb&&a.sb.keyonly!=s.data.keyonly){a.sb.keyonly=s.data.keyonly;MistUtil.event.send("slideshowchange",null,i)}if(a.debugging)console.log("on_time received",s.data.current/1e3,"currtime",i.currentTime,n+"x","buffer",Math.round(o),"/",Math.round(f),e.info.type=="live"?"latency:"+Math.round(s.data.end-i.currentTime*1e3)+"ms":"","bitrate:"+MistUtil.format.bits(a.monitor.currentBps)+"/s","listeners",a.ws.listeners&&a.ws.listeners.on_time?a.ws.listeners.on_time:0,"msgqueue",a.msgqueue?a.msgqueue.length:0,"readyState",e.video.readyState,s.data);if(!a.sb){e.log("Received on_time, but the source buffer is being cleared right now. Ignoring.");break}if(a.sb._duration!=s.data.end*.001){a.sb._duration=s.data.end*.001;MistUtil.event.send("durationchange",null,e.video)}e.info.meta.buffer_window=s.data.end-s.data.begin;a.sb.paused=false;if(!s.data.keyonly){if(e.info.type=="live"){if(n==1){if(s.data.play_rate_curr=="auto"){if(i.currentTime>0){if(o-f>f){n=1.1+Math.min(1,(o-f)/f)*.15;i.playbackRate*=n;e.log("Our buffer is big, so increase the playback speed to catch up.")}else if(o<f/2){n=.9;i.playbackRate*=n;e.log("Our buffer is small, so decrease the playback speed to catch up.")}}}}else if(n>1){if(o<f){i.playbackRate/=n;n=1;e.log("Our buffer is small enough, so return to real time playback.")}}else{if(o>f){i.playbackRate/=n;n=1;e.log("Our buffer is big enough, so return to real time playback.")}}}else{if(n==1){if(s.data.play_rate_curr=="auto"){if(o<f/2){if(o<-1e4){u({type:"seek",seek_time:i.currentTime*1e3})}else{n=2;e.log("Our buffer is negative, so request a faster download rate.");u({type:"set_speed",play_rate:n})}}else if(o-f>f){e.log("Our buffer is big, so request a slower download rate.");n=.5;u({type:"set_speed",play_rate:n})}}}else if(n>1){if(o>f){u({type:"set_speed",play_rate:"auto"});n=1;e.log("The buffer is big enough, so ask for realtime download rate.")}}else{if(o<f){u({type:"set_speed",play_rate:"auto"});n=1;e.log("The buffer is small enough, so ask for realtime download rate.")}}}}if(e.reporting&&s.data.tracks){e.reporting.stats.d.tracks=s.data.tracks.join(",")}break}case"tracks":{e.player.api.slideshow_available=!!s.data.keyonly_supported;MistUtil.event.send("slideshowavailable",!!s.data.keyonly_supported,i);function c(e,t){if(!t){return false}if(e.length!=t.length){return false}for(var i in e){if(t.indexOf(e[i])<0){return false}}return true}if(c(a.last_codecs?a.last_codecs:a.sb._codecs,s.data.codecs)){if(a.debugging)console.log("reached switching point");if(s.data.current>0){if(a.sb){a.sb._do(function(){a.sb.remove(0,s.data.current*.001)})}}e.log("Player switched tracks, keeping source buffer as codecs are the same as before.")}else{if(a.debugging){console.warn("Different codecs!");console.warn("video time",i.currentTime,"waiting until",s.data.current*.001)}a.last_codecs=s.data.codecs;if(a.msgqueue){a.msgqueue.push([])}else{a.msgqueue=[[]]}var l=function(){if(a&&a.sb){a.sb._do(function(t){if(!a.sb.updating){if(!isNaN(a.ms.duration))a.sb.remove(0,Infinity);a.sb.queue=[];a.ms.removeSourceBuffer(a.sb);a.sb=null;var n=(s.data.current*.001).toFixed(3);i.src="";a.ms.onsourceclose=null;a.ms.onsourceended=null;if(a.debugging&&t&&t.length){console.warn("There are do_on_updateend functions queued, which I *should* re-apply after clearing the sb.")}a.msinit().then(function(){a.sbinit(s.data.codecs);a.sb.do_on_updateend=t;var r=MistUtil.event.addListener(i,"loadedmetadata",function(){e.log("Buffer cleared, setting playback position to "+MistUtil.format.time(n,{ms:true}));var t=function(){i.currentTime=n;if(i.currentTime<n){a.sb._doNext(t);if(a.debugging){console.log("Could not set playback position")}}else{if(a.debugging){console.log("Set playback position to "+MistUtil.format.time(n,{ms:true}))}var e=function(){a.sb._doNext(function(){if(i.buffered.length){if(a.debugging){console.log(i.buffered.start(0),i.buffered.end(0),i.currentTime)}if(i.buffered.start(0)>i.currentTime){var t=i.buffered.start(0);i.currentTime=t;if(i.currentTime!=t){e()}}}else{e()}})};e()}};t();MistUtil.event.removeListener(r)})})}else{l()}})}else{if(a.debugging){console.warn("sb not available to do clear")}a.onsbinit.push(l)}};if(!s.data.codecs||!s.data.codecs.length){e.showError("Track switch does not contain any codecs, aborting.");e.options.setTracks=false;l();break}if(a.debugging){console.warn("reached switching point",s.data.current*.001,MistUtil.format.time(s.data.current*.001))}l()}}}if(s.type in this.listeners){for(var p=this.listeners[s.type].length-1;p>=0;p--){this.listeners[s.type][p](s)}}return}var b=new Uint8Array(t.data);if(b){for(var p in a.monitor.bitCounter){a.monitor.bitCounter[p]+=t.data.byteLength*8}if(a.sb&&!a.msgqueue){if(a.sb.updating||a.sb.queue.length||a.sb._busy){a.sb.queue.push(b)}else{a.sb._append(b)}}else{if(!a.msgqueue){a.msgqueue=[[]]}a.msgqueue[a.msgqueue.length-1].push(b)}}else{e.log("Expecting data from websocket, but received none?!")}};this.ws.serverDelay={delays:[],log:function(e){var t=false;switch(e){case"seek":case"set_speed":{t=e;break}case"request_codec_data":{t="codec_data";break}default:{return}}if(t){var i=(new Date).getTime();function s(){a.ws.serverDelay.add((new Date).getTime()-i);a.ws.removeListener(t,s)}a.ws.addListener(t,s)}},add:function(e){this.delays.unshift(e);if(this.delays.length>5){this.delays.splice(5)}},get:function(){if(this.delays.length){let e=0;let t=0;for(null;t<this.delays.length;t++){if(t>=3){break}e+=this.delays[t]}return e/t}return 500}}}.bind(this))};this.wsconnect().then(function(){var t=function(s){a.sbinit(s.data.codecs);e.player.api.slideshow_available=!!s.data.keyonly_supported;MistUtil.event.send("slideshowavailable",!!s.data.keyonly_supported,i);o();a.ws.removeListener("codec_data",t)};this.ws.addListener("codec_data",t);u({type:"request_codec_data",supported_codecs:e.source.supportedCodecs})}.bind(this));function u(e){if(!a.ws){throw"No websocket to send to"}if(a.ws.readyState>=a.ws.CLOSING){a.wsconnect().then(function(){u(e)});return}if(a.debugging){console.log("ws send",e)}a.ws.serverDelay.log(e.type);a.ws.send(JSON.stringify(e))}a.findBuffer=function(e){var t=false;for(var s=0;s<i.buffered.length;s++){if(i.buffered.start(s)<=e&&i.buffered.end(s)>=e){t=s;break}}return t};this.api={play:function(t){return new Promise(function(s,n){var r=function(o){if(!a.sb){e.log("Attempting to play, but the source buffer is being cleared. Waiting for next on_time.");return}if(e.info.type=="live"){if(t||i.currentTime==0){var u=function(){if(i.buffered.length){var t=a.findBuffer(o.data.current*.001);if(t!==false){if(i.buffered.start(t)>i.currentTime||i.buffered.end(t)<i.currentTime){i.currentTime=o.data.current*.001;e.log("Setting live playback position to "+MistUtil.format.time(i.currentTime))}i.play().then(s).catch(n);a.sb.paused=false;a.sb.removeEventListener("updateend",u)}}};a.sb.addEventListener("updateend",u)}else{a.sb.paused=false;i.play().then(s).catch(n)}a.ws.removeListener("on_time",r)}else if(o.data.current>i.currentTime){a.sb.paused=false;i.currentTime=o.data.current*.001;i.play().then(s).catch(n);a.ws.removeListener("on_time",r)}};a.ws.addListener("on_time",r);var o={type:"play"};if(t){o.seek_time="live"}u(o)})},pause:function(){i.pause();u({type:"hold"});if(a.sb){a.sb.paused=true}},setTracks:function(e){e.type="tracks";e=MistUtil.object.extend({type:"tracks",seek_time:Math.max(0,i.currentTime*1e3-(500+a.ws.serverDelay.get()))},e);u(e)},unload:function(){a.api.pause();a.sb._do(function(){a.sb.remove(0,Infinity);try{a.ms.endOfStream()}catch(e){}});a.ws.close()}};if(MistUtil.getBrowser()!="firefox"){this.api.slideshow=function(e){if(typeof e=="undefined"){return a&&a.sb?a.sb.keyonly?a.sb.keyonly:false:null}e=e?true:false;u({type:"tracks",keyonly:e});return e};this.api.slideshow_available=false}Object.defineProperty(this.api,"currentTime",{get:function(){return i.currentTime},set:function(t){MistUtil.event.send("seeking",t,i);u({type:"seek",seek_time:Math.max(0,t*1e3-(250+a.ws.serverDelay.get()))});var s=function(e){a.ws.removeListener("seek",s);var n=function(e){a.ws.removeListener("on_time",n);t=(e.data.current*.001).toFixed(3);var s=function(){i.currentTime=t;if(i.currentTime!=t){if(a.debugging)console.log("Failed to set video.currentTime, wanted:",t,"got:",i.currentTime);a.sb._doNext(s)}};s()};a.ws.addListener("on_time",n)};a.ws.addListener("seek",s);i.currentTime=t;e.log("Seeking to "+MistUtil.format.time(t,{ms:true})+" ("+t+")")}});Object.defineProperty(this.api,"duration",{get:function(){return a.sb?a.sb._duration:1}});Object.defineProperty(this.api,"playbackRate",{get:function(){return i.playbackRate},set:function(e){var t=function(e){i.playbackRate=e.data.play_rate};a.ws.addListener("set_speed",t);u({type:"set_speed",play_rate:e==1?"auto":e})}});function d(e){Object.defineProperty(a.api,e,{get:function(){return i[e]},set:function(t){return i[e]=t}})}var f=["volume","buffered","muted","loop","paused",,"error","textTracks","webkitDroppedFrameCount","webkitDecodedFrameCount"];for(var n in f){d(f[n])}MistUtil.event.addListener(i,"ended",function(){if(a.api.loop){a.api.currentTime=0;a.sb._do(function(){a.sb.remove(0,Infinity)})}});var c=false;MistUtil.event.addListener(i,"seeking",function(){c=true;var e=MistUtil.event.addListener(i,"seeked",function(){c=false;MistUtil.event.removeListener(e)})});MistUtil.event.addListener(i,"waiting",function(){if(c){return}var t=a.findBuffer(i.currentTime);if(t!==false){if(t+1<i.buffered.length&&i.buffered.start(t+1)-i.currentTime<1e4){e.log("Skipped over buffer gap (from "+MistUtil.format.time(i.currentTime)+" to "+MistUtil.format.time(i.buffered.start(t+1))+")");i.currentTime=i.buffered.start(t+1)}}});MistUtil.event.addListener(i,"pause",function(){if(a.sb&&!a.sb.paused){e.log("The browser paused the vid - probably because it has no audio and the tab is no longer visible. Pausing download.");u({type:"hold"});a.sb.paused=true;var t=MistUtil.event.addListener(i,"play",function(){if(a.sb&&a.sb.paused){u({type:"play"})}MistUtil.event.removeListener(t)})}});if(a.debugging){MistUtil.event.addListener(i,"waiting",function(){var e=[];var t=false;for(var s=0;s<i.buffered.length;s++){if(i.currentTime>=i.buffered.start(s)&&i.currentTime<=i.buffered.end(s)){t=true}e.push([i.buffered.start(s),i.buffered.end(s)])}console.log("waiting","currentTime",i.currentTime,"buffers",e,t?"contained":"outside of buffer","readystate",i.readyState,"networkstate",i.networkState);if(i.readyState>=2&&i.networkState>=2){console.error("Why am I waiting?!")}})}this.monitor={bitCounter:[],bitsSince:[],currentBps:null,nWaiting:0,nWaitingThreshold:3,listener:MistUtil.event.addListener(i,"waiting",function(){a.monitor.nWaiting++;if(a.monitor.nWaiting>=a.monitor.nWaitingThreshold){a.monitor.nWaiting=0;e.log("ABR threshold triggered, requesting lower quality");a.monitor.action()}}),getBitRate:function(){if(a.sb&&!a.sb.paused){this.bitCounter.push(0);this.bitsSince.push((new Date).getTime());var t,i;if(this.bitCounter.length>5){t=a.monitor.bitCounter.shift();i=this.bitsSince.shift()}else{t=a.monitor.bitCounter[0];i=this.bitsSince[0]}var s=(new Date).getTime()-i;this.currentBps=t/(s*.001)}e.timers.start(function(){a.monitor.getBitRate()},500)},action:function(){a.api.setTracks({video:"max<"+Math.round(this.currentBps)+"bps"})}};this.monitor.getBitRate()};