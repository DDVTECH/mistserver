mistplayers.mews = {
  name: "MSE websocket player",
  mimes: ["ws/video/mp4","ws/video/webm"],
  priority: MistUtil.object.keys(mistplayers).length + 1,
  isMimeSupported: function (mimetype) {
    return (this.mimes.indexOf(mimetype) == -1 ? false : true);
  },
  isBrowserSupported: function (mimetype,source,MistVideo) {
    
    if ((!("WebSocket" in window)) || (!("MediaSource" in window)) || (!("Promise" in window))) { return false; }

    if (MistVideo.info.capa && !MistVideo.info.capa.ssl) {
      MistVideo.log("This player requires websocket support");
      return false;
    }
    
    //check for http/https mismatch
    if (location.protocol.replace(/^http/,"ws") != MistUtil.http.url.split(source.url.replace(/^http/,"ws")).protocol) {
      MistVideo.log("HTTP/HTTPS mismatch for this source");
      return false;
    }
    
    //it runs on MacOS, but breaks often on seek/track switch etc
    if (navigator.platform.toUpperCase().indexOf('MAC') >= 0) {
      return false;
    }

    //check (and save) codec compatibility
    function translateCodec(track) {
      if (track.codecstring){return track.codecstring;}
      function bin2hex(index) {
        return ("0"+track.init.charCodeAt(index).toString(16)).slice(-2);
      }
      switch (track.codec) {
        case "AAC":
          return "mp4a.40.2";
        case "MP3":
          return "mp4a.40.34";
        case "AC3":
          return "ec-3";
        case "H264":
          return "avc1."+bin2hex(1)+bin2hex(2)+bin2hex(3);
        case "HEVC":
          return "hev1."+bin2hex(1)+bin2hex(6)+bin2hex(7)+bin2hex(8)+bin2hex(9)+bin2hex(10)+bin2hex(11)+bin2hex(12);
        default:
          return track.codec.toLowerCase();
      }
      
    }
    var codecs = {};
    var playabletracks = {};
    var hassubtitles = false;
    for (var i in MistVideo.info.meta.tracks) {
      if (MistVideo.info.meta.tracks[i].type != "meta") {
        /*if (MistVideo.info.meta.tracks[i].codec == "HEVC") {
          //the iPad claims to be able to play MP4/WS H265 tracks.. haha no.
          continue;
        }*/
        codecs[translateCodec(MistVideo.info.meta.tracks[i])] = MistVideo.info.meta.tracks[i];
      }
      else if (MistVideo.info.meta.tracks[i].codec == "subtitle") { hassubtitles = true; }
    }
    var container = mimetype.split("/")[2];
    function test(codecs) {
      //if (container == "webm") { return true; }
      return MediaSource.isTypeSupported("video/"+container+";codecs=\""+codecs+"\"");
    }
    source.supportedCodecs = [];
    for (var i in codecs) {
      //i is the long name (like mp4a.40.2), codecs[i] is the track meta, codecs[i].codec is the short name (like AAC)
      var s = test(i);
      if (s) {
        source.supportedCodecs.push(codecs[i].codec);
        playabletracks[codecs[i].type] = 1;
      }
    }

    if (hassubtitles) {
      //there is a subtitle track, check if there is a webvtt source
      for (var i in MistVideo.info.source) {
        if (MistVideo.info.source[i].type == "html5/text/vtt") {
          playabletracks.subtitle = 1;
          break;
        }
      }
    }

    return MistUtil.object.keys(playabletracks);
  },
  player: function(){}
};
var p = mistplayers.mews.player;
p.prototype = new MistPlayer();
p.prototype.build = function (MistVideo,callback) {
  
  var video = document.createElement("video");
  video.setAttribute("playsinline",""); //iphones. effin' iphones.
  
  var main = this;
  //this.debugging = true;
  //this.debugging = "dl"; //download appended data on ms close
  
  function WSMP4(){
    var controller = this;
    
    this.control = false;
    this.ms = false;
    Object.defineProperty(this,"sb",{
      get: function(){
        return this.ms ? this.ms.sb : false;
      }
    });
    this.queues = false;
    this.bm = false;

    this.createControlChannel = function(){
      controller.control = new MistUtil.shared.ControlChannel(function(){
        var ws = new WebSocket(MistVideo.source.url);
        ws.binaryType = "arraybuffer";
        return ws;
      },MistVideo,controller.onmessage);
      controller.connection = controller.control; //the control channel is also the data connection
      controller.control.lock();
      Object.defineProperty(controller.control,"debugging",{
        get: function(){
          return main.debugging; 
        }
      });
      controller.control.addListener("channel_error").then(function(){
        MistVideo.showError("MP4 over WS: websocket error");
      });
      controller.control.addListener("channel_close").then(function(){
        MistVideo.log("MP4 over WS: websocket closed");
        if (
          controller.control.was_connected && (controller.control.readyState != "closed")
          && (!MistVideo.destroyed)
          && (!controller.sb || !controller.sb.paused)
          && (MistVideo.state == "Stream is online")
          && (!(MistVideo.video && MistVideo.video.error))
        ) {
          MistVideo.log("MP4 over WS: reopening websocket");
          controller.control.reconnect();
        }
      });
      controller.control.addListener("channel_timeout").then(function(){
        MistVideo.log("MP4 over WS: socket timeout - try next combo");
        MistVideo.nextCombo();
        controller.connecting = false;
        reject();
      });

      seeking = false;
      controller.control.addSendListener("seek",function(msg){
        seeking = true;
        var value =  msg.seek_time == "live" ? "live" : msg.seek_time*1e-3;

        if (value != "live") video.currentTime = value; //put cursor at position already
        if (main.debugging) console.warn("[Seeking]","[1/5]",MistUtil.format.time(value,{ms:true}),"Command sent");
        controller.control.addListener("seek").then(function(msg){
          //the message "seek" was received
          if (main.debugging) console.warn("[Seeking]","[2/5]",MistUtil.format.time(value,{ms:true}),"Seek reply received");

          //clear the old buffer to prevent confusion / jumping
          controller.sb.do(function(){
            controller.sb.sb.remove(0,Infinity);
          });

          return controller.control.addListener("on_time");
        }).then(function(msg){
          //the next "on_time" message was received - MistServer is sending the new data
          if (main.debugging) console.warn("[Seeking]","[3/5]",MistUtil.format.time(value,{ms:true}),"First on_time received",{ begin: MistUtil.format.time(msg.begin*1e-3), current: MistUtil.format.time(msg.current*1e-3) });
          if ((value != "live") && (msg.begin > value*1e3)) {
            value = msg.begin; //the seek target is before the server buffer, update to start of buffer
            if (main.debugging) console.warn("[Seeking]","[-]","Updated seek target to",MistUtil.format.time(value,{ms:true}),"because it is before the server buffer")
          }
          return new Promise(function(resolve,reject){
            var seekprom;
            var evtl = MistUtil.event.addListener(video,"progress",function(){
              if (main.debugging == "verbose") console.log("progress","target",value,"buffers:",function(b){
                var out = [];
                for (var i = 0; i < b.length; i++) {
                  out.push([b.start(i),b.end(i)]);
                }
                return out;
              }(video.buffered));

              if (value == "live") {
                if (video.buffered.length) {
                  value = video.buffered.start(video.buffered.length-1);
                  if (main.debugging) console.warn("[Seeking]","[-]","Updated seek target to",MistUtil.format.time(value,{ms:true}));
                  resolve(video.buffered.length-1);
                }
              }

              //check if the target is buffered
              var buffern = main.findBuffer(value);
              if (buffern !== false) {
                MistUtil.event.removeListener(evtl);
                resolve(buffern);
                seekprom.removeListener();
              }
            });
            seekprom = main.controller.control.addListener("seek");
            seekprom.then(function(){
              //another seek is activated - cancel this seek
              MistUtil.event.removeListener(evtl);
              reject("Cancelling old seek to "+MistUtil.format.time(value,{ms:true})+" because a new seek was requested");
            }).catch(function(){});
          });
        }).then(function(buffern){
          seeking = false;
          if (value) video.currentTime = value; //make sure we're where we wanted to be
          MistUtil.event.send("seeked",null,video);
          if (main.debugging) console.warn("[Seeking]","[4/5]",MistUtil.format.time(value,{ms:true}),"Target time in buffer.","Buffer size:",buffern !== false ? Math.round((video.buffered.end(buffern) - value)*1e3)+"ms" : "N/A");

          return video.play();
        }).then(function(){
          if (main.debugging) console.warn("[Seeking]","[5/5]",MistUtil.format.time(value,{ms:true}),"Video playing");
        }).catch(function(e){
          if (main.debugging) console.error("Seek failed",e);
          MistVideo.log("Seek failed: "+e);
        });
      });
      controller.control.addListener("tracks",function(msg){
        //check if all codecs are equal to the ones we were using before
        function checkEqual(arr1,arr2) {
          if (!arr2) { return false; }
          if (arr1.length != arr2.length) { return false; }
          for (var i in arr1) {
            if (arr2.indexOf(arr1[i]) < 0) {
              return false;
            }
          }
          return true;
        }

        if (checkEqual(controller.ms.codecs,msg.codecs)) {
          MistVideo.log("Player switched tracks, keeping source buffer as codecs are the same as before.");
        }
        else {

          //start gathering messages in a new msg queue. They won't be appended to the current source buffer
          if (controller.queues) {
            controller.queues.push([]);
          }
          else {
            controller.queues = [[]];
          }

          var switchpoint = msg.current*1e-3;
          if (main.debugging) console.warn("[Track switch]","[1/5]",msg.tracks,"Different codecs detected","Current time:",video.currentTime,"Switching point:",switchpoint);
          //play out buffer, then when we reach the starting timestamp of the new data, reset the source buffers
          new Promise(function(resolve,reject){
            if (switchpoint <= video.currentTime) return resolve("immediate");
            var evtl_tu = MistUtil.event.addListener(video,"timeupdate",function(){
              if (switchpoint >= video.currentTime) {
                resolve("timeupdate");
                MistUtil.event.removeListener(evtl_tu);
                MistUtil.event.removeListener(evtl_wa);
              }
            });
            var evtl_wa =  MistUtil.event.addListener(video,"waiting",function(){
              resolve("waiting");
              MistUtil.event.removeListener(evtl_tu);
              MistUtil.event.removeListener(evtl_wa);
            });
          }).then(function(type){
            if (main.debugging) console.warn("[Track switch]","[2/5]",msg.tracks,"Reached switching point!",type);
            return controller.ms.setCodecs(msg.codecs);
          }).then(function(action){
            if (main.debugging) console.warn("[Track switch]","[3/5]",msg.tracks,action ? action : "The source buffer has been closed and re-created");
            return new Promise(function(resolve,reject){
              var evtl = MistUtil.event.addListener(video,"progress",function(){
                if (main.debugging == "verbose") console.log("progress","target",switchpoint,"buffers:",function(b){
                  var out = [];
                  for (var i = 0; i < b.length; i++) {
                    out.push([b.start(i),b.end(i)]);
                  }
                  return out;
                }(video.buffered));

                //check if the target is buffered
                var buffern = main.findBuffer(switchpoint);
                if (buffern !== false) {
                  MistUtil.event.removeListener(evtl);
                  resolve(buffern);
                }
              });
            })
          }).then(function(buffern){
            if (main.debugging) console.warn("[Track switch]","[4/5]",msg.tracks,"Switch point in buffer","Buffer size:",buffern !== false ? Math.round((video.buffered.end(buffern) - switchpoint)*1e3)+"ms" : "N/A");

            video.currentTime = switchpoint;

            return video.play();
          }).then(function(){
            if (main.debugging) console.warn("[Track switch]","[5/5]",msg.tracks,"Video playing");
          }).catch(function(e){
            if (main.debugging) console.error("Track switch failed",e);
            MistVideo.log("Track switch failed: "+e);
          });

        }

      });
    };
    this.createControlChannel();

    this.connect = function(){
      if (this.connecting) {
        return this.connecting;
      }
      this.connecting = new Promise(function(resolve,reject){

        var control = controller.control;
        if ((control.connectionState == "closing") || (control.connectionState == "closed")) {
          control.reconnect();
        }

        control.addListener("binary",controller.receiver);

        controller.ms = new myMediaSource();
        controller.bm = new MistUtil.shared.BufferManager(controller.control,MistVideo,video,{
          //getter
          desiredBuffer: new MistUtil.shared.DesiredBuffer({ //all buffer components in ms
            base: MistVideo.info.type == "live" ? 100 : 500,   //never changes
            keepAway: 500,                                     //slowly decays by keepAwayDecay every on_time if buffer state is ok, increases when waiting event is triggered
            serverDelay: controller.control.serverDelay.get
          }),
          buffer: function(){
            var n = main.findBuffer(video.currentTime);
            if (n === false) return null;
            return (video.buffered.end(n) - video.currentTime)*1e3;
          },
          keepAwayDecay: 0.25
        });
        controller.bm.addListener("buffer_low",function(){
          main.ABR.badness++;
        });
        if (main.debugging) console.warn("[Connecting]","[1/5]","Started control channel and media source");
        var channelReady = control.readyState == "open" ? Promise.resolve() : control.addListener("channel_open");
        channelReady.then(function(){
          if (main.debugging) console.warn("[Connecting]","[2/5]","Control channel open, requesting codecs");
          controller.control.send({type:"request_codec_data",supported_codecs:MistVideo.source.supportedCodecs},true);
          return controller.control.addListener("codec_data");
        }).then(function(msg){
          if (main.debugging) console.warn("[Connecting]","[3/5]","Codec info received, configuring media source");
          return controller.ms.setCodecs(msg.codecs);
        }).then(function(){
          if (main.debugging) console.warn("[Connecting]","[4/5]","Source buffer configured, ready for data");
          controller.control.unlock();
          controller.control.send({type:"play"});
          return new Promise(function(resolve,reject){
            var evtl = MistUtil.event.addListener(video,"progress",function(){
              if (video.buffered.length) {
                var startpoint = video.buffered.start(0);
                video.currentTime = startpoint;
                MistUtil.event.removeListener(evtl);
                resolve();
              }
            });
          });
        }).then(function(){
          if (main.debugging) console.warn("[Connecting]","[5/5]","Data in buffer, ready for playback");

          if (video.paused) {
            if (MistVideo.options.autoplay) {
              video.play().catch(function(){});
            }
            else {
              controller.control.send({type:"hold"});
            }
          }
          else {
            //probably a reconnect
            video.play().catch(function(){});
          }
          controller.connecting = false;
          resolve();
        }).catch(reject);
      });

      return this.connecting;
    };

    this.receiver = function(data){
      /*if (main.monitor && main.monitor.bitCounter) {
        for (var i in main.monitor.bitCounter) {
          main.monitor.bitCounter[i] += e.data.byteLength*8;
        }
      }*/
      if ((controller.sb) && (!controller.queues)) {
        if (controller.sb.updating || controller.sb.queue.length || controller.sb.busy) {
          controller.sb.queue.push(data);
        }
        else {
          //console.log("appending new data");
          controller.sb.append(data);
        }
      }
      else {
        //There is no active source buffer or we're preparing for a track switch.
        //Any data is kept in a separate buffer and won't be appended to the source buffer until it is reinitialised.
        if (!controller.queues) { controller.queues = [[]]; }
        //There may be more than one separate buffer (in case of rapid track switches), always append to the last of the buffers
        controller.queues[controller.queues.length-1].push(data);
      }
    };

    this.close = function(){
      return new Promise(function(resolve,reject){
        if (main.debugging) console.warn("[WSMP4.close] Closing control channel..");
        controller.control.removeListener("binary",controller.receiver);
        controller.control.close();
        if (main.debugging) console.warn("[WSMP4.close] Closing MediaSource..");
        controller.ms.close().then(function(){
          controller.ms = false;
          if (main.debugging) console.warn("[WSMP4.close] MediaSource closed");
          resolve();
        }).catch(function(e){
          reject(e);
        });
      });
    };

    this.connect();
  }
  function myMediaSource(){
    var self = this;
    this.ms = false;
    this.sb = false;
    this.codecs = null;
    this.was_connected;

    function mySourceBuffer(codecs){
      var msb = this;

      var sb = self.ms.addSourceBuffer("video/"+MistVideo.source.type.split("/")[2]+";codecs=\""+codecs.join(",")+"\"");
      sb.mode = "segments";
      this.sb = sb;

      this.queue = [];
      this.onupdateend = [];
      this.appending = null;
      this.appended = [];
      this.busy = false;
      this.paused = true;
      Object.defineProperty(this,"updating",{get:function(){
        return sb ? sb.updating : false;
      }});
      var n = 0;

      sb.addEventListener("updateend",function(){
        if (!sb) {
          MistVideo.log("Reached updateend but the source buffer is "+JSON.stringify(player.sb)+". ");
          return;
        }

        if (main.debugging) {
          if (msb.appending) msb.appended.push(msb.appending);
          msb.appending = null;
        }

        //every 500 fragments, clean the buffer (about every 15 sec)
        if (n >= 500) {
          //console.log(n,video.currentTime - video.buffered.start(0));
          n = 0;
          msb.clean(10); //keep 10 sec
        }
        else {
          n++;
        }

        var do_funcs = msb.onupdateend.slice(); //clone the array
        msb.onupdateend = [];
        for (var i in do_funcs) {
          //console.log("do_funcs",Number(i)+1,"/",do_funcs.length);
          if (!sb) {
            if (main.debugging) { console.warn("I was doing onupdateend but the sb was reset"); } 
            break;
          }
          if (sb.updating) {
            //it's updating again >_>
            msb.onupdateend.concat(do_funcs.slice(i)); //add the remaining functions to do_on_updateend
            if (player.debugging) { console.warn("I was doing onupdateend but was interrupted"); }
            break;
          }
          do_funcs[i](i < do_funcs.length-1 ? do_funcs.slice(i) : []); //pass remaining do_funcs as argument
        }

        if (!sb) return;

        msb.busy = false;
        MistUtil.event.send("progress",null,video);

        if (sb && msb.queue.length > 0 && !sb.updating && !video.error) {
          //console.log("appending from queue");
          msb.append(msb.queue.shift());
        }
      });
      sb.error = function(e){
        console.error("sb error",e);
      };
      sb.abort = function(e){
        console.error("sb abort",e);
      };

      this.doNext = function(func){
        this.onupdateend.push(func);
      };
      this.do = function(func){
        if (sb.updating || this.busy) {
          this.doNext(func);
        }
        else {
          func();
        }
      };
      this.append = function(data){
        if (!data) { return; }
        if (!data.buffer) { return; }
        if (main.debugging) { msb.appending = new Uint8Array(data); }
        if (msb.busy) {
          if (main.debugging) console.warn("I wanted to append data, but now I won't because the thingy was still busy. Putting it back in the queue.");
          msb.queue.unshift(data);
          return;
        }
        msb.busy = true;
        //console.log("appendBuffer");
        try {
          sb.appendBuffer(data);
        }
        catch(e){
          switch (e.name) {
            case "QuotaExceededError": {
              if (video.buffered.length) {
                if (video.currentTime - video.buffered.start(0) > 1) {
                  //clear as much from the buffer as we can
                  MistVideo.log("Triggered QuotaExceededError: cleaning up "+(Math.round((video.currentTime - video.buffered.start(0) - 1)*10)/10)+"s");
                  msb.clean(1);
                }
                else {
                  var bufferEnd = video.buffered.end(video.buffered.length-1);
                  MistVideo.log("Triggered QuotaExceededError but there is nothing to clean: skipping ahead "+(Math.round((bufferEnd - video.currentTime)*10)/10)+"s");
                  video.currentTime = bufferEnd;
                }
                msb.busy = false;
                msb.append(data); //now try again
                return;
              }
              break;
            }
            case "InvalidStateError": {
              main.api.pause(); //playback is borked, so stop downloading more data
              if (MistVideo.video.error) {
                //Failed to execute 'appendBuffer' on 'SourceBuffer': The HTMLMediaElement.error attribute is not null
                //CHUNK_DEMUXER_ERROR_APPEND_FAILED: Failed to prepare video sample for decode
                if (MistVideo.video.error.message.slice(0,33) == "CHUNK_DEMUXER_ERROR_APPEND_FAILED") {
                  //decode error: try again, once
                  //TODO
                }

                //the video element error is already triggering the showError()
                return;
              }
              break;
            }
          }
          MistVideo.showError(e.message);
          if (main.debugging) console.error(e.name,e.message,e);
        }
      };
      this.clean = function(keepaway){
        if (typeof keepaway == "undefined") keepaway = 180;
        if (video.currentTime > keepaway) {
          msb.do(function(){
            //make sure end time is never 0
            sb.remove(0,Math.max(0.1,video.currentTime - keepaway));
          });
        }
      };
      if ("changeType" in sb) {
        this.setCodecs = function(codecs){
          sb.changeType("video/"+MistVideo.source.type.split("/")[2]+";codecs=\""+codecs.join(",")+"\"");
          this.applyQueue();
        };
      }

      this.close = function(){
        return new Promise(function(resolve,reject){
          msb.queue = [];
          msb.do(function(remaining_do_on_updateend){
            if (!sb) {
              //already done
              return resolve();
            }

            if (sb.updating) {
              if (sb.abort) sb.abort();
              else {
                return msb.close().then(resolve).catch(reject);
              }
            }

            if (main.debugging == "dl") {
              function downloadBlob (data, fileName, mimeType) {
                var blob, url;
                blob = new Blob([data], {
                  type: mimeType
                });
                url = window.URL.createObjectURL(blob);
                downloadURL(url, fileName);
                setTimeout(function() {
                  return window.URL.revokeObjectURL(url);
                }, 1000);
              };
              function downloadURL (data, fileName) {
                var a;
                a = document.createElement('a');
                a.href = data;
                a.download = fileName;
                document.body.appendChild(a);
                a.style = 'display: none';
                a.click();
                a.remove();
              };

              var l = 0;
              for (var i = 0; i < msb.appended.length; i++) {
                l += msb.appended[i].length;
              }
              var d = new Uint8Array(l);
              var l = 0;
              for (var i = 0; i < msb.appended.length; i++) {
                d.set(msb.appended[i],l);
                l += msb.appended[i].length;
              }

              downloadBlob(d, 'appended.mp4.bin', 'application/octet-stream');
            }
            
            if (self.ms.sourceBuffers.length) {
              //empty the buffer
              sb.remove(0,Infinity);
              self.ms.removeSourceBuffer(sb);
            }
            sb = null;
            self.sb = null;

            if (main.debugging) console.warn("[SourceBuffer]",".close() complete");
            
            resolve();
          });
        });
      };

      this.applyQueue = function(){
        var queues = main.controller.queues;
        if (queues && queues.length) {
          //There may be more than one msg queue, i.e. when rapidly switching tracks. Add only one msg queue and always add the oldest msg queue first.
          if (queues[0]) {
            var do_do = false; //if there are no messages in the queue, make sure to execute any do_on_updateend functions right away
            if (queues[0].length) {
              for (var i in queues[0]) {
                if (sb.updating || msb.queue.length || msb.busy) {
                  msb.queue.push(queues[0][i]);
                }
                else {
                  //console.log("appending new data");
                  msb.append(queues[0][i]);
                }
              }
            }
            else {
              do_do = true;
            }
            queues.shift();
            if (queues.length == 0) { main.controller.queues = false; }
            MistVideo.log("The newly initialized source buffer was filled with data from a separate message queue."+(queues ? " "+queues.length+" more message queue(s) remain." : ""));
            if (do_do) {
              MistVideo.log("The separate message queue was empty; manually triggering any onupdateend functions");
              sb.dispatchEvent(new Event("updateend"));
            }
          }
        }
      }

      this.applyQueue();
    }

    this.init = function(){
      return new Promise(function(resolve,reject){
        self.ms = new MediaSource();
        video.src = URL.createObjectURL(self.ms);

        self.ms.onsourceopen = function(){
          if (self.codecs) {
            self.addSourceBuffer().then(resolve,reject);
          }
        };
      });
    };
    this.setCodecs = function(codecs){
      var old = this.codecs;
      this.codecs = codecs;

      function typeHasChanged(arr1,arr2){
        if (arr1.length != arr2.length) return true;
        if (arr1.length >= 2) return false; //both arrays must contain both audio and video

        //both arrays have length 1
        //check track types
        if (MistVideo.info && MistVideo.info.meta && MistVisdeo.info.meta.tracks) {
          var found = [];
          for (var i in MistVideo.info.meta.tracks) {
            var t = MistVideo.info.meta.tracks[i];
            if ((t.codecstring == arr1[0]) || (t.codecstring == arr2[0])) {
              found.push(t.type);
              if (found.length == 2) {
                return found[0] == found[1];
              }
            }
          }
        }

        //track info not found - should not reach this point
        return true;
      }

      return new Promise(function(resolve,reject){
        if (!codecs || !codecs.length) return reject("No codecs provided");

        if (self.sb) {
          if (("setCodecs" in self.sb) && (!typeHasChanged(old,codecs))) {
            try {
              self.sb.setCodecs(codecs);
              resolve("The source buffer's type was changed to "+codecs.join(","));
            }
            catch(e){
              reject(e);
            }
          }
          else {
            self.sb.close().then(self.addSourceBuffer).then(resolve,reject);
          }
        }
        else {
          self.addSourceBuffer().then(resolve,reject);
        }
      });
    };
    this.addSourceBuffer = function(){
      return new Promise(function(resolve,reject){
        if (!self.codecs || !self.codecs.length) return reject("No codecs provided");
        try {
          self.sb = new mySourceBuffer(self.codecs);
          self.was_connected = true;
          resolve();
        }
        catch(e){
          if (self.was_connected) {
            self.was_connected = false;
            self.ms = false;
            video.src = "";
            self.init().then(resolve,reject)
          }
          else {
            reject("SourceBuffer initialization failed: "+e);
          }
        }
      });
    };

    this.close = function(){
      return this.sb.close().then(function(){
        try { 
          self.ms.endOfStream();
        } catch(e) {}
        video.paused = true;
        //URL.revokeObjectURL(video.src);
        //video.src = "";
        //technically the media source is not closed until sourceclose has fired, but that's fine
      });
    };

    this.init();
  }

  this.controller = new WSMP4();
  this.api = new MistUtil.shared.ControlChannelAPI(this.controller,MistVideo,video,{
    play: function(skipToLive){
      return new Promise(function(resolve,reject){
        if (!video.paused) { 
          //we're already playing, what are you doing?
          resolve();
          return;
        }

        if (("paused" in main.controller.sb) && !main.controller.sb.paused) {
          video.play().then(resolve).catch(reject);
          return;
        }

        main.controller.control.addListener("on_time").then(function(data){

          if (!main.controller.sb) {
            MistVideo.log("Attempting to play, but the source buffer is being cleared. Waiting for next on_time.");
            return;
          }
          if (MistVideo.info.type == "live") {
            if (skipToLive || (video.currentTime == 0)) {
              var f = function(){
                if (video.buffered.length) {
                  //is data.current contained within a buffer? is video.currentTime also contained in that buffer? if not, seek the video
                  var buffern = main.findBuffer(data.current*1e-3);
                  if (buffern !== false) {
                    if ((video.buffered.start(buffern) > video.currentTime) || (video.buffered.end(buffern) < video.currentTime)) {
                      video.currentTime = data.current*1e-3;
                      MistVideo.log("Setting live playback position to "+MistUtil.format.time(video.currentTime));
                    }
                    video.play().then(resolve).catch(function(){
                      //could not play video, pause the download
                      return reject.apply(this,arguments);
                    });
                    main.controller.sb.paused = false;                   
                    main.controller.sb.sb.removeEventListener("updateend",f);
                  }
                }
              };
              main.controller.sb.sb.addEventListener("updateend",f);
            }
            else {
              main.controller.sb.paused = false;
              video.play().then(resolve).catch(function(){
                //could not play video, pause the download
                main.api.pause();
                return reject.apply(this,arguments);
              });
            }
          }
          else if (data.current > video.currentTime) {
            main.controller.sb.paused = false;
            if (video.buffered.length && video.buffered.start(0) > video.currentTime) {
              video.currentTime = video.buffered.start(0);
            }
            video.play().then(resolve).catch(reject);
          }
        });
        var cmd = {type:"play"};
        if (skipToLive) { cmd.seek_time = "live"; }
        main.controller.control.send(cmd);
        
      });
    },
    pause: function(){
      video.pause();
      main.controller.control.send({type: "hold"});
      if (main.controller.sb) { main.controller.sb.paused = true; }
    },
    currentTime: {
      get: function(){
        return video.currentTime;
      },
      set: function(value){
        seeking = true;
        value = (value == "live" ? "live" : Math.round(value*1e3)); //now in ms
        main.controller.control.send({
          type: "seek",
          seek_time: value
        });
        value = value*1e-3; //back to seconds
      }
    }
  });
  this.ABR = new MistUtil.shared.ABRController(MistVideo,{
    bitCounter: function(){ return main.controller.control.bitCounter; }
  });

  var seeking = false;
  MistUtil.event.addListener(video,"waiting",function(){
    //check if there is a gap in the buffers, and if so, jump it
    if (seeking) { 
      if (main.debugging) console.log("Waiting while seeking - not jumping");
      return;
    }
    var buffern = main.findBuffer(video.currentTime);
    if (buffern !== false) {
      if ((buffern+1 < video.buffered.length) && (video.buffered.start(buffern+1) - video.currentTime < 10e3)) {
        MistVideo.log("Skipped over buffer gap (from "+MistUtil.format.time(video.currentTime)+" to "+MistUtil.format.time(video.buffered.start(buffern+1))+")");
        video.currentTime = video.buffered.start(buffern+1);
      }
      /*else {
        if (main.debugging) console.log("Not a valid gap - not jumping",{
          buffern: buffern,
          "video buffers": video.buffered.length,
          "gap length [s]": buffern+1 < video.buffered.length ? video.buffered.start(buffern+1) - video.currentTime : "N/A"
        });
      }*/
    }
    /*else {
      if (main.debugging) console.log("No buffer found - not jumping");
    }*/
  });
  MistUtil.event.addListener(video,"pause",function(){
    if (main.controller.sb && !main.controller.sb.paused) {
      if ((video.error) && (video.error instanceof MediaError) && (video.error.code == 3)) {
        /*main.controller.sb.do(function(){
          //clear buffer
          main.controller.sb.sb.remove(0,Infinity);
          main.api.play().catch();
        });*/
      }
      else {
        MistVideo.log("The browser paused the vid - probably because it has no audio and the tab is no longer visible. Pausing download.");
        main.controller.control.send({type:"hold"});
        main.controller.sb.paused = true;
        var p = MistUtil.event.addListener(video,"play",function(){
          if (main.controller.sb && main.controller.sb.paused) {
            main.controller.control.send({type:"play"});
          }
          MistUtil.event.removeListener(p);
        });
      }
    }
  });

  //TODO try once, reset when it plays
  var recovering = false;
  MistUtil.event.addListener(video,"error",function(e){
    console.error(e,video.error);
    if (video.error && (video.error.code == 3) && !recovering) {
      if (video.error.message.slice(0,33) == "CHUNK_DEMUXER_ERROR_APPEND_FAILED") {
        recovering = true;

        //decoding error: clear sb and try to carry on
        if (main.controller.sb) {
          main.controller.sb.close().then(function(){
            main.controller.ms.init();
            main.controller.control.send({
              type: "play"
            });
          });
        }
        else {
          main.controller.ms.init();
          main.controller.control.send({
            type: "play"
          });
        }
        MistUtil.event.addListener(video,"progress").then(function(){
          recovering = false;
        });
      }
    }
  });


  this.findBuffer = function (position) {
    var buffern = false;
    for (var i = 0; i < video.buffered.length; i++) {
      if ((video.buffered.start(i) <= position) && (video.buffered.end(i) >= position)) {
        buffern = i;
        break;
      }
    }
    return buffern;
  };

  Object.defineProperty(this.api,"buffer_manager",{
    get: function(){
      if (main.controller && main.controller.bm) return main.controller.bm;
      return null;
    }
  });

  callback(video);
};
