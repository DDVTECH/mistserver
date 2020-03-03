var MistUtil = {
  format: {
    time: function(secs,options){
      if (isNaN(secs) || !isFinite(secs)) { return secs; }
      if (!options) { options = {}; }
      
      var ago = (secs < 0 ? " ago" : "");
      secs = Math.abs(secs);
      
      var days = Math.floor(secs / 86400)
      secs = secs - days * 86400;
      var hours = Math.floor(secs / 3600);
      secs = secs - hours * 3600;
      var mins  = Math.floor(secs / 60);
      var ms = Math.round((secs % 1)*1e3);
      secs = Math.floor(secs - mins * 60);
      var str = [];
      if (days) {
        days = days+" day"+(days > 1 ? "s" : "")+", ";
      }
      if ((hours) || (days)) {
        str.push(hours);
        str.push(("0"+mins).slice(-2));
      }
      else {
        str.push(mins); //don't use 0 padding if there are no hours in front
      }
      str.push(("0"+Math.floor(secs)).slice(-2));
      
      if (options.ms) {
        str[str.length-1] += "."+("000"+ms).slice(-3);
      }
      
      return (days ? days : "")+str.join(":")+ago;
    },
    ucFirst: function(string){
      return string.charAt(0).toUpperCase()+string.slice(1);
    },
    number: function(num) {
      if ((isNaN(Number(num))) || (num == 0)) { return num; }
      
      //rounding
      //use a significance of three, but don't round "visible" digits
      var sig = Math.max(3,Math.ceil(Math.log(num)/Math.LN10));
      var mult = Math.pow(10,sig - Math.floor(Math.log(num)/Math.LN10) - 1);
      num = Math.round(num * mult) / mult;
      
      //thousand seperation
      if (num >= 1e4) {
        var seperator = " ";
        number = num.toString().split(".");
        var regex = /(\d+)(\d{3})/;
        while (regex.test(number[0])) {
          number[0] = number[0].replace(regex,"$1"+seperator+"$2");
        }
        num = number.join(".");
      }
      
      return num;
    },
    bytes: function(val){
      if (isNaN(Number(val))) { return val; }
      
      var suffix = ["bytes","KB","MB","GB","TB","PB"];
      if (val == 0) {
        unit = suffix[0];
      }
      else {
        var exponent = Math.floor(Math.log(Math.abs(val)) / Math.log(1024));
        if (exponent < 0) {
          unit = suffix[0];
        }
        else {
          val = val / Math.pow(1024,exponent);
          unit = suffix[exponent];
        }
      }
      return this.number(val)+unit;
    },
    mime2human: function(mime){
      switch (mime) {
        case "html5/video/webm": {
          return "WebM";
          break;
        }
        case "html5/application/vnd.apple.mpegurl": {
          return "HLS (TS)";
          break;
        }
        case "html5/application/vnd.apple.mpegurl;version=7": {
          return "HLS (CMAF)";
          break;
        }
        case "flash/10": {
          return "Flash (RTMP)";
          break;
        }
        case "flash/11": {
          return "Flash (HDS)";
          break;
        }
        case "flash/7": {
          return "Flash (Progressive)";
          break;
        }
        case "html5/video/mpeg": {
          return "TS";
          break;
        }
        case "html5/application/vnd.ms-sstr+xml":
        case "html5/application/vnd.ms-ss": {
          return "Smooth Streaming";
          break;
        }
        case "dash/video/mp4": {
          return "DASH";
          break;
        }
        case "webrtc": {
          return "WebRTC";
          break;
        }
        case "silverlight": {
          return "Smooth streaming (Silverlight)";
          break;
        }
        case "html5/text/vtt": {
          return "VTT subtitles";
          break;
        }
        case "html5/text/plain": {
          return "SRT subtitles";
          break;
        }
        default: {
          return mime.replace("html5/","").replace("video/","").replace("audio/","").toLocaleUpperCase();
        }
      }
    }
  },
  
  class: {
    //reroute classList functionalities if not supported; also avoid indexOf
    add: function(DOMelement,item){
      if ("classList" in DOMelement) {
        DOMelement.classList.add(item);
      }
      else {
        var classes = this.get(DOMelement);
        
        classes.push(item);
        this.set(DOMelement,classes);
      }
    },
    remove: function(DOMelement,item){
      if ("classList" in DOMelement) {
        DOMelement.classList.remove(item);
      }
      else {
        var classes = this.get(DOMelement);
        
        for (var i = classes.length-1; i >= 0; i--) {
          if (classes[i] == item) {
            classes.splice(i);
          }
        }
        this.set(DOMelement,classes);
      }
    },
    get: function(DOMelement) {
      var classes;
      var className = DOMelement.getAttribute("class"); //DOMelement.className does not work on svg elements
      
      if ((!className) || (className == "")) { classes = []; }
      else { classes = className.split(" "); }
      
      return classes;
    },
    set: function(DOMelement,classes) {
      DOMelement.setAttribute("class",classes.join(" "));
    },
    has: function(DOMelement,hasClass){
      return (DOMelement.className.split(" ").indexOf(hasClass) >= 0)
    }
  },
  
  object: {
    //extend object1 with object2
    extend: function(object1,object2,deep) {
      for (var i in object2) {
        if (deep && (typeof object2[i] == "object") && (!("nodeType" in object2[i]))) {
          if (!(i in object1)) {
            if (MistUtil.array.is(object2[i])) {
              object1[i] = [];
            }
            else {
              object1[i] = {};
            }
          }
          this.extend(object1[i],object2[i],true);
        }
        else {
          object1[i] = object2[i];
        }
      }
      
      return object1;
    },
    //replace Object.keys
    //if sorting: sort the keys alphabetically or use passed sorting function
    //sorting gets these arguments: keya,keyb,valuea,valueb
    keys: function(obj,sorting){
      
      var keys = [];
      for (var i in obj) {
        keys.push(i);
      }
      
      if (sorting) {
        if (typeof sorting != "function") {
          sorting = function(a,b){
            return a.localeCompare(b);
          };
        }
        
        keys.sort(function(keya,keyb){
          return sorting(keya,keyb,obj[keya],obj[keyb]);
        });
      }
      
      return keys;
    },
    //replace Object.values
    //if sorting: sort the keys alphabetically or use passed sorting function
    //sorting gets these arguments: keya,keyb,valuea,valueb
    values: function(obj,sorting){
      
      var keys = this.keys(obj,sorting);
      
      values = [];
      for (var i in keys) {
        values.push(obj[keys[i]]);
      }
      
      return values;
    }
  },
  array: {
    //replace [].indexOf
    indexOf: function(array,entry) {
      if (!(array instanceof Array)) { throw "Tried to use indexOf on something that is not an array"; }
      if ("indexOf" in array) { return array.indexOf(entry); }
      
      for (var i; i < array.length; i++) {
        if (array[i] == entry) {
          return i;
        }
      }
      return -1;
    },
    //replace isArray
    is: function(array) {
      if ("isArray" in Array) {
        return Array.isArray(array);
      }
      return Object.prototype.toString.call(array) === '[object Array]';
    },
    multiSort: function (array,sortby) {
      /*
       MistUtil.array.multiSort([].concat(video.info.source),[
        {type: ["html5/video/webm","silverlight"]} or ["type",["html5/video/webm","silverlight"]]
        ,{simul_tracks:-1} or ["simul_tracks",-1]
        ,function(a){ return a.priority * -1; }
        ,"url"
       ]);
      */
      
      var sortfunc = function(a,b){
        if (isNaN(a) || isNaN(b)) {
          return a.localeCompare(b);
        }
        return Math.sign(a-b);
      };
      
      if (!sortby.length) { return array.sort(sortfunc); }
      
      function getValue(key,a) {
        
        function parseIt(item,key,sortvalue){
          if (!(key in item)) {
            throw "Invalid sorting rule: "+JSON.stringify([key,sortvalue])+". \""+key+"\" is not a key of "+JSON.stringify(item);
          }
          
          if (typeof sortvalue == "number") {
            //deals with something like {priority: -1}
            if (key in item) {
              return item[key] * sortvalue;
            }
          }
          
          //deals with something like {type:["webrtc"]}
          var i = sortvalue.indexOf(item[key])
          return (i >= 0 ? i : sortvalue.length);
        }
        
        //deals with something like function(a){ return a.foo + a.bar; }
        if (typeof key == "function") { return key(a); }
        
        if (typeof key == "object") {
          if (key instanceof Array) {
            //it's an array
            return parseIt(a,key[0],key[1]);
          }
          //it's an object
          for (var j in key) { //only listen to a single key
            return parseIt(a,j,key[j]);
          }
        }
        
        if (key in a) {
          return a[key];
        }
        
        throw "Invalid sorting rule: "+key+". This should be a function, object or key of "+JSON.stringify(a)+".";
      }
      
      array.sort(function(a,b){
        var output = 0;
        for (var i in sortby) {
          var key = sortby[i];
          output = sortfunc(getValue(key,a),getValue(key,b));
          if (output != 0) {
            break;
          }
        }
        return output;
      });
      
      return array;
    }
  },
  
  createUnique: function() {
    var i = "uid"+Math.random().toString().replace("0.","");
    if (document.querySelector("."+i)) {
      //if this is already used, try again
      return createUnique();
    }
    return i;
  },
  
  http: {
    getpost: function(type,url,data,callback,errorCallback) {
      var xhr = new XMLHttpRequest();
      xhr.open(type, url, true);
      if (type == "POST") { xhr.setRequestHeader("Content-type", "application/x-www-form-urlencoded"); }
      
      if (errorCallback) { xhr.timeout = 8e3; } //go to timeout function after 8 seconds
      
      xhr.onload = function() {
        var status = xhr.status;
        if ((status >= 200) && (status < 300)) {
          callback(xhr.response);
        }
        else if (errorCallback) {
          errorCallback(xhr);
        }
      };
      if (errorCallback) {
        xhr.onerror = function() {
          errorCallback(xhr);
        }
        xhr.ontimeout = xhr.onerror;
      }
      if (type == "POST") {
        var poststr;
        var post = [];
        for (var i in data) {
          post.push(i+"="+encodeURIComponent(data[i]));
        }
        if (post.length) { poststr = post.join("&"); }
        xhr.send(poststr);
      }
      else {
        xhr.send();
      }
    },
    get: function(url,callback,errorCallback){
      this.getpost("GET",url,null,callback,errorCallback);
    },
    post: function(url,data,callback,errorCallback){
      this.getpost("POST",url,data,callback,errorCallback);
    },
    url: {
      addParam: function(url,params){
        var spliturl = url.split("?");
        var ret = [spliturl.shift()];
        var splitparams = [];
        if (spliturl.length) {
          splitparams = spliturl[0].split("&");
        }
        for (var i in params) {
          splitparams.push(i+"="+params[i]);
        }
        if (splitparams.length) { ret.push(splitparams.join("&")); }
        return ret.join("?");
      },
      split: function(url){
        var a = document.createElement("a");
        a.href = url;
        return {
          protocol: a.protocol,
          host: a.hostname,
          hash: a.hash,
          port: a.port,
          path: a.pathname.replace(/\/*$/,"")
        };
      },
      sanitizeHost: function(host){
        var split = MistUtil.http.url.split(host);
        var out = split.protocol + "//" + split.host + (split.port && (split.port != "") ? ":"+split.port : "") + (split.hash && (split.hash != "") ? "#"+split.hash : "") + (split.path ? (split.path.charAt(0) == "/" ? split.path : "/"+split.path) : "");
        //console.log("converted",host,"to",out);
        return out;
      }
    }
  },
  
  css: {
    cache: {},
    load: function(url,colors,callback){
      var style = document.createElement("style");
      style.type = "text/css";
      style.setAttribute("data-source",url);
      if (callback) { style.callback = callback; }
      var cache = this.cache;
      
      function onCSSLoad(d) {
        //parse rules and replace variables; expected syntax $abc[.abc]
        var css = MistUtil.css.applyColors(d,colors);
        
        if ("callback" in style) { style.callback(css); }
        else { style.textContent = css; }
      }
      
      if (url in cache) {
        if (cache[url] instanceof Array) {
          cache[url].push(onCSSLoad);
        }
        else {
          onCSSLoad(cache[url]);
        }
      }
      else {
        //retrieve file contents
        cache[url] = [onCSSLoad];
        MistUtil.http.get(url,function(d){
          for (var i in cache[url]) {
            cache[url][i](d);
          }
          cache[url] = d;
        },function(){
          var d = "/*Failed to load*/";
          for (var i in cache[url]) {
            cache[url][i](d);
          }
          cache[url] = d;
          
        });
      }
      
      return style; //its empty now, but will be filled on load
    },
    applyColors: function(css,colors) {
      return css.replace(/\$([^\s^;^}]*)/g,function(str,variable){
        var index = variable.split(".");
        var val = colors;
        for (var j in index) {
          val = val[index[j]];
        }
        return val;
      });
    },
    createStyle: function(css,prepend,applyToChildren){
      var style = document.createElement("style");
      style.type = "text/css";
      
      if (css) {
        if (prepend) {
          css = this.prependClass(css,prepend,applyToChildren);
        }
        style.textContent = css;
      }
      
      return style;
    },
    prependClass: function (css,prepend,applyToChildren) {
      var style = false;
      if (typeof css != "string") {
        style = css;
        if (!("unprepended" in style)) {
          style.unprepended = style.textContent;
        }
        css = style.unprepended;
      }
      //remove all block comments
      css = css.replace(/\/\*.*?\*\//g,"");
      
      //remove all @ {} blocks (media, keyframes, screen etc) and save it to re-insert them after class prepending
      //match anything starting with @ something {,  until the first }
      var save = css.match(/@[^}]*}/g);
      
      for (var i in save) {
        //add a placeholder for unfinished replace
        css = css.replace(save[i],"@@#@@");
        
        var replacecount = 1;
        
        //while the amount of }s we've replaced is smaller than the amount of {'s in the match
        while (replacecount < (save[i].match(/{/g).length)) {
          //find the next } and save it in a group
          var match = css.match(/@@#@@([^}]*})/); //match anything starting with @@#@@ until the first }
          
          //replace the full match with the unfinished placeholder
          css = css.replace(match[0],"@@#@@");
          
          //add the group (the code untill the next }) to the save
          save[i] += match[1];
          
          //increase the counter
          replacecount++;
        }
        
        //after the edits, @@@@ will be replaced with the contents of save[i]
        css = css.replace("@@#@@","@@@@");
      }
      
      //find and replace selectors
      css = css.replace(/[^@]*?{[^]*?}/g,function(match){
        var split = match.split("{")
        var selectors = split[0].split(",");
        var properties = "{"+split.slice(1).join("}");
        
        for (var i in selectors) {
          selectors[i] = selectors[i].trim();
          var str = "."+prepend+selectors[i];
          if (applyToChildren) {
            str += ",\n."+prepend+" "+selectors[i];
          }
          selectors[i] = str;
        }
        
        
        return "\n"+selectors+" "+properties;
      });
      
      //reinsert saved blocks
      for (var i in save) {
        css = css.replace(/@@@@/,save[i]);
      }
      
      if (style) {
        style.textContent = css;
        return;
      }
      
      return css;
    }
  },
  
  empty: function(DOMelement) {
    while (DOMelement.lastChild){
      if (DOMelement.lastChild.lastChild) {
        //also empty this child
        this.empty(DOMelement.lastChild);
      }
      if ("attachedListeners" in DOMelement.lastChild) {
        //remove attached event listeners
        for (var i in DOMelement.lastChild.attachedListeners) {
          MistUtil.event.removeListener(DOMelement.lastChild.attachedListeners[i]);
        }
      }
      DOMelement.removeChild(DOMelement.lastChild);
    }
  },
  
  event: {
    send: function(type,message,target){
      try {
        var event = new Event(type,{
          bubbles: true,
          cancelable: true
        });
        event.message = message;
        target.dispatchEvent(event);
        return event;
      }
      catch (e) {
        try {
          var event = document.createEvent('Event');
          event.initEvent(type,true,true);
          event.message = message;
          target.dispatchEvent(event);
          return event;
        }
        catch (e) { return false; }
      }
      return true;
    },
    addListener: function(DOMelement,type,callback,storeOnElement) {
      //add an event listener and store the handles, so they can be cleared
      
      DOMelement.addEventListener(type,callback);
      
      if (!storeOnElement) { storeOnElement = DOMelement; }
      if (!("attachedListeners" in storeOnElement)) {
        storeOnElement.attachedListeners = [];
      }
      var output = {
        element: DOMelement,
        type: type,
        callback: callback
      };
      
      storeOnElement.attachedListeners.push(output);
      return output;
    },
    removeListener: function(data) {
      data.element.removeEventListener(data.type, data.callback);
    }
  },
  
  scripts: {
    list: {},
    insert: function(src,onevent,MistVideo){
      var scripts = this;
      
      if (MistVideo) {
        //register so we can remove it on unload
        MistVideo.errorListeners.push({
          src: src,
          onevent: onevent
        });
      }
      if (src in this.list) {
        //already present
        //register to error listening
        this.list[src].subscribers.push(onevent.onerror);
        //execute onload
        if ("onload" in onevent) {
          if (this.hasLoaded) {
            onevent.onload(); 
          }
          else {
            MistUtil.event.addListener(this.list[src].tag,"load",onevent.onload);
          }
        }
        return;
      }
      
      var scripttag = document.createElement("script");
      scripttag.hasLoaded = false;
      scripttag.setAttribute("src",src);
      scripttag.setAttribute("crossorigin","anonymous"); //must be set to get info about errors thrown
      document.head.appendChild(scripttag);
      scripttag.onerror = function(e){
        onevent.onerror(e);
      }
      scripttag.onload = function(e){
        this.hasLoaded = true;
        if (!MistVideo.destroyed) { onevent.onload(e); }
      }
      scripttag.addEventListener("error",function(e){
        onevent.onerror(e);
      });
      
      
      //error catching
      var oldonerror = false;
      if (window.onerror) {
        oldonerror = window.onerror;
      }
      window.onerror = function(message,source,line,column,error){
        if (oldonerror) {
          oldonerror.apply(this,arguments);
        }
        if (source == src) {
          onevent.onerror(error);
          for (var i in scripts.list[src].subscribers) {
            scripts.list[src].subscribers[i](error);
          }
        }
      };
      
      this.list[src] = {
        subscribers: [onevent.onerror],
        tag: scripttag
      };
      
      return scripttag;
    }
  },
  
  tracks: {
    parse: function(metaTracks){
      var output = {};
      for (var i in metaTracks) {
        var track = MistUtil.object.extend({},metaTracks[i]);
        if (track.type == "meta") {
          track.type = track.codec;
          track.codec = "meta";
        }
        
        if (!(track.type in output)) { output[track.type] = {}; }
        output[track.type][("idx" in track ? track.idx : track.trackid)] = track;
        
        //make up something logical for the track displayname
        var name = {};
        
        for (var j in track) {
          switch (j) {
            case "width":
              name[j] = track.width+"×"+track.height;
              break;
            case "bps":
              if (track.codec == "meta") { continue; }
              if (track.bps > 0) {
                var val;
                if (track.bps > 1024*1024/8) {
                  val = Math.round(track.bps/1024/1024*8)+"mbps";
                }
                else {
                  val = Math.round(track.bps/1024*8)+"kbps";
                }
                name[j] = val;
              }
              break;
            case "fpks":
              if (track.fpks > 0) {
                name[j] = track.fpks/1e3+"fps";
              }
              break;
            case "channels":
              if (track.channels > 0) {
                name[j] = (track.channels == 1 ? "Mono" : (track.channels == 2 ? "Stereo" : "Surround ("+track.channels+"ch)"));
              }
              break;
            case "rate":
              name[j] = Math.round(track.rate)+"Khz";
              break;
            case "language":
              if (track[j] != "Undetermined") { name[j] = track[j]; }
              break;
            case "codec":
              if (track.codec == "meta") { continue; }
              name[j] = track[j];
              break;
          }
        }
        
        track.describe = name;
        
      }
      
      //filter what to display based on what is different
      for (var type in output) {
        var equal = false;
        for (var i in output[type]) {
          if (!equal) {
            //fill equal with all the keys and values of the first track of this type
            equal = MistUtil.object.extend({},output[type][i].describe);
            continue;
          }
          if (MistUtil.object.keys(output[type]).length > 1) {
            //if there is more than one track of this type
            for (var j in output[type][i].describe) {
              if (equal[j] != output[type][i].describe[j]) {
                //remove key from equal if not equal
                delete equal[j];
              }
            }
          }
        }
        //apply
        for (var i in output[type]) {
          var different = {};
          var same = {};
          for (var j in output[type][i].describe) {
            if (!(j in equal)){
              different[j] = output[type][i].describe[j];
            }
            else {
              same[j] = output[type][i].describe[j];
            }
          }
          output[type][i].different = different;
          output[type][i].same = same;
          var d = MistUtil.object.values(different);
          output[type][i].displayName = (d.length ? d.join(", ") : MistUtil.object.values(output[type][i].describe).join(" "));
        }
        
        //check if some tracks have the same display name
        var names = {};
        for (var i in output[type]) {
          if (output[type][i].displayName in names) {
            //we have double names, add the track id
            var n = 1;
            for (var i in output[type]) {
              output[type][i].different.trackid = n+")";
              output[type][i].displayName = "Track "+n+" ("+output[type][i].displayName+")";
              n++;
            }
            break;
          }
          names[output[type][i].displayName] = 1;
        }
      }
      
      return output;
    }
  },
  isTouchDevice: function(){
    return (('ontouchstart' in window) || (navigator.msMaxTouchPoints > 0));
    //return true;
  },
  getPos: function(element,cursorLocation){
    var style = element.currentStyle || window.getComputedStyle(element, null);
    
    var zoom = 1;
    var node = element;
    while (node) {
      if ((node.style.zoom) && (node.style.zoom != "")) {
        zoom *= parseFloat(node.style.zoom,10);
      }
      node = node.parentElement;
    }
    
    var pos0 = element.getBoundingClientRect().left - (parseInt(element.borderLeftWidth,10) || 0);
    
    var width = element.getBoundingClientRect().width;;
    var perc = Math.max(0,((cursorLocation.clientX/zoom) - pos0) / width);
    perc = Math.min(perc,1);
    
    return perc;
  },
  
  createGraph: function(data,options){
    var ns = "http://www.w3.org/2000/svg";
    
    var svg = document.createElementNS(ns,"svg");
    svg.setAttributeNS(null,"height","100%");
    svg.setAttributeNS(null,"width","100%");
    svg.setAttributeNS(null,"class","mist icon graph");
    svg.setAttributeNS(null,"preserveAspectRatio","none");
    
    var x_correction = data.x[0];
    var lasty = data.y[0];
    if (options.differentiate) {
      for (var i = 1; i < data.y.length; i++) {
        var diff = data.y[i] - lasty;
        lasty = data.y[i];
        data.y[i] = diff;
      }
    }
    
    var path = [];
    var area = {
      x: {
        min: data.x[0] - x_correction,
        max: data.x[0] - x_correction
      },
      y: {
        min: data.y[0]*-1,
        max: data.y[0]*-1
      }
    };
    
    function updateMinMax(x,y) {
      if (arguments.length) {
        area.x.min = Math.min(area.x.min,x);
        area.x.max = Math.max(area.x.max,x);
        area.y.min = Math.min(area.y.min,y*-1);
        area.y.max = Math.max(area.y.max,y*-1);
      }
      else {
        //reprocess the entire path
        var d = path[0].split(",");
        area = {
            x: {
            min: d[0],
            max: d[0]
          },
          y: {
            min: d[1],
            max: d[1]
          }
        };
        for (var i = 1; i < path.length; i++) {
          var d = path[i].split(",");
          updateMinMax(d[0],d[1]*-1);
        }
      }
    }
    
    path.push([data.x[0] - x_correction,data.y[0]*-1].join(","));
    for (var i = 1; i < data.y.length; i++) {
      updateMinMax(data.x[i] - x_correction,data.y[i]*-1);
      path.push("L "+[data.x[i] - x_correction,data.y[i]*-1].join(","));
    }
    
    //define gradient
    var defs = document.createElementNS(ns,"defs");
    svg.appendChild(defs);
    var gradient = document.createElementNS(ns,"linearGradient");
    defs.appendChild(gradient);
    gradient.setAttributeNS(null,"id",MistUtil.createUnique());
    gradient.setAttributeNS(null,"gradientUnits","userSpaceOnUse");
    gradient.innerHTML += '<stop offset="0" stop-color="green"/>';
    gradient.innerHTML += '<stop offset="0.33" stop-color="yellow"/>';
    gradient.innerHTML += '<stop offset="0.66" stop-color="orange"/>';
    gradient.innerHTML += '<stop offset="1" stop-color="red"/>';
    
    function updateViewBox() {
      if ("x" in options) {
        if ("min" in options.x) { area.x.min = options.x.min; }
        if ("max" in options.x) { area.x.max = options.x.max; }
        if ("count" in options.x) { area.x.min = area.x.max - options.x.count; }
      }
      if ("y" in options) {
        if ("min" in options.y) { area.y.min = options.y.max*-1; }
        if ("max" in options.y) { area.y.max = options.y.min*-1; }
      }
      svg.setAttributeNS(null,"viewBox",[area.x.min,area.y.min,area.x.max - area.x.min,area.y.max - area.y.min].join(" "));
      
      gradient.setAttributeNS(null,"x1",0);
      gradient.setAttributeNS(null,"x2",0);
      if (options.reverseGradient) {
        gradient.setAttributeNS(null,"y1",area.y.max);
        gradient.setAttributeNS(null,"y2",area.y.min);
      }
      else {
        gradient.setAttributeNS(null,"y1",area.y.min);
        gradient.setAttributeNS(null,"y2",area.y.max);
      }
    }
    updateViewBox();
    
    var line = document.createElementNS(ns,"path");
    svg.appendChild(line);
    //line.setAttributeNS(null,"vector-effect","non-scaling-stroke");
    line.setAttributeNS(null,"stroke-width","0.1");
    line.setAttributeNS(null,"fill","none");
    line.setAttributeNS(null,"stroke","url(#"+gradient.getAttribute("id")+")");
    line.setAttributeNS(null,"d","M"+path.join(" L"));
    
    line.addData = function(newData) {
      
      if (isNaN(newData.y)) { return; }
      
      if (options.differentiate) {
        var diff = newData.y - lasty;
        lasty = newData.y;
        newData.y = diff;
      }
      
      path.push([newData.x - x_correction,newData.y*-1].join(","));
      if (options.x && options.x.count) {
        if (path.length > options.x.count) {
          path.shift();
          updateMinMax();
        }
      }
      updateMinMax(newData.x - x_correction,newData.y*-1);
      this.setAttributeNS(null,"d","M"+path.join(" L"));
      updateViewBox();
    };
    svg.addData = function(newData){
      line.addData(newData);
    };
    
    return svg;
  },
  getBrowser: function(){
    var ua = window.navigator.userAgent;
    
    if ((ua.indexOf("MSIE ") >= 0) || (ua.indexOf("Trident/") >= 0)) {
      return "ie";
    }
    if (ua.indexOf("Edge/") >= 0) {
      return "edge";
    }
    if ((ua.indexOf("Opera") >= 0) || (ua.indexOf('OPR') >= 0)) {
      return "opera";
    }
    if (ua.indexOf("Chrome") >= 0) {
      return "chrome";
    }
    if (ua.indexOf("Safari") >= 0) {
      return "safari";
    }
    if (ua.indexOf("Firefox") >= 0) {
      return "firefox";
    }
    return false; //unknown
  },
  getAndroid: function(){
    var match = navigator.userAgent.toLowerCase().match(/android\s([\d\.]*)/i);
    return match ? match[1] : false;
  }
};
