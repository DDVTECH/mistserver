/* MistServer LSP internationalization runtime.
 *
 * Source-string-as-key (gettext style): the English string is the msgid, so a
 * missing catalog entry simply falls back to the source string and English
 * needs no catalog at all.
 *
 * This file is concatenated *before* mist.js, so it must not touch UI/mist.
 * mist.js hooks it up as UI.lang.
 */

var MistLang = {
  //Currently active language code, "en" means "no catalog, use the msgids".
  current: "en",
  //msgid -> msgstr (string, or array of plural forms)
  catalog: {},
  //code -> native language name, filled in by loadIndex(). English is implicit.
  available: {"en": "English"},
  //Set to true once the index has been fetched (successfully or not)
  indexLoaded: false,
  //Callbacks to run whenever the active language changes
  onchange: [],

  /* Where to look for catalogs. When the panel is served by MistController the
   * first location applies; the second one is the dev layout (lsp/index.html
   * next to lsp/lang/). */
  paths: ["translations/","lang/"],

  //gettext-style plural selector. Only the ones we ship are listed; anything
  //else falls back to the English/Germanic rule.
  pluralRules: {
    "en": function(n){ return n != 1 ? 1 : 0; },
    "de": function(n){ return n != 1 ? 1 : 0; }
  },

  isEnglish: function(code){
    return (!code) || (code == "en") || (code.slice(0,3) == "en-");
  },

  /* Look up a msgid. Returns the msgid itself when there is no translation.
   * Non-strings are passed through untouched so this is safe to use as a
   * blanket pass-through on values that may be jQuery objects or numbers. */
  lookup: function(msgid){
    if (typeof msgid != "string") { return msgid; }
    var hit = this.catalog[msgid];
    if (hit === undefined) { return msgid; }
    if (hit instanceof Array) { hit = hit[0]; }
    return (hit === "" || hit === undefined ? msgid : hit);
  },

  lookupPlural: function(singular,plural,n){
    var hit = this.catalog[singular];
    if (hit instanceof Array) {
      var rule = this.pluralRules[this.current] || this.pluralRules[this.current.split("-")[0]] || this.pluralRules["en"];
      var idx = rule(n);
      if ((idx < hit.length) && (hit[idx] !== "")) { return hit[idx]; }
    }
    else if ((typeof hit == "string") && (hit !== "") && (n == 1)) {
      return hit;
    }
    return (n == 1 ? singular : plural);
  },

  /* printf-ish interpolation. Supports %s (sequential), %1$s (positional) and
   * %% for a literal percent sign. Anything else is left alone. */
  format: function(str,args){
    if ((typeof str != "string") || !args || !args.length) { return str; }
    var next = 0;
    return str.replace(/%(?:(\d+)\$)?([%s])/g,function(match,pos,kind){
      if (kind == "%") { return "%"; }
      var val = (pos ? args[Number(pos)-1] : args[next++]);
      return (val === undefined || val === null ? "" : String(val));
    });
  },

  /* Fetch <path><code>.json, trying each configured path in order. */
  fetchCatalog: function(code,callback){
    var paths = this.paths.slice(0);
    function attempt(){
      if (!paths.length) { callback(false); return; }
      var url = paths.shift()+encodeURIComponent(code)+".json";
      var xhr = new XMLHttpRequest();
      xhr.open("GET",url,true);
      xhr.onreadystatechange = function(){
        if (xhr.readyState != 4) { return; }
        if ((xhr.status >= 200) && (xhr.status < 300)) {
          var parsed = null;
          try { parsed = JSON.parse(xhr.responseText); } catch (e) { parsed = null; }
          if (parsed) { callback(parsed); return; }
        }
        attempt();
      };
      try { xhr.send(); } catch (e) { attempt(); }
    }
    attempt();
  },

  /* Load the list of catalogs the server has available. */
  loadIndex: function(callback){
    var me = this;
    if (this.indexLoaded) { if (callback) { callback(this.available); } return; }
    this.fetchCatalog("index",function(d){
      me.indexLoaded = true;
      if (d && (typeof d == "object")) {
        for (var code in d) { me.available[code] = d[code]; }
      }
      if (callback) { callback(me.available); }
    });
  },

  /* Switch to the given language, fetching its catalog if needed.
   * English never fetches anything. */
  setLanguage: function(code,callback){
    var me = this;
    function done(){
      document.documentElement.setAttribute("lang",me.current);
      for (var i in me.onchange) { me.onchange[i](me.current); }
      if (callback) { callback(me.current); }
    }
    if (this.isEnglish(code)) {
      this.current = "en";
      this.catalog = {};
      done();
      return;
    }
    if (code == this.current) { done(); return; }
    this.fetchCatalog(code,function(d){
      if (d) {
        me.current = code;
        me.catalog = d;
      }
      else {
        //Catalog unavailable: stay on / fall back to English rather than
        //pretending we're translated.
        me.current = "en";
        me.catalog = {};
      }
      done();
    });
  },

  /* Pick a language for a first-time visitor from the browser preferences.
   * We match on the full code including region (de-DE), then on the bare
   * language subtag (de -> de-DE) so a "de-AT" browser still gets German. */
  detect: function(){
    var prefs = [];
    if (navigator.languages && navigator.languages.length) {
      prefs = Array.prototype.slice.call(navigator.languages);
    }
    else if (navigator.language) { prefs = [navigator.language]; }
    for (var i in prefs) {
      var want = prefs[i];
      for (var code in this.available) {
        if (code.toLowerCase() == want.toLowerCase()) { return code; }
      }
      var base = want.split("-")[0].toLowerCase();
      for (var code in this.available) {
        if (code.split("-")[0].toLowerCase() == base) { return code; }
      }
    }
    return "en";
  }
};

/* Translate a single string.
 *
 * Exposed as a global (and on window) rather than a local so that terser /
 * closure-compiler cannot mangle the name away. The string *arguments* are
 * always preserved by minifiers, which is what xgettext extracts.
 *
 *   tr("Save")
 *   tr("Failed (%s)", err)
 *   tr("%1$s of %2$s", done, total)
 */
function tr(msgid) {
  var out = MistLang.lookup(msgid);
  if (arguments.length > 1) {
    return MistLang.format(out,Array.prototype.slice.call(arguments,1));
  }
  return out;
}

/* ngettext-style plural form.
 *
 *   trn("%s stream","%s streams",n,n)
 */
function trn(singular,plural,n) {
  var out = MistLang.lookupPlural(singular,plural,n);
  if (arguments.length > 3) {
    return MistLang.format(out,Array.prototype.slice.call(arguments,3));
  }
  return MistLang.format(out,[n]);
}

if (typeof window != "undefined") {
  window.MistLang = MistLang;
  window.tr = tr;
  window.trn = trn;
}
