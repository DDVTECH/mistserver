
//Google analytics. Can be fully disabled on request (or, if compiling yourself, add the NOGA=1 cmake flag).
//Is also disabled if the browser has the do not track flag setting turned on.
var oldTab = UI.showTab;
UI.showTab = function(){
  var r = oldTab.apply(this,arguments);
  if ((!navigator.doNotTrack) && (mist.user.loggedin)) {
    UI.elements.main.append(
      $("<img>").attr("src","https://www.google-analytics.com/collect?v=1&tid=UA-32426932-1&cid="+mist.data.config.iid+"&t=pageview&dp="+encodeURIComponent("/MI/"+arguments[0])+"&dh=MI."+(mist.data.LTS ? "Pro" : "OS")).css({width:"1px",height:"1px","min-width":"1px",opacity:0.1,position:"absolute",left:"-1000px"})
    );
  }
  return r;
};

