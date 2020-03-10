function MistUI(MistVideo,structure) {
  MistVideo.UI = this;
  this.elements = [];
  
  this.buildStructure = function(structure){
    if (typeof structure == "function") { structure = structure.call(MistVideo); }
    
    if ("if" in structure) {
      var result = false;
      if (structure.if.call(MistVideo,structure)) {
        result = structure.then;
      }
      else if ("else" in structure) {
        result = structure.else;
      }
      
      if (!result) { return; }
      
      //append the result with structure options
      for (var i in structure) {
        if (["if","then","else"].indexOf(i) < 0) {
          if (i in result) {
            if (!(result[i] instanceof Array)) {
              result[i] = [result[i]];
            }
            result[i] = result[i].concat(structure[i]);
          }
          else {
            result[i] = structure[i];
          }
        }
      }
      return this.buildStructure(result);
    }
    
    if ("type" in structure) {
      if (structure.type in MistVideo.skin.blueprints) {
        
        //create the element; making sure to pass "this" to blueprint function
        var container = MistVideo.skin.blueprints[structure.type].call(MistVideo,structure);
        if (!container) { return; }
        MistUtil.class.add(container,"mistvideo-"+structure.type);
        
        if ("css" in structure) {
          var uid = MistUtil.createUnique();
          structure.css = [].concat(structure.css); //convert to array; should be in string format with colors already applied
          
          for (var i in structure.css) {
            var style = MistUtil.css.createStyle(structure.css[i],uid);
            container.appendChild(style);
          }
          MistUtil.class.add(container,uid);
          container.uid = uid;
        }
        
        if ("classes" in structure) {
          for (var i in structure.classes) {
            MistUtil.class.add(container,structure.classes[i]);
          }
        }
        
        if ("title" in structure) {
          container.title = structure.title;
        }
        
        if ("style" in structure) {
          for (var i in structure.style) {
            container.style[i] = structure.style[i];
          }
        }
        
        if ("children" in structure) {
          for (var i in structure.children) {
            var child = this.buildStructure(structure.children[i]);
            if (child) {
              container.appendChild(child);
            }
          }
        }
        //save the returned element so they can be killed on unload
        MistVideo.UI.elements.push(container);
        return container;
      }
    }
    
    return false;
  };
  this.build = function(){
    return this.buildStructure(structure ? structure : MistVideo.skin.structure.main);
  };
  
  var container = this.build();
  
  //apply skin CSS
  var uid = MistUtil.createUnique();
  var loaded = 0;
  if (MistVideo.skin.css.length) { container.style.opacity = 0; }
  for (var i in MistVideo.skin.css) {
    var style = MistVideo.skin.css[i];
    style.callback = function(css) {
      if (css == "/*Failed to load*/") {
        this.textContent = css;
        MistVideo.showError("Failed to load CSS from "+this.getAttribute("data-source"));
      }
      else {
        this.textContent = MistUtil.css.prependClass(css,uid,true);
      }
      loaded++;
      if (MistVideo.skin.css.length <= loaded) {
        container.style.opacity = "";
      }
    };
    if (style.textContent != "") {
      //it has already loaded
      style.callback(style.textContent);
    }
    container.appendChild(style);
  }
  MistUtil.class.add(container,uid);
  
  //add browser class
  var browser = MistUtil.getBrowser();
  if (browser) {
    MistUtil.class.add(container,"browser-"+browser);
  }
  
  return container;
}
