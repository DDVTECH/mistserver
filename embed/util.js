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
    ago: function(date,range){
      //format a date nicely depending on how long ago it was
      //if the range param [ms] is specified, use that to choose how to format the date string

      if (isNaN(date.getTime())) { return ""; }

      var ago = range ? range : new Date().getTime() - date.getTime();
      var out = "";
      var negative = (ago < 0);

      if (negative) { ago *= -1; }

      if (ago < 1000) {
        //less than a second ago
        out = "live";
      }
      else if (ago < 60e3) {
        //less than a minute ago
        out = Math.round(ago/1e3)+" sec";
        //out = Math.round(ago/10)/100+" sec"; //more detail for debugging purposes
        if (negative) {
          out = "in "+out;
        }
        else {
          out += " ago";
        }
        
      }
      else if ((!range && (new Date().toLocaleDateString() == date.toLocaleDateString())) || (range < 86400e3)) {
        //today
        out = date.toLocaleTimeString(undefined,{
          hour: "numeric",
          minute: "2-digit",
          second: "2-digit"
        });
      }
      else if (ago < 518400e3) {
        //less than 6 days ago
        out = date.toLocaleString(undefined,{
          weekday: "short",
          hour: "numeric",
          minute: "2-digit",
          second: "2-digit"
        });
      }
      else if ((!range && (new Date().getFullYear() == date.getFullYear())) || (range < 31622400e3)) {
        //this year
        out = date.toLocaleString(undefined,{
          month: "short",
          day: "numeric",
          weekday: "short",
          hour: "numeric",
          minute: "2-digit",
          second: "2-digit"
        });
      }
      else {
        //before this year
        out = date.toLocaleString(undefined,{
          year: "numeric",
          month: "short",
          day: "numeric",
          hour: "numeric",
          minute: "2-digit",
          second: "2-digit"
        });
      }

      return out;
    },
    ucFirst: function(string){
      return string.charAt(0).toUpperCase()+string.slice(1);
    },
    number: function(num) {
      if ((isNaN(Number(num))) || (Number(num) == 0)) { return num; }
      
      //rounding
      //use a significance of three, but don't round "visible" digits
      var sig = Math.max(3,Math.ceil(Math.log(Math.abs(num))/Math.LN10));
      var mult = Math.pow(10,sig - Math.floor(Math.log(Math.abs(num))/Math.LN10) - 1);
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
    bytes: function(val,bits){
      if (isNaN(Number(val))) { return val; }
      
      var suffix = bits ? ["bits","Kb","Mb","Gb","Tb","Pb"] : ["bytes","KB","MB","GB","TB","PB"];
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
    bits: function(val) { return this.bytes(val,true); },
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
          return "WebRTC (WS)";
          break;
        }
        case "whep": {
          return "WebRTC (WHEP)";
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
    },
    offer2human: function(str) {
      var arr = str.split("\r\n");
      var out = [];
      var group = [];
      for (var i in arr) {
        var line = arr[i];
        var stripped = line.slice(2);
        switch (line.slice(0,2)) {
          case "m=": {
            out.push(group.join("\r\n"));
            group = [];
          }
        }
        group.push(line);
      }
      return out;
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
    multiSort: function (array,sortby,getEntry) {
      /*
       MistUtil.array.multiSort([].concat(video.info.source),[
        {type: ["html5/video/webm","silverlight"]} or ["type",["html5/video/webm","silverlight"]]
        ,{simul_tracks:-1} or ["simul_tracks",-1]
        ,function(a){ return a.priority * -1; }
        ,"url"
       ]);
      */
      if (!getEntry) getEntry = function(i){
        return i;
      };
      
      var sortfunc = function(a,b){
        if (isNaN(a) || isNaN(b)) {
          return a.localeCompare(b);
        }
        return a > b ? 1 : (a < b ? -1 : 0);
      };
      
      if (!sortby.length) { return array.sort(sortfunc); }
      
      function getValue(key,a) {
        a = getEntry(a);
        
        function parseIt(item,key,sortvalue,getter){
          if (typeof getter == "function") item = getter(item);

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
            return parseIt(a,key[0],key[1],key.length > 2 ? key[2] : null);
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
      append: function(url,append){
        var a = document.createElement("a");
        a.href = url;
        if (append[0] == "?") {
          if (a.search == "") { 
            a.search = append;
          }
          else {
            a.search += "&"+append.slice(1);
          }
        }
        else if (append[0] == "&") {
          if (a.search == "") { 
            a.search = "?"+append.slice(1);
          }
          else {
            a.search += append;
          }
        }
        else {
          a.href += append;
        }
        return a.href;
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
        
        //try to load 3 times, then give up
        var attempts = 3;
        function retry() {
          MistUtil.http.get(url,function(d){
            for (var i in cache[url]) {
              cache[url][i](d);
            }
            cache[url] = d;
          },function(){
            if (attempts > 0) {
              attempts--;
              setTimeout(retry,2e3);
            }
            else {
              var d = "/*Failed to load*/";
              for (var i in cache[url]) {
                cache[url][i](d);
              }
              cache[url] = d;
            }
          });
        }
        retry();
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
      var output;

      //if no callback is passed, asume promise mode
      var promise;
      if (!callback) {
        promise = {};
        promise.p = new Promise(function(resolve,reject){
          promise.resolve = resolve;
          promise.reject = reject;
        });
        callback = function(){
          MistUtil.event.removeListener(output);
          promise.resolve.apply(this,arguments);
        };
      }
      
      DOMelement.addEventListener(type,callback);
      
      if (!storeOnElement) { storeOnElement = DOMelement; }
      if (!("attachedListeners" in storeOnElement)) {
        storeOnElement.attachedListeners = [];
      }
      output = {
        element: DOMelement,
        type: type,
        callback: callback
      };
      
      storeOnElement.attachedListeners.push(output);
      return promise ? promise.p : output;
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
          if (this.list[src].tag.hasLoaded) {
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
      if (onevent.type) { scripttag.setAttribute("type",onevent.type); }
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
              name[j] = Math.round(track.rate*1e-3)+"Khz";
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
    },
    translateCodec: function(track){
      if (track.codecstring) return track.codecstring;
    
      function bin2hex(index) {
        return ("0"+track.init.charCodeAt(index).toString(16)).slice(-2);
      }

      switch (track.codec) {
        case "AAC":
          return "mp4a.40.2";
        case "MP3":
          return "mp3";
          //return "mp4a.40.34";
        case "AC3":
          return "ec-3";
        case "H264":
          return "avc1."+bin2hex(1)+bin2hex(2)+bin2hex(3);
        case "HEVC":
          return "hev1."+bin2hex(1)+bin2hex(6)+bin2hex(7)+bin2hex(8)+bin2hex(9)+bin2hex(10)+bin2hex(11)+bin2hex(12);
        default:
          return track.codec.toLowerCase();
      }
    },
    getSupported: function(trackmeta,source){
      //filters the tracks from the general metadata to show only audio and video tracks that the protocol (source) supports
      if (!trackmeta) throw "Missing track metadata";
      if (!source) throw "Please include the source object";

      var result = {};
      for (var i in trackmeta) {
        var track = trackmeta[i];

        if (track.type == "meta") continue; //we're only interested in audio and video tracks
        if (source && ("sup_trk" in source)) { //if supported, backend reports what tracks can be put into this protocol
          if (MistUtil.array.indexOf(source.sup_trk,track.idx) == -1) continue;
        }

        result[i] = track;
      }
      return result;
    },
    tracktypes: function(tracklist){
      //takes a trackmeta object and returns an array of the track types it contains
      var out = {};
      for (var i in tracklist) {
        var track = tracklist[i];
        out[track.type] = true;
      }
      return MistUtil.object.keys(out);
    }
  },
  isTouchDevice: function(){
    return (('ontouchstart' in window) || (navigator.msMaxTouchPoints > 0));
    //return true;
  },
  getPos: function(element,cursorLocation){
    var pos0 = element.getBoundingClientRect().left - (parseInt(element.borderLeftWidth,10) || 0);
    
    var width = element.getBoundingClientRect().width;
    var perc = Math.max(0,(cursorLocation.clientX - pos0) / width);
    perc = Math.min(perc,1);
    
    return perc;
  },
  
  createGraph: function(data,options){
    if (!options) { options = {}; }

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
        area.y.min = Math.min(area.y.min,y);
        area.y.max = Math.max(area.y.max,y);
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
          updateMinMax(d[0],d[1]);
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
        if ("max" in options.y) { area.y.min = options.y.max*-1; }
        if ("min" in options.y) { area.y.max = options.y.min*-1; }
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
    line.setAttributeNS(null,"vector-effect","non-scaling-stroke");
    line.setAttributeNS(null,"stroke-width","1");
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
  },
  sources: {
    find: function(sources,matchObj){
      /*
        Example use: 
        MistUtil.sources.find(MistVideo.info.source,{
          type: "html5/text/javascript",
          protocol: "wss:"
        })
      */

      outer:
      for (var i in sources) {
        for (var j in matchObj) {
          if (j == "protocol") {
            if (sources[i].url.slice(0,matchObj.protocol.length) != matchObj.protocol) {
              continue outer;
            }
          }
          else {
            if (sources[i][j] != matchObj[j]) {
              continue outer;
            }
          }
        }
        //if any key of matchObj did not match the source, the outer loop was continued and this code does not execute
        return sources[i];
      }
      return false;
    }
  },
  shared: {
    ControlChannel: function(create_channel_func,MistVideo,externalListenersObj){
      /*
        Takes a WebSocket or RTCDataChannel and adds:
        - send: function(msg)
          if the channel is not yet connected, queues the messages
        - addListener: function(type,callback)
          if callback is omitted, returns a promise (for listening for a single message)
          listeners can also be added to the channel state: "channel_open", "channel_close", "channel_error", "channel_timeout"

        externalListeners (optional): object to store listeners that does not get destroyed if the control channel is re-initialized
      */

      var control = this;
      this.locked = false;
      this.channel = typeof create_channel_func == "function" ? create_channel_func() : create_channel_func;
      this.debugging = true;
      this.was_connected = false;
      this.bitCounter = 0;
      var queue = [];
      var listeners = externalListenersObj || {};

      //the api can add listeners for types of messages here
      this.addListener = function(type,callback){
        if (!(type in listeners)) {
          listeners[type] = {};
        }
        var eid = Object.keys(listeners[type]).length;

        if (callback) {
          listeners[type][eid] = callback;
          return eid;
        }
        else {
          var rejecter;
          var promise = new Promise(function(resolve,reject){
            rejecter = reject;
            listeners[type][eid] = function(data){
              delete listeners[type][eid];
              resolve(data);
            };
          });
          promise.removeListener = function(){
            delete listeners[type][eid];
            rejecter("EventListener was removed");
          };
          return promise;
        }
      };
      this.addSendListener = function(type,callback){
        type = "send_"+type;
        return this.addListener(type,callback);
      };
      function callListeners(type,data,full_message) {
        var haveChanged = false;
        if (type in listeners) {
          for (var eid in listeners[type]) {
            try {
              var out = listeners[type][eid].apply(control,[data,full_message]);
              if (out && (type.slice(0,5) == "send_")) {
                //this listener wants to change the message data
                data = out;
                haveChanged = true;
              }
            }
            catch(err) {
              MistVideo.log("Error in "+type+" listener "+eid+": "+err,"error");
              console.warn("🎮",err);
            }
          }
          if (haveChanged) return data;
        }
      }
      this.removeListener = function(type,callback){
        if (type in listeners) {
          if (typeof callback == "function") {
            for (var eid in listeners[type]) {
              if (listeners[type][eid] == callback) {
                delete listeners[type][eid];
                return true;
              }
            }
          }
          else {
            var eid = callback;
            if (eid in listeners[type]) delete listeners[type][eid];
          }
        }
        return false;
      };
      this.removeSendListener = function(type,callback) {
        type = "send_"+type;
        return this.removeListener(type,callback);
      }
      this.lock = function(){
        this.locked = true;
      };
      this.unlock = function(){
        if (this.readyState == "open") {
          if (queue.length) {
            for (var i = 0; i <= queue.length; i++) {
              control.send(queue[i],true);
            }
            queue = [];
          }
        }
        this.locked = false;
        if (this.debugging) console.log("🎮","The control channel was unlocked");
      };

      this.init = function(){
        this.channel.addEventListener("open",function(ev){
          control.was_connected = true;
          callListeners("channel_open",ev);
          if (queue.length && !control.locked) {
            for (var i = 0; i <= queue.length; i++) {
              control.send(queue[i]);
            }
            queue = [];
          }
          if (control.timeout) {
            MistVideo.timers.stop(control.timeout);
          }
        });
        this.timeout = MistVideo.timers.start(function(){
          if (control.readyState == "connecting") {
            MistVideo.log("Control socket timeout","error");
            if (control.debugging) console.log("🎮","The control channel timed out");
            callListeners("channel_timeout",control.channel);
          }
        },5e3);
        this.channel.addEventListener("close",function(ev){
          callListeners("channel_close",ev);
          if (control.debugging) console.log("🎮","The control channel was closed",ev);
          MistVideo.log("The control channel was closed");
          //callListeners("on_stop",ev);
        });
        this.channel.addEventListener("error",function(ev){
          callListeners("channel_error",ev);
          if (control.debugging) console.log("🎮","The control channel threw an error",ev);
          MistVideo.log("The control channel threw an error: "+ev);
        });
        this.channel.addEventListener("message",function(e){
          var message;

          if (typeof e.data == "string") {
            try {
              message = JSON.parse(e.data);
            } catch(err) {
              MistVideo.log("Received invalid control message: "+err+" in "+e.data,"error");
            }

            if (message) {
              var data = "data" in message ? message.data : message; //some control messages do not have the data key, e.g. websocket webrtc's on_answer_sdp
              if (!message.type) {
                MistVideo.log("Received invalid control message: missing type in "+e.data,"error");
              }
              if (control.debugging) console.info("🎮","Received:",message.type,data);
              callListeners(message.type,data,message);
            }
          }
          else {
            var data = new Uint8Array(e.data);
            if (data) {
              callListeners("binary",data);
              control.bitCounter += data.byteLength*8;
            }
          }
        });
      }
      this.init();



      Object.defineProperty(this,"readyState",{
        get: function(){
          var state = this.channel.readyState;
          if (typeof state == "string") return state;
          switch (state) {
            case 0: return "connecting";
            case 1: return "open";
            case 2: return "closing";
            case 3: return "closed";
          }
          return state; //unknown readyState
        }
      });
      Object.defineProperty(this,"connectionState",{
        get: function(){
          return this.readyState;
        }
      });

      function LogServerDelay() {
        var serverDelay = this;
        var delays = [];

        this.log = function(type){
          var responseType = false;
          switch (type) {
            case "seek":
            case "set_speed": {
              //wait for cmd.type
              responseType = type;
              break;
            }
            case "request_codec_data": {
              responseType = "codec_data";
              break;
            }
            case "offer_sdp": {
              responseType = "on_answer_sdp";
              break;
            }
            default: {
              //do nothing
              return;
            }
          }
          if (responseType) {
            var starttime = performance.now();
            control.addListener(responseType).then(function(){
              serverDelay.add(performance.now() - starttime);
            });
          }
        };

        this.add = function(delay){
          delays.unshift(delay);
          if (delays.length > 3) {
            delays.splice(3);
          }
        };

        this.get = function(){
          if (delays.length) {
            //return average of the last recorded delays
            var sum = 0;
            var i = 0;
            for (null; i < delays.length; i++){
              sum += delays[i];
            }
            return sum/i;
          }
          return 500;
        };

        Object.defineProperty(this,"length",{
          get: function(){ return delays.length }
        });
      }
      this.serverDelay = new LogServerDelay();

      this.send = function(cmdObj,bypassLock){
        if (!this.channel || this.readyState != "open") {
          //wait for it to open
          queue.push(cmdObj);
          if (this.debugging) console.warn("🎮","Want to send but control channel is "+this.readyState+". Queue: "+queue.length);
          return;
        }
        if (!bypassLock && this.locked) {
          queue.push(cmdObj);
          if (this.debugging) console.warn("🎮","Want to send but control channel is locked. Queue: "+queue.length,cmdObj);
          return;
        }
        var str;
        try {
          str = JSON.stringify(cmdObj);
        } catch(e) {
          MistVideo.log("Tried to send invalid command: "+e,"error");
        }
        if (str) {
          var out = callListeners("send_"+cmdObj.type,cmdObj);
          if (out) {
            //one of the send listeners changed the message
            try {
              str = JSON.stringify(cmdObj);
            } catch(e) {
              MistVideo.log("Tried to send invalid command: "+e,"error");
            }
          }
          this.channel.send(str);
          this.serverDelay.log(cmdObj.type);
          if (this.debugging) console.warn("🎮","Sent:",cmdObj.type,cmdObj);
        }
      };

      this.addListener("on_error",function(msg){
        callListeners("on_stop",msg);
        MistVideo.showError(msg.message);
      });

      this.close = function(){
        this.channel.close();
      };
      if (typeof create_channel_func == "function") {
        this.reconnect = function(){
          if (this.readyState == "open") {
            this.channel.addEventListener("close",function(){
              control.channel = create_channel_func();
              control.init();
            });
            this.close();
          }
          else {
            this.channel = create_channel_func();
            this.init();
          }
        };
      }
      
    },
    DataChannel2WebSocket: function(){
      /*

        Takes a WebRTC data channel and pretends it is a metadata websocket.
        Note: it is possible MistServer is compiled without datachannel support. If it is available, this is exposed in MistVideo.info.capa.datachannels

        Usage:
         api.metaTrackSocket = new new MistUtil.shared.DataChannel2WebSocket()
       
        Once the datachannel is created, call
         api.metaTrackSocket.init(datachannel);

      */

      this.origin = {};
      this.CONNECTING = 0;
      this.OPEN = 1;
      this.CLOSING = 2;
      this.CLOSED = 3;

      this.debugging = false; //when true, logs metadata received to dev console

      this.readyState = 0;
      //follow readystate of origin, except when the converter is asked to close, then *pretend* to close and remove event listeners.

      this.listeners = [];
      var converter = this;

      this.init = function(datachannel){
        this.origin = datachannel || {};
        if (this.origin._processed) { return true; } //already set up

        if ("readyState" in this.origin) {
          this.origin._processed = true;

          function onopen() {
            converter.readyState = converter.OPEN;
            converter.onopen();
          }

          //for some reason, onopen gets called twice if added with an eventlistener
          this.origin.addEventListener("open",function(){
            onopen();
          });
          //this.origin.onopen = onopen; 

          this.origin.onmessage = function(e){
            if (converter.debugging) console.log("🔀","Received metadata:",JSON.parse(e.data));
          };
          this.origin.addEventListener("close",function(){
            converter.readyState = converter.CLOSED;
            converter.onclose();
          });
          if (this.origin.readyState == "open") { onopen(); }

          return true;
        }
        else {
          return false;
        }
      };

      this.open = function(){
        //should be open once webrtc is active

        if (this.readyState == this.OPEN) return; //already open

        switch (this.origin.readyState) {
          case "connecting":  { this.readyState = this.CONNECTING; break; }
          case "open":        { this.readyState = this.OPEN; break; }
          case "closing":     { this.readyState = this.CLOSING; break; }
          case "closed":      { this.readyState = this.CLOSED; break; }
        }

        for (var i in this.listeners) {
          this.origin.addEventListener.apply(this.origin,this.listeners[i]);
        }
      };
      this.close = function(){
        //don't actually close, but pretend
        if (this.readyState >= this.CLOSING) return; //already closed

        this.readyState = this.CLOSED;

        //remove listeners
        for (var i in this.listeners) {
          this.removeEventListener.apply(this,this.listeners[i]);
        }
      };
      this.send = function(){
        return false; //MistServer ignores control messages that are sent over the metadata channel
        //if (converter.debugging) { console.warn("Sent to metadata:",arguments[0]); }
        //if (this.origin.readyState == "open") return this.origin.send.apply(this.origin,arguments);
      };
      this.onopen = function(){};
      this.onclose = function(){};
      this.addEventListener = function(){
        this.listeners.push(arguments);
        return this.origin.addEventListener.apply(this.origin,arguments);
      };
      this.removeEventListener = function(name,func){
        //remove them from the listeners array and the origin
        for (var i = this.listeners.length-1; i >= 0; i--) {
          if ((name == this.listeners[i][0]) && (func == this.listeners[i][1])) {
            this.listeners.splice(i,1);
            break;
          }
        }
        return this.origin.removeEventListener.apply(this.origin,arguments);
      };

      this.init();

      return this;

    },
    ControlChannelAPI: function(controller,MistVideo,video,custom_funcs){

      /*

       Takes a (WebRTC) controller object and video DOMelement and exposes MistPlayer api functions
       The controller object must have:
       {
         connection: {
           connectionState: "connected|failed|closed"
         } for example instanceof RTCPeerConnection, (required)
         connecting: false or instanceof Promise (required), set by .connect()
         control: instanceof ControlChannel, (required)
              should also attempt to reconnect the channel if closed etc
         connect: function(){}, (required)
              initializes the data and control connection, returns a promise
         meta: instanceof RTCDataChannel (optional)
       }

       Usage:
         api = new ControlChannelAPI();

      */


      var api = this;
      var control = controller.control;


      function defineProperty(index,descriptor) {
        if (typeof descriptor == "function") {
          api[index] = descriptor;
          return;
        }
        var opts = MistUtil.object.extend({
          configurable: true,
          enumerable: true
        },descriptor);
        Object.defineProperty(api,index,opts);
      }


      video.setAttribute("playsinline",""); //iphones. effin' iphones.

      //apply options
      var attrs = ["autoplay","loop","poster"];
      for (var i in attrs) {
        var attr = attrs[i];
        if (MistVideo.options[attr]) {
          video.setAttribute(attr,(MistVideo.options[attr] === true ? "" : MistVideo.options[attr]));
        }
      }
      if (MistVideo.options.muted) {
        video.muted = true; //don't use attribute because of Chrome bug
      }
      if (MistVideo.info.type == "live") {
        video.loop = false;
      }
      if (MistVideo.options.controls == "stock") {
        video.setAttribute("controls","");
      }
      video.setAttribute("crossorigin","anonymous");

      //redirect properties
      ["volume"
        ,"muted"
        ,"loop"
        ,"paused"
        ,"error"
        ,"buffered"
        ,"textTracks"
        ,"webkitDroppedFrameCount"
        ,"webkitDecodedFrameCount"
      ].forEach(function(item){
        defineProperty(item,{
          get: function(){ return video[item]; },
          set: function(value){
            return video[item] = value;
          }
        });
      });

      //redirect methods
      ["load","getVideoPlaybackQuality"].forEach(function(item){
        if (item in video) {
          api[item] = function(){
            return video[item].call(video,arguments);
          };
        }
      });

      //other properties and methods that cannot be copied one on one
      this.play = function(){
        if (controller.connection){
          switch (controller.connection.connectionState) {
            case "open":
            case "connected": {
              controller.control.send({type:"play"});
              return video.play();
            }
            case "failed":
            case "closed": {
              return new Promise(function(resolve,reject){
                controller.connect().then(function(){
                  video.play().then(resolve).catch(reject);
                }).catch(reject);
              })
            }
            default: {
              //we're still connecting - wait
              //=> bubble down to controller.connection == false
            }
          }

        }

        //we're not connected
        return new Promise(function(resolve,reject){
          if (!controller.connecting) {
            MistVideo.log("Received call to play while not connected, connecting..");
            controller.connect();
          }
          else {
            MistVideo.log("Received call to play while still connecting, waiting..");
          }

          if (controller.connecting) {
            controller.connecting.then(resolve).catch(reject);
          }
          else {
            //the connecting Promise should exist after the call to connect, this code should not be triggered
            reject();
          }
        });

      }

      this.pause = function(){
        return new Promise(function(resolve,reject){
          try {
            video.pause();
            controller.control.send({type: "hold"});
            resolve();
          }
          catch(err) {
            reject(err);
          }
        });
      }
      controller.control.addListener("pause",function(msg,m){
        if (msg.reason && (msg.reason == "at_dead_point")) {
          //we're running out of the buffer for some reason - attempt to fix it by seeking ahead
          MistVideo.log("At dead point: seeking to return into buffer.");
          //NB: when sending both set_speed and seek, set_speed must be sent first
          if (play_rate < 1) { 
            //reset speed to real time
            controller.control.send({type:"seek",seek_time:msg.begin+1000});
            controller.control.send({type:"set_speed",play_rate:"auto"});
          }
          else {
            controller.control.send({type:"seek",seek_time:msg.begin+5e3});
            //controller.control.send({type:"seek",seek_time:"live"});
          }
          return;
        }
        if (m.paused) video.pause();
      });
      this.stop = function(){
        return new Promise(function(resolve,reject){
          try {
            video.pause();
            controller.control.send({type: "stop"});
            resolve();
          }
          catch(err) {
            reject(err);
          }
        });
      }

      var seekoffset = 0, last_on_time, duration, play_rate, currenttracks = [], looping = false;

      //set generic on_time handler
      controller.control.addListener("on_time",function(msg){
        last_on_time = msg;
        last_on_time._received = new Date();

        //save currentTime offset
        seekoffset = msg.current*1e-3 - video.currentTime;

        //save duration
        var d = (msg.end == 0 ? Infinity : msg.end*1e-3);
        if (d != duration) {
          duration = d;
          MistUtil.event.send("durationchange",d,video);
        }

        //save buffer
        if (MistVideo.info && MistVideo.info.meta) MistVideo.info.meta.buffer_window = msg.end - msg.begin;

        //save playback speed
        play_rate = msg.play_rate_curr;

        //save which tracks are playing and monitor changes
        if ((msg.tracks) && (currenttracks.join(",") != msg.tracks.join(","))) {
          var tracks = MistVideo.info ? MistUtil.tracks.parse(MistVideo.info.meta.tracks) : [];
          for (var i in msg.tracks) {
            if (currenttracks.indexOf(msg.tracks[i]) < 0) {
              //find track type
              var type;
              for (var j in tracks) {
                if (msg.tracks[i] in tracks[j]) {
                  type = j;
                  break;
                }
              }
              if (!type) {
                //track type not found, this should not happen
                continue;
              }
              if (type == "subtitle") { continue; }

              //create an event to pass this to the skin
              MistUtil.event.send("playerUpdate_trackChanged",{
                type: type,
                trackid: msg.tracks[i]
              },video);
            }
          }

          currenttracks = msg.tracks;
        }

        if (MistVideo.reporting && msg.tracks) {
          MistVideo.reporting.stats.d.tracks = msg.tracks;
        }

      });

      //looping
      api.stream_end = function(){
        MistUtil.event.send("ended",null,video);
      };
      api.restart = function(){
        if (!looping) {
          looping = true;
          seekoffset = 0;
          //console.warn("set seekoffset to zero")
          MistVideo.log("Looping..");
          //console.warn(controller.connection.connectionState);
          var result = controller.close();
          if (result instanceof Promise) {
            result.then(function(){
              if (controller.debugging) console.warn("[Looping] Controller closed, reconnecting..");
              return controller.connect();
            }).then(function(){
              looping = false;
              if (controller.debugging) console.warn("[Looping] Complete");
            }).catch(function(err){
              MistVideo.showError("Looping failed: "+err)
            });
          }
          else {
            controller.connect().then(function(){
              looping = false;
              if (controller.debugging) console.warn("[Looping] Complete");
            }).catch(function(err){
              MistVideo.showError("Looping failed: "+err)
            });
          }
        }
      };
      controller.control.addListener("on_stop",function(msg){
        if (api.buffered.length) {
          if (api.buffered.end(api.buffered.length-1) > api.currentTime) {
            //using a timer ensures this will always fire even if playback stops prematurely :)
            var left = api.buffered.end(api.buffered.length-1) - api.currentTime;
            left /= api.playbackRate;
            if (controller.debugging) console.warn("Received on_stop, waiting ",left,"s for buffer to play out");
            MistVideo.timers.start(function(){
              api.stream_end();
              if (api.loop) {
                api.restart();
              }
              else {
                video.pause();
              }
            },left*1e3);
            return;
          }
        }
        api.stream_end();
        if (api.loop) {
          api.restart();
        }
        else {
          video.pause();
        }
      });

      //override seeking
      var override_timestamp = false;
      defineProperty("currentTime",{
        get: function(){
          if (override_timestamp !== false) {
            return override_timestamp;
          }
          return seekoffset + video.currentTime;
        },
        set: function(value){
          MistUtil.event.send("seeking",value,video);
          var to = (value == "live" ? Infinity : value);

          //immediately place playback cursor at seek point
          override_timestamp = to;

          controller.control.send({
            type: "seek",
            "seek_time": (value == "live" ? "live" : value*1e3)
          });
          controller.control.addListener("seek").then(function(msg){
            //the message "seek" was received
            return controller.control.addListener("on_time");
          }).then(function(msg){
            //the next "on_time" message was received

            //seekoffset is set in the generic on_time handler, but the data hasn't been added to the video yet = therefore seekoffset will be wrong
            //console.warn("current seekoffset (seeked)",seekoffset,api.currentTime);

            MistUtil.event.addListener(video,"timeupdate").then(function(){
              seekoffset = 0;
              override_timestamp = false;
            });
            MistUtil.event.send("seeked",seekoffset,video);

            return video.play();
          }).catch(function(){
            //do nothing
          });
        }
      });

      //duration
      defineProperty("duration",{
        get: function(){
          if (MistVideo.info.type == "live") {
            return duration + (last_on_time ? new Date().getTime() - last_on_time._received.getTime() : 0)*1e-3;
          }
          return duration; 
        }
      });


      //playbackrate
      controller.control.addListener("set_speed",function(msg){
        play_rate = msg.play_rate_curr;
      });
      defineProperty("playbackRate",{
        get: function(){
          if (play_rate) {
            switch (play_rate) {
              case "auto":
              case "fast-forward": {
                //MistServer is controlling the playback rate
                //fast forwards means it is speeding now but it will return to auto once the requested position is reached (after seeking)
                return 1;
              }
              default: {
                return play_rate;
              }
            }
          }
          return 1;
        },
        set: function(value){
          if (value == 1) {
            if ((MistVideo.info.type != "live") || (MistVideo.options.liveCatchup && (last_on_time.end - last_on_time.current < MistVideo.options.liveCatchup*1e3))) {
              value = "auto";
            }
          }
          control.send({
            type: "set_speed",
            play_rate: value
          });
        }
      });

      //setTracks
      this.setTracks = function(obj){
        obj.type = "tracks";
        control.send(obj);
      };

      //getStats
      if (window.RTCPeerConnection && (controller.connection instanceof RTCPeerConnection)) {
        this.getStats = function(){
          if (controller && controller.connection && controller.connection.connectionState == "connected") {
            return controller.connection.getStats().then(function(a){
              var r = {
                audio: null,
                video: null
              };
              var obj = Object.fromEntries(a);
              for (var i in obj) {
                var s = obj[i];
                switch (s.type) {
                  case "inbound-rtp": {
                    r[s.kind] = s;
                    break;
                  }
                  case "data-channel": {
                    var label;
                    switch (s.label) {
                      case "MistControl": { label = "Control Channel";  break; }
                      case "*":           { label = "Metadata Channel"; break; }
                      default:            { label = s.label; }
                    }
                    r[label] = s;
                    break;
                  }
                }
              }
              return r;
            });
          }
        };

        if ("decodingIssues" in MistVideo.skin.blueprints) {
          //get additional dev stats
          var vars = ["nackCount","pliCount","packetsLost","packetsReceived","bytesReceived","messagesReceived","messagesSent"];
          for (var j in vars) {
            api[vars[j]] = {};
          }
          api.jitterDelay = {};
          var last_stats;
          var f = function() {
            MistVideo.timers.start(function(){
              var stats = api.getStats();
              if (stats) {
                stats.then(function(d){
                  for (var i in vars) {
                    var v = vars[i];
                    var out = {}
                    for (var channel in d) {
                      if (d[channel] && (v in d[channel])) out[channel] = d[channel][v];
                    }
                    if (Object.keys(out).length) {
                      api[v] = out;
                    }
                  }

                  api.jitterDelay = {};
                  if (last_stats) {
                    for (var channel in d) {
                      if ((channel in last_stats) && last_stats[channel] && d[channel] && ("jitterBufferDelay" in d[channel])) {
                        api.jitterDelay[channel] = (d[channel].jitterBufferDelay - last_stats[channel].jitterBufferDelay) / (d[channel].jitterBufferEmittedCount - last_stats[channel].jitterBufferEmittedCount); 
                        //average jitterDelay [s] between the last time stats were checked
                      }
                    }
                  }

                  last_stats = d;
                });
              }
              f();
            },1e3);
          };
          f();

          this.getLatency = function() {
            return api.jitterDelay;
          }
        }

      }

      //metaTrackSocket: use datachannel instead of (another) websocket
      if (controller.meta && MistVideo.info && MistVideo.info.capa && MistVideo.info.capa.datachannels) {
        this.metaTrackSocket = function() {
          var converter = MistUtil.shared.DataChannel2WebSocket();
          if (controller.meta) converter.init(controller.meta);
          else if (controller.connecting) {
            controller.connecting.then(function(){
              converter.init(controller.meta);
            });
          }
          else {
            MistVideo.log("Failed to attach to MetaTrack datachannel","error");
          }

          //live passthrough of the debugging flag
          Object.defineProperty(converter,"debugging",{
            configurable: true,
            get: function(){
              return MistVideo.player.debugging; 
            }
          });

          return converter;
        }
      }

      // ABR_resize
      /*this.ABR_resize = function(size){
        MistVideo.log("Requesting the video track with the resolution that best matches the player size");
        this.setTracks({video:"~"+[size.width,size.height].join("x")});
      };*/ //TODO restore for webRTC

      //relay server delay stats if applicable
      if ("serverDelay" in controller.control) {
        Object.defineProperty(this,"server_delay",{
          configurable: true,
          get: function() {
            return controller.control.serverDelay.get();
          }
        });
      }

      //return latest on_time information
      Object.defineProperty(api,"on_time",{
        get: function(){
          if (MistVideo.info.type == "live") {
            //provide a sliding window for "begin" and "end"
            function OnTime(orig){
              var me = this;
              function wrap(key) {
                switch (key) {
                  case "begin":
                  case "end": {
                    Object.defineProperty(me,key,{
                      get: function(){
                        return orig[key] + (new Date().getTime() - last_on_time._received.getTime());
                      }
                    });
                    break;
                  }
                  default: {
                    me[key] = orig[key];
                  }
                }
              }

              for (var key in orig) {
                wrap(key);
              }
            }
            return new OnTime(last_on_time);
          }
          return last_on_time; 
        }
      });

      // unload
      this.unload = function(){
        controller.control.send({type: "stop"});
        controller.connection.close();
      };

      this.setSize = function(size){
        video.style.width = size.width+"px";
        video.style.height = size.height+"px";
      };

      if (custom_funcs) {
        for (var i in custom_funcs) {
          defineProperty(i,custom_funcs[i]);
        }
      }


    },
    BufferManager: function(controlChannel,MistVideo,video,get){
      /*
       requires:
        - get.desiredBuffer()  : either instanceof DesiredBuffer or returns value in ms
        - get.buffer()         : returns value in ms

       optional:
        - get.keepAwayDecay = [ms]   : value with which keepAway is decreased (to a minimum of 0) every time on_time is received while timing.speed.tweak >= 1
        - get.keepAwayPenalty = [ms] : value with which keepAway is increased every time the buffer is empty/waiting (defaults to 100)
        - get.setPlaybackRate(value) : function to use to set video playbackRate
      */
      var manager = this;
      this.settings = {
        bounds: { //if the buffer <> desiredBuffer*bounds[low,high], take action
          low: 0.6,
          high: 2
        },
        actions: { //action to take when the bounds are reached
          faster: 1.05,
          slower: 0.98
        }
      };
      function TimeControl(){
        this.speed = {
          main: 1,
          tweak: 1,
          combined: 1
        };
        this.tweakSpeed = function(tweak){
          this.setSpeed(this.speed.main,tweak);
        };
        this.setSpeed = function(speed,tweak){
          if (!tweak) tweak = this.speed.tweak;

          if ((speed == this.speed.main) && (tweak == this.speed.tweak)) return; //nothing to do

          var combinedSpeed = speed*tweak;
          //video.playbackRate = combinedSpeed;
          get.setPlaybackRate(combinedSpeed);
          if (this.speed.main != speed) {
            MistUtil.event.send("ratechange",speed,video);
          }

          this.speed.main = speed;
          this.speed.tweak = tweak;
          this.speed.combined = combinedSpeed;

        };
      }
      this.timing = new TimeControl();
      var bounds = this.settings.bounds;
      var actions = this.settings.actions;
      var timing = this.timing;
      var state = {  //what the buffer manager is currently doing
        seeking: false,
        pending: false
      };
      Object.defineProperty(this,"state",{
        get: function(){
          if (state.seeking) return "seeking";
          if (state.pending) return "requesting more data";
          if (timing.speed.tweak == 1) return "ok";
          if (timing.speed.tweak > 1) return "catching up";
          return "backing off";
        }
      });
      if (!get.desiredBuffer) {
        get.desiredBuffer = new MistUtil.shared.DesiredBuffer({
          base: 500,
          keepAway: 500,
          serverDelay: controlChannel.serverDelay.get
        });
      }
      this.desiredBuffer = get.desiredBuffer;
      this.buffer = get.buffer;
      if (!get.setPlaybackRate) {
        get.setPlaybackRate = function(value){
          return video.playbackRate = value;
        };
      }
      var listeners = {
        "buffer_ok": [],
        "buffer_low": [],
        "buffer_high": []
      }
      this.addListener = function(type,func){
        if (!type in listeners) throw "Not an event type: "+type; return;
        listeners[type] = func;
      };
      this.removeListener = function(type,func){
        if (!type in listeners) throw "Not an event type: "+type; return;
        var index = listeners[type].indexOf(func);
        if (index < 0) return false;
        listeners[type].splice(index,1);
        return true;
      };
      function emit(type) {
        if (!type in listeners) throw "Not an event type: "+type; return;
        for (var i in listeners[type]) {
          listeners[type][i]();
        }
      }

      //listen to set_speed
      controlChannel.addListener("set_speed",function(msg){
        var speed;
        switch (msg.play_rate_curr) {
          case "auto": {
            //MistServer is controlling the playback rate
            speed = 1;
            break;
          }
          case "fast-forward": {
            //fast forwards means it is speeding now but it will return to auto once the requested position is reached (after seeking)
            return;
          }
          default: {
            speed = msg.play_rate_curr;
            break;
          }
        }
        manager.timing.setSpeed(speed);
      });
      //show the main speed as the current api.playbackRate
      if (MistVideo.player.api) {
        Object.defineProperty(MistVideo.player.api,"playbackRate",{
          get: function(){ return manager.timing.speed.main; }
        });
      }
      else {
        MistVideo.player.onready(function(){
          Object.defineProperty(MistVideo.player.api,"playbackRate",{
            get: function(){ return manager.timing.speed.main; }
          });
        });
      }

      //listen for seeks and add desiredBuffer
      controlChannel.addSendListener("seek",function(msg){
        state.seeking = true;
        if (!msg.ff_add) {
          msg.ff_add = Math.round(get.desiredBuffer); //NB: ensure cast to number
        }
        return msg; //return modified message object
      });
      MistUtil.event.addListener(video,"seeked",function(){
        state.seeking = false;
      });
      //also add desiredBuffer to play commands
      controlChannel.addSendListener("play",function(msg){
        if (!msg.ff_add) {
          msg.ff_add = Math.round(get.desiredBuffer); //NB: ensure cast to number
          if (!msg.ff_add) return;
          return msg; //return modified message object
        }
      });



      //listen to on_time
      controlChannel.addListener("on_time",function(msg){
        var buffer = get.buffer();
        var desired = typeof get.desiredBuffer == "function" ? get.desiredBuffer() : get.desiredBuffer;
        if ((buffer !== null) && !state.seeking && !state.pending) {
          //if the buffer is known, and we're not in the middle of a seek or additional data request

          if ((buffer < desired*bounds.low) && (msg.play_rate_curr != "fast-forward") && (timing.speed.tweak >= 1)) { //the buffer is low
            
            if (msg.current < msg.end) { //there is more data in MistServer's buffer: request more data
              state.pending = true;
              controlChannel.send({
                type: "fast_forward",
                ff_add: desired+0 //+0 ensures cast to number
              });
              if (timing.speed.tweak > 1) {
                timing.tweakSpeed(1);
              }
              MistVideo.log("Our buffer ("+Math.round(buffer)+"ms) is small (<"+Math.round(desired*bounds.low)+"ms), requesting more data (+"+Math.round(desired)+"ms)..");

              //test if we received enough data
              var gotsetspeed = false;
              controlChannel.addListener("set_speed").then(function(m){
                gotsetspeed = true;
                if (m.play_rate_prev == "fast-forward") {
                  controlChannel.addListener("on_time").then(function(m){
                    var increase = m.current - msg.current - (m._received - msg._received);
                    //if (main.debugging) console.warn("▶️","Extra buffer received:",m.current - msg.current,"ms","Time taken:",m._received - msg._received,"ms","Increase:",increase,"ms");
                    if (buffer + increase < desired*bounds.low) {
                      timing.tweakSpeed(actions.slower);
                      if (typeof desiredBuffer == "object") desiredBuffer.factors.keepAway += get.keepAwayPenalty || 100;
                      MistVideo.log("Didn't receive enough extra data to increase our buffer ("+increase+"/"+Math.round(desired*bounds.low - buffer)+"ms): slowing down..");
                      emit("buffer_low");
                      //once slowed down, the fast_forward request code will not trigger
                      //it may be tried again if the buffer shrinks again after playback speed returned to 1
                    }
                    else {
                      MistVideo.log("Received +"+increase+"ms extra data")
                    }
                    state.pending = false;
                  });
                }
                else {
                  //eh? reset
                  state.pending = false;
                }
              });
              //it's possible we don't receive a set_speed answer - in that case there is no extra data available
              controlChannel.addListener("on_time").then(function(m){
                if (gotsetspeed) return;

                if (state.pending && (m.play_rate_curr != "fast-forward")) {
                  state.pending = false;
                  timing.tweakSpeed(actions.slower);
                  if (typeof desiredBuffer == "object") desiredBuffer.factors.keepAway += get.keepAwayPenalty || 100;
                  MistVideo.log("Didn't receive extra data: slowing down..");
                  emit("buffer_low");
                }
              });
            }
            else { //(msg.current >= msg.end) these is no data in MistServer's buffer
              if (timing.speed.main > 1) {
                //if main playback speed is faster than real time, reset it to 1
                controlChannel.send({type:"set_speed",play_rate:"auto"});
              }
              timing.tweakSpeed(actions.slower);
              MistVideo.log("Our buffer ("+Math.round(buffer)+"ms) is small (<"+Math.round(desired*bounds.low)+"ms), but can't request more data: slowing down..");
              emit("buffer_low");
            }

          }
          else {
            if ((timing.speed.tweak < 1) && (buffer >= desired)) {
              timing.tweakSpeed(1);
              MistVideo.log("Our buffer ("+Math.round(buffer)+"ms) is large enough (>"+Math.round(desired)+"ms), so return to normal playback.");
              emit("buffer_ok");
            }
            else {
              if ((MistVideo.info.type == "live") && (MistVideo.options.liveCatchup)) { //in an else to prevent sending fast_forward more than once

                //if the buffer is large, tweak playback speed to catch up
                if ((msg.play_rate_curr == "auto") && timing) {
                  if ((timing.speed.tweak <= 1) && (buffer > desired*bounds.high)) {
                    timing.tweakSpeed(actions.faster);
                    MistVideo.log("Our buffer ("+Math.round(buffer)+"ms) is big (>"+Math.round(desired*bounds.high)+"ms), so tweak the playback speed to catch up.");
                    emit("buffer_high");

                  }
                  else if ((timing.speed.tweak > 1) && (buffer <= desired)) {
                    timing.tweakSpeed(1);
                    MistVideo.log("Our buffer ("+Math.round(buffer)+"ms) is small enough (<"+Math.round(desired)+"ms), so return to normal playback.");
                    emit("buffer_ok");
                  }
                }

                //live catchup
                if (msg.play_rate_curr != "fast-forward") {
                  var distanceToLive = msg.end - msg.current;
                  if (
                    (distanceToLive < MistVideo.options.liveCatchup*1e3)  // we're within a minute of the live point
                    && (distanceToLive > Math.max(msg.jitter*1.1,msg.jitter+250)) // the current (download) timestamp is more than jitter*1.1 and jitter+250 away from the live point
                    && (buffer-desired < 1e3) // our buffer is less than a second larger than the desired buffer size
                  ) {
                    controlChannel.send({
                      type: "fast_forward",
                      ff_add: 5e3 //request an additional 5 seconds of data
                    });
                    MistVideo.log("We're away ("+(distanceToLive)+"ms) from the live point, requesting more data..");
                  }
                }
              }
            }
          }
        }

        if (get.keepAwayDecay && (timing.speed.tweak >= 1) && (typeof desired == "object")) {
          desired.factors.keepAway = Math.max(0,desired.factors.keepAway - get.keepAwayDecay);
          //console.log("keepAway",desired.factors.keepAway);
        }
      });

    },
    DesiredBuffer: function(factors,clamp){
      /*
       - factors:  should be an object of additive factors for the desired buffer. A factor may be a value or a function that returns a value [ms]
                   e.g. {
                      base: 100,
                      keepAway: 500,
                      serverDelay: controlChannel.serverDelay.get
                   }
       - clamp:    optional; should be an object with min and/or max keys, with factors which which the desired buffer should be clamped. These factors may also be a value or a function
                    e.g. {
                      min: {
                        maxFrameDuration: function(){ return value_in_ms; }
                      }
                    }
      */
      this.factors = {}; 
      this.clamp = null;
      this.get = function(){
        var out = 0;
        for (var i in this.factors) {
          out += this.factors[i];
        }
        if (this.clamp) {
          var min, max;
          if ("min" in this.clamp) {
            min = Math.max.apply(null,Object.values(this.clamp.min));
            out = Math.max(out,min);
          }
          if ("max" in this.clamp) {
            max = Math.min.apply(null,Object.values(this.clamp.max));
            out = Math.min(out,max);
          }
          if ((typeof min != "undefined") && (typeof max != "undefined") && (max > min)) {
            throw "Minimum desired buffer is higher than maximum desired buffer";
          }
        }
        return out;
      };
      function add(target,name,getter) {
        if (typeof getter == "function") {
          Object.defineProperty(target,name,{ get: getter, enumerable: true });
        }
        else {
          target[name] = getter;
        }
      }
      this.addFactor = function(name,getter) {
        return add(this.factors,name,getter);
        /*
        if (typeof getter == "function") {
          Object.defineProperty(this.factors,name,{ get: getter, enumerable: true });
        }
        else {
          this.factors[name] = getter;
        }*/
      };
      if (factors) {
        for (var i in factors) {
          this.addFactor(i,factors[i]);
        }
      }
      if (clamp) {
        this.clamp = {};
        if ("min" in clamp) {
          this.clamp.min = {};
          for (var i in clamp.min) {
            add(this.clamp.min,i,clamp.min[i]);
          }
        }
        if ("max" in clamp) {
          this.clamp.max = {};
          for (var i in clamp.max) {
            add(this.clamp.max,i,clamp.max[i]);
          }
        }
      }

      this.valueOf = function(){ return this.get(); };
      this.toString = function(){ return this.get(); };
      Object.defineProperty(this,"value",{get: this.valueOf});
    },
    ABRController: function(MistVideo,getter,threshold){
      /* getter should contain:
       * - .bitCounter() [bits] (if missing, automatic bitrate ABR is disabled, but it can still be called with ABR.request("bitrate"));
       *
       * NB: some outside event should increase this.badness: once it is > threshold, the trackrequest will be the current bitrate
      */

      var ABR = this;
      if (!threshold) threshold = 3;
      var api = MistVideo.player.api;
      this.current = {
        size: null,
        bitrate: null
      };
      var current = this.current;

      this.request = function(type,value){
        current[type] = value;

        var request = ["!jpeg"];
        if (current.bitrate !== null) {
          var req = current.bitrate / MistVideo.api.playbackRate; //correct for playback speed
          try {
            //subtract bps of current audio track
            var trackidx = MistVideo.reporting.stats.d.tracks;
            var meta = MistVideo.info.meta.tracks;
            for (var i in meta) {
              var t = meta[i];
              if ((t.type == "audio") && (trackidxs.indexOf(t.idx) > -1)) {
                req -= t.bps;
                break;
              }
            }
          } catch(e) {}
          if (req <= 0) request.push("minbps");
          else request.push("<"+Math.round(req)+"bps,minbps");
        }
        if (current.size !== null) {
          request.push("~"+[current.size.width,current.size.height].join("x"));
        }
        else {
          request.push("maxres");
        }

        return api.setTracks({
          video: request.join(",|")
        });
      };

      function BitMonitor(){
        var bm = this;
        this.history = [];
        this.since = [];
        this.peak = 0;
        Object.defineProperty(bm,"current",{
          get: function(){
            if (!bm.history.length) return null;
            
            var dbits = getter.bitCounter() - bm.history[0];
            var dt = now() - bm.since[0];
            if (dt == 0) dt = 1; //just in case :)

            return dbits / dt;
          }
        });
        var now = function(){
          return new Date().getTime()*1e-3;
        };
        if ("now" in performance) {
          now = function(){
            return performance.now()*1e-3;
          };
        }
          
        this.logBitRate = function(){
          this.history.push(getter.bitCounter());
          this.since.push(now());

          if (this.history.length > 3) {
            this.history.shift();
            this.since.shift();
            this.peak = Math.max(this.peak,this.current);
          }
        };
        function timer() {
          MistVideo.timers.start(function(){
            bm.logBitRate();
            timer();
          },500);
        }
        timer();

        //for stats display
        api.currentBps = function(){ return bm.current; }
        api.maxBps = this.peak;
        Object.defineProperty(api,"maxBps",{ get: function(){ return bm.peak; } });
      }
      if ("bitCounter" in getter) this.bitMonitor = new BitMonitor();

      // When this.badness is above threshold, the track bitrate should be decreased
      // this.badness should be increased by some outside event 
      //   for example:
      //   BufferManager.addListener("buffer_low",function(){ ABRController.badness++; }));

      var badness = 0;
      Object.defineProperty(this,"badness",{
        get: function(){ return badness; },
        set: function(value){
          badness = value;

          if (!MistVideo.options.ABR_bitrate) return; //bitrate based ABR is currently off
          if (MistVideo.options.setTracks && MistVideo.options.setTracks.video) {
            //a video track was selected by the user, do not change it
            return;
          }
          if (badness > threshold) {
            MistVideo.log("ABR threshold triggered, requesting lower quality");
            ABR.request("bitrate",this.bitMonitor.current);
            badness = 0;
          }
        }
      });

      //TODO it would be possible to "test" higher bitrates by requesting a fast forward, unless we're very close to live
      //maybe use this to up the bps request when the stream has been stable for a while?
      //if current.bitrate != null, the track bitrate was (attempted to get) limited

      api.ABR_resize = function(size){
        if (size.width + size.height <= 0) return;
        MistVideo.log("Requesting the video track with the resolution that best matches the player size");
        ABR.request("size",size);
      };
    },
    testMediaSource: function(trackmeta) {
      //for each track in the track meta object, test if MediaSource can play it

      if (!window.MediaSource) { return {}; }
      if (!MediaSource.isTypeSupported) { return true; } //we can't ask

      var out = {};
      for (var i in trackmeta) {
        var track = trackmeta[i];
        var codec = MistUtil.tracks.translateCodec(track);

        if (MediaSource.isTypeSupported(track.type+"/mp4;codecs=\""+codec+"\"")) {
          out[i] = track;
        }
      }

      return out;
    },
    testRTC: function(trackmeta) {
      //for each track in the track meta object, test if RTCRttpReceiver supports it
      var check = {
        audio: {},
        video: {}
      };
      for (var type in check) {
        var supported = RTCRtpReceiver.getCapabilities(type).codecs;
        for (var i in supported) {
          var codec = supported[i].mimeType.toLowerCase();
          codec = codec.replace(type+"/","");
          check[type][codec] = true;
        }
      }

      var out = {};
      for (var i in trackmeta) {
        var track = trackmeta[i];
        var codec = track.codec;
        if (codec == "HEVC") codec = "H265";

        if (check[track.type][codec.toLowerCase()]) {
          out[i] = track;
        }
      }
      return out;
    }
  }
};
