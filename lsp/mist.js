$(function(){

  UI.elements = {
    menu: $('nav > .menu'),
    main: $('main'),
    header: $('header'),
    connection: {
      status: $('#connection'),
      user_and_host: $('#user_and_host'),
      msg: $('#message')
    },
    context_menu: []
  };
  UI.buildMenu();
  UI.stored.getOpts();

  document.body.setAttribute("data-browser",function(){
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
  }());

  $("body").on("keydown",function(e){
    switch (e.key) {
      case "Escape": {
        //if context menu, hide
        for (let menu of UI.elements.context_menu) {
          menu.hide();
        }
        break;
      }
    }
  });

  UI.elements.main.click(function(e){
    //if context menu, hide
    if (!e.isDefaultPrevented()) {
      for (let menu of UI.elements.context_menu) {
        menu.hide();
      }
    }
  });
  //add right click functionality for touch devices
  var long_click = { timeout: false, delay: 1500 };
  UI.elements.main.on("mousedown",function(e){
    var ele = e.target;
    long_click.timeout = setTimeout(function(){
      long_click.timeout = false;
      var event = new Event("contextmenu",{bubbles:true});
      event.pageX = e.pageX;
      event.pageY = e.pageY;
      ele.dispatchEvent(event);

      //prevent the click after mouseup that would remove the context menu when let go
      function captureClick(e) {
        e.preventDefault();
      }
      function cleanUp() {
        window.removeEventListener('click',captureClick,true);
        document.removeEventListener('mouseup',cleanUp);
      }
      window.addEventListener('click',captureClick,true);
      document.addEventListener('mouseup',function(e){
        requestAnimationFrame(cleanUp);
      });
    },long_click.delay);
  });
  UI.elements.main.on("mouseleave",function(e){
    if (long_click.timeout) {
      clearTimeout(long_click.timeout);
      long_click.timeout = false;
    }
  });
  UI.elements.main.on("mouseup",function(e){
    if (long_click.timeout) {
      clearTimeout(long_click.timeout);
      long_click.timeout = false;
    }
  });

  //get stored login data
  try {
    if ('mistLogin' in sessionStorage) {
      var stored = JSON.parse(sessionStorage['mistLogin']);
      mist.user.name = stored.name;
      mist.user.password = stored.password;
      mist.user.host = stored.host;
    }
  }
  catch (e) {}
  
  //check if username and host have been stored in the url
  if (location.hash) {
    var hash = decodeURIComponent(location.hash).substring(1).split('@');
    var user = hash[0].split('&');
    mist.user.name = user[0];
    if (user[1]) { mist.user.host = user[1]; }
  }
  
  //check if we are logged in
  mist.send(function(d){
    //we're logged in
    $(window).trigger('hashchange');
  },{},{timeout: 5, hide: true});
  
  var lastpos = 0;
  $('body > div.filler').on('scroll',function(){
    var pos = $(this).scrollLeft();
    if (pos !=  lastpos) {
      UI.elements.header.css('margin-right',-1*pos+'px');
    }
    lastpos = pos;
  });
});

var lastpage = [];
$(window).on('hashchange', function(e) {
  var loc = decodeURIComponent(location.hash).substring(1).split('@');
  if (!loc[1]) { loc[1] = ''; }
  var tab = loc[1].split('&');
  if (tab[0] == '') { tab[0] = 'Overview'; }
  UI.showTab(tab[0],tab[1],lastpage);
  if (lastpage[0] != tab[0] || lastpage[1] != tab[1]) lastpage = [tab[0],tab[1]];
});

var MistVideoObject = {};
var otherhost = {
  host: false,
  https: false
};
var UI = {
  debug: false,
  elements: {},
  stored: {
    getOpts: function(){
      var stored = localStorage['stored'];
      if (stored) {
        stored = JSON.parse(stored);
      }
      $.extend(true,this.vars,stored);
      return this.vars;
    },
    saveOpt: function(name,val){
      this.vars[name] = val;
      localStorage['stored'] = JSON.stringify(this.vars);
      return this.vars;
    },
    vars: {
      helpme: true
    }
  },
  interval: {
    list: {},
    clear: function(){
      for (var i in this.list) {
        clearInterval(this.list[i].id);
      }
      this.list = {};
    },
    set: function(callback,delay){
      if (this.opts) {
        log('[interval]','Set called on interval, but an interval is already active.');
      }
      
      var opts = {
        delay: delay,
        callback: callback,
        id: setInterval(callback,delay)
      };
      this.list[opts.id] = opts;
      return opts.id;
    }
  },
  websockets: {
    list: [],
    clear: function(){
      for (var i in this.list) {
        this.list[i].close();
      }
      //they will remove themselves from the list in the onclose call, defined in websockets.create
    },
    create: function(url,error_callback){
      var ws = new WebSocket(url);
      var me = this;
      this.list.push(ws);
      ws.addEventListener("close",function(){
        //remove from list
        for (var i = me.list.length - 1; i >=0; i--) {
          if (me.list[i] == ws) { me.list.splice(i,1); }
        }
      });
      ws.addEventListener("error",error_callback);
      return ws;
    }
  },
  countrylist: {'AF':'Afghanistan','AX':'&Aring;land Islands','AL':'Albania','DZ':'Algeria','AS':'American Samoa','AD':'Andorra',
    'AO':'Angola','AI':'Anguilla','AQ':'Antarctica','AG':'Antigua and Barbuda','AR':'Argentina','AM':'Armenia','AW':'Aruba',
    'AU':'Australia','AT':'Austria','AZ':'Azerbaijan','BS':'Bahamas','BH':'Bahrain','BD':'Bangladesh','BB':'Barbados',
    'BY':'Belarus','BE':'Belgium','BZ':'Belize','BJ':'Benin','BM':'Bermuda','BT':'Bhutan','BO':'Bolivia, Plurinational State of',
    'BQ':'Bonaire, Sint Eustatius and Saba','BA':'Bosnia and Herzegovina','BW':'Botswana','BV':'Bouvet Island','BR':'Brazil',
    'IO':'British Indian Ocean Territory','BN':'Brunei Darussalam','BG':'Bulgaria','BF':'Burkina Faso','BI':'Burundi','KH':'Cambodia',
    'CM':'Cameroon','CA':'Canada','CV':'Cape Verde','KY':'Cayman Islands','CF':'Central African Republic','TD':'Chad','CL':'Chile',
    'CN':'China','CX':'Christmas Island','CC':'Cocos (Keeling) Islands','CO':'Colombia','KM':'Comoros','CG':'Congo',
    'CD':'Congo, the Democratic Republic of the','CK':'Cook Islands','CR':'Costa Rica','CI':'C&ocirc;te d\'Ivoire','HR':'Croatia',
    'CU':'Cuba','CW':'Cura&ccedil;ao','CY':'Cyprus','CZ':'Czech Republic','DK':'Denmark','DJ':'Djibouti','DM':'Dominica',
    'DO':'Dominican Republic','EC':'Ecuador','EG':'Egypt','SV':'El Salvador','GQ':'Equatorial Guinea','ER':'Eritrea','EE':'Estonia',
    'ET':'Ethiopia','FK':'Falkland Islands (Malvinas)','FO':'Faroe Islands','FJ':'Fiji','FI':'Finland','FR':'France','GF':'French Guiana',
    'PF':'French Polynesia','TF':'French Southern Territories','GA':'Gabon','GM':'Gambia','GE':'Georgia','DE':'Germany','GH':'Ghana',
    'GI':'Gibraltar','GR':'Greece','GL':'Greenland','GD':'Grenada','GP':'Guadeloupe','GU':'Guam','GT':'Guatemala','GG':'Guernsey',
    'GN':'Guinea','GW':'Guinea-Bissau','GY':'Guyana','HT':'Haiti','HM':'Heard Island and McDonald Islands',
    'VA':'Holy See (Vatican City State)','HN':'Honduras','HK':'Hong Kong','HU':'Hungary','IS':'Iceland','IN':'India','ID':'Indonesia',
    'IR':'Iran, Islamic Republic of','IQ':'Iraq','IE':'Ireland','IM':'Isle of Man','IL':'Israel','IT':'Italy','JM':'Jamaica',
    'JP':'Japan','JE':'Jersey','JO':'Jordan','KZ':'Kazakhstan','KE':'Kenya','KI':'Kiribati',
    'KP':'Korea, Democratic People\'s Republic of','KR':'Korea, Republic of','KW':'Kuwait','KG':'Kyrgyzstan',
    'LA':'Lao People\'s Democratic Republic','LV':'Latvia','LB':'Lebanon','LS':'Lesotho','LR':'Liberia','LY':'Libya',
    'LI':'Liechtenstein','LT':'Lithuania','LU':'Luxembourg','MO':'Macao','MK':'Macedonia, the former Yugoslav Republic of',
    'MG':'Madagascar','MW':'Malawi','MY':'Malaysia','MV':'Maldives','ML':'Mali','MT':'Malta','MH':'Marshall Islands',
    'MQ':'Martinique','MR':'Mauritania','MU':'Mauritius','YT':'Mayotte','MX':'Mexico','FM':'Micronesia, Federated States of',
    'MD':'Moldova, Republic of','MC':'Monaco','MN':'Mongolia','ME':'Montenegro','MS':'Montserrat','MA':'Morocco','MZ':'Mozambique',
    'MM':'Myanmar','NA':'Namibia','NR':'Nauru','NP':'Nepal','NL':'Netherlands','NC':'New Caledonia','NZ':'New Zealand','NI':'Nicaragua',
    'NE':'Niger','NG':'Nigeria','NU':'Niue','NF':'Norfolk Island','MP':'Northern Mariana Islands','NO':'Norway','OM':'Oman',
    'PK':'Pakistan','PW':'Palau','PS':'Palestine, State of','PA':'Panama','PG':'Papua New Guinea','PY':'Paraguay','PE':'Peru',
    'PH':'Philippines','PN':'Pitcairn','PL':'Poland','PT':'Portugal','PR':'Puerto Rico','QA':'Qatar','RE':'R&eacute;union',
    'RO':'Romania','RU':'Russian Federation','RW':'Rwanda','BL':'Saint Barth&eacute;lemy','SH':'Saint Helena, Ascension and Tristan da Cunha',
    'KN':'Saint Kitts and Nevis','LC':'Saint Lucia','MF':'Saint Martin (French part)','PM':'Saint Pierre and Miquelon',
    'VC':'Saint Vincent and the Grenadines','WS':'Samoa','SM':'San Marino','ST':'Sao Tome and Principe','SA':'Saudi Arabia',
    'SN':'Senegal','RS':'Serbia','SC':'Seychelles','SL':'Sierra Leone','SG':'Singapore','SX':'Sint Maarten (Dutch part)','SK':'Slovakia',
    'SI':'Slovenia','SB':'Solomon Islands','SO':'Somalia','ZA':'South Africa','GS':'South Georgia and the South Sandwich Islands',
    'SS':'South Sudan','ES':'Spain','LK':'Sri Lanka','SD':'Sudan','SR':'Suriname','SJ':'Svalbard and Jan Mayen','SZ':'Swaziland',
    'SE':'Sweden','CH':'Switzerland','SY':'Syrian Arab Republic','TW':'Taiwan, Province of China','TJ':'Tajikistan',
    'TZ':'Tanzania, United Republic of','TH':'Thailand','TL':'Timor-Leste','TG':'Togo','TK':'Tokelau','TO':'Tonga',
    'TT':'Trinidad and Tobago','TN':'Tunisia','TR':'Turkey','TM':'Turkmenistan','TC':'Turks and Caicos Islands','TV':'Tuvalu',
    'UG':'Uganda','UA':'Ukraine','AE':'United Arab Emirates','GB':'United Kingdom','US':'United States',
    'UM':'United States Minor Outlying Islands','UY':'Uruguay','UZ':'Uzbekistan','VU':'Vanuatu','VE':'Venezuela, Bolivarian Republic of',
    'VN':'Viet Nam','VG':'Virgin Islands, British','VI':'Virgin Islands, U.S.','WF':'Wallis and Futuna','EH':'Western Sahara','YE':'Yemen',
    'ZM':'Zambia','ZW':'Zimbabwe'
  },
  tooltip: {
    show: function (pos,contents){
      $tooltip = this.element;
      
      if (!$.contains(document.body,$tooltip[0])) {
        $('body').append($tooltip);
      }
      
      $tooltip.html(contents);
      clearTimeout(this.hiding);
      delete this.hiding;
      
      var mh = $(document).height() - $tooltip.outerHeight();
      var mw = $(document).width() - $tooltip.outerWidth();
      
      $tooltip.css('left',Math.min(pos.pageX+10,mw-10));
      $tooltip.css('top',Math.min(pos.pageY+25,mh-10));
      
      $tooltip.show().addClass('show');
    },
    hide: function() {
      $tooltip = this.element;
      $tooltip.removeClass('show');
      this.hiding = setTimeout(function(){
        $tooltip.hide();
      },500);
    },
    element: $('<div>').attr('id','tooltip')
  },
context_menu: function(){
    var $ele = $("<section>").attr("class","context_menu").click(function(e){ e.stopPropagation(); });
    $ele[0].style.display = "none";
    this.ele = $ele;

    UI.elements.context_menu.push(this);
    
    this.pos = function(pos){
      var $parent = $ele.offsetParent();
      var parpos = $parent[0].getBoundingClientRect();

      var mh = $parent.height() - $ele.outerHeight();
      var mw = $parent.width() - $ele.outerWidth();

      $ele.css('left',Math.min(pos.pageX - parpos.x,mw));
      $ele.css('top',Math.min(pos.pageY - parpos.y,mh));
    };
    this.show = function(html,pos){
      /*  
       Expects something like:
       menu.show([
        [ $("<div>").addClass("header").text("My header") ],
        [
          [ "Entry 1", onClick, "icon", "This is the first menu entry of this section" ],
          {
            text: "Entry 2",
            icon: "copy",
            title: "This is the second menu entry of this section",
            "function": function(e){
              this._setText("Loading..");
              let me = this;
              doSomething.then(function(){
                me._setText("Complete!");
                setTimeout(function(){ context_menu.hide(); },300);
              }).catch(function(){
                me._setText("Failed!");
                setTimeout(function(){ context_menu.hide(); },1e3);
              });
  
              return false; //do not hide the context menu after this function has returned
            },

          }
        ]
       ],PointerEvent);
      
       *  */



      if ((typeof html == "string") || (html instanceof jQuery)) {
        $ele.html(html);
      }
      else if (typeof html == "object") {
        $ele.html("");
        if (!Array.isArray(html)) {
          html = [html];
        }
        for (var i in html) {
          var section = html[i];
          if (section instanceof jQuery) {
            $ele.children().last().remove(); //remove previous <hr>
            $ele.append(section); 
            $ele.append($("<hr>")); //so that finishing code removes this <hr> and not the section we just inserted

            continue;
          }

          for (var j in section) {
            var entry = section[j];
            var $entry = $("<div>");
            if (typeof entry == "string") {
              $entry.text(entry);
            }
            else if (entry instanceof jQuery) {
              $ele.append(entry);
              continue;
            }
            else {
              function createEntry(opts) {
                if (Array.isArray(opts)) {
                  var obj = {};
                  obj.text = opts[0];
                  if ((opts.length >= 2) && (typeof opts[1] == "function")) {
                    obj["function"] = opts[1];
                  }
                  if (opts.length >=3) {
                    obj.icon = opts[2];
                    if (opts.length >= 4) {
                      obj.title = opts[3];
                    }
                  }
                  opts = obj;
                }
                
                if ("function" in opts) {
                  //onclick action
                  $entry.click(function(e){
                    var returnValue = opts["function"].apply(this,arguments);
                    if (returnValue !== false) {
                      $ele.hide();
                    }
                  });
                  $entry.on("keydown",function(e){
                    switch (e.key) {
                      case "Enter": {
                        $(this).click();
                        break;
                      }
                    }
                  });
                  $entry.attr("tabindex","0");
                }

                if ("icon" in opts) {
                  $entry.append(
                    $("<div>").addClass("icon").attr("data-icon",opts.icon)
                  );
                }

                if ("title" in opts) {
                  $entry.attr("title",opts.title);
                }

                if (typeof opts.text == "string") {
                  $entry[0]._text = document.createTextNode(opts.text);
                  $entry.append($entry[0]._text);
                  //add helper function to easily update just the text of this menu entry
                  $entry[0]._setText = function(str) {
                    this._text.nodeValue = str;
                  }
                }
                else {
                  $entry.append(opts.text);
                  $entry[0]._setText = function(str) {
                    $(this).html(str);
                  }
                }
              }
              createEntry(entry);

            }

            $ele.append($entry);
          }
          $ele.append($("<hr>"));
        }
        $ele.children().last().remove();
        $ele.find("[tabindex]").first().focus();
      }

      if (!$ele.parent()) { $("body").append($ele); }
      $ele[0].style.display = "";
      if (pos) { this.pos(pos); }

      $ele.find("[tabindex]").first().focus();
    };
    this.hide = function(){
      $ele[0].style.display = "none";
    };
    this.remove = function(){
      delete UI.element.context_menu;
      $ele.remove();
    };

    $ele.on("keydown",function(e){
      function getFocussed(dir) {
        var focussed = $ele.find(":focus");
        if (!focussed.length) {
          $ele.find("[tabindex]").first().focus();
          return;
        }
        if (dir == "down") {
          var next = focussed.nextAll("[tabindex]");
          if (!next.length) {
            $ele.find("[tabindex]").first().focus();
          }
          else {
            next.first().focus();
          }
        }
        else {
          var prev = focussed.prevAll("[tabindex]");
          if (!prev.length) {
            $ele.find("[tabindex]").last().focus();
          }
          else {
            prev.first().focus();
          }
        }
      }
      switch (e.key) {
        case "ArrowDown": {
          getFocussed("down");
          break;
        }
        case "ArrowUp": {
          getFocussed("up");
          break;
        }
      }
    });

    this.hide();
  },
  pagecontrol: function(page_ele,default_page_size){
    //takes table / div of elements (page_ele)
    //take into account hidden elements (class = hidden)
    //add page-functions to paged element
    //keep track of total amount of (non hidden) items
    //update control buttons
    //returns controls container

    var controls = $("<div>").addClass("page_control");
    controls.elements = {
      prev: $("<button>").text("Previous").click(function(){
        show_page("previous");
      }),
      next: $("<button>").text("Next").click(function(){
        show_page("next");
      }),
      page_buttons: {},
      page_button_cont: $("<div>").addClass("page_numbers"),
      pagelength: $("<select>").append(
        $("<option>").text(5)
      ).append(
        $("<option>").text(10)
      ).append(
        $("<option>").text(25)
      ).append(
        $("<option>").text(50)
      ).append(
        $("<option>").text(100)
      ).change(function(){
        controls.vars.page_size = $(this).val();
        show_page();
      }),
      jumpto: $("<select>").addClass("jump_to").change(function(){
        show_page($(this).val());
      }),
      summary: document.createElement("span"),
      style: document.createElement("style")
    };
    controls.vars = {
      currentpage: 1,
      page_size: default_page_size || 25,
      entries: 0,
      uid: "paginated_"+(Math.random()+"").slice(2) //used for the css rule that controls which items are shown/hidden
    };
    controls.elements.pagelength.val(controls.vars.page_size);
    controls.elements.summary.className = "summary";
    controls.elements.summary.elements = {
      start: document.createTextNode(""),
      end: document.createTextNode(""),
      total: document.createTextNode("")
    };
    controls.elements.summary.appendChild(document.createTextNode("Showing "));
    controls.elements.summary.appendChild(controls.elements.summary.elements.start);
    controls.elements.summary.appendChild(document.createTextNode("-"));
    controls.elements.summary.appendChild(controls.elements.summary.elements.end);
    controls.elements.summary.appendChild(document.createTextNode(" of "));
    controls.elements.summary.appendChild(controls.elements.summary.elements.total);
    controls.elements.summary.appendChild(document.createTextNode(" items."));
    controls.elements.summary.update = function(){
      this.elements.start.nodeValue = Math.max(0,(controls.vars.currentpage - 1) * controls.vars.page_size + 1);
      this.elements.end.nodeValue = Math.min(controls.vars.currentpage * controls.vars.page_size,controls.vars.entries);
      this.elements.total.nodeValue = controls.vars.entries;

      var maxpage = Math.floor((controls.vars.entries-1)/controls.vars.page_size)+1;
      while (controls.elements.jumpto.children().length < maxpage) {
        controls.elements.jumpto.append(
          $("<option>").text(controls.elements.jumpto.children().length + 1)
        );
      }
      while (controls.elements.jumpto.children().length > maxpage) {
        controls.elements.jumpto.children().last().remove();
      }
    };
    page_ele.classList.add(controls.vars.uid);

    function createPageButton(pagenumber) {
      var button = document.createElement("button");
      button.appendChild(document.createTextNode(pagenumber));
      button.addEventListener("click",function(){
        show_page(pagenumber);
      });
      controls.elements.page_buttons[pagenumber] = button;
      controls.elements.page_button_cont.append(button);
      return button;
    }

    var default_display_value = false;
    function show_page(page){
      perpage = controls.vars.page_size;
      if (!page) page = controls.vars.currentpage;
      if (page == "next") { page = controls.vars.currentpage + 1; }
      if (page == "previous") { page = controls.vars.currentpage - 1; }

      //count current elements
      var l = page_ele.querySelectorAll(":scope > :not(.hidden)");
      controls.vars.entries = l.length;

      if (!default_display_value && (controls.vars.entries > 0)) {
        let css = false;
        if (controls.elements.style.textContent != "") {
          //it's possible that the new entr(y/ies) is being hidden by our own pagination code: temporarily disable it
          css = controls.elements.style.textContent;
          controls.elements.style.textContent = "";
        }
        default_display_value = getComputedStyle(l[0]).getPropertyValue("display");
        if (default_display_value == "none") default_display_value = false; //if the calculated value is none, it's useless
        if (css) {
          controls.elements.style.textContent = css;
        }
      }
      l = l.length;

      if (page instanceof HTMLElement) {
        var n = Array.from(page_ele.children).indexOf(page);
        page = Math.floor(n / perpage)+1;
      }


      page = Math.max(1,page);
      //clamp to last page with content if above total entries
      maxpage = Math.floor((l-1)/perpage)+1;
      page = Math.min(page,maxpage);

      //check if we need to add page buttons
      //always show first page, last page, and pages around the current one
      for (var i = 1; i <= maxpage; i++) {
        if (!(i in controls.elements.page_buttons)) {
          createPageButton(i);
        }
        //hide buttons except first, last and around current page
        switch(i) {
          case 1:
          //case page-2:
          case page-1:
          case page:
          case page+1:
          //case page+2:
          case maxpage: {
            controls.elements.page_buttons[i].classList.remove("hidden");
            break;
          }
          default: {
            controls.elements.page_buttons[i].classList.add("hidden");
          }
        }
      }
      //hide buttons beyond the current page range
      //NB: assumes buttons are created in order and their index matches their key-1
      var button_indexes = Object.keys(controls.elements.page_buttons);
      for (var i = maxpage; i < button_indexes.length; i++) {
        controls.elements.page_buttons[button_indexes[i]].classList.add("hidden");
      }

      if (controls.vars.currentpage in controls.elements.page_buttons) controls.elements.page_buttons[controls.vars.currentpage].classList.remove("active");

      //disable prev/next buttons if applicable
      if (page == 1) { controls.elements.prev.attr("disabled",""); }
      else { controls.elements.prev.removeAttr("disabled"); }
      if (page == maxpage) { controls.elements.next.attr("disabled",""); }
      else { controls.elements.next.removeAttr("disabled"); }

      controls.vars.currentpage = page;
      if (controls.vars.currentpage in controls.elements.page_buttons) controls.elements.page_buttons[page].classList.add("active");

      //create css rules
      //hide everything
      var css = "."+controls.vars.uid+" > * { display: none !important; }\n";
      //show everything but the pages before the current one
      css += "."+controls.vars.uid+" > *:not(.hidden) ";
      css += "~ *:not(.hidden) ".repeat(Math.max(0,perpage*(page-1)));
      css += "{ display: "+(default_display_value ? default_display_value : "revert")+" !important; }\n";

      //hide everthing after the pages including the current one
      css += "."+controls.vars.uid+"> *:not(.hidden) ";
      css += "~ *:not(.hidden) ".repeat(perpage*page);
      css += "{ display: none !important; }\n";

      controls.elements.style.textContent = css;
      controls.elements.summary.update();

      controls.elements.jumpto.val(page);

    }
    page_ele.show_page = show_page;
    show_page();

    controls.append(
      $("<div>").addClass("pages").append(
        controls.elements.prev
      ).append(
        controls.elements.page_button_cont
      ).append(
        controls.elements.next
      )
    ).append(
      controls.elements.summary
    ).append(
      $("<label>").addClass("input_container").append(
        $("<span>").text("Jump to page:")
      ).append(
        controls.elements.jumpto
      )
    ).append(
      $("<label>").addClass("input_container").append(
        $("<span>").text("Items per page:")
      ).append(
        controls.elements.pagelength
      )
    ).append(
      controls.elements.style
    );

    return controls;
  },
  sortableItems: function(item_container,getVal,options){
    options = Object.assign({
      controls: false, //container of header-like elements that will be clickable to sort the index it belongs to
      sortby: false, //initial index to sort by 
      sortdir: 1, //initial sorting direction
      container: item_container,
      sortsave: false //name of the mist.stored variable where the last used sorting should be stored
    },options);

    var lastsortby = options.sortby;
    var lastsortdir = options.sortdir;
    if (options.controls) {
      if (options.controls.getAttribute("data-sortby")) {
        lastsortby = options.controls.getAttribute("data-sortby");
      }
      if (options.controls.getAttribute("data-sortdir")) {
        lastsortdir = options.controls.getAttribute("data-sortdir");
      }
      for (var i = 0; i < options.controls.children.length; i++) {
        if (options.controls.children[i].hasAttribute("data-index")) {
          options.controls.children[i].addEventListener("click",function(){
            var v = this.getAttribute("data-index");
            item_container.sort(v);
          });
        }
      }
    }

    if (options.sortsave) {
      var stored = mist.stored.get();
      if (!(options.sortsave in stored)) {
        stored[options.sortsave] = {};
      }
      var result = Object.assign({by: lastsortby, dir: lastsortdir},stored[options.sortsave]);
      lastsortby = result.by;
      lastsortdir = result.dir;
    }

    item_container.sort = function(sortby,sortdir){
      if (!sortdir) {
        sortdir = lastsortdir;
        if (sortby == lastsortby) {
          sortdir *= -1;
        }
      } 
      sortby = (sortby || (sortby == "")) ? sortby : lastsortby;

      lastsortby = sortby;
      lastsortdir = sortdir;
      if (options.sortsave) {
        mist.stored.set(options.sortsave,{by: sortby, dir: sortdir});
      }
      if (options.controls) {
        options.controls.setAttribute("data-sortby",sortby);
        options.controls.setAttribute("data-sortdir",sortdir);
        var old = options.controls.querySelector("[data-sorting]");
        if (old) { old.removeAttribute("data-sorting"); }
        var a = options.controls.querySelector("[data-index=\""+sortby+"\"]");
        if (a) a.setAttribute("data-sorting","");
      }

      try {
        var compare = new Intl.Collator('en',{numeric:true, sensitivity:'accent'}).compare;
        for (var i = 0; i < options.container.children.length-1; i++) {
          var row = options.container.children[i];
          var next = options.container.children[i+1];
          //console.warn("sorting: comparing",getVal.call(row,sortby),getVal.call(next,sortby))
          if (sortdir * compare(getVal.call(row,sortby),getVal.call(next,sortby)) > 0) {
            //the next row should be before the current one
            //put it before and then check if it needs to go up further
            options.container.insertBefore(next,row); 
            if (i > 0) {
              i = i-2;
            }
          }
        }
      }
      catch (e) {
        //something went wrong, sorting failed. It's possible we tried sorting for a column that doesn't exist
        var msg = ["Failed to sort items in ",item_container," by '"+sortby+"'"];
        if (sortby != options.sortby) { //if the column we tried to sort on is not the default as passed from initialization, fall back to that
          msg.push(", falling back to '"+options.sortby+"'");
          console.warn.apply(this,msg);
          this.sort(options.sortby);
        }
        else {
          console.warn.apply(this,msg);
        }
      }

    };

    if (lastsortby !== false) { item_container.sort(); }

  },
  humanMime: function (type) {
    var human = false;
    switch (type) {
      case 'html5/application/vnd.apple.mpegurl':
        human = 'HLS (TS)';
        break;
      case "html5/application/vnd.apple.mpegurl;version=7":
        human = "HLS (CMAF)";
        break;
      case "html5/application/sdp":
        human = "SDP";
        break;
      case 'html5/video/webm':
        human = 'WebM';
        break;
      case 'html5/video/raw':
        human = 'Raw';
        break;
      case 'html5/video/mp4':
        human = 'MP4';
        break;
      case 'ws/video/mp4':
        human = 'MP4 (websocket)';
        break;
      case 'ws/video/raw':
        human = 'Raw (websocket)';
        break;
      case 'dtsc':
        human = 'DTSC';
        break;
      case 'html5/audio/aac':
        human = 'AAC';
        break;
      case 'html5/audio/flac':
        human = 'FLAC';
        break;
      case 'html5/image/jpeg':
        human = 'JPG';
        break;
      case 'dash/video/mp4':
        human = 'DASH';
        break;
      case 'flash/11':
        human = 'HDS';
        break;
      case 'flash/10':
        human = 'RTMP';
        break;
      case 'flash/7':
        human = 'Progressive';
        break;
      case 'html5/audio/mp3':
        human = 'MP3';
        break;
      case 'html5/audio/wav':
        human = 'WAV';
        break;
      case 'html5/video/mp2t':
      case 'html5/video/mpeg':
        human = 'TS';
        break;
      case "html5/application/vnd.ms-sstr+xml":
      case 'html5/application/vnd.ms-ss':
        human = 'Smooth Streaming';
        break;
      case 'html5/text/vtt':
        human = 'VTT Subtitles';
        break;
      case 'html5/text/plain':
        human = 'SRT Subtitles';
        break;
      case 'html5/text/javascript':
        human = 'JSON Subtitles';
        break;
      case 'rtsp':
        human = 'RTSP';
        break;
      case 'srt':
        human = 'SRT';
        break;
      case 'webrtc':
        human = "WebRTC (websocket)";
        break;
      case 'whep':
        human = "WebRTC (WHEP)";
        break;
    }
    return human;
  },
  popup: function(html){
    var popup = {
      element: $('<dialog>').addClass('popup'),
      show: function(content) {
        this.element.html(
          $('<button>').text('Close').addClass('close').click(function(){
            popup.close();
          })
        ).append(content);
        if (!this.element[0].open) {
          $('body').append(this.element);
          popup.element[0].showModal();
        }
        popup.element.find('.field').first().focus();
      },
      close: function(){
        this.element[0].close();
      }
    };
    //close modal when clicking on the modal backdrop
    popup.element[0].addEventListener("mousedown",function(e){
      if (e.target == this) {
        //no children were clicked, but it might be on the padding
        let rect = this.getBoundingClientRect();
        let isInDialog = (rect.top <= e.clientY && e.clientY <= rect.top + rect.height &&
    rect.left <= e.clientX && e.clientX <= rect.left + rect.width);
        if (!isInDialog) {
          popup.close();
        }
      }
    });
    popup.element[0].addEventListener("close",function(){
      setTimeout(function(){
        popup.element.remove();
        popup.element = null;
      },1e3);
    });
    if (html) {
      popup.show(html);
    }
    return popup;
  },
  menu: [
    {
      Overview: {},
      General: {
        hiddenmenu: {
          "Edit variable": {},
          "Edit external writer": {},
          "Edit JWK": {},
          "Stream keys": {}
        }
      },
      Protocols: {},
      Streams: {
        keepParam: true, 
        hiddenmenu: {
          Edit: {},
          Status: {},
          Preview: {},
          Embed: {}
        }
      },
      Push: {},
      Triggers: {},
      Logs: {},
      Statistics: {},
      'Server Stats': {}
    },
    {
      Disconnect: {
        classes: ['red']
      }
    },
    {
      Documentation: {
        link: 'https://docs.mistserver.org/'
      },
      Changelog: {
        link: 'https://releases.mistserver.org/changelog'
      },
      'Email for Help': {}
      /*Tools: {
        submenu: {
          'Release notes': {
            link: 'http://mistserver.org/documentation#Devdocs'
          },
          'Email for Help': {}
        }
      }*/
    }
  ],
  buildMenu: function(){
    function createButton(j,button) {
      var $button = $('<a>').addClass('button');
      $button.html(
        $('<span>').addClass('plain').text(j)
      ).append(
        $('<span>').addClass('highlighted').text(j)
      );
      for (var k in button.classes) {
        $button.addClass(button.classes[k]);
      }
      if ('link' in button) {
        $button.attr('href',button.link).attr('target','_blank');
      }
      else if (!('submenu' in button)) {
        $button.click(function(e){
          if ($(this).closest('.menu').hasClass('hide')) { return; }
          var other;
          var $sub = $(this).closest('.hiddenmenu[data-param]');
          if ($sub.length) {
            other = $sub.attr("data-param");
          }
          UI.navto(j,other);
          e.stopPropagation();
        });
      }
      return $button;
    }
    
    var $menu = UI.elements.menu;
    for (var i in UI.menu) {
      if (i > 0) {
        $menu.append($('<br>'));
      }
      for (var j in UI.menu[i]) {
        var button = UI.menu[i][j];
        var $button = createButton(j,button);
        $menu.append($button);
        if ('submenu' in button) {
          var $sub = $('<span>').addClass('submenu');
          $button.addClass('arrowdown').append($sub);
          for (var k in button.submenu) {
            $sub.append(createButton(k,button.submenu[k]));
          }
        }
        else if ('hiddenmenu' in button) {
          var $sub = $('<span>').addClass('hiddenmenu');
          if (button.keepParam) { $sub.attr("data-param",""); }
          $button.append($sub);
          for (var k in button.hiddenmenu) {
            $sub.append(createButton(k,button.hiddenmenu[k]));
          }
        }
      }
    }
    
    var $ihb = $('<div>').attr('id','ih_button').text('?').click(function(){
      $('body').toggleClass('helpme');
      UI.stored.saveOpt('helpme',$('body').hasClass('helpme'));
    }).attr('title','Click to toggle the display of integrated help');
    if (UI.stored.getOpts().helpme) {
      $('body').addClass('helpme');
    }
    $menu.after($ihb).after(
      $('<div>').addClass('separator')
    );
  },
  findInput: function(name) {
    return this.findInOutput('inputs',name);
  },
  findOutput: function(name) {
    return this.findInOutput('connectors',name);
  },
  findInOutput: function(where,name) {
    if ('capabilities' in mist.data) {
      var output = false;
      var loc = mist.data.capabilities[where];
      if (name in loc) { output = loc[name]; }
      if (name+'.exe' in loc) { output = loc[name+'.exe']; }
      return output;
    }
    else {
      throw 'Request capabilities first';
    }
  },
  findFolderSubstreams: function(stream,callback){

    function createWcStreamObject(streamname,parent) {
      var wcstream = $.extend({},parent);
      delete wcstream.meta;
      delete wcstream.error;
      wcstream.online = 2; //should either be available (2) or active (1)
      wcstream.name = streamname;
      wcstream.ischild = true;
      return wcstream;
    }

    function hasMatchingInput(filename){
      for (var j in mist.data.capabilities.inputs) {
        if ((j.indexOf('Buffer') >= 0) || (j.indexOf('Buffer.exe') >= 0) || (j.indexOf('Folder') >= 0) || (j.indexOf('Folder.exe') >= 0)) { continue; }
        if (mist.inputMatch(mist.data.capabilities.inputs[j].source_match,"/"+filename)) {
          return true;
        }
      }
      return false;
    }

    mist.send(function(d,opts){
      var s = stream.name;
      var matches = 0;
      var output = {};
      for (var i in d.browse.files) {
        if (hasMatchingInput(d.browse.files[i])) {
          var streamname = s+'+'+d.browse.files[i];
          output[streamname] = createWcStreamObject(streamname,stream);
          output[streamname].source = stream.source+d.browse.files[i];

          matches++;
          /*
            if (matches >= 50) {
              //stop retrieving more file names TODO properly display when this happens
              output[s+"+zzzzzzzzz"] = {
                name: "... (too many substreams found)",
                online: -1
              };
              break;
            }
          */
        }
      }
      if (('files' in d.browse) && (d.browse.files.length)) {
        stream.filesfound = true;
      }
      else {
        stream.filesfound = false;
      }
      callback(output);
    },{browse:stream.source});
  },
  findStreamKeys: function(streamname) {
    if ("streamkeys" in mist.data) {
      let streamkeys = [];
      if (typeof mist.data.streamkeys == "object"){
        for (let key in mist.data.streamkeys) {
          if (mist.data.streamkeys[key] == streamname) { streamkeys.push(key); }
        }
      }
      return streamkeys;
    }
    throw "Please request streamkeys first.";
  },
  findInputBySource: function(source) {
    if (source == '') { return null; }
    if ("capabilities" in mist.data) {

      //check for cache
      let matches = {};
      if ("matches" in mist.data.capabilities.inputs) {
        matches = mist.data.capabilities.inputs.matches;
      }
      else {
        //gather a list of match strings
        for (let i in mist.data.capabilities.inputs) {
          let input = mist.data.capabilities.inputs[i];
          if (typeof input.source_match == 'undefined') { continue; }
          let m = input.source_match;
          if (typeof m == "string") {
            m = [m];
          }
          for (let str of m) {
            if ((str in matches) && (matches[str] != i)) {
              //uhoh: duplicate match string for input i and matches[str]: use input with highest input.priority
              if (input.priority > ((a)=>a ? a : 0)(mist.data.capabilities.inputs[matches[str]].priority)) {
                matches[str] = i;
              }
            }
            else {
              matches[str] = i;
            }
          }
        }
      }

      let sorted;
      if ("matches_sorted" in mist.data.capabilities.inputs) {
        sorted = mist.data.capabilities.inputs.matches_sorted;
      }
      else {

        //sort them by length: a more detailed match gets priority
        sorted = Object.keys(matches).sort((a,b) => b.length - a.length);

        //now sort them by input.name+":": these are overrides and should take priority
        let inputs = {};
        for (let input of Object.keys(mist.data.capabilities.inputs)) {
          inputs[input.replace(".exe","").toLowerCase()] = 1;
        }
        function isOverride(str) {
          if (str.slice(-2) == ":*") {
            let input_name = str.slice(0,-2);
            if (input_name in inputs) return 1;
          }
          return 0;
        }
        sorted.sort((a,b)=>{
          return isOverride(b) - isOverride(a);
        });

        //for caching purposes :)
        //these properties will not be enumerable
        //they will be overwritten (with nothing) when capabilities are refreshed
        Object.defineProperty(mist.data.capabilities.inputs,"matches",{
          value: matches
        });
        Object.defineProperty(mist.data.capabilities.inputs,"matches_sorted",{
          value: sorted
        });
      }

      for (let match_string of sorted) {
        if (mist.inputMatch(match_string,source)) {
          let input_name = matches[match_string];
          mist.data.capabilities.inputs[input_name].index = input_name; //'INPUT.exe' for windows, whereas input.name would be 'INPUT'
          return mist.data.capabilities.inputs[input_name];
        }
      }
      return null; //no matching input for this source
    }
    else {
      throw "Please request capabilities first.";
    }
  },
  updateLiveStreamHint: function(streamname,source,$cont,input,streamkeys) {
    let rawmode;
    if ($cont == "raw") {
      rawmode = {};
      $cont = false; 
    }
    if (!$cont) { $cont = $("<span>"); }
    $cont.html("");
    if (!streamname || !source) { 
      return rawmode || $cont;
    }
    if (!input) {
      input = UI.findInputBySource(source);
      if (input === null) {
        return rawmode || $cont;
      }
    }
    if (!streamkeys) {
      streamkeys = UI.findStreamKeys(streamname);
    }

    let show;
    switch (input.name) {
      case 'Buffer':
      case 'Buffer.exe':
        show = ["RTMP","TSSRT","RTSP","WebRTC"];
        break;
      case 'TS':
      case 'TS.exe':
        if ((source.charAt(0) != "/") && (source.slice(0,7) != "ts-exec")) {
          show = ["TS"];
        }
        break;
      case 'TSSRT':
      case 'TSSRT.exe': {
        show = ["TSSRT"];
        break;
      }
    }
    if (!show) {
      return rawmode || $cont;
    }

    var host = parseURL(mist.user.host);
    //var source = $main.find('[name=source]').val();
    var passw = source.match(/@.*/);
    if (passw) { passw = passw[0].substring(1); }
    var ip = source.replace(/(?:.+?):\/\//,'');
    ip = ip.split('/');
    ip = ip[0];
    ip = ip.split(':');
    ip = ip[0];
    var custport = source.match(/:\d+/)?.[0];

    var matchhost = source.match(/^push:\/\/([^:@\/]*)/)?.[1];
    if (matchhost != "invalid,host") {
      streamkeys = [streamname].concat(streamkeys.map((v)=>encodeURIComponent(v)));
    }

    let trythese = ['RTMP','RTSP','TSSRT','WebRTC','HTTPS','HTTP'];
    for (let i = trythese.length - 1; i >= 0; i--) {
      trythese.push(trythese[i]+".exe");
    }
    //add .exe to each for windowz

    let mistdefport = {}; //retrieve MistServer's default ports from capabilities
    for (let i = trythese.length - 1; i >= 0; i--) {
      let p = trythese[i];
      if (p in mist.data.capabilities.connectors) {
        mistdefport[p.replace(".exe","")] = mist.data.capabilities.connectors[p].optional.port['default'];
      }
      else {
        //not in capabilities, stop trying
        trythese.splice(i,1);
      }
    }
    let defport = { //these are the default ports used when using the protocol - not necesarily the same as MistServer's
      RTMP: 1935,
      RTSP: 554,
      HTTP: 80,
      HTTPS: 443,
      TSSRT: -1,
      TS: -1,
    };
    let ports = {};
    for (let protocol of trythese) {
      let protocolname = protocol.replace(".exe","");
      ports[protocolname] = [];
      for (let i in mist.data.config.protocols) {
        var p = mist.data.config.protocols[i];
        if (p.connector == protocol) {
          let port = false;
          if (protocolname in mistdefport) port = ":"+mistdefport[protocolname];
          if ("port" in p) port = ":"+p.port;
          if (port == (":"+defport[protocolname])) port = "";

          switch (protocolname) {
            case "TSSRT": {
              if (p.acceptable == 1) port = false; //this SRT config only allows outgoing connections
              if (port !== false) {
                port = {
                  port: port,
                  passphrase: p.passphrase
                }
              }
              break;
            }
            case "HTTP":
            case "HTTPS": {
              if (("pubaddr" in p) && (p.pubaddr.length)) {
                ports[protocolname].push(p.pubaddr);
                //the ports array will contain strings: the ports, and arrays, the public urls
              }
              break;
            }
          }

          if (port !== false) ports[protocolname].push(port);
        }
      }
    }
    //TODO webrtc input

    
    if (!rawmode) {
      context_menu = new UI.context_menu();
    }

    function createSection(kind) {
      let label = kind;
      function createSpan(text,help) {
        return !!rawmode || $("<span>").addClass("value").addClass("clickable").attr("title",help).text(text).on("contextmenu",function(e){
          e.preventDefault();
          context_menu.show([
            [
              $("<div>").addClass("header").append(
                $("<div>").text(text)
              ).append(
                help ? $("<div>").addClass("description").text(help) : ""
              )
            ],[[
              "Copy to clipboard",function(){
                UI.copy(text).then(()=>{
                  this._setText("Copied!")
                  setTimeout(function(){ context_menu.hide(); },300);
                }).catch((e)=>{
                  this._setText("Copy: "+e);
                  setTimeout(function(){ context_menu.hide(); },300);

                  var popup =  UI.popup(UI.buildUI([
                    $("<h1>").text("Copy to clipboard"),{
                      type: "help",
                      help: "Automatic copying failed ("+e+"). Instead you can manually copy from the field below."
                    },{
                      type: "str",
                      label: "Text",
                      value: text,
                      rows: Math.ceil(text.length/50+2)
                    }
                  ]));
                  popup.element.find("textarea").select();
                });
              },"copy","Copy "+text+" to the clipboard"
            ]]
          ],e);
        }).click(function(e){
          this.dispatchEvent(new MouseEvent("contextmenu",e));
          e.stopPropagation();
        });
      }

      let $values = $("<span>").addClass("values");
      let values = [];
      switch (kind) {
        case "RTMP": {
          function buildRTMPurl(nopass,port) {
            return "rtmp://"+host.host+port+"/"+(passw && !nopass ? passw : "live")+"/";
          }
          if (rawmode) {
            values = {
              full_url: [],
              pairs: {}
            };
            for (let port of ports["RTMP"]) {
              for (const key of streamkeys) {
                values.full_url.push(buildRTMPurl(key != streamname,port) + key);
              }
              for (const key of streamkeys) {
                const url = buildRTMPurl(key != streamname,port);
                if (!(url in values.pairs)) { values.pairs[url] = []; }
                values.pairs[url].push(key);
              }
            }
            break;
          }

          for (let port of ports["RTMP"]) {
            let $sub = $("<div>").addClass("sub");
            for (const key of streamkeys) {
              $sub.append(
                $("<label>").append(
                  $("<span>").addClass("label")
                ).append(
                  createSpan(buildRTMPurl(key != streamname,port) + key,"Use this RTMP url if your client doens't ask for a stream key")
                )
              );
            }
            $sub.children().first().children().first().text("Full url:");
            $values.append($sub);

            $sub = $("<div>").addClass("sub");
            let theurl;
            let n = 0;
            for (const key of streamkeys) {
              let newurl = buildRTMPurl(key != streamname,port);
              if (theurl != newurl) {
                if ($sub.children().length) $values.append($sub);
                $sub = $("<div>").addClass("sub");
                $sub.append(
                  $("<label>").append(
                    $("<span>").addClass("label").text(n ? "Or url:" : "Url:")
                  ).append(
                    createSpan(newurl,"Use this RTMP url if your client also asks for a stream key")
                  )
                ).append(
                  $("<label>").append(
                    $("<span>").addClass("label").text("with key:")
                  ).append(
                    createSpan(key)
                  )
                );
                theurl = newurl;
                n++;
              }
              else {
                $sub.append(
                  $("<label>").append(
                    $("<span>").addClass("label")
                  ).append(
                    createSpan(key)
                  )
                );
              }
            }
            $values.append($sub);
          }

          break;
        }
        case "TSSRT": {
          label = "SRT";
          if (source.slice(0,6) == "srt://") { 
            let out;
            if (custport) {
              var source_parsed = parseURL(source.replace());
              if (source_parsed.host == "") {
                //url is invalid, parser gets funky
                source_parsed = parseURL(source.replace(/^srt:\/\//,"http://localhost"));
                source_parsed.host = source_parsed.host.replace(/^localhost/,"");
              }
              if ((source_parsed.host != "") && (!source_parsed.search || !source_parsed.searchParams || source_parsed.searchParams.get("mode") != "listener")) {
                out = "Caller mode: pulling stream from provided source.";
              }
              else if (source_parsed.search && source_parsed.searchParams && (source_parsed.searchParams.get("mode") == "caller")) {
                out = "Caller mode: you should probably add an address.";
              }
              else {
                out = rawmode ? 'srt://'+host.host+custport : createSpan('srt://'+host.host+custport)
              }
              //if adres -> caller of ?mode=caller, geen push url
              //als ?mode=listener, wel push url
            }
            else {
              out = "You must specify a port.";
            }
            values.push(out);
            $values.append(typeof out == "string" ? $("<span>").addClass("value").text(out) : out);
          }
          else {
            for (let port of ports["TSSRT"]) {
              for (const key of streamkeys) {
                if (passw && (key == streamname)) {
                  //if there is a @password, SRT cannot set it. So this will only work if there is a streamkey
                  if (streamkeys.length <= 1) {
                    let out = "SRT cannot be used for input if there is a @password. Did you want to configure an SRT passphrase instead?";
                    values.push(out);
                    $values.append(
                      $("<span>").addClass("value").text(out)
                    );
                  }
                  continue;
                  //TODO remove?
                  if ((passw.length < 10) || (passw.length > 79)) {
                    let out = "For SRT, the password length must be between 10 and 79 characters.";
                    values.push(out);
                    $values.append(
                      $("<span>").addClass("value").text(out)
                    );
                    continue;
                  }
                }
                let out = 'srt://'+host.host+port.port+'?streamid='+key+(port.passphrase ? "&passphrase="+port.passphrase : "");
                values.push(out);
                $values.append(createSpan(out));
              }
            }
          }
          break;
        }
        case "WebRTC": {
          label += " (WHIP)";
          
          //http(s)://localhost/webrtc/streamname
          //TODO skip if passw
          
          //gather hosts
          let hosts = {}; //in an object to de-duplicate
          for (let kind of ["HTTP","HTTPS"]) {
            for (let port of ports[kind]) {
              if (typeof port == "string") {
                //it's a port
                hosts[kind.toLowerCase()+"://"+host.host+port] = 1;
              }
              else {
                //it's an array of public urls
                for (let url of port) {
                  hosts[url] = 1;
                }
              }
            }
          }
          hosts = Object.keys(hosts);

          for (let host of hosts) {
            for (const key of streamkeys) {
              if (passw && (key == streamname)) {
                //@password is not implemented for WebRTC WHIP input
                if (streamkeys.length <= 1) {
                  let out = label+" cannot be used for input if there is a @password.";
                  values.push(out);
                  $values.append(
                    $("<span>").addClass("value").text(out)
                  );
                }
                continue;
              }

              let out = host+(host[host.length-1] == "/" ? "" : "/")+"webrtc/"+key;
              values.push(out);
              $values.append(createSpan(out));
            }
          }

          break;
        }
        case "RTSP": {
          for (const port of ports["RTSP"]) {
            for (const key of streamkeys) {
              let out = 'rtsp://'+host.host+port+'/'+(key)+(passw && (key == streamname) ? '?pass='+passw : '')
              values.push(out);
              $values.append(
                createSpan(out)
              );
            }
          }
          break;
        }
        case "TS": {
          if ((source.charAt(0) == "/") || (source.slice(0,7) == "ts-exec")) {
            return; //do not return section
          }
          if (source.slice(0,8) == "tsudp://") {
            let out = 'udp://'+(ip == '' ? host.host : ip)+(custport ? custport : ":[port]")+'/';
            values.push(out);
            $values.append(
              createSpan(out)
            );
          }
          else { return; }
          break;
        }
      }

      if (rawmode) {
        rawmode[label] = values;
        return;
      }

      let $section = $("<label>").append(
        $("<span>").addClass("label").text(label+":")
      );
      $section.append($values);

      return $section;
    }


    if (rawmode) {
      for (const kind of show) {
        createSection(kind);
      }
    }
    else {
      $cont.addClass("livestreamhint").html(
        $('<span>').addClass("unit").html(
          $("<span>").addClass("info").text("i")
        )
      ).append(
        $('<b>').text('Configure your source to push to:')
      );

      if (streamkeys.length == 0) {
        $cont.append($("<div>").text("There is no valid push endpoint with your current settings."));
      }
      else {
        for (const kind of show) {
          $cont.append(createSection(kind));
        }
        $cont.append(context_menu.ele);
      }
    }
    return rawmode || $cont;
  },
  buildUI: function(elements){
    /*elements should be an array of objects, the objects containing the UI element options 
     * (or a jQuery object that will be inserted instead).
    
    element options: 
      {
        label: 'Username',                                      //label to display in front of the field
        type: 'str',                                           //type of input field
        pointer: {main: mist.user, index: 'name'},              //pointer to the value this input field controls
        help: 'You should enter your username here.',           //what should be displayed in the integrated help balloon
        validate: ['required',function(){}]                     //how the input should be validated
      }
    */
    
    var $c = $('<div>').addClass('input_container');
    for (var i in elements) {
      var e = elements[i];
      if (e === false) { continue; }
      if (e instanceof jQuery) {
        $c.append(e);
        continue;
      }
      if (e.type == 'help') {
        var $s = $('<span>').addClass('text_container').append(
          $('<span>').addClass('description').append(e.help)
        );
        $c.append($s);
        if ('classes' in e) {
          for (var j in e.classes) {
            $s.addClass(e.classes[j]);
          }
        }
        continue;
      }
      if (e.type == 'text') {
        $c.append(
          $('<span>').addClass('text_container').append(
            $('<span>').addClass('text').append(e.text)
          )
        );
        continue;
      }
      if (e.type == 'custom') {
        $c.append(e.custom);
        continue;
      }
      if (e.type == 'buttons') {
        var $bc = $('<span>').addClass('button_container').on('keydown',function(e){
          e.stopPropagation();
        });
        if ('css' in e) {
          $bc.css(e.css);
        }
        $c.append($bc);
        for (var j in e.buttons) {
          var button = e.buttons[j];
          var $b = $('<button>').text(button.label).data('opts',button);
          if ('css' in button) {
            $b.css(button.css);
          }
          if ('classes' in button) {
            for (var k in button.classes) {
              $b.addClass(button.classes[k]);
            }
          }
          $bc.append($b);
          switch (button.type) {
            case 'cancel':
              $b.addClass('cancel').attr("data-icon","cross").click(button['function']);
              break;
            case 'save':
              $b.addClass('save').attr("data-icon","check").click(function(e){
                var fn = $(this).data('opts')['preSave'];
                if (fn) { fn.call(this); }
                
                var $ic = $(this).closest('.input_container');
                
                //ensure any grouped options (with non-default settings) are expanded
                $ic.find(".itemgroup:has(.summary:not(:empty))").addClass("expanded");

                //skip any hidden fields
                
                //validate
                var error = false;
                $ic.find('.hasValidate:visible, input[type="hidden"].hasValidate').each(function(){
                  var vf = $(this).data('validate');
                  error = vf(this,true); //focus the field if validation failed
                  if (error) {
                    return false; //break loop
                  }
                });
                var fn = $(this).data('opts')['failedValidate'];
                if (fn) { fn.call(this); }
                if (error) { return; } //validation failed
                
                //for all inputs
                $ic.find('.isSetting:visible, input[type="hidden"].isSetting').each(function(){
                  var val = $(this).getval();
                  var pointer = $(this).data('pointer');
                  
                  if (val === '') {
                    if ('default' in $(this).data('opts')) {
                      val = $(this).data('opts')['default'];
                    }
                    else {
                      //this value was not entered
                      pointer.main[pointer.index] = null;
                      return true; //continue
                    }
                  }
                  
                  //save
                  pointer.main[pointer.index] = val;

                  var fn = $(this).data('opts')['postSave'];
                  if (fn) { fn.call(this); }
                  
                });
                
                var fn = $(this).data('opts')['function'];
                if (fn) { fn(this); }
              });
              break;
            default:
              $b.click(button['function']);
              break;
          }
          if ("icon" in button) {
            $b.attr("data-icon",button.icon);
          }
        }
        continue;
      }
      
      var $e = $('<label>').addClass('UIelement');
      $c.append($e);
      
      if ('css' in e) {
        $e.css(e.css);
      }
      
      //label
      $e.append(
        $('<span>').addClass('label').html(('label' in e ? e.label+':' : ''))
      );
      if ('classes' in e) {
        for (var k in e.classes) {
          $e.addClass(e.classes[k]);
        }
      }
      
      //field
      var $fc = $('<span>').addClass('field_container');
      $e.append($fc);
      var $field;
      switch (e.type) {
        case 'password':
          $field = $('<input>').attr('type','password');
          break;
        case 'int':
          $field = $('<input>').attr('type','number');
          if ('min' in e) {
            $field.attr('min',e.min);
          }
          if ('max' in e) {
            $field.attr('max',e.max);
          }
          if (!('step' in e)) { e.step = 1; }
          $field.attr('step',e.step);
          
          if ('validate' in e) {
            e.validate.push('int');
          }
          else {
            e.validate = ['int'];
          }
          break;
        case 'span':
          $field = $('<span>');
          break;
        case 'debug':
          e.select = [
            ['','Default'],
            [0,'0 - NONE: All debugging messages disabled'],
            [1,'1 - FAIL: Messages about failed operations'],
            [2,'2 - ERROR: Previous level, and error messages'],
            [3,'3 - WARN: Previous level, and warning messages'],
            [4,'4 - INFO: Previous level, and status messages for development'],
            [5,'5 - MEDIUM: Previous level, and more status messages for development'],
            [6,'6 - HIGH: Previous level, and verbose debugging messages'],
            [7,'7 - VERY HIGH: Previous level, and very verbose debugging messages'],
            [8,'8 - EXTREME: Report everything in extreme detail'],
            [9,'9 - INSANE: Report everything in insane detail'],
            [10,'10 - DONTEVEN: All messages enabled']
          ];
        case 'select':
          $field = $('<select>');
          for (var j in e.select) {
            var $option = $('<option>');
            if (typeof e.select[j] == 'string') {
              $option.text(e.select[j]);
            }
            else {
              $option.val(e.select[j][0]).text(e.select[j][1])
            }
            $field.append($option);
          }
          break;
        case 'textarea':
          $field = $('<textarea>').on('keydown',function(e){
            e.stopPropagation();
          });
          break;
        case 'checkbox':
          $field = $('<input>').attr('type','checkbox');
          break;
        case 'hidden':
          $field = $('<input>').attr('type','hidden');
          $e.hide();
          break;
        case 'email':
          $field = $('<input>').attr('type','email').attr('autocomplete','on').attr('required','');
          break;
        case 'browse':
          $field = $('<input>').attr('type','text');
          if ('filetypes' in e) { 
            $field.data('filetypes',e.filetypes);
          }
          break;
        case 'geolimited':
        case 'hostlimited':
          $field = $('<input>').attr('type','hidden');
          //the custom subUI is defined later, but this hidden input will hold the processed value
          break;
        case 'radioselect':
          $field = $('<div>').addClass('radioselect');
          for (var i in e.radioselect) {
            var $radio = $('<input>').attr('type','radio').val(e.radioselect[i][0]).attr('name',e.label);
            if (e.readonly) {
              $radio.prop('disabled',true);
            }
            var $label = $('<label>').append(
              $radio
            ).append(
              $('<span>').html(e.radioselect[i][1])
            );
            $field.append($label);
            if (e.radioselect[i].length > 2) {
              var $select = $('<select>').change(function(){
                $(this).parent().find('input[type=radio]:enabled').prop('checked','true');
              });
              $label.append($select);
              if (e.readonly) {
                $select.prop('disabled',true);
              }
              for (var j in e.radioselect[i][2]) {
                var $option = $('<option>')
                $select.append($option);
                if (e.radioselect[i][2][j] instanceof Array) {
                  $option.val(e.radioselect[i][2][j][0]).html(e.radioselect[i][2][j][1]);
                }
                else {
                  $option.html(e.radioselect[i][2][j])
                }
              }
            }
          }
          break;
        case 'checklist':
          $field = $('<div>').addClass('checkcontainer');
          $controls = $('<div>').addClass('controls');
          $checklist = $('<div>').addClass('checklist');
          $field.append($checklist);
          for (var i in e.checklist) {
            if (typeof e.checklist[i] == 'string') {
              e.checklist[i] = [e.checklist[i], e.checklist[i]];
            }
            $checklist.append(
              $('<label>').text(e.checklist[i][1]).prepend(
                $('<input>').attr('type','checkbox').attr('name',e.checklist[i][0])
              )
            );
          }
          break;
        case 'DOMfield':
          $field = e.DOMfield;
          break;
        case "unix":
          $field = $("<input>").attr("type","datetime-local").attr("step",1);
          e.unit = $("<button>").text("Now").click(function(){
            $(this).closest(".field_container").find(".field").setval((new Date()).getTime()/1e3);
          });
          break;
        case "selectinput":
          $field = $('<div>').addClass('selectinput');
          var $select = $("<select>");
          $field.append($select);
          $select.data("input",false);
          
          for (var i in e.selectinput) {
            var $option = $("<option>");
            $select.append($option);
            if (typeof e.selectinput[i] == "string") {
              $option.text(e.selectinput[i]);
            }
            else {
              $option.text(e.selectinput[i][1]);
              if (typeof e.selectinput[i][0] == "string") {
                $option.val(e.selectinput[i][0])
              }
              else {
                $option.val("CUSTOM");
                if (!$select.data("input")) {
                  $select.data("input",UI.buildUI([e.selectinput[i][0]]).children());
                }
              }
            }
          }
          if ($select.data("input")) {
            $field.append($select.data("input"));
          }
          $select.change(function(){
            if ($(this).val() == "CUSTOM") {
              $(this).data("input").css("display","flex");
            }
            else {
              $(this).data("input").hide();
            }
          });
          $select.trigger("change");
          break;
        case "inputlist":
          function createField(e) {
            let $field = $('<div>').addClass('inputlist');
            var newitem = function(){
              var $part;
              if ("input" in e) {
                $part = UI.buildUI([e.input]).find(".field_container");
                //forward help container
                $part.find(".field").data("help_container",function(){
                  return $field.data("help_container");
                });
              }
              else {
                var o = Object.assign({},e);
                delete o.validate;
                delete o.pointer;
                o.type = "str";
                $part = UI.buildUI([o]).find(".field_container");
              }
              $part.removeClass("isSetting");
              $part.addClass("listitem");

              var keyup = function(e){
                let $item = $(this).find(".field");
                if ($(this).is(":last-child")) {
                  if ($item.getval() != "") {
                    $(this).after(newitem());
                  }
                  else if (e.which == 8) { //backspace
                    $(this).prev().find(".field").focus();
                  }
                }
                else {
                  if ($item.getval() == "") {

                    //focus on the previous item
                    var $f = $(this).prev();
                    if (!$f.length) {
                      $f = $(this).next();
                    }
                    $f.find(".field").focus();

                    //remove this field's error balloons if any
                    let $hc = $item.data("help_container");
                    if ($hc) {
                      if (typeof $hc == "function") $hc = $hc();
                      if ($hc?.length) $hc.find(".err_balloon[data-uid='"+$item.data("uid")+"']")?.remove();
                    }

                    //remove the current field
                    $(this).remove();

                    //re-validate remaining field(s)
                    $field.find(".field").trigger('change');
                  }
                }
              };

              $part.keyup(keyup);
              return $part;
            };
            $field.data("newitem",newitem);

            $field.append($field.data("newitem")());

            $field.focus = function(){
              return $(this).find(".listitem").first().find(".field").focus()
            };

            return $field;
          }
          $field = createField(e);
          break;
        case "sublist": {
          //saves an array with objects contain more settings
          $field = $("<div>").addClass("sublist");
          var $curvals = $("<div>").addClass("curvals");
          $curvals.append($("<span>").text("None."));
          var $itemsettings = $("<div>").addClass("itemsettings");
          var $newitembutton = $("<button>").text("New "+e.itemLabel);
          var sublist = e.sublist;
          var local_e = e;
          var $local_field = $field;
          var $local_e = $e;
          $field.data("build",function(values,index){
            var savepos = index;
            
            //apply settings of var values
            for (var i in local_e.saveas) {
              if (!(i in values)) {
                delete local_e.saveas[i];
              }
            }
            local_e.saveas = Object.assign(local_e.saveas,values);
            
            var mode = "New";
            if (typeof index != "undefined") {
              mode = "Edit";
            }
            //Object.assign(e.saveas,values);
            var newUI = UI.buildUI(
              [$("<h4>").text(mode+" "+local_e.itemLabel)].concat(
                sublist
              ).concat([
                {
                  label: "Save first",
                  type: "str",
                  classes: ["onlyshowhelp"],
                  validate: [function(){
                    return {
                      msg: "Did you want to save this "+local_e.itemLabel+"?",
                      classes: ["red"]
                    };
                  }]
                },{
                  type: "buttons",
                  buttons: [{
                    label: "Cancel",
                    type: "cancel",
                    "function": function(){
                      $itemsettings.html("");
                      $newitembutton.show();
                      $local_e.show();
                    }
                  },{
                    label: "Save "+local_e.itemLabel,
                    type: "save",
                    preSave: function(){
                      $(this).closest('.input_container').find(".onlyshowhelp").closest("label").hide();
                    },
                    failedValidate: function(){
                      $(this).closest('.input_container').find(".onlyshowhelp").closest("label").show();
                    },
                    "function": function(){
                      var savelist = $local_field.getval();
                      var save = Object.assign({},local_e.saveas);
                      for (var i in save) {
                        if (save[i] === null) {
                          delete save[i];
                        }
                      }
                      if (typeof savepos == "undefined") {
                        savelist.push(save);
                      }
                      else {
                        savelist[savepos] = save;
                      }
                      $local_field.setval(savelist);
                      $itemsettings.html("");
                      $newitembutton.show();
                      $local_e.show();
                    }
                  }]
                }
              ])
            );
            $itemsettings.html(newUI);
            $newitembutton.hide();
            $local_e.hide();
          });
          var $sublistfield = $field;
          $newitembutton.click(function(){
            $sublistfield.data("build")({});
          });
          sublist.unshift({
            type: "str",
            label: "Human readable name",
            placeholder: "none",
            help: "A convenient name to describe this "+e.itemLabel+". It won't be used by MistServer.",
            pointer: {
              main: e.saveas,
              index: "x-LSP-name"
            }
          });
          $field.data("savelist",[]);
          $field.append($curvals).append($newitembutton);
          $c.append($itemsettings);
          break;
        }
        case "json": {
          $field = $("<textarea>").on('keydown',function(e){
            e.stopPropagation();
          }).on('keyup change',function(e){
            this.style.height = "";
            this.style.height = (this.scrollHeight ? this.scrollHeight + 20 : this.value.split("\n").length*14 + 20)+"px";
          }).css("min-height","3em");
          var f = function (val,me){
            if ($(me).val() == "") { return; }
            if (val === null) {
              return {
                msg: 'Invalid json',
                classes: ['red']
              }
            }
          };
          if ('validate' in e) {
            e.validate.push(f);
          }
          else {
            e.validate = [f];
          }
          break;
        }
        case "bitmask": {
          $field = $("<div>").addClass("bitmask");
          for (var i in e.bitmask) {
            $field.append(
              $("<label>").append(
                $("<input>").attr("type","checkbox").attr("name","bitmask_"+("pointer" in e ? e.pointer.index : "")).attr("value",e.bitmask[i][0]).addClass("field")
              ).append(
                $("<span>").text(e.bitmask[i][1])
              )
            );
          }

          //when the main label is clicked, do nothing (instead of toggeling the first checkbox)
          $e.attr("for","none");
          break;
        }
        case "group":{
          let $cont = $("<div>").addClass("itemgroup");
          let children = e.options;
          let $summary = $("<ul>").addClass("summary");
          children.unshift($summary);
          if ("help" in e) {
            children.unshift(
              $("<span>").addClass("description").text(e.help)
            );
          }
          if ("label" in e) {
            children.unshift(
              $("<b>").text(e.label).attr("tabindex",0).keydown(function(e){
                e.stopPropagation();
                if (e.which == 13) {
                  //enter
                  $(this).trigger("click");
                }
              }).click(function(){
                $cont.toggleClass("expanded")
              }).attr("title","Click to show / hide these options")
            );
          }
          if (e.expand || (!(e.expand === false) && Object.keys(e.options).length < 2)) {
            //do not collapse fields on creation if expand: true is passed
            //always collapse fields if expand: false is passed
            //otherwise, collapse if group contains 2 fields or more
            $cont.addClass("expanded"); 
          }
          $cont.change(function(){
            $summary.html("");
            $(this).find(".isSetting, input[type=\"hidden\"].isSetting").each(function(){ 
              var val = $(this).getval();
              if (val == "") { return; }
              var opts = $(this).data('opts');
              if (val != opts['default']) {
                var label = opts["label"]+": ";
                switch (opts.type) {
                  case "select": {
                    val = opts.select.filter(function(v){ if (v[0] == val) return true; return false; })[0][1]; break;
                  }
                  case "unix": {
                    val = UI.format.dateTime(val); break;
                  }
                  case "checkbox": {
                    val = ""; 
                    label = label.slice(0,-2);
                    break;
                  }
                  case "inputlist": {
                    val = val.join(", ");
                    break;
                  }
                }
                $summary.append(
                  $("<li>").addClass("setting").append(
                    $("<span>").addClass("label").text(label)
                  ).append(
                    $("<span>").text(val)
                  ).append(
                    $("<span>").addClass("unit").text(typeof opts.unit  == "string" ? opts["unit"] : "")
                  )
                );
              }
            });
          }).append(UI.buildUI(children)).trigger("change");
          $c.append($cont); //add this to input_container
          $e.remove(); //remove the created label UIelement from input_container
          continue; //continue for (var i in elements)
          break;
        }
        case "str": 
        default: {
          $field = $('<input>').attr('type','text');
          if ("maxlength" in e) {
            $field.attr("maxlength",e.maxlength);
          }
          if ("minlength" in e) {
            $field.attr("minlength",e.minlength);
          }
        }
      }
      $field.addClass('field').data('opts',e).data('uid',Math.random().toString().slice(2));

      //add generic field options
      if ('pointer' in e) { $field.attr('name',e.pointer.index); }
      $fc.append($field);
      if ('classes' in e) {
        for (var j in e.classes) {
          $field.addClass(e.classes[j]);
        }
      }
      if ('placeholder' in e) {
        $field.attr('placeholder',e.placeholder);
      }
      if ('default' in e) {
        $field.attr('placeholder',e['default']);
      }
      if ('unit' in e) {
        if (Array.isArray(e.unit)) {
          var $unit = $("<select>").change(function(){
            var $field = $(this).closest(".field_container").find(".field");
            var e = $field.data("opts");
            var curval = $field.getval(); //get "raw" value of field

            //save factor and apply to min/max attributes
            e.factor = Number($(this).val());
            if ("min" in e) {
              $field.attr("min",e.min / e.factor);
            }
            if ("max" in e) {
              $field.attr("max",e.max / e.factor);
            }
            if ("step" in e) {
              $field.attr("step",e.step / e.factor);
            }
            if ("placeholder" in e) {
              $field.attr("placeholder",e.placeholder / e.factor);
            }

            //set "raw" value of field to update displayed value
            if (Number(curval) != 0) $field.setval(curval);
          });
          for (var i in e.unit) {
            $unit.append($("<option>").val(e.unit[i][0]).text(e.unit[i][1]));
          }
          $fc.append(
            $('<span>').addClass('unit').html($unit)
          );
          $unit.trigger("change");
          $field.change(function(e,kind){
            //initially, set the unit selector to the unit that has the shortest value string length
            //this will not trigger when the value field is changed for other reasons
            if (kind == "initial") {
              var val = $(this).getval();
              var opts = $(this).data("opts");
              if ((opts.placeholder) && (Number(val) == 0)) {
                val = opts.placeholder;
              }
              if (Number(val) != 0) {
                //set unit to "shortest" value
                var shortest = 1e9;
                var unit = false;
                for (var i in opts.unit) {
                  //calculate display value for this unit
                  var display = (val / opts.unit[i][0]).toString();
                  if (display.length < shortest) {
                    shortest = display.length;
                    unit = opts.unit[i][0];
                  }
                }
                if (unit) {
                  $(this).closest(".field_container").find(".unit select").val(unit).trigger("change");
                }
              }
            }
          });
          $field.trigger("change","initial");
        }
        else {
          $fc.append(
            $('<span>').addClass('unit').html(e.unit)
          );
        }
      }
      if ('prefix' in e) {
        $fc.prepend(
          $('<span>').addClass('unit').html(e.prefix)
        );
      }
      if ('readonly' in e) {
        $field.attr('readonly','readonly');
        $field.click(function(){
          if (this.selectionStart == this.selectionEnd) { //nothing has been selected yet
            $(this).select();
          }
        });
      }
      if ('qrcode' in e) {
        $fc.append(
          $('<span>').addClass('unit').html(
            $('<button>').text('QR').on('keydown',function(e){
            e.stopPropagation();
          }).click(function(){
              var text = String($(this).closest('.field_container').find('.field').getval());
              var $qr = $('<div>').addClass('qrcode');
              UI.popup(
                $('<span>').addClass('qr_container').append(
                  $('<p>').text(text)
                ).append($qr)
              );
              $qr.qrcode({
                text: text,
                size: Math.min($qr.width(),$qr.height())
              })
            })
          )
        );
      }
      if (('clipboard' in e) && (document.queryCommandSupported('copy'))) {
        $fc.append(
          $('<span>').addClass('unit').html(
            $('<button>').text('Copy').on('keydown',function(e){
            e.stopPropagation();
          }).click(function(){
              var text = String($(this).closest('.field_container').find('.field').getval());
              
              var textArea = document.createElement("textarea");
              textArea.value = text;
              document.body.appendChild(textArea);
              textArea.select();
              var yay = false;
              try {
                yay = document.execCommand('copy');
              } catch (err) {
                
              }
              if (yay) {
                $(this).text('Copied to clipboard!');
                document.body.removeChild(textArea);
                var me = $(this);
                setTimeout(function(){
                  me.text('Copy');
                },5e3);
              }
              else {
                document.body.removeChild(textArea);
                alert("Failed to copy:\n"+text);
              }
            })
          )
        );
      }
      if ('rows' in e) {
        $field.attr('rows',e.rows);
      }
      if ("dependent" in e) {
        for (var i in e.dependent) {
          $e.attr("data-dependent-"+i,e.dependent[i]);
        }
      }
      
      //additional field type code
      switch (e.type) {
        case 'browse':
          var $master = $('<div>').addClass('grouper').append($e);
          $c.append($master);
          
          
          var $browse_button = $('<button>').text('Browse').on('keydown',function(e){
            e.stopPropagation();
          });
          $fc.append($browse_button);
          $browse_button.click(function(){
            var $bc = $('<div>').addClass('browse_container');
            var $field = $(this).siblings(".field");
            var $fields;
            var $c = $(this).closest('.grouper');
            if ($c.length) {
              $c.append($bc);
              $fields = $field;
            }
            else {
              //this browse field is probably part of an inputlist
              if ($(this).closest(".inputlist").length) {
                $bc.insertAfter($(this).closest(".listitem"));
                $fields = $bc.siblings(".field_container").find('.field');
              }
              else{
                throw "Could not locate browse grouper container";
              }
            }
            $fields.attr('readonly','readonly').attr("disabled","disabled").css('opacity',0.5);
            var $browse_button = $(this);
            var $cancel = $('<button>').text('Stop browsing').click(function(){
                $bc.remove();
                $fields.removeAttr('readonly').removeAttr("disabled").css('opacity',1);
                $field.trigger("change");
            });
            
            var $path = $('<span>').addClass('field');
            
            var $folder_contents = $('<div>').addClass('browse_contents');
            var $folder = $('<a>').addClass('folder');
            var filetypes = $field.data('filetypes');
            
            $bc.append(
              $('<label>').addClass('UIelement').append(
                $('<span>').addClass('label').text('Current folder:')
              ).append(
                $('<span>').addClass('field_container').append($path).append(
                  $cancel
                )
              )
            ).append(
              $folder_contents
            );
            
            function browse(path){
              $folder_contents.text('Loading..');
              mist.send(function(d){
                $path.text(d.browse.path[0]);
                $field.setval(d.browse.path[0]+(d.browse.path[0].slice(-1) == '/' ? '' : '/')).trigger("keyup");
                $folder_contents.html(
                  $folder.clone(true).text('..').attr('title','Folder up')
                );
                if (d.browse.subdirectories) {
                  d.browse.subdirectories.sort();
                  for (var i in d.browse.subdirectories) {
                    var f = d.browse.subdirectories[i];
                    $folder_contents.append(
                      $folder.clone(true).attr('title',$path.text()+($path.text().slice(-1) == '/' ? '' : '/')+f).text(f)
                    );
                  }
                }
                if (d.browse.files) {
                  d.browse.files.sort();
                  for (var i in d.browse.files) {
                    var f = d.browse.files[i];
                    var src = $path.text()+(($path.text().slice(-1*seperator.length) == seperator ? '' : seperator))+f;
                    var $file = $('<a>').text(f).addClass('file').attr('title',src);
                    $folder_contents.append($file);
                    
                    if (filetypes) {
                      var hide = true;
                      for (var j in filetypes) {
                        if (typeof filetypes[j] == 'undefined') {
                          continue;
                        }
                        if (mist.inputMatch(filetypes[j],src)) {
                          hide = false;
                          break;
                        }
                      }
                      if (hide) { $file.hide(); }
                    }
                    
                    $file.click(function(){
                      var src = $(this).attr('title');
                      
                      $field.setval(src);
                      $fields.removeAttr('readonly').removeAttr("disabled").css('opacity',1);
                      $bc.remove();
                      $field.trigger("keyup").trigger("change");
                    });
                  }
                }
              },{browse:path});
            }
            
            //determine file path seperator (/ for linux and \ for windows)
            var seperator = '/';
            if (mist.data.config.version.indexOf('indows') > -1) {
              //without W intended to prevent caps issues ^
              seperator = '\\';
            }
            $folder.click(function(){
              var path = $path.text()+(($path.text().slice(-1*seperator.length) == seperator ? '' : seperator))+$(this).text();
              browse(path);
            });
            
            var path = $field.getval();
            
            var protocol = path.split('://');
            if (protocol.length > 1) {
              if (protocol[0] == 'file') {
                path = protocol[1];
              }
              else {
                path = '';
              }
            }
            
            
            path = path.split(seperator);
            path.pop();
            path = path.join(seperator);
            
            browse(path);
            
          });
          break;
        case 'geolimited':
        case 'hostlimited':
          var subUI = {
            field: $field
          };
          subUI.blackwhite = $('<select>').append(
            $('<option>').val('-').text('Blacklist')
          ).append(
            $('<option>').val('+').text('Whitelist')
          );
          subUI.values = $('<span>').addClass('limit_value_list');
          switch (e.type) {
            case 'geolimited':
              subUI.prototype = $('<select>').append(
                $('<option>').val('').text('[Select a country]')
              );
              for (var i in UI.countrylist) {
                subUI.prototype.append(
                  $('<option>').val(i).html(UI.countrylist[i])
                );
              }
              break;
            case 'hostlimited':
              subUI.prototype = $('<input>').attr('type','text').attr('placeholder','type a host');
              break;
          }
          subUI.prototype.on('change keyup',function(){
            var subUI = $(this).closest('.field_container').data('subUI');
            subUI.blackwhite.trigger('change');
          });
          subUI.blackwhite.change(function(){
            var subUI = $(this).closest('.field_container').data('subUI');
            var values = [];
            var lastval = false;
            subUI.values.children().each(function(){
              lastval = $(this).val();
              if (lastval != '') {
                values.push(lastval);
              }
              else {
                $(this).remove();
              }
            });
            subUI.values.append(subUI.prototype.clone(true));
            if (values.length > 0) {
              subUI.field.val($(this).val()+values.join(' '));
            }
            else {
              subUI.field.val('');
            }
            subUI.field.trigger('change');
          });
          subUI.values.append(subUI.prototype.clone(true));
          $fc.data('subUI',subUI).addClass('limit_list').append(subUI.blackwhite).append(subUI.values);
          break;
      }
     
      if ('value' in e) {
        $field.setval(e.value,["initial"]);
      }
      if ('pointer' in e) {
        $field.data('pointer',e.pointer).addClass('isSetting');
        if (e.pointer.main) {
          var val = e.pointer.main[e.pointer.index];
          if (typeof val != 'undefined') {
            $field.setval(val,["initial"]);
          }
        }
      }

      if ('datalist' in e) {
        var r = 'datalist_'+i+MD5($field[0].outerHTML); //magic to hopefully make sure the id is unique
        $field.attr('list',r);
        var $datalist = $('<datalist>').attr('id',r);
        $fc.append($datalist);
        for (var i in e.datalist) {
          $datalist.append(
            $('<option>').val(e.datalist[i])
          );
        }
      }
      
      //integrated help
      var $ihc = $('<span>').addClass('help_container');
      $e.append($ihc);
      $field.data('help_container',$ihc);
      if ('help' in e) {
        $ihc.append(
          $('<span>').addClass('ih_balloon').html(e.help)
        );
        $field.on('focusin mouseover',function(){
          $(this).closest('label').addClass('active');
        }).on('focusout mouseout',function(){
          $(this).closest('label').removeClass('active');
        });
      }
      
      //validation
      if ('validate' in e) {
        var fs = [];
        for (var j in e.validate) {
          var validate = e.validate[j];
          var f;
          if (typeof validate == 'function') {
            //custom validation function
            f = validate;
          }
          else {
            switch (validate) {
              case 'required':
                f = function(val,me){
                  if ((val == '') || (val == null)) {
                    return {
                      msg:'This is a required field.',
                      classes: ['red']
                    }
                  }
                  return false;
                }
                break;
              case 'int':
                f = function(val,me) {
                  var ele = $(me).data('opts');
                  if (!me.validity.valid) {
                    if ("factor" in ele && (ele.factor != 1)) {
                      var msg = 'Please enter a number';
                      var msgs = [];
                      if (me.validity.stepMismatch) {
                        msg += " divisible by "+(ele.step/ele.factor);
                      }
                      else if (me.validity.rangeUnderflow || me.validity.rangeOverflow) {
                        var unit = $(me).closest(".field_container").find(".unit select option:selected").text();
                        if ('min' in ele) {
                          msgs.push(' greater than or equal to '+(ele.min/ele.factor)+" "+unit);
                        }
                        if ('max' in ele) {
                          msgs.push(' smaller than or equal to '+(ele.max/ele.factor)+" "+unit);
                        }
                      }
                      return {
                        msg: msg+msgs.join(' and')+'.',
                        classes: ['red']
                      };

                    }
                    else {
                      var msg = 'Please enter an integer';
                      var msgs = [];
                      if ('min' in ele) {
                        msgs.push(' greater than or equal to '+ele.min);
                      }
                      if ('max' in ele) {
                        msgs.push(' smaller than or equal to '+ele.max);
                      }
                      return {
                        msg: msg+msgs.join(' and')+'.',
                        classes: ['red']
                      };
                    }
                  }
                }
                break;
              case 'streamname': {
                f = function(val,me) {
                  if (val == "") { return; }
                  
                  if (val.toLowerCase() != val) {
                    return {
                      msg: 'Uppercase letters are not allowed.',
                      classes: ['red']
                    };
                  }
                  if (val.replace(/[^\da-z_\-\.]/g,'') != val) {
                    return {
                      msg: 'Special characters (except for underscores (_), periods (.) and dashes (-)) are not allowed.',
                      classes: ['red']
                    };
                  }
                  //check for duplicate stream names
                  if (('streams' in mist.data) && (val in mist.data.streams)) {
                    //check that we're not simply editing the stream
                    if ($(me).data('pointer').main.name != val) {
                      return {
                        msg: 'This streamname already exists.<br>If you want to edit an existing stream, please click edit on the the streams tab.',
                        classes: ['red']
                      };
                    }
                  }
                };
                break;
              }
              case 'streamname_with_wildcard': {
                f = function(val,me) {
                  if (val == "") { return; }
                  
                  streampart = val.split("+");
                  var wildpart = streampart.slice(1).join("+");
                  streampart = streampart[0];
                  
                  //validate streampart
                  if (streampart.toLowerCase() != streampart) {
                    return {
                      msg: 'Uppercase letters are not allowed in a stream name.',
                      classes: ['red']
                    };
                  }
                  if (streampart.replace(/[^\da-z_]/g,'') != streampart) {
                    return {
                      msg: 'Special characters (except for underscores) are not allowed in a stream name.',
                      classes: ['red']
                    };
                  }
                  
                  if (streampart != val) {
                    //validate wildcard part
                    //anything is allowed except / and nullbytes
                    if (wildpart.replace(/[\00|\0|\/]/g,'') != wildpart) {
                      return {
                        msg: 'Slashes or null bytes are not allowed in wildcards.',
                        classes: ['red']
                      };
                    }
                  }
                };
                break;
              }
              case 'streamname_with_wildcard_and_variables': {
                f = function(val,me) {
                  if (val == "") { return; }
                  
                  streampart = val.split("+");
                  var wildpart = streampart.slice(1).join("+");
                  streampart = streampart[0];
                  
                  //validate streampart
                  if (streampart.toLowerCase() != streampart) {
                    return {
                      msg: 'Uppercase letters are not allowed in a stream name.',
                      classes: ['red']
                    };
                  }
                  if (streampart.replace(/[^\da-z_$]/g,'') != streampart) {
                    return {
                      msg: 'Special characters (except for underscores) are not allowed in a stream name.',
                      classes: ['red']
                    };
                  }
                  
                  if (streampart != val) {
                    //validate wildcard part
                    //anything is allowed except / and nullbytes
                    if (wildpart.replace(/[\00|\0|\/]/g,'') != wildpart) {
                      return {
                        msg: 'Slashes or null bytes are not allowed in wildcards.',
                        classes: ['red']
                      };
                    }
                  }
                };
                break;
              }
              case 'track_selector_parameter': {
                //the value passed to audio= or video=,
                //so something like 1, maxbps or eng
                //leave default for now
                f = function(){};
                break;

              }
              case 'track_selector': {
                //something like "audio=1&video=eng"
                //leave default for now

                f = function(){};
                break;

              }
              default:
                f = function(){};
                break;
            }
          }
          fs.push(f);
        }
        
        //add standard html5 validation
        if ("checkValidity" in $field[0]) {
          fs.push(function(val,me){
            if ("checkValidity" in me) {
              if (me.checkValidity() == false) {
                return {
                  msg: "validationMessage" in me ? me.validationMessage : "This value ("+val+") is invalid." ,
                  classes: ["red"]
                };
              }
            }
          });
        }

        $field.data('validate_functions',fs).data('validate',function(me,focusonerror){
          if ((!$(me).is(":visible")) && (!$(me).is("input[type=\"hidden\"]"))) { return; }
          var val = $(me).getval();
          var fs = $(me).data('validate_functions');
          var $ihc = $(me).data('help_container');
          var uid = $(me).data("uid");
          if (typeof $ihc == "function") {
            $ihc = $ihc();
          }
          $ihc.find('.err_balloon[data-uid="'+uid+'"]')?.remove(); //only remove balloons that were created by this field (relevant for inputlist)
          for (var i in fs) {
            var error = fs[i](val,me);
            if (error) {
              $err = $('<span>').addClass('err_balloon').attr("data-uid",uid).html(error.msg);
              for (var j in error.classes) {
                $err.addClass(error.classes[j]);
              }
              $ihc.prepend($err);
              if ((typeof error != "object") || (!("break" in error)) || (error.break)) {
                if (focusonerror) { $(me).focus(); }
                return true;
              }
            }
          }
          return false;
        }).addClass('hasValidate').on('change keyup',function(){
          var f = $(this).data('validate');
          f(this);
        });
        if ($field.getval() != '') {
          $field.trigger('change');
        }
      }
      
      if ('function' in e) {
        $field.on('change keyup',e['function']);
        $field.trigger('change');
      }
    }
    $c.on('keydown',function(e){
      var $button = false;
      switch (e.which) {
        case 13:
          //enter
          $button = $(this).find('button.save').first();
          break;
        case 27:
          //escape
          $button = $(this).find('button.cancel').first();
          break;
      }
      if (($button) && ($button.length)) {
        $button.trigger('click');
        e.stopPropagation();
      }
    });
    
    return $c;
  },
  buildVheaderTable: function(opts){
    var $table = $('<table>');
    var $header = $('<tr>').addClass('header').append(
      $('<td>').addClass('vheader').attr('rowspan',opts.labels.length+1).append(
        $('<span>').text(opts.vheader)
      )
    );
    var $trs = [];
    $header.append($('<td>'));
    for (var i in opts.labels) {
      $trs.push(
        $('<tr>').append(
          $('<td>').html((opts.labels[i] == '' ? '&nbsp;' : opts.labels[i]+':'))
        )
      );
    }
    
    for (var j in opts.content) {
      $header.append(
        $('<td>').html(opts.content[j].header)
      );
      for (var i in opts.content[j].body) {
        $trs[i].append(
          $('<td>').html(opts.content[j].body[i])
        );
      }
    }
    
    //no thead, it messes up the vheader
    $table.append(
      $('<tbody>').append($header).append($trs)
    );
    
    return $table;
  },
  plot: {
    addGraph: function(saveas,$graph_c){
      var graph = {
        id: saveas.id,
        xaxis: saveas.xaxis,
        datasets: [],
        elements: {
          cont: $('<div>').addClass('graph'),
          plot: $('<div>').addClass('plot'),
          legend: $('<div>').addClass('legend').attr('draggable','true')
        }
      }
      UI.draggable(graph.elements.legend);
      graph.elements.cont.append(
        graph.elements.plot
      ).append(
        graph.elements.legend
      );
      $graph_c.append(graph.elements.cont);
      return graph;
    },
    go: function(graphs) {
      if (Object.keys(graphs).length < 1) { return; }
      
      //get plotdata
      //build request object
      //changed data to show up in graphs to -15 sec to match API calls.
      var reqobj = {
        totals: [],
        clients: []
      };
      for (var g in graphs) {
        for (var d in graphs[g].datasets) {
          var set = graphs[g].datasets[d];
          switch (set.datatype) {
            case 'clients':
            case 'upbps':
            case 'downbps':
            case 'perc_lost':
            case 'perc_retrans':
              switch (set.origin[0]) {
                case 'total':     reqobj['totals'].push({fields: [set.datatype], end: -15});                               break;
                case 'stream':    reqobj['totals'].push({fields: [set.datatype], streams: [set.origin[1]], end: -15});     break;
                case 'protocol':  reqobj['totals'].push({fields: [set.datatype], protocols: [set.origin[1]], end: -15});   break;
              }
              break;
            case 'cpuload':
            case 'memload':
              reqobj['capabilities'] = {};
              break;
          }
        }
      }
      if (reqobj.totals.length == 0) { delete reqobj.totals; }
      if (reqobj.clients.length == 0) { delete reqobj.clients; }
      
      mist.send(function(){
        for (var g in graphs) {
          var graph = graphs[g];
          if (graph.datasets.length < 1) {
            graph.elements.plot.html('');
            graph.elements.legend.html('');
            return;
          }
          switch (graph.xaxis) {
            case 'time':
              var yaxes = [];
              graph.yaxes = {};
              var plotsets = [];
              
              for (var i in graph.datasets) {
                var dataobj = graph.datasets[i];
                if (dataobj.display) {
                  dataobj.getdata();
                  if (!(dataobj.yaxistype in graph.yaxes)) {
                    yaxes.push(UI.plot.yaxes[dataobj.yaxistype]);
                    graph.yaxes[dataobj.yaxistype] = yaxes.length;
                  }
                  dataobj.yaxis = graph.yaxes[dataobj.yaxistype];
                  //dataobj.color = Number(i);
                  plotsets.push(dataobj);
                }
              }
              
              if (yaxes[0]) { yaxes[0].color = 0; }
              graph.plot = $.plot(
                graph.elements.plot,
                plotsets,
                {
                  legend: {show: false},
                  xaxis: UI.plot.xaxes[graph.xaxis],
                  yaxes: yaxes,
                  grid: {
                    hoverable: true,
                    borderWidth: {top: 0, right: 0, bottom: 1, left: 1},
                    color: 'black',
                    backgroundColor: {colors: ['rgba(0,0,0,0)','rgba(0,0,0,0.025)']}
                  },
                  crosshair: {
                    mode: 'x'
                  }
                }
              );
              
              //now the legend
              var $list = $('<table>').addClass('legend-list').addClass('nolay').html(
                $('<tr>').html(
                  $('<td>').html(
                    $('<h3>').text(graph.id)
                  )
                ).append(
                  $('<td>').css('padding-right','2em').css('text-align','right').html(
                    $('<span>').addClass('value')
                  ).append(
                    $('<button>').data('opts',graph).text('X').addClass('close').click(function(){
                      var graph = $(this).data('opts');
                      if (confirm('Are you sure you want to remove '+graph.id+'?')) {
                        graph.elements.cont.remove();
                        var $opt = $('.graph_ids option:contains('+graph.id+')');
                        var $select = $opt.parent();
                        $opt.remove();
                        UI.plot.del(graph.id);
                        delete graphs[graph.id];
                        $select.trigger('change');
                        UI.plot.go(graphs);
                      }
                    })
                  )
                )
              );
              graph.elements.legend.html($list);
              
              function updateLegendValues(x) {
                var $spans = graph.elements.legend.find('.value');
                var n = 1;
                
                if (typeof x == 'undefined') {
                  $spans.eq(0).html('Latest:');
                }
                else {
                  var axis = graph.plot.getXAxes()[0];
                  x = Math.min(axis.max,x);
                  x = Math.max(axis.min,x);
                  $spans.eq(0).html(UI.format.time(x/1e3));
                }
                
                
                for (var i in graph.datasets) {
                  var label = '&nbsp;';
                  if (graph.datasets[i].display) {
                    var tickformatter = UI.plot.yaxes[graph.datasets[i].yaxistype].tickFormatter;
                    var data = graph.datasets[i].data;
                    if (!x) {
                      label = tickformatter(graph.datasets[i].data[graph.datasets[i].data.length-1][1]);
                    }
                    else {
                      for (var j in data) {
                        if (data[j][0] == x) {
                          label = tickformatter(data[j][1]);
                          break;
                        }
                        if (data[j][0] > x) {
                          if (j != 0) {
                            var p1 = data[j];
                            var p2 = data[j-1];
                            var y = p1[1] + (x - p1[0]) * (p2[1] - p1[1]) / (p2[0] - p1[0]);
                            label = tickformatter(y);
                          }
                          break;
                        }
                      }
                    }
                  }
                  $spans.eq(n).html(label);
                  n++;
                }
              }
              
              var plotdata = graph.plot.getOptions();
              for (var i in graph.datasets) {
                var $checkbox = $('<input>').attr('type','checkbox').data('index',i).data('graph',graph).click(function(){
                  var graph = $(this).data('graph');
                  if ($(this).is(':checked')) {
                    graph.datasets[$(this).data('index')].display = true;
                  }
                  else {
                    graph.datasets[$(this).data('index')].display = false;
                  }
                  var obj = {};
                  obj[graph.id] = graph;
                  UI.plot.go(obj);
                });
                if (graph.datasets[i].display) {
                  $checkbox.attr('checked','checked');
                }
                $list.append(
                  $('<tr>').html(
                    $('<td>').html(
                      $('<label>').html(
                        $checkbox
                      ).append(
                        $('<div>').addClass('series-color').css('background-color',graph.datasets[i].color)
                      ).append(
                        graph.datasets[i].label
                      )
                    )
                  ).append(
                    $('<td>').css('padding-right','2em').css('text-align','right').html(
                      $('<span>').addClass('value')
                    ).append(
                      $('<button>').text('X').addClass('close').data('index',i).data('graph',graph).click(function(){
                        var i = $(this).data('index');
                        var graph = $(this).data('graph');
                        if (confirm('Are you sure you want to remove '+graph.datasets[i].label+' from '+graph.id+'?')) {
                          graph.datasets.splice(i,1);
                          
                          if (graph.datasets.length == 0) {
                            graph.elements.cont.remove();
                            var $opt = $('.graph_ids option:contains('+graph.id+')');
                            var $select = $opt.parent();
                            $opt.remove();
                            $select.trigger('change');
                            UI.plot.del(graph.id);
                            delete graphs[graph.id];
                            UI.plot.go(graphs);
                          }
                          else {
                            UI.plot.save(graph);
                            
                            var obj = {};
                            obj[graph.id] = graph;
                            UI.plot.go(obj);
                          }
                        }
                      })
                    )
                  )
                );
              }
              updateLegendValues();
              
              //and the tooltip
              var lastval = false;
              graph.elements.plot.on('plothover',function(e,pos,item){
                if (pos.x != lastval) {
                  updateLegendValues(pos.x);
                  lastval = pos.x;
                }
                if (item) {
                  var $t = $('<span>').append(
                    $('<h3>').text(item.series.label).prepend(
                      $('<div>').addClass('series-color').css('background-color',item.series.color)
                    )
                  ).append(
                    $('<table>').addClass('nolay').html(
                      $('<tr>').html(
                        $('<td>').text('Time:')
                      ).append(
                        $('<td>').html(UI.format.dateTime(item.datapoint[0]/1e3,'long'))
                      )
                    ).append(
                      $('<tr>').html(
                        $('<td>').text('Value:')
                      ).append(
                        $('<td>').html(item.series.yaxis.tickFormatter(item.datapoint[1],item.series.yaxis))
                      )
                    )
                  );
                  
                  UI.tooltip.show(pos,$t.children());
                }
                else {
                  UI.tooltip.hide();
                }
              }).on('mouseout',function(){
                updateLegendValues();
              });
              
              break;
            case 'coords':
              break;
          }
        }
      },reqobj)
    },
    save: function(opts){
      var graph = {
        id: opts.id,
        xaxis: opts.xaxis,
        datasets: []
      };
      for (var i in opts.datasets) {
        graph.datasets.push({
          origin: opts.datasets[i].origin,
          datatype: opts.datasets[i].datatype
        });
      }
      
      var graphs = mist.stored.get().graphs || {};
      
      graphs[graph.id] = graph;
      mist.stored.set('graphs',graphs);
    },
    del: function(graphid){
      var graphs = mist.stored.get().graphs || {};
      delete graphs[graphid];
      mist.stored.set('graphs',graphs);
    },
    datatype: {
      getOptions: function (opts) {
        var general = $.extend(true,{},UI.plot.datatype.templates.general);
        var specialized = $.extend(true,{},UI.plot.datatype.templates[opts.datatype]);
        opts = $.extend(true,specialized,opts);
        opts = $.extend(true,general,opts);
        
        //append the origin to the label
        switch (opts.origin[0]) {
          case 'total':
            switch (opts.datatype) {
              case 'cpuload':
              case 'memload':
                break;
              default:
                opts.label += ' (total)';
            }
            break;
          case 'stream':
          case 'protocol':
            opts.label += ' ('+opts.origin[1]+')';
            break;
        }
        
        //slightly randomize the color
        var color = [];
        var variation = 50;
        for (var i in opts.basecolor) {
          var c = opts.basecolor[i];
          c += variation * (0.5 - Math.random());
          c = Math.round(c);
          c = Math.min(255,Math.max(0,c));
          color.push(c);
        }
        opts.color = 'rgb('+color.join(',')+')';
        
        return opts;
      },
      templates: {
        general: {
          display: true,
          datatype: 'general',
          label: '',
          yaxistype: 'amount',
          data: [],
          lines: { show: true },
          points: { show: false },
          getdata: function() {
            var streamindex = (this.origin[0] == 'stream' ? this.origin[1] : 'all_streams');
            var protocolindex = (this.origin[0] == 'protocol' ? this.origin[1] : 'all_protocols');
            var thedata = mist.data.totals[streamindex][protocolindex][this.datatype]
            this.data = thedata;
            return thedata;
          }
        },
        cpuload: {
          label: 'CPU use',
          yaxistype: 'percentage',
          basecolor: [237,194,64],
          cores: 1,
          getdata: function(dataobj){
            //remove any data older than 10 minutes
            var removebefore = false;
            for (var i in this.data) {
              if (this.data[i][0] < (mist.data.config.time-600)*1000) {
                removebefore = i;
              }
            }
            if (removebefore !== false) {
              this.data.splice(0,Number(removebefore)+1);
            }
            this.data.push([mist.data.config.time*1000,mist.data.capabilities.cpu_use/10]);
            return this.data;
          }
        },
        memload: {
          label: 'Memory load',
          yaxistype: 'percentage',
          basecolor: [175,216,248],
          getdata: function(){
            //remove any data older than 10 minutes
            var removebefore = false;
            for (var i in this.data) {
              if (this.data[i][0] < (mist.data.config.time-600)*1000) {
                removebefore = i;
              }
            }
            if (removebefore !== false) {
              this.data.splice(0,Number(removebefore)+1);
            }
            this.data.push([mist.data.config.time*1000,mist.data.capabilities.load.memory]);
            return this.data;
          }
        },
        clients: {
          label: 'Connections',
          basecolor: [203,75,75]
        },
        upbps: {
          label: 'Bandwidth up',
          yaxistype: 'bytespersec',
          basecolor: [77,167,77]
        },
        downbps: {
          label: 'Bandwidth down',
          yaxistype: 'bytespersec',
          basecolor: [148,64,237]
        },
        perc_lost: {
          label: 'Lost packages',
          yaxistype: 'percentage',
          basecolor: [255,33,234]
        },
        perc_retrans: {
          label: 'Re-transmitted packages',
          yaxistype: 'percentage',
          basecolor: [0,0,255]
        }
      }
    },
    yaxes: {
      percentage: {
        name: 'percentage',
        color: 'black',
        tickColor: 0,
        tickDecimals: 0,
        tickFormatter: function(val,axis){
          return UI.format.addUnit(UI.format.number(val),'%');
        },
        tickLength: 0,
        min: 0,
        max: 100
      },
      amount: {
        name: 'amount',
        color: 'black',
        tickColor: 0,
        tickDecimals: 0,
        tickFormatter: function(val,axis){
          return UI.format.number(val);
        },
        tickLength: 0,
        min: 0
      },
      bytespersec: {
        name: 'bytespersec',
        color: 'black',
        tickColor: 0,
        tickDecimals: 1,
        tickFormatter: function(val,axis){
          return UI.format.bits(val*8,true).html();
        },
        tickLength: 0,
        ticks: function(axis,a,b,c,d){
          //taken from flot source code (function setupTickGeneration),
          //modified to think in multiples of 1024 by Carina van der Meer for DDVTECH
          
          // heuristic based on the model a*sqrt(x) fitted to
          // some data points that seemed reasonable
          var noTicks = 0.3 * Math.sqrt($('.graph').first().height());
          
          var delta = (axis.max - axis.min) / noTicks,
          exponent = Math.floor(Math.log(Math.abs(delta)) / Math.log(1024)),
          correcteddelta = delta / Math.pow(1024,exponent),
          dec = -Math.floor(Math.log(correcteddelta) / Math.LN10),
          maxDec = axis.tickDecimals;
          
          if (maxDec != null && dec > maxDec) {
            dec = maxDec;
          }
          
          var magn = Math.pow(10, -dec),
          norm = correcteddelta / magn, // norm is between 1.0 and 10.0
          size;
          
          if (norm < 1.5) {
            size = 1;
          } else if (norm < 3) {
            size = 2;
            // special case for 2.5, requires an extra decimal
            if (norm > 2.25 && (maxDec == null || dec + 1 <= maxDec)) {
              size = 2.5;
              ++dec;
            }
          } else if (norm < 7.5) {
            size = 5;
          } else {
            size = 10;
          }
          
          size *= magn;
          size = size * Math.pow(1000,exponent);
          
          if (axis.minTickSize != null && size < axis.minTickSize) {
            size = axis.minTickSize;
          }
          
          axis.delta = delta;
          axis.tickDecimals = Math.max(0, maxDec != null ? maxDec : dec);
          axis.tickSize = size;
          
          var ticks = [],
          start = axis.tickSize * Math.floor(axis.min / axis.tickSize),
          i = 0,
          v = Number.NaN,
          prev;
          
          do {
            prev = v;
            v = start + i * axis.tickSize;
            ticks.push(v);
            ++i;
          } while (v < axis.max && v != prev);
          return ticks;
        },
        min: 0
      }
    },
    xaxes: {
      time: {
        name: 'time',
        mode: 'time',
        timezone: 'browser',
        ticks: 5
      }
    }
  },
  draggable: function(ele){
    ele.attr('draggable',true);
    ele.on('dragstart',function(e){
      $(this).css('opacity',0.4).data('dragstart',{
        click: {
          x: e.originalEvent.pageX,
          y: e.originalEvent.pageY
        },
        ele: {
          x: this.offsetLeft,
          y: this.offsetTop
        }
      });
    }).on('dragend',function(e){
      var old = $(this).data('dragstart');
      var x = old.ele.x - old.click.x + e.originalEvent.pageX;
      var y = old.ele.y - old.click.y + e.originalEvent.pageY;
      $(this).css({
        'opacity': 1,
        'top': y,
        'left': x,
        'right' : 'auto',
        'bottom' : 'auto'
      });
    });
    ele.parent().on('dragleave',function(){
      //end the drag ?
    });
  },
  copy: function(text){
    return new Promise(function (resolve,reject) {
      navigator.permissions.query({ name: "clipboard-write" }).then(function(result) {
        if (result.state === "granted" || result.state === "prompt") {
          navigator.clipboard.writeText(text).then(function(){
            resolve();
          }).catch(function(){
            throw "Failed";
          });
        }
        else {
          throw "Not permitted";
        }
      }).catch(function(e){

        //attempt the old method
        var textArea = document.createElement("textarea");
        textArea.value = text;
        textArea.textContent = text;
        textArea.style.position = "absolute";
        textArea.style.opacity = 0.00001;
        var focussed = document.activeElement;
        //add the text area to the DOM somewhere where hopefully it is "visible" so that it can obtain focus
        function addToDOM(ele,tryHere) {
          if (!tryHere) { tryHere = focussed; }
          if (tryHere.checkVisibility()) {
            tryHere.appendChild(ele);
            return true; //success!
          }
          if (tryHere.parentNode) { return addToDOM(ele,tryHere.parentNode); }
          return addToDOM(ele,document.body);
          return false;
        }
        addToDOM(textArea);
        textArea.focus();
        if (document.activeElement != textArea) {
          if (focussed)
          reject("Copy failed (could not obtain focus)");
          return;
        }
        textArea.setSelectionRange(0,text.length);

        var yay = false;
        try {
          yay = document.execCommand('copy');
        } catch (err) {
          console.errror(err);
        }
        textArea.parentNode.removeChild(textArea);
        focussed.focus();
        if (yay) {
          resolve();
        }
        else {
          reject(e);
        }
      });
    });
  },
  upload: function(accept){
    if (!accept) {
      accept = {
        description: "text files",
        accept: {
          "text/*": []
        }
      };
    }
    if (!Array.isArray(accept)) {
      accept = [accept];
    }

    return new Promise(function(resolve,reject){
      if (window.showOpenFilePicker) {
        try {
          showOpenFilePicker({
            startIn: "downloads",
            types: accept
          }).then(function(handles){
            handles[0].getFile().then(resolve).catch(reject);
          }).catch(reject);
        }
        catch (e) {
          reject(e);
        }
      }
      else {
        var $input = $("<input>").attr("type","file").hide().change(function(e){
          if (this.files && this.files.length) {
            let resolved = resolve(this.files[0]);
            if (resolved instanceof Promise) {
              resolved.finally(function(){
                $input.remove();
              });
            }
            else {
              setTimeout(function(){
                $input.remove()
              },1);
            }
            return;
          }
        });
        $(document.body).append($input.click());
      }

    });
  },
  format: {
    time: function(secs,type){
      var d = new Date(secs * 1000);
      var str = [];
      str.push(('0'+d.getHours()).slice(-2));
      str.push(('0'+d.getMinutes()).slice(-2));
      if (type != 'short') { str.push(('0'+d.getSeconds()).slice(-2)); }
      return str.join(':');
    },
    date: function(secs,type) {
      var d = new Date(secs * 1000);
      var days = ['Sun','Mon','Tue','Wed','Thu','Fri','Sat'];
      var months = ['Jan','Feb','Mar','Apr','May','Jun','Jul','Aug','Sep','Oct','Nov','Dec'];
      var str = [];
      if (type == 'long') { str.push(days[d.getDay()]); }
      str.push(('0'+d.getDate()).slice(-2));
      str.push(months[d.getMonth()]);
      if (type != 'short') { str.push(d.getFullYear()); }
      return str.join(' ');
    },
    dateTime: function(secs,type) {
      return UI.format.date(secs,type)+', '+UI.format.time(secs,type);
    },
    duration: function(seconds,notimestamp) {
      //get the amounts
      var multiplications = [1e-3,  1e3,   60,  60,   24, 1e99];
      var units =           ['ms','sec','min','hr','day'];
      var amounts = {};
      var minus = !!(seconds < 0);
      var left = Math.abs(seconds);
      for (var i in units) {
        left = Math.round(left / multiplications[i]); //round needed for floating point annoyances
        var amount = left % multiplications[Number(i)+1];
        amounts[units[i]] = amount;

        left -= amount;
      }
     

      //format it
      var unit; //if all amounts are 0, format as 00:00:00
      for (var i = units.length-1; i >= 0; i--) {
        var amount = amounts[units[i]];
        if (amounts[units[i]] > 0) {
          unit = units[i];
          break;
        }
      }
      var $s = $('<span>');
      switch (unit) {
        case 'day':
          if (notimestamp) {
            $s.append(UI.format.addUnit(amounts.day,'days, ')).append(UI.format.addUnit(amounts.hr,'hrs'));
            break;
          }
          else {
            $s.append(UI.format.addUnit(amounts.day,'days, '));
            //no break
          }
        default:
          if (notimestamp) {
            switch (unit) {
              case "hr": { 
                $s.append(UI.format.addUnit(amounts.hr,'hrs, ')).append(UI.format.addUnit(amounts.min,'mins'));
                break;
              }
              case "min": { 
                $s.append(UI.format.addUnit(amounts.min,'mins, ')).append(UI.format.addUnit(amounts.sec,'s'));
                break;
              }
              case "sec": {
                var v = Math.round(amounts.sec*1000 + amounts.ms)/1000;
                $s.append(UI.format.addUnit(v,'s'));
                break;
              }

              case "ms": { 
                $s.append(UI.format.addUnit(amounts.ms,'ms'));
                break;
              }
            }
          }
          else {
            $s.append(
              [
                ('0'+amounts.hr).slice(-2),
                ('0'+amounts.min).slice(-2),
                ('0'+amounts.sec).slice(-2)+(amounts.ms ? '.'+('00'+amounts.ms).slice(-3) : '')
              ].join(':')
            );
          }
          break;
      }
      var out =  (minus ? "- " : "")+$s[0].innerHTML;
      return out;
    },
    number: function(num,opts) {
      if ((isNaN(Number(num))) || (num == 0)) { return num; }

      opts = Object.assign({
        round: true
      },opts);

      
      //rounding
      if (opts.round) {
        var sig = 3;
        var mult = Math.pow(10,sig - Math.floor(Math.log(num)/Math.LN10) - 1);
        num = Math.round(num * mult) / mult;
      }
      
      //thousand seperation
      if (num >= 1e4) {
        var seperator = ' ';
        number = num.toString().split('.');
        var regex = /(\d+)(\d{3})/;
        while (regex.test(number[0])) {
          number[0] = number[0].replace(regex,'$1'+seperator+'$2');
        }
        num = number.join('.');
      }
      
      return num;
    },
    status: function(item) {
      var $s = $('<span>');
      
      if (typeof item.online == 'undefined') {
        $s.text('Unknown, checking..');
        if (typeof item.error != 'undefined') {
          $s.text(item.error);
        }
        return $s;
      }
      
      switch (item.online) {
        case -1: $s.text('Enabling'); break;
        case  0: $s.text('Unavailable').addClass('red'); break;
        case  1: $s.text('Active').addClass('green'); break;
        case  2: $s.text('Standby').addClass('orange'); break;
        default: $s.text(item.online);
      }
      if ('error' in item) {
        $s.text(item.error);
      }
      return $s;
    },
    capital: function(string) {
      return string.charAt(0).toUpperCase() + string.substring(1);
    },
    addUnit: function(number,unit){
      var $s = $('<span>').html(number);
      $s.append(
        $('<span>').addClass('unit').html(unit)
      );
      return $s[0].innerHTML;
    },
    bitbytes: function(val,opts){
      opts = Object.assign({
        persec: false,
        bytes: false,
        base: 1000,
        info: true
      },opts);

      var suffix = {
        bits: {
          1000: ['bit','kbit','Mbit','Gbit','Tbit','Pbit','Ebit','Zbit'],
          1024: ['bit','Kibit','Mibit','Gibit','Tibit','Pibit','Eibit','Zibit']
        },
        bytes: {
          1000: ['byte','kbyte','Mbyte','Gbyte','Tbyte','Pbyte','Ebyte','Zbyte'],
          1024: ['byte','Kibyte','Mibyte','Gibyte','Tibyte','Pibyte','Eibyte','Zibyte']
        }
      };
      if (!(opts.base in suffix[opts.bytes ? "bytes" : "bits"])) {
        opts.base = 1000;
      }
      suffix = suffix[[opts.bytes ? "bytes" : "bits"]][opts.base];
      var persec = "";
      if (opts.persec) {
        persec = "/s";
      }

      var newval = val;
      var unit;
      if (newval == 0) { 
        unit = suffix[0];
      }
      else {
        var exponent = Math.floor(Math.log(Math.abs(val)) / Math.log(opts.base));
        if (exponent < 0) {
          unit = suffix[0];
        }
        else {
          newval = newval / Math.pow(opts.base,exponent);
          unit = suffix[exponent];
        }
      }
      if ((unit == suffix[0]) && (newval != 1)) {
        unit += "s";
      }

      return $("<span>").text(
        UI.format.number(newval)
      ).append(
        $("<span>").addClass("unit").text(unit+persec).append(opts.info && (val != 0) ? 
          $("<span>").addClass("info").text("i").hover(function(e){
            var $header = $("<h3>").html(UI.format.addUnit(UI.format.number(newval),unit+persec));
            if (newval != val) {
              $header.append(": "+UI.format.addUnit(UI.format.number(Math.round(val),{round:false}),(opts.bytes ? "bytes" : "bits")+persec));
            }
            UI.tooltip.show(e,
              $("<div>").append(
                $header
              ).append(
                $("<p>").text("These are "+(opts.bytes ? "bytes" : "bits")+(persec == "" ? "" : " per second")+" with a base of "+opts.base+" ("+(opts.base == 1000 ? "decimal" : "binary")+"). This equals:")
              ).append(
                $("<ul>").append(
                  $("<li>").append(UI.format.bitbytes(val,{
                    bytes: opts.bytes,
                    persec: opts.persec,
                    base: opts.base == 1000 ? 1024 : 1000,
                    info: false
                  }))
                ).append(
                  $("<li>").append(UI.format.bitbytes(opts.bytes ? val*8 : val/8,{
                    bytes: !opts.bytes,
                    persec: opts.persec,
                    base: opts.base,
                    info: false
                  }))
                ).append(
                  $("<li>").append(UI.format.bitbytes(opts.bytes ? val*8 : val/8,{
                    bytes: !opts.bytes,
                    persec: opts.persec,
                    base: opts.base == 1000 ? 1024 : 1000,
                    info: false
                  }))
                )
              )
            );
          },function(){
            UI.tooltip.hide();
          })
        : "")
      );
    },
    bytes: function(val,persec){
      return UI.format.bitbytes(val,{bytes: true, persec: persec, base: 1024});
    },
    bits: function(val,persec){
      return UI.format.bitbytes(val,{persec: persec, base: 1000});
    }
  },
  navto: function(tab,other){
    var prevhash = location.hash;
    var hash = prevhash.split('@');
    hash[0] = [mist.user.name,mist.user.host].join('&');
    hash[1] = [tab,other].join('&');
    if (typeof screenlog != 'undefined') { screenlog.navto(hash[1]); } //allow logging if screenlog is active
    location.hash = hash.join('@');
    if (location.hash == prevhash) {
      //manually trigger hashchange even though hash hasn't changed
      $(window).trigger('hashchange');
    }
  },
  showTab: function(tab,other,prev) {
    var $main = UI.elements.main;
    if (typeof prev == "undefined") { prev = []; }
    
    if (mist.user.loggedin) {
      if (!('ui_settings' in mist.data)) {
        $main.html('Loading..');
        mist.send(function(){
          UI.showTab(tab,other);
        },{ui_settings: true});
        return;
      }
      
      if (mist.data.config.serverid) {
        document.title = mist.data.config.serverid+" - MistServer MI";
      }
    }
    
    var $currbut = UI.elements.menu.removeClass('hide').find('.plain:contains("'+tab+'")').filter(function(){
      return $(this).text() === tab;
    }).closest('.button');
    if ($currbut.length > 0) {
      //only remove previous button highlight if the current tab is found in the menu
      UI.elements.menu.find('.button.active').removeClass('active');
      $currbut.addClass('active');
      $submenu = $currbut.closest("[data-param]");
      if ($submenu.length) {
        $submenu.attr("data-param",other);
      }
    }
    
    //unload any video's that might still be playing
    if ((window.mv) && (mv.reference)) {
      mv.reference.unload();
    }
    
    UI.interval.clear();
    UI.websockets.clear();
    UI.elements.contextmenu = [];

    $main.attr("data-tab",tab).html(
      $('<h2>').text(tab)
    );
    switch (tab) {
      case 'Login':
        if (mist.user.loggedin) {
          //we're already logged in what are we doing here
          UI.navto('Overview');
          return;
        }
        document.title = "MistServer MI";
        UI.elements.menu.addClass('hide');
        UI.elements.connection.status.text('Disconnected').removeClass('green').addClass('red');
        $main.append(UI.buildUI([
          {
            type: 'help',
            help: 'Please provide your account details.<br>You were asked to set these when MistController was started for the first time. If you did not yet set any account details, log in with your desired credentials to create a new account.'
          },{
            label: 'Host',
            help: 'Url location of the MistServer API. Generally located at http://MistServerIP:4242/api',
            'default': 'http://localhost:4242/api',
            pointer: {
              main: mist.user,
              index: 'host'
            },
            validate: [function(val,me){
              if (val.slice(0,5) != location.href.slice(0,5)) {
                return {
                  msg: "It looks like you're attempting to connect to a host using "+parseURL(val).protocol+", while this page has been loaded over "+parseURL(location.href).protocol+". Your browser may refuse this because it is insecure.",
                  "break": false,
                  classes: ['orange']
                }
              }
            }]
          },{
            label: 'Username',
            help: 'Please enter your username here.',
            validate: ['required'],
            pointer: {
              main: mist.user,
              index: 'name'
            }
          },{
            label: 'Password',
            type: 'password',
            help: 'Please enter your password here.',
            validate: ['required'],
            pointer: {
              main: mist.user,
              index: 'rawpassword'
            }
          },{
            type: 'buttons',
            buttons: [{
              label: 'Login',
              type: 'save',
              'function': function(){
                mist.user.password = MD5(mist.user.rawpassword);
                delete mist.user.rawpassword;
                mist.send(function(){
                  UI.navto('Overview');
                });
              }
            }]
          }
        ]));
        break;
      case 'Create a new account':
        UI.elements.menu.addClass('hide');
        $main.append(
          $('<p>').text('No account has been created yet in the MistServer at ').append(
            $('<i>').text(mist.user.host)
          ).append('.')
        );
        
        $main.append(UI.buildUI([
          {
            type: 'buttons',
            buttons: [{
              label: 'Select other host',
              type: 'cancel',
              css: {'float': 'left'},
              'function': function(){
                UI.navto('Login');
              }
            }]
          },{
            type: 'custom',
            custom: $('<br>')
          },{
            label: 'Desired username',
            type: 'str',
            validate: ['required'],
            help: 'Enter your desired username. In the future, you will need this to access the Management Interface.',
            pointer: {
              main: mist.user,
              index: 'name'
            }
          },{
            label: 'Desired password',
            type: 'password',
            validate: ['required',function(val,me){
              $('.match_password.field').not($(me)).trigger('change');
              return false;
            }],
            help: 'Enter your desired password. In the future, you will need this to access the Management Interface.',
            pointer: {
              main: mist.user,
              index: 'rawpassword'
            },
            classes: ['match_password']
          },{
            label: 'Repeat password',
            type: 'password',
            validate: ['required',function(val,me){
              if (val != $('.match_password.field').not($(me)).val()) {
                return {
                  msg:'The fields "Desired password" and "Repeat password" do not match.',
                  classes: ['red']
                }
              }
              return false;
            }],
            help: 'Repeat your desired password.',
            classes: ['match_password']
          },{
            type: 'buttons',
            buttons: [{
              type: 'save',
              label: 'Create new account',
              'function': function(){
                mist.send(function(){
                  UI.navto('Account created');
                },{
                  authorize: {
                    new_username: mist.user.name,
                    new_password: mist.user.rawpassword
                  }
                });
                mist.user.password = MD5(mist.user.rawpassword);
                delete mist.user.rawpassword;
              }
            }]
          }]));
        break;
      case 'Account created':
        UI.elements.menu.addClass('hide');;
        $main.append(
          $('<p>').text('Your account has been created succesfully.')
        ).append(UI.buildUI([
          {
            type: 'text',
            text: 'Would you like to enable all (currently) available protocols with their default settings?'
          },{
            type: 'buttons',
            buttons: [{
              label: 'Enable protocols',
              type: 'save',
              'function': function(){
                if (mist.data.config.protocols) {
                  $main.append('Unable to enable all protocols as protocol settings already exist.<br>');
                  return;
                }
                
                $main.append('Retrieving available protocols..<br>');
                mist.send(function(d){
                  var protocols = [];
                  
                  for (var i in d.capabilities.connectors) {
                    var connector = d.capabilities.connectors[i];
                   
                    if (("PUSHONLY" in connector) || ("NODEFAULT" in connector)) {
                      continue;
                    }

                    if (connector.required) {
                      $main.append('Could not enable protocol "'+i+'" because it has required settings.<br>');
                      continue;
                    }
                    
                    protocols.push(
                      {connector: i}
                    );
                    $main.append('Enabled protocol "'+i+'".<br>');
                  }
                  $main.append('Saving protocol settings..<br>')
                  mist.send(function(d){
                    $main.append('Protocols enabled. Redirecting..');
                    setTimeout(function(){
                      UI.navto('Overview');
                    },5000);
                    
                  },{config:{protocols:protocols}});
                  
                },{capabilities:true});
              }
            },{
              label: 'Skip',
              type: 'cancel',
              'function': function(){
                UI.navto('Overview');
              }
            }]
          }
        ]));
        
        break;
      case 'Overview':
        
        if (typeof mist.data.bandwidth == 'undefined') {
          mist.send(function(d){
            UI.navto(tab);
          },{bandwidth: true});
          $main.append('Loading..');
          return;
        }
        
        var $versioncheck = $('<span>').text('Loading..');
        var $streamsactive = $('<span>');
        var $errors = $('<span>').addClass('logs');
        var $viewers = $('<span>');
        var $servertime = $('<span>');
        var $activeproducts = $('<span>').text("Unknown");
        var $protocols_on = $('<span>');
        var $protocols_off = $('<span>');
        
        var host = parseURL(mist.user.host);
        host = host.protocol+host.host+host.port;

        var s = {};
        
        $main.append(UI.buildUI([
          {
            type: 'help',
            help: 'You can find most basic information about your MistServer here.<br>You can also set the debug level and force a save to the config.json file that MistServer uses to save your settings. '
          },{
            type: 'span',
            label: 'Version',
            pointer: {
              main: mist.data.config,
              index: 'version'
            }
          },{
            type: 'span',
            label: 'Version check',
            value: $versioncheck
          },{
            type: 'span',
            label: 'Server time',
            value: $servertime
          },{
            type: 'span',
            label: 'Licensed to',
            value: ("license" in mist.data.config ? mist.data.config.license.user : "")
          },{
            type: 'span',
            label: 'Active licenses',
            value: $activeproducts
          },{
            type: 'span',
            label: 'Configured streams',
            value: (mist.data.streams ? Object.keys(mist.data.streams).length : 0)
          },{
            type: 'span',
            label: 'Active streams',
            value: $streamsactive
          },{
            type: 'span',
            label: 'Current connections',
            value: $viewers
          },{
            type: 'span',
            label: 'Enabled protocols',
            value: $protocols_on
          },{
            type: 'span',
            label: 'Disabled protocols',
            value: $protocols_off
          },{
            type: 'span',
            label: 'Recent problems',
            value: $errors
          },
          $("<br>"),
          $("<span>").addClass("bigbuttons").append(
            $("<button>").attr("data-icon","disk").text("Force configuration save").attr("title","Force an immediate save to the config.json MistServer uses to save your settings. Saving will otherwise happen upon closing MistServer.").click(function(){
              var $me = $(this);
              $me.text("Saving..");
              mist.send(function(){
                $me.attr("data-icon","check").text("Configuration saved!");
                setTimeout(function(){
                  $me.attr("data-icon","disk").text("Force configuration save");
                },5e3);
              },{save:true})
            })
          ).append(
            $("<button>").attr("data-icon","down").text("Download configuration").attr("title","Download the current MistServer configuration file so you can save it on your computer as a backup, or so you can transfer it to other MistServer instances.").click(function(){
              var $me = $(this);
              $me.text("Loading..");
              mist.send(function(d){
                $me.attr("data-icon","check").text("Configuration retrieved");

                var file = new Blob([JSON.stringify(d.config_backup)], {type: "text/plain"});
                var filename = "MistServer_config_"+(mist.data.config.serverid ? mist.data.config.serverid+"_" : "")+(new Date().toISOString())+".json";
                function showErr(err) {
                  console.warn(err);
                  var msg = "Download failed";
                  if (err.name == "AbortError") {
                    msg = "Download aborted";
                  }
                  $me.attr("data-icon","cross").text(msg);
                  setTimeout(function(){
                    $me.attr("data-icon","down").text("Download configuration");
                  },5e3);
                }
                if (window.showSaveFilePicker) {
                  try {
                    // Show the file save dialog.
                    showSaveFilePicker({
                      startIn: "downloads",
                      suggestedName: filename
                    }).then(function(handle){
                      handle.createWritable().then(function(writable){
                        writable.write(file).then(function(){
                          writable.close().then(function(){
                            $me.attr("data-icon","check").text("Configuration saved!");

                            setTimeout(function(){
                              $me.attr("data-icon","down").text("Download configuration");
                            },5e3);
                          }).catch(showErr);
                        }).catch(showErr);
                      }).catch(showErr);
                    }).catch(showErr);
                    return;
                  }
                  catch (err) {
                    showErr(err);
                  }
                }
                if (window.navigator.msSaveOrOpenBlob) { // IE10+
                  window.navigator.msSaveOrOpenBlob(file,filename);
                }
                else { // Others
                  var a = document.createElement("a");
                  var url = URL.createObjectURL(file);
                  a.href = url;
                  a.download = filename;
                  document.body.appendChild(a);
                  a.click();
                  setTimeout(function() {
                    document.body.removeChild(a);
                    window.URL.revokeObjectURL(url); 
                    $me.attr("data-icon","check").text("Configuration saved!");
                  },0); 
                }

                setTimeout(function(){
                  $me.attr("data-icon","down").text("Download configuration");
                },5e3);
              },{config_backup:true});
            })
          ).append(
            $("<button>").attr("data-icon","up").text("Upload configuration").attr("title","Upload a MistServer configuration file and apply its settings. This can be used to restore a backup or to import configuration that you've downloaded from other MistServer instances.\nYour current config will be overwritten!").click(function(){
              var $me = $(this);

              function compareAndSubmit(file) {
                return new Promise(function(resolve,reject){
                  file.text().then(function(text){
                    try {
                      var json = JSON.parse(text);
                    }
                    catch (e) {
                      reject("Selected file does not contain json");
                    }
                    if (json){
                      //retrieve current config
                      mist.send(function(d){
                        var currentconfig = d.config_backup;

                        //compare config file with current config
                        var out = [];
                        var map = {
                          streams:   function(d){ return d.streams ? Object.keys(d.streams).length : 0 },
                          protocols: function(d){ return d.config && d.config.protocols ? d.config.protocols.length : 0 },
                          "automatic pushes":    function(d){ return d.auto_push ? Object.keys(d.auto_push).length : 0 },
                          triggers:  function(d){ return d.config && d.config.triggers ? Object.entries(d.config.triggers).map(function(a){ return a[1].length}).reduce(function(sum,a){ return sum+a; },0) : 0 }
                        };
                        for (var kind in map) {
                          var o = map[kind](currentconfig);
                          var n = map[kind](json);
                          if (o != n) {
                            out.push("- "+UI.format.capital(kind)+": "+o+" to "+n);
                          }
                        }
                        var msg = "Are you sure you want to apply this config? Your current config will be overwritten!\n\n";
                        if (out.length) {
                          msg += "These are some of the changes:\n";
                          msg += out.join("\n")+"\n";
                        }
                        else {
                          var kinds = Object.keys(map);
                          msg += "There are no changes in the amount of "+kinds.slice(0,-1).join(", ")+" or "+kinds.slice(-1)+" configured.\n";
                        }
                        var lcurrent = new TextEncoder().encode(JSON.stringify(currentconfig)).length;
                        var lnew = file.size;
                        if (lcurrent != lnew) {
                          var perc = lnew/lcurrent;
                          msg += "The config file size will be "
                          if (perc > 1) {
                            perc--;
                            msg += "increased";
                          }
                          else {
                            perc = 1 - perc;
                            msg += "decreased";
                          }
                          msg += " by "+Math.round(perc*100)+"%.\n";
                        }
                        else {
                          msg += "The config file size will not change.\n"
                        }


                        if (confirm(msg)) {
                          mist.send(function(){
                            resolve();
                            UI.navto('Overview');
                          },{config_restore: json});
                        }
                        else {
                          reject("Upload canceled");
                        }
                      },{config_backup: true});

                    }
                    else {
                      reject("Selected file does not contain json");
                    }
                  })
                });
              }

              if (window.showOpenFilePicker) {
                function showErr(err) {
                  console.warn(err);
                  var msg = "Upload failed";
                  if (err.name == "AbortError") {
                    msg = "Upload aborted";
                  }
                  $me.attr("data-icon","cross").text(msg);
                  setTimeout(function(){
                    $me.attr("data-icon","up").text("Upload configuration");
                  },5e3);
                }
                try {
                  showOpenFilePicker({
                    startIn: "downloads",
                    types: [
                      {
                        description: "Configuration files",
                        accept: {
                          "text/*": [".json",".config",".cfg",".conf",".txt"]
                        }
                      }
                    ]
                  }).then(function(handles){
                    handles[0].getFile().then(function(file){
                      compareAndSubmit(file).catch(showErr);
                    }).catch(showErr);
                  }).catch(showErr);
                }
                catch (e) {
                  showErr(e);
                }
              }
              else {
                var $input = $("<input>").attr("type","file").hide().change(function(e){
                  if (this.files && this.files.length) {
                    compareAndSubmit(this.files[0]).catch(showErr).finally(function(){
                      $input.remove();
                    });
                    return;
                  }
                });
                $me.append($input.click());
              }
            })
          )
        ]));
        
        function update_update(d) {
          function update_progress(d) {
            if (!d.update) {
              UI.showTab("Overview");
              return;
            }
            var perc = "";
            if ("progress" in d.update) {
              perc = " ("+d.update.progress+"%)";
            }
            $versioncheck.text("Updating.."+perc);
            add_logs(d.log);
            if (d.update.installing){
              setTimeout(function(){
                mist.send(function(d){
                  update_progress(d);
                },{update:true});
              },1e3);
            }
          }
          function add_logs(log) {
            var msgs = log.filter(function(a){return a[1] == "UPDR"});
            if (msgs.length) {
              var $cont = $("<div>");
              $versioncheck.append($cont);
              for (var i in msgs) {
                $cont.append(
                  $("<div>").text(msgs[i][2])
                );
                
              }
            }
          }
          
          if ((!d.update) || (!('uptodate' in d.update))) {
            
            $versioncheck.text('Unknown, checking..');
            setTimeout(function(){
              mist.send(function(d){
                if ("update" in d) {
                  update_update(d);
                }
              },{checkupdate:true});
            },5e3);
            return;
          }
          else if (d.update.error) {
            $versioncheck.addClass('red').text(d.update.error);
            return;
          }
          else if (d.update.uptodate) {
            $versioncheck.text('Your version is up to date.').addClass('green');
            return;
          }
          else if (d.update.progress) {
            $versioncheck.addClass('orange').removeClass('red').text('Updating..');
            update_progress(d);
          }
          else {
            $versioncheck.text("");
            $versioncheck.append(
              $("<span>").addClass('red').text('On '+new Date(d.update.date).toLocaleDateString()+' version '+d.update.version+' became available.')
            );
            if (!d.update.url || (d.update.url.slice(-4) != ".zip")) {
              //show update button if not windows version
              $versioncheck.append(
                $('<button>').text('Rolling update').css({'font-size':'1em','margin-left':'1em'}).click(function(){
                  if (confirm('Are you sure you want to execute a rolling update?')) {
                    $versioncheck.addClass('orange').removeClass('red').text('Rolling update command sent..');
                    
                    mist.send(function(d){
                      update_progress(d);
                    },{autoupdate: true});
                  }
                })
              );
            }
            var a = $("<a>").attr("href",d.update.url).attr("target","_blank").text("Manual download");
            a[0].protocol = "https:";
            $versioncheck.append(
              $("<div>").append(a)
            );
          }
          add_logs(d.log);
        }
        
        update_update(mist.data);
        
        //show license information
        if ("license" in mist.data.config) {
          if (("active_products" in mist.data.config.license) && (Object.keys(mist.data.config.license.active_products).length)) {
            var $t = $("<table>").css("text-indent","0");
            $activeproducts.html($t);
            $t.append(
              $("<tr>").append(
                $("<th>").append("Product")
              ).append(
                $("<th>").append("Updates until")
              ).append(
                $("<th>").append("Use until")
              ).append(
                $("<th>").append("Max. simul. instances")
              )
            );
            for (var i in mist.data.config.license.active_products) {
              var p = mist.data.config.license.active_products[i];
              $t.append(
                $("<tr>").append(
                  $("<td>").append(p.name)
                ).append(
                  $("<td>").append((p.updates_final ? p.updates_final : "&infin;"))
                ).append(
                  $("<td>").append((p.use_final ? p.use_final : "&infin;"))
                ).append(
                  $("<td>").append((p.amount ? p.amount : "&infin;"))
                )
              );
            }
          }
          else {
            $activeproducts.text("None. ");
          }
          $activeproducts.append(
            $("<a>").text("More details").attr("href","https://shop.mistserver.org/myinvoices").attr("target","_blank")
          );
        }
        
        function updateViewers() {
          var request = {
            totals:{
              fields: ['clients'],
              start: -10
            },
            active_streams: true
          };
          if (!('capabilities' in mist.data)) {
            request.capabilities = true;
          }
          mist.send(function(d){
            enterStats()
          },request);
        }
        function enterStats() {
          if ('active_streams' in mist.data) {
            var active = (mist.data.active_streams ? mist.data.active_streams.length : 0)
          }
          else {
            var active = '?';
          }
          $streamsactive.text(active);
          if (('totals' in mist.data) && ('all_streams' in mist.data.totals)) {
            var clients = mist.data.totals.all_streams.all_protocols.clients;
            clients = (clients.length ? UI.format.number(clients[clients.length-1][1]) : 0);
          }
          else {
            clients = 'Loading..';
          }
          $viewers.text(clients);
          $servertime.text(UI.format.dateTime(mist.data.config.time,'long'));
          
          $errors.html('');
          var n = 0;
          if (("license" in mist.data.config) && ("user_msg" in mist.data.config.license)) {
            mist.data.log.unshift([mist.data.config.license.time,"ERROR",mist.data.config.license.user_msg]);
          }
          for (var i in mist.data.log) {
            var l = mist.data.log[i];
            if (['FAIL','ERROR'].indexOf(l[1]) > -1) {
              n++;
              var $content = $('<span>').addClass('content').addClass('red');
              var split = l[2].split('|');
              for (var i in split) {
                $content.append(
                  $('<span>').text(split[i])
                );
              }
              $errors.append(
                $('<div>').append(
                  $('<span>').append(UI.format.time(l[0])).css("margin-right","0.5em")
                ).append(
                  $content
                )
              );
              if (n == 5) { break; }
            }
          }
          if (n == 0) {
            $errors.html('None.');
          }
          
          var protocols = {
            on: [],
            off: []
          };
          for (var i in mist.data.config.protocols) {
            var p = mist.data.config.protocols[i];
            if (protocols.on.indexOf(p.connector) > -1) { continue; }
            protocols.on.push(p.connector);
          }
          $protocols_on.text((protocols.on.length ? protocols.on.join(', ') : 'None.'));
          if ('capabilities' in mist.data) {
            for (var i in mist.data.capabilities.connectors) {
              if (protocols.on.indexOf(i) == -1) {
                protocols.off.push(i);
              }
            }
            $protocols_off.text((protocols.off.length ? protocols.off.join(', ') : 'None.'));
          }
          else {
            $protocols_off.text('Loading..')
          }
        }
        updateViewers();
        enterStats();
        UI.interval.set(updateViewers,30e3);
        
        break;
      case 'General': {

        var s_general = {
          serverid: mist.data.config.serverid,
          debug: mist.data.config.debug,
          accesslog: mist.data.config.accesslog,
          prometheus: mist.data.config.prometheus,
          defaultStream: mist.data.config.defaultStream,
          trustedproxy: mist.data.config.trustedproxy
        };
        var s_sessions = {
          sessionViewerMode: mist.data.config.sessionViewerMode,
          sessionInputMode: mist.data.config.sessionInputMode,
          sessionOutputMode: mist.data.config.sessionOutputMode,
          sessionUnspecifiedMode: mist.data.config.sessionUnspecifiedMode,
          tknMode: mist.data.config.tknMode,
          sessionStreamInfoMode: mist.data.config.sessionStreamInfoMode
        };
        var s_balancer = {
          location: "location" in mist.data.config ? mist.data.config.location : {}
        };

        $main.html(UI.buildUI([
          $("<h2>").text("General settings"),{
            type: "help",
            help: "These are settings that apply to your MistServer instance in general."
          },{
            type: 'str',
            label: 'Human readable name',
            pointer: {
              main: s_general,
              index: 'serverid'
            },
            help: 'You can name your MistServer here for personal use. You\'ll still need to set host name within your network yourself.'
          },{
            type: 'debug',
            label: 'Debug level',
            pointer: {
              main: s_general,
              index: 'debug'
            },
            help: 'You can set the amount of debug information MistServer saves in the log. A full reboot of MistServer is required before some components of MistServer can post debug information.'
          },{
            type: "selectinput",
            label: "Access log",
            selectinput: [
              ["","Do not track"],
              ["LOG","Log to MistServer log"],
              [{
                type:"str",
                label:"Path",
                LTSonly: true
              },"Log to file"]
            ],
            pointer: {
              main: s_general,
              index: "accesslog"
            },
            help: "Enable access logs.",
            LTSonly: true
          },{
            type: "selectinput",
            label: "Prometheus stats output",
            selectinput: [
              ["","Disabled"],
              [{
                type: "str",
                label:"Passphrase",
                LTSonly: true
              },"Enabled"]
            ],
            pointer: {
              main: s_general,
              index: "prometheus"
            },
            help: "Make stats available in Prometheus format. These can be accessed via "+host+"/PASSPHRASE or "+host+"/PASSPHRASE.json.",
            LTSonly: true
          },{
            type: "inputlist",
            label: "Trusted proxies",
            help: "List of proxy server addresses that are allowed to override the viewer IP address to arbitrary values.<br>You may use a hostname or IP address.",
            pointer: {
              main: s_general,
              index: "trustedproxy"
            }
          },{
            type: "str",
            validate: ['streamname_with_wildcard_and_variables'],
            label: 'Fallback stream',
            pointer: {
              main: s_general,
              index: "defaultStream"
            },
            help: "When this is set, if someone attempts to view a stream that does not exist, or is offline, they will be redirected to this stream instead. $stream may be used to refer to the original stream name.",
            LTSonly: true
          },{
            type: 'buttons',
            buttons: [{
              type: 'save',
              label: 'Save',
              'function': function(ele){
                $(ele).text("Saving..");

                var save = {config: s_general};
                                
                mist.send(function(){
                  UI.navto('General');
                },save)
              }
            }]
          }
        ]));



        $main.append(UI.buildUI([
          $("<h3>").text("Sessions"),
          {
            type: 'bitmask',
            label: 'Bundle viewer sessions by',
            bitmask: [
              [8,"Stream name"],
              [4,"IP address"],
              [2,"Token"],
              [1,"Protocol"]
            ],
            pointer: {
              main: s_sessions,
              index: 'sessionViewerMode'
            },
            help: 'Change the way viewer connections are bundled into sessions.<br>Default: stream name, viewer IP and token'
          },{
            type: 'bitmask',
            label: 'Bundle input sessions by',
            bitmask: [
              [8,"Stream name"],
              [4,"IP address"],
              [2,"Token"],
              [1,"Protocol"]
            ],
            pointer: {
              main: s_sessions,
              index: 'sessionInputMode'
            },
            help: 'Change the way input connections are bundled into sessions.<br>Default: stream name, input IP, token and protocol'
          },{
            type: 'bitmask',
            label: 'Bundle output sessions by',
            bitmask: [
              [8,"Stream name"],
              [4,"IP address"],
              [2,"Token"],
              [1,"Protocol"]
            ],
            pointer: {
              main: s_sessions,
              index: 'sessionOutputMode'
            },
            help: 'Change the way output connections are bundled into sessions.<br>Default: stream name, output IP, token and protocol'
          },{
            type: 'bitmask',
            label: 'Bundle unspecified sessions by',
            bitmask: [
              [8,"Stream name"],
              [4,"IP address"],
              [2,"Token"],
              [1,"Protocol"]
            ],
            pointer: {
              main: s_sessions,
              index: 'sessionUnspecifiedMode'
            },
            help: 'Change the way unspecified connections are bundled into sessions.<br>Default: none'
          },{
            type: 'select',
            label: 'Treat HTTP-only sessions as',
            select: [
              [1, 'A viewer session'],
              [2, 'An output session: skip executing the USER_NEW and USER_END triggers'],
              [4, 'A separate \'unspecified\' session: skip executing the USER_NEW and USER_END triggers'],
              [3, 'Do not start a session: skip executing the USER_NEW and USER_END triggers and do not count for statistics']
            ],
            pointer: {
              main: s_sessions,
              index: 'sessionStreamInfoMode'
            },
            help: 'Change the way the stream info connection gets treated.<br>Default: as a viewer session'
          },{
            type: "bitmask",
            label: "Communicate session token",
            bitmask: [
              [8,"Write to cookie"],
              [4,"Write to URL parameter"],
              [2,"Read from cookie"],
              [1,"Read from URL parameter"]
            ],
            pointer: {
              main: s_sessions,
              index: "tknMode"
            },
            help: "Change the way the session token gets passed to and from MistServer, which can be set as a cookie or URL parameter named `tkn`. Reading the session token as a URL parameter takes precedence over reading from the cookie.<br>Default: all"
          },{
            type: 'buttons',
            buttons: [{
              type: 'save',
              label: 'Save',
              'function': function(ele){
                $(ele).text("Saving..");

                var save = {config: s_sessions};
                                
                mist.send(function(){
                  UI.navto('General');
                },save)
              }
            }]
          }

        ]));

        var $variables = $("<div>").html("Loading..");
        mist.send(function(d){
          if (!d.variable_list) {
            $variables.html("None configured.");
            return;
          }
          var $tbody = $("<tbody>");
          $variables.html(
            $("<table>").html(
              $("<thead>").html(
                $("<tr>").append(
                  $("<th>").text("Variable")
                ).append(
                  $("<th>").text("Latest value")
                ).append(
                  $("<th>").text("Command or url")
                ).append(
                  $("<th>").text("Check interval")
                ).append(
                  $("<th>").text("Last checked")
                ).append(
                  $("<th>")
                )
              )
            ).append($tbody)
          );
          for (var i in d.variable_list) {
            var v = d.variable_list[i];
            $tbody.append(
              $("<tr>").addClass("variable").attr("data-name",i).html(
                $("<td>").text("$"+i)
              ).append(
                $("<td>").html(
                  $("<code>").text(
                    "target" in v ?
                    (v.lastunix > 0 ? JSON.stringify(v.value) : "" ) :
                    JSON.stringify(v.value)
                  )
                )
              ).append(
                $("<td>").html(
                  "target" in v ?
                  (v.target.match(/^http(s)?:\/\//) ? $("<a>").attr("target","_blank").attr("href",v.target).text(v.target) : v.target) :
                  ""
                )
              ).append(
                $("<td>").html("target" in v ? (v.interval == 0 ? "Once" : UI.format.duration(v.interval)) : "Never")
              ).append(
                $("<td>").attr("title",
                  "target" in v ?                  
                  (
                    v.lastunix > 0 ?
                    "At "+UI.format.dateTime(new Date(v.lastunix),"long") :
                    "Not yet"
                  ) :
                  ""
                ).html(
                  "target" in v ? 
                  (
                    v.lastunix > 0 ?
                    UI.format.duration(new Date().getTime()*1e-3 - v.lastunix)+" ago" :
                    "Not yet"
                  ) :
                  ""
                )
              ).append(
                $("<td>").html(
                  $("<button>").text("Edit").click(function(){
                    var i = $(this).closest("tr").attr("data-name");
                    UI.navto("Edit variable",i);
                  })
                ).append(
                  $("<button>").text("Remove").click(function(){
                    var i = $(this).closest("tr").attr("data-name");
                    if (confirm("Are you sure you want to remove the custom variable $"+i+"?")) {
                      mist.send(function(){
                        UI.showTab("General");
                      },{variable_remove:i});
                    }
                  })
                )
              )
            );
          }
        },{variable_list:true});

        $main.append(UI.buildUI([
          $('<h3>').text("Custom variables"),
          {
            type: "help",
            help: "In certain places, like target URL's and pushes, variable substitution is applied in order to replace a $variable with their corresponding value. Here you can define your own constants and variables which will be used when variable substitution is applied. Variables can be used within variables but will not be reflected in their latest value on this page."
          },
          $("<div>").addClass("button_container").css("text-align","right").html(
            $("<button>").attr("data-icon","plus").text("New variable").click(function(){
              UI.navto("Edit variable","");
            })
          ),
          $variables
        ]));

        var $balancer = $("<div>").text("Loading..");
        $main.append(UI.buildUI([
          $('<h3>').text("Load balancer"),{
            type: "help",
            help: "If you're using MistServer's load balancer, the information below is passed to it so that it can make informed decisions."
          },
          $balancer
        ]));
        mist.send(function(d){
          var b = {limit:""};
          if ("bandwidth" in d) {
            b = d.bandwidth;
            if (b == null) { b = {}; }
            if (!b.limit) {
              b.limit = "";
            }
          }

          $balancer.html(UI.buildUI([
            {
              type: "selectinput",
              label: "Server's bandwidth limit",
              selectinput: [
                ["","Default (1 Gbit/s)"],
                [{
                  label: "Custom",
                  type: "number",
                  min: 0,
                  unit: [ //save the value in bytes/s, display it in bits/s with a base of 1000
                    [.125,"bit/s"],
                    [125,"kbit/s"],
                    [125e3,"Mbit/s"],
                    [125e6,"Gbit/s"]
                  ]
                },"Custom"]
              ],
              pointer: {
                main: b,
                index: "limit"
              },
              help: "This is the amount of traffic this server is willing to handle."
            },{
              type: "inputlist",
              label: "Bandwidth exceptions",
              pointer: {
                main: b,
                index: "exceptions"
              },
              help: "Data sent to the hosts and subnets listed here will not count towards reported bandwidth usage.<br>Examples:<ul><li>192.168.0.0/16</li><li>localhost</li><li>10.0.0.0/8</li><li>fe80::/16</li></ul>"
            },{
              type: "int",
              step: 0.00000001,
              label: "Server latitude",
              pointer: {
                main: s_balancer.location,
                index: "lat"
              },
              help: "This setting is only useful when MistServer is combined with a load balancer. When this is set, the balancer can send users to a server close to them."
            },{
              type: "int",
              step: 0.00000001,
              label: "Server longitude",
              pointer: {
                main: s_balancer.location,
                index: "lon"
              },
              help: "This setting is only useful when MistServer is combined with a load balancer. When this is set, the balancer can send users to a server close to them."
            },{
              type: "str",
              label: "Server location name",
              pointer: {
                main: s_balancer.location,
                index: "name"
              },
              help: "This setting is only useful when MistServer is combined with a load balancer. This will be displayed as the server's location."
            },{
              type: 'buttons',
              buttons: [{
                type: 'save',
                label: 'Save',
                'function': function(ele){
                  $(ele).text("Saving..");

                  var save = {config: s_balancer};

                  var bandwidth = {};
                  bandwidth.limit = b.limit;
                  bandwidth.exceptions = b.exceptions;
                  if (bandwidth.exceptions === null) {
                    bandwidth.exceptions = [];
                  }
                  save.bandwidth = bandwidth;

                  mist.send(function(){
                    UI.navto('Overview');
                  },save)
                }
              }]
            }
          ]));

        },{bandwidth:true});

        var $uploaders = $("<div>").html("Loading..");
        $main.append(UI.buildUI([
          $('<h3>').text("External writers"),
          {
            type: "help",
            help: "When pushing a stream to a target unsupported by MistServer like S3 storage, an external writer can be provided which handles writing the media data to the target location. The writer will receive data over stdin and MistServer will print any info written to stdout and stderr as log messages."
          },
          $("<div>").addClass("button_container").css("text-align","right").html(
            $("<button>").attr("data-icon","plus").text("New external writer").click(function(){
              UI.navto("Edit external writer","");
            })
          ),
          $uploaders
        ]));

        let $jwks = $("<div>").html("Loading..");
        $main.append(UI.buildUI([
          $('<h3>').text("JSON web keys (JWK)"),
          {
            type: "help",
            help: "You can use JSON web tokens (JWT) to control permissions for viewing certain streams, inputting a stream or even access to this Management Interface.<br>Here, you can store your public keys (JWK) that will be used to validate the JWT's. You can also supply an url from which MistServer can download a key set (JWKS)."
          },
          $("<div>").addClass("button_container").css("text-align","right").html(
            $("<button>").attr("data-icon","plus").text("Add JWK").click(function(){
              UI.navto("Edit JWK","");
            })
          ),
          $jwks
        ]));

        mist.send(function(d){
          if (!d.external_writer_list) {
            $uploaders.html("None configured.");
          }
          else {
            var $tbody = $("<tbody>");
            $uploaders.html(
              $("<table>").html(
                $("<thead>").html(
                  $("<tr>").append(
                    $("<th>").text("Name")
                  ).append(
                    $("<th>").text("Command line")
                  ).append(
                    $("<th>").text("URI protocols handled")
                  ).append(
                    $("<th>")
                  )
                )
              ).append($tbody)
            );
            for (var i in d.external_writer_list) {
              var uploader = d.external_writer_list[i];
              $tbody.append(
                $("<tr>").addClass("uploader").attr("data-name",i).html(
                  $("<td>").text(uploader[0])
                ).append(
                  $("<td>").html($("<code>").html(uploader[1]))
                ).append(
                  $("<td>").text(uploader[2] ? uploader[2].join(", ") : "none").addClass("desc")
                ).append(
                  $("<td>").html(
                  $("<button>").text("Edit").click(function(){
                    var i = $(this).closest("tr").attr("data-name");
                    UI.navto("Edit external writer",i);
                  })
                ).append(
                  $("<button>").text("Remove").click(function(){
                    var i = $(this).closest("tr").attr("data-name");
                    var name = d.external_writer_list[i][0];
                    if (confirm("Are you sure you want to remove the Uploader '"+name+"'?")) {
                      mist.send(function(){
                        UI.showTab("General");
                      },{external_writer_remove:name});
                    }
                  })
                )
                )
              );
            }
          }
        
          if (!d.jwks) {
            $jwks.html("None configured.");
          }
          else {
            let $tbody = $("<tbody>");
            let context_menu = new UI.context_menu();
            for (let entry of d.jwks) {

              let key = entry[0]; /*if key is a string, it's an url to a key set, otherwise it's the key itself*/
              let permissions = entry.length > 1 ? entry[1] : false;
              if (!permissions) {
                //permissions are default
                permissions = {
                  input: true,
                  output: true,
                  admin: false,
                  stream: "*"
                };
              }

              let $kid;
              if (typeof key == "string") {
                $kid = $("<a>").attr("href",key).attr("target","_blank").text(key);
              }
              else {
               let kid = key?.kid ? key.kid : JSON.stringify(key,null,2);
                $kid = $("<div>").addClass("key").addClass("clickable").text(kid).attr("title",kid);
              }
              $tbody.append(
                $("<tr>").attr("title",typeof key == "string" ? key : JSON.stringify(key,null,2)).on("contextmenu",function(e){
                  e.preventDefault();
                  context_menu.show([[
                    $("<div>").addClass("header").html($kid.clone())
                  ],[
                    ["Copy "+(typeof key == "string" ? "url" : "key"),function(){
                      let text = (typeof key == "string" ? key : JSON.stringify(key));
                      UI.copy(text).then(()=>{
                        this._setText("Copied!")
                        setTimeout(function(){ context_menu.hide(); },300);
                      }).catch((e)=>{
                        this._setText("Copy: "+e);
                        setTimeout(function(){ context_menu.hide(); },300);

                        var popup =  UI.popup(UI.buildUI([
                          $("<h1>").text("Copy to clipboard"),{
                            type: "help",
                            help: "Automatic copying failed ("+e+"). Instead you can manually copy from the field below."
                          },{
                            type: "str",
                            label: "Text",
                            value: text,
                            rows: Math.ceil(text.length/50+2)
                          }
                        ]));
                        popup.element.find("textarea").select();
                      });
                    },"copy","Copy this "+(typeof key == "string" ? "url" : "key")+" to the clipboard."],
                    ["Edit",function(){
                      UI.navto("Edit JWK",$(e.target).closest("tr").index());
                    },"Edit","Edit this "+(typeof key == "string" ? "url" : "key")+" or its permissions."]
                  ]],e);
                }).append(
                  $("<td>").html(
                    $kid
                  )
                ).append(
                  $("<td>").text(typeof key == "string" ? "url" : key.kty)
                ).append(
                  function(){
                    let $td = $("<td>").addClass("permissions");
                    if (permissions.output) {
                      $td.append($("<div>").addClass("output").attr("title","Viewing"));
                    }
                    if (permissions.input) {
                      $td.append($("<div>").addClass("input").attr("title","Input"));
                    }
                    if (permissions.admin) {
                      $td.append($("<div>").addClass("admin").attr("title","MI and API"));
                    }
                    if (!permissions.stream) permissions.stream = "*";
                    $td.append($("<div>").addClass("streams").text((Array.isArray(permissions.stream) ? "For streams: "+permissions.stream.join(", ") : (permissions.stream == "*" ? "For all streams" : "For streams: "+permissions.stream))))
                    return $td;
                  }()
                ).append(
                  $("<td>").append(
                    $("<button>").attr("data-icon","Edit").text("Edit").click(function(){
                      UI.navto("Edit JWK",$(this).closest("tr").index());
                    })
                  ).append(
                    $("<button>").attr("data-icon","trash").text("Delete").click(function(){
                      if (confirm("Are you sure you want to delete this key?\n"+(typeof key == "string" ? key : $kid[0].innerText))) {
                        mist.send(function(){
                          UI.navto("General");
                        },{
                          deletejwks: (typeof key == "string" ? key : (key.kid ? key.kid : key))
                        });
                      }
                    })
                  )
                )
              );
            }
            $jwks.html($("<table>").addClass("JWKs").append(
              $("<thead>").append(
                $("<tr>").append(
                  $("<th>").text("Key id or url to key set")
                ).append(
                  $("<th>").text("Key type")
                ).append(
                  $("<th>").text("Can permit")
                ).append(
                  $("<th>")
                )
              )
            ).append($tbody)).append(context_menu.ele);
          }

        },{ external_writer_list: true, jwks: true });

        break;
      }
      case "Edit external writer": {
        var editing = false;
        if (other != '') { editing = true; }

        function buildPage() {
          if (!editing) {
            $main.html($('<h2>').text('New external writer'));
          }
          else {
            $main.html($('<h2>').text('Edit external writer \''+other+'\''));
          }

          var saveas = {};
          if (mist.data.external_writer_list && (other in mist.data.external_writer_list)) {
            var uploader = mist.data.external_writer_list[other];
            saveas.name = uploader[0];
            saveas.cmdline = uploader[1];
            saveas.protocols = uploader[2];
          }

          $main.append(UI.buildUI([
            {
              type: "str",
              label: "Human readable name",
              help: "A human readable name for the external writer.",
              validate: ['required'],
              pointer: {
                main: saveas,
                index: "name"
              }
            },{
              type: "str",
              label: "Command line",
              help: "Command line for a local command (with optional arguments) which will write media data to the target.",
              validate: ['required'],
              pointer: {
                main: saveas,
                index: "cmdline"
              }
            },{
              type: "inputlist",
              label: "URI protocols handled",
              help: "URI protocols which the external writer will be handling.",
              validate: ['required',function(val){
                for (var i in val) {
                  var v = val[i];
                  if (v.match(/^([a-z\d\+\-\.])+?$/) === null) {
                    return {
                      classes: ["red"],
                      msg: "There was a problem with the protocol URI '"+function(s){ return $("<div>").text(s).html() }(v)+"':<br>A protocol URI may only contain lower case letters, digits, and the following special characters . + and -"
                    }
                    break;
                  }
                }
              }],
              input: {
                type: "str",
                unit: "://"
              },
              pointer: {
                main: saveas,
                index: "protocols"
              }
            },{
              type: "buttons",
              buttons: [
                {
                  type: 'cancel',
                  label: 'Cancel',
                  'function': function(){
                    UI.navto('General');
                  }
                },{
                  type: 'save',
                  label: 'Save',
                  'function': function(){
                    var o = {external_writer_add:saveas};
                    var prev_name = null;
                    if ((other != "") && (other in mist.data.external_writer_list)) { 
                      prev_name = mist.data.external_writer_list[other][0]; 
                    }

                    if ((prev_name !== null) && (saveas.name != prev_name)) {
                      o.external_writer_remove = prev_name;
                    }
                    mist.send(function(){
                      UI.navto('General');
                    },o);
                  }
                }
              ]
            }
          ]));
        }

        if ("external_writer_list" in mist.data) {
          buildPage();
        }
        else {
          mist.send(function(){
            buildPage();
          },{external_writer_list:true});
        }

        break;
      }
      case 'Edit variable': {
        var editing = false;
        if (other != '') { editing = true; }

        function build(saveas,saveas_dyn) {
          if (!editing) {
            $main.html($('<h2>').text('New Variable'));
          }
          else {
            $main.html($('<h2>').text('Edit Variable "$'+other+'"'));
          }

          var $dynamicinputs = $("<div>");

          $main.append(UI.buildUI([
            {
              type: "str",
              maxlength: 31,
              label: "Variable name",
              prefix: "$",
              help: "What should the variable be called? A dollar sign will automatically be prepended.",
              pointer: {
                main: saveas,
                index: "name"
              },
              validate: ["required",function(val){
                if (val.length && (val[0] == "$")) {
                  return {
                    msg: 'The dollar sign will automatically be prepended. You don\'t need to type it here.',
                    classes: ['red']
                  };
                }
                if ((val.indexOf("{") !== -1) || (val.indexOf("}") !== -1) || (val.indexOf("$") !== -1)) {
                  return {
                    msg: 'The following symbols are not permitted: "$ { }".',
                    classes: ['red']
                  };
                }
              }]
            },{
              type: "select",
              label: "Type",
              help: "What kind of variable is this? It can either be a static value that you can enter below, or a dynamic one that is returned by a command or url.",
              select: [
                ["value","Static value"],
                ["command","Dynamic through command or url"]
              ],
              value: "value",
              pointer: {
                main: saveas_dyn,
                index: "type"
              },
              'function': function(){
                var b = [$("Invalid variable type")];
                switch ($(this).val()) {
                  case "value": {
                    b = [{
                      type: "str",
                      label: "Value",
                      pointer: {
                        main: saveas_dyn,
                        index: "value"
                      },
                      help: "The static value that this variable should be replaced with. There is a character limit of 63 characters.",
                      validate: ["required"]
                    }];
                    break;
                  }
                  case "command": {
                    b = [{
                      type: "str",
                      label: "Command",
                      help: "The command that should be executed or the url that should be downloaded to retrieve the value for this variable.<br>For example:<br><code>/usr/bin/date +%A</code><br>There is a character limit of 511 characters.",
                      validate: ["required"],
                      pointer: {
                        main: saveas_dyn,
                        index: "target"
                      }
                    },{
                      type: "int",
                      min: 0,
                      max: 4294967295,
                      step: 1e-3,
                      'default': 0,
                      label: "Checking interval",
                      unit: "s",
                      help: "At what interval, in seconds, MistServer should execute the command and update the value.<br>To execute the command once when MistServer starts up (and then never update), set the interval to 0.",
                      pointer: {
                        main: saveas_dyn,
                        index: "interval"
                      }
                    },{
                      type: "int",
                      min: 0,
                      max: 4294967295,
                      step: 1e-3,
                      'default': 1,
                      label: "Wait time",
                      unit: "s",
                      help: "Specifies the maximum time, in seconds, MistServer should wait for data when executing the variable target. If set to 0 this variable takes on the same value as the interval.<br>MistServer only updates one variable at a time, so setting this value too high can block other variables from updating.",
                      pointer: {
                        main: saveas_dyn,
                        index: "waitTime"
                      }
                    }];
                    break;
                  }
                }
                $dynamicinputs.html(UI.buildUI(b));
              }
            },
            $dynamicinputs,
            {
              type: "buttons",
              buttons: [
                {
                  type: 'cancel',
                  label: 'Cancel',
                  'function': function(){
                    UI.navto('General');
                  }
                },{
                  type: 'save',
                  label: 'Save',
                  'function': function(){
                    var o = {variable_add:saveas};

                    switch (saveas_dyn.type) {
                      case "value": {
                        saveas.value = saveas_dyn.value;
                        break;
                      }
                      case "command": {
                        saveas.target = saveas_dyn.target;
                        saveas.interval = saveas_dyn.interval;
                        saveas.waitTime = saveas_dyn.waitTime;
                        break;
                      }
                    }

                    if (saveas.name != other) {
                      o.variable_remove = other;
                    }
                    mist.send(function(){
                      UI.navto('General');
                    },o);
                  }
                }
              ]
            }
          ]));
        }

        $main.html("Loading..");
        if (!editing) {
          build({},{});
        }
        else {
          mist.send(function(d){
            if (other in d.variable_list) {
              var v = d.variable_list[other];
              v.type = "target" in v ? "command" : "value";
              build({
                name: other
              },v);
            }
            else {
              $main.append('Variable "$'+other+'" does not exist.');
            }
          },{variable_list:true});
        }


        break;
      }
      case 'Edit JWK': {
        let editing = false;
        if (other != '') {
          editing = true;
          $main.html("Loading..");
          mist.send(buildPage,{jwks:true});
        }
        else {
          buildPage();
        }

        function buildPage(d){
          if (editing && d && d.jwks) {
            editing = d.jwks?.[Number(other)];
            if (editing && ((editing.length < 2) || !editing[1])) {
              //permissions are not set - assume defaults
              editing[1] = {
                input: true,
                output: true,
                admin: false,
                stream: "*"
              };
            } 
          }

          $main.html(
            $("<h2>").text(editing ? (typeof editing[0] == "string" ? "Edit url to JWKS" : "Edit JSON web key") : "Add JSON web key(s)")
          );
          let saveas = { 
            urls: [],
            keys: [],
            permissions: {} 
          };
          if (editing) {
            if (typeof editing[0] == "string") {
              saveas.urls = [editing[0]];
            }
            else {
              saveas.keys = [JSON.stringify(editing[0],null,2)];
            }
            saveas.permissions = editing[1];
          }


          $main.append(UI.buildUI([{
            type: "help",
            help: "You can use JSON web tokens (JWT) to control permissions for viewing certain streams, inputting a stream or even access to this Management Interface.<br>Here, you can add your public keys (JWK) that will be used to validate the JWT's. You can also supply an url from which MistServer can download a key set (JWKS)."
          },$("<h3>").text("Keys"),!editing || (typeof editing[0] == "string") ? {
            label: "Url(s) to JSON web key set (JWKS)",
            help: "Enter one or more urls where MistServer can download a JSON web key set.",
            pointer: { main: saveas, index: "urls" },
            type: "inputlist",
            validate: editing ? ["required"] : [], //used on "Add"-button
            input: {
              type: "str",
              validate: [function(val){
                if (val == "") return;
                function isValidHttpUrl(string) {
                  let url;
                  try {
                    url = new URL(string);
                  } catch (e) {
                    return false;  
                  }
                  return url.protocol === "http:" || url.protocol === "https:";
                }

                if (!isValidHttpUrl(val)) {
                  return {
                    msg: "Please enter a valid url.",
                    classes: ["red"]
                  };
                }
              }]
            }
          } : false,!editing ? {
            type: "help",
            help: "- and / or -",
            css: { margin: "1em 0" }
          } : false,!editing || (typeof editing[0] != "string") ? {
            type: "span",
            label: "JSON web key (set)",
            value: $("<button>").attr("data-icon","up").text("Upload JWKS file").css({
              fontSize:"1.25em",
              marginLeft:0
            }).click(function(){
              let $me = $(this);
              let $field = $(this).closest(".input_container")?.find(".field[name=\"keys\"]");
              let $ihc = $me.parent().data("help_container");
              let uid = $me.parent().data("uid");
              if ($ihc) {
                $ihc.find(".err_balloon[data-uid='"+uid+"']")?.remove();
              }

              function showErr(err) {
                console.warn(err);
                var msg = "Upload failed";
                if (err.name == "AbortError") {
                  msg = "Upload aborted";
                }
                $me.attr("data-icon","cross").text(msg);
                if ($ihc) {
                  let $err = $('<span>').addClass('err_balloon').addClass("orange").attr("data-uid",uid).html(err);
                  $ihc.prepend($err);
                }
                setTimeout(function(){
                  $me.attr("data-icon","up").text("Upload JWKS file");
                },5e3);
              }

              UI.upload({
                description: "JWKS files",
                accept: {
                  "*/json": [".jwk",".jwks",".json"]
                }
              }).then(function(result){
                result.text().then(function(text){
                  try {
                    let json = JSON.parse(text);
                    let out = $field.getval();
                    let foundkeys = false;
                    function add(json) {
                      if (("kty" in json) && ("alg" in json)) {
                        out.push(JSON.stringify(json,null,2));
                        foundkeys = true;
                      }
                    }

                    if ("keys" in json) {
                      for (let i in json.keys) {
                        add(json.keys[i]);
                      }
                    }
                    else if (Array.isArray(json)) {
                      for (let i in json) {
                        add(json[i]);
                      }
                    }
                    else {
                      add(json);
                    }
                    if (!foundkeys) throw "Could not find any keys in this file ("+result.name+")";

                    $field.setval(out);
                  }
                  catch(e) { showErr(e); }
                });
              }).catch(showErr);

            }),
            help: "Upload a JWKS file from your computer.",
          } : false, !editing || (typeof editing[0] != "string") ? {
            type: "inputlist",
            pointer: { main: saveas, index: "keys" },
            help: "Enter a JWK or a JWKS: a json object or array of objects.",
            validate: editing ? ["required"] : [],
            input: {
              type: "textarea",
              placeholder: JSON.stringify({alg:"youralg",kid:"youruuid",kty:"oct",k:42},null,2),
              rows: 6,
              validate: [function (val,me){
                let fieldn = 1+$(me).closest(".listitem").index();
                if (val == "") { return; }
                let json;
                try {
                  json = JSON.parse(val);
                }
                catch (e) {}
                if (!json) {
                  return {
                    msg: "Field "+fieldn+": Invalid json.",
                    classes: ["red"]
                  };
                }
                if ((json === null) || (typeof json != "object")) {
                  return {
                    msg: "Field "+fieldn+": Please enter a JSON object or array of objects.",
                    classes: ["red"]
                  };
                }
                if (Array.isArray(json)) {
                  for (let key of json) {
                    if (!("kty" in key)) {
                      return {
                        msg: "Field "+fieldn+": All keys should contain the 'kty' index.",
                        classes: ["red"]
                      };
                    }
                  }
                }
                else if (!("kty" in json)) {
                  return {
                    msg: "Field "+fieldn+": All keys should contain the 'kty' index.",
                    classes: ["red"]
                  };
                }
              }]
            }
          } : false,$("<h3>").text("Grant permissions"),{
            label: "Use keys to permit viewing",
            type: "checkbox",
            pointer: { main: saveas.permissions, index: "output" },
            help: "When a valid JWT - signed with one of these keys - is provided, access should be granted to view the stream.",
            value: true
          },{
            label: "Use keys to permit stream input",
            type: "checkbox",
            pointer: { main: saveas.permissions, index: "input" },
            help: "When a valid JWT - signed with one of these keys - is provided, access should be granted to input the stream.",
            value: true
          },{
            label: "Apply to",
            type: "selectinput",
            pointer: { main: saveas.permissions, index: "stream" },
            help: "The keys listed above should only grant access to the streams configured here.<br>You may use STREAMNAME+* to include all wildcard children.",
            selectinput: [
              ["*","All streams"],
              [{
                type: "inputlist",
              },"These stream names .."]
            ]
          },{
            label: "API authentication",
            type: "checkbox",
            pointer: { main: saveas.permissions, index: "admin" },
            help: "When a valid JWT - signed with one of these keys - is provided, access should be granted to the MistServer Management Interface and API."
          },{
            type: "buttons",
            buttons: [{
              type: "save",
              label: editing ? "Save" : "Add",
              icon: editing ? "check" : "plus",
              "function": function(me){
                for (let i in saveas.keys) {
                  saveas.keys[i] = JSON.parse(saveas.keys[i]);
                }
                let concatenated = saveas.urls.concat(saveas.keys);

                if (concatenated.length == 0) {
                  //no urls or keys given: show error balloons
                  let $fields = $(me).closest(".input_container").find(".field[name]");
                  $fields.each(function(){
                    //for each field of the form: add a validation function that triggers if it is empty, show the message, and then remove the extra validation function again
                    let fs = $(this).data("validate_functions");
                    fs.push(function(val,me){
                      if (val.length == 0) {
                        return {
                          msg: "Please enter something in one of these fields.",
                          classes: ["orange"]
                        };
                      }
                    });
                    $(this).data("validate_functions",fs);
                    $(this).data("validate")(this,true);
                    fs.pop();
                    $(this).data("validate_functions",fs)
                  });
                  return;
                }

                let command = { addjwks: [] }
                for (let i in concatenated) {
                  command.addjwks.push([concatenated[i],saveas.permissions]);
                }

                if (editing) {
                  command.deletejwks = (typeof editing[0] == "string" ? editing[0] : (editing[0].kid ? editing[0].kid : editing[0]));
                }

                mist.send(function(d){
                  UI.navto("General");
                },command);
              }
            },{
              type: "cancel",
              label: "Return",
              "function": function(){
                UI.navto("General");
              }
            }]
          }]));
        }

        break;
      }
      case 'Protocols':
        if (typeof mist.data.capabilities == 'undefined') {
          mist.send(function(d){
            UI.navto(tab);
          },{capabilities: true});
          $main.append('Loading..');
          return;
        }
        
        var $tbody = $('<tbody>');
        $main.append(
          UI.buildUI([{
            type: 'help',
            help: 'You can find an overview of all the protocols and their relevant information here. You can add, edit or delete protocols.'
          }])
        ).append(
          $('<button>').text('Delete all protocols').click(function(){
            if (confirm('Are you sure you want to delete all currently configured protocols?')) {
              mist.data.config.protocols = [];
              mist.send(function(d){
                UI.navto('Protocols');
              },{config: mist.data.config});
            }
          })
        ).append(
          $('<button>').text('Enable default protocols').click(function(){
            var toenable = Object.keys(mist.data.capabilities.connectors);
            for (var i in mist.data.config.protocols) {
              var p = mist.data.config.protocols[i];
              var index = toenable.indexOf(p.connector)
              if (index > -1) {
                toenable.splice(index,1);
              }
            }
            var dontskip = [];
            for (var i in toenable) {
              var connector = mist.data.capabilities.connectors[toenable[i]];
              if (("PUSHONLY" in connector) || ("NODEFAULT" in connector)) {
                continue;
              }
              if (!('required' in connector) || (Object.keys(connector.required).length == 0)) {
                dontskip.push(toenable[i]);
              }
            }
            var msg = 'Click OK to enable disabled protocols with their default settings:'+"\n  ";
            if (dontskip.length) {
              msg += dontskip.join(', ');
            }
            else {
              msg += 'None.';
            }
            if (dontskip.length != toenable.length) {
              var skip = toenable.filter(function(ele){
                return dontskip.indexOf(ele) < 0;
              });
              msg += "\n\n"+'The following protocols can only be set manually:'+"\n  "+skip.join(', ');
            }
            
            if (confirm(msg) && dontskip.length) {
              if (mist.data.config.protocols === null) { mist.data.config.protocols = []; }
              for (var i in dontskip) {
                mist.data.config.protocols.push({connector: dontskip[i]});
              }
              mist.send(function(d){
                UI.navto('Protocols');
              },{config: mist.data.config});
            }
          })
        ).append('<br>').append(
          $('<button>').text('New protocol').click(function(){
            UI.navto('Edit Protocol');
          }).css('clear','both')
        ).append(
          $('<table>').html(
            $('<thead>').addClass("sticky").html(
              $('<tr>').html(
                $('<th>').text('Protocol')
              ).append(
                $('<th>').text('Status')
              ).append(
                $('<th>').text('Settings')
              ).append(
                $('<th>')
              )
            )
          ).append(
            $tbody
          )
        );
        
        function updateProtocols() {
          function displaySettings(protocol){
            var capabilities = mist.data.capabilities.connectors[protocol.connector];
            if (!capabilities) {
              return '';
            }
            var str = [];
            var types = ['required','optional']
            for (var j in types) {
              for (var i in capabilities[types[j]]) {
                if ((protocol[i]) && (protocol[i] != '')) {
                  str.push(i+': '+protocol[i]);
                }
                else if (capabilities[types[j]][i]['default']) {
                  str.push(i+': '+capabilities[types[j]][i]['default']);
                }
              }
            }
            return $('<span>').addClass('description').text(str.join(', '));
          }
          
          $tbody.html('');
          for (var i in mist.data.config.protocols) {
            var protocol = mist.data.config.protocols[i];
            var capa = mist.data.capabilities.connectors[protocol.connector];
            $tbody.append(
              $('<tr>').data('index',i).append(
                $('<td>').text(capa && capa.friendly ? capa.friendly : protocol.connector)
              ).append(
                $('<td>').html(UI.format.status(protocol))
              ).append(
                $('<td>').html(displaySettings(protocol))
              ).append(
                $('<td>').css('text-align','right').html(
                  $('<button>').text('Edit').click(function(){
                    UI.navto('Edit Protocol',$(this).closest('tr').data('index'));
                  })
                ).append(
                  $('<button>').text('Delete').click(function(){
                    var index = $(this).closest('tr').data('index');
                    if (confirm('Are you sure you want to delete the protocol "'+mist.data.config.protocols[index].connector+'"?')) {
                      mist.send(function(d){
                        UI.navto('Protocols');
                      },{deleteprotocol: mist.data.config.protocols[index]});
                      mist.data.config.protocols.splice(index,1);
                    }
                  })
                )
              )
            );
          }
        }
        updateProtocols();
        UI.interval.set(function(){
          mist.send(function(){
            updateProtocols();
          });
        },10e3);
        break;
      case 'Edit Protocol':
        if (typeof mist.data.capabilities == 'undefined') {
          mist.send(function(d){
            UI.navto(tab,other);
          },{capabilities: true});
          $main.append('Loading..');
          return;
        }
        
        var editing = false;
        if ((other != '') && (other >= 0)) { editing = true; }
        var current = {};
        for (var i in mist.data.config.protocols) {
          current[mist.data.config.protocols[i].connector] = 1;
        }
        
        function buildProtocolSettings(kind) {
          var input = mist.data.capabilities.connectors[kind];
          var build = mist.convertBuildOptions(input,saveas);
          if (editing) {
            var orig = $.extend({},saveas);
          }
          build.push({
            type: 'hidden',
            pointer: {
              main: saveas,
              index: 'connector'
            },
            value: kind
          });
          build.push({
            type: 'buttons',
            buttons: [
            {
              type: 'save',
              label: 'Save',
              'function': function(){
                var send = {};
                if (editing) {
                  send.updateprotocol = [orig,saveas];
                }
                else {
                  send.addprotocol = saveas;
                }
                mist.send(function(d){
                  UI.navto('Protocols');
                },send);
              }
            },{
              type: 'cancel',
              label: 'Cancel',
              'function': function(){
                UI.navto('Protocols');
              }
            }
            ]
          });
          
          if (('deps' in input) && (input.deps != '')) {
            $t = $('<span>').text('Dependencies:');
            $ul = $('<ul>');
            $t.append($ul);
            if (typeof input.deps == 'string') { input.deps = input.deps.split(', '); }
            for (var i in input.deps) {
              var $li = $('<li>').text(input.deps[i]+' ');
              $ul.append($li);
              if ((typeof current[input.deps[i]] != 'undefined') || (typeof current[input.deps[i]+'.exe'] != 'undefined')) {
                //also check for the windows executable
                $li.append(
                  $('<span>').addClass('green').text('(Configured)')
                );
              }
              else {
                $li.append(
                  $('<span>').addClass('red').text('(Not yet configured)')
                );
              }
            }
            build.unshift({
              type: 'text',
              text: $t[0].innerHTML
            });
          }
          
          return UI.buildUI(build);
        }
        
        var current = {};
        for (var i in mist.data.config.protocols) {
          current[mist.data.config.protocols[i].connector] = 1;
        }
        if (!editing) {
          //new
          $main.html(
            $('<h2>').text('New Protocol')
          );
          var saveas = {};
          var select = [['','']];
          for (var i in mist.data.capabilities.connectors) {
            select.push([i,(mist.data.capabilities.connectors[i].friendly ? mist.data.capabilities.connectors[i].friendly : i)]);
          }
          var $cont = $('<span>');
          $main.append(UI.buildUI([{
            label: 'Protocol',
            type: 'select',
            select: select,
            'function': function(){
              if ($(this).getval() == '') { return; }
              $cont.html(buildProtocolSettings($(this).getval()));
            }
          }])).append(
            $cont
          );
        }
        else {
          //editing
          var protocol = mist.data.config.protocols[other];
          var saveas = protocol;
          $main.find('h2').append(' "'+protocol.connector+'"');
          $main.append(buildProtocolSettings(protocol.connector));
        }
        break;
      case 'Streams': {
        if (!('capabilities' in mist.data)) {
          $main.html('Loading..');
          mist.send(function(){
            UI.navto(tab,other);
          },{capabilities: true, streamkeys: true});
          return;
        }


        var stored = mist.stored.get();
        var sortstreams = {
          by: "name",
          dir: 1
        };
        var pagesize;
        if (other == '') {
          if ('viewmode' in stored) {
            other = stored.viewmode;
          }
        }
        if ('sortstreams' in stored) {
          sortstreams = stored.sortstreams;
        }
        if ('streams_pagesize' in stored) {
           pagesize = stored.streams_pagesize;
        }



        var $streams;

        var $form = UI.buildUI([
          {
            type: 'help',
            help: "This is an overview of the streams you\'ve currently configured.<br>You can left click a stream to "+(other == "thumbnails" ? "edit" : "preview")+" it"+(other == "thumbnails" ? ", or its thumbnail to preview it" : "")+". You can right click a stream for an action menu."
          },
          $('<div>').css({
            width: '45.25em',
            display: 'flex',
            'justify-content':'flex-end'
          }).append(
            $("<button>").text('Switch to '+(other == 'thumbnails' ? 'list' : 'thumbnail')+' view').click(function(){
              mist.stored.set('viewmode',(other == 'thumbnails' ? 'list' : 'thumbnails'));
              UI.navto('Streams',(other == 'thumbnails' ? 'list' : 'thumbnails'));
            })
          ).append(
            $("<button>").attr("data-icon","key").text("Manage stream keys").click(function(){
              UI.navto("Stream keys");
            })
          ).append(
            $('<button>').attr("data-icon","plus").text('Create a new stream').click(function(){
              UI.navto('Edit');
            })
          ),
          {
            label: "Filter streams",
            classes: ["filter"],
            help: "Stream names that do not contain the text you enter here will be hidden.",
            "function": function(e){
              var val = $(this).getval();
              if ($streams) $streams.filter(val);
            },
            css: {"margin-top":"3em"}
          }
        ]);

        $main.append($form);


        var current_streams = $.extend({},mist.data.streams);
        var context_menu = new UI.context_menu();

        if (other == "thumbnails") {
          $streams = UI.dynamic({
            create: function(){
              var cont = document.createElement("div");
              cont.className = "streams thumbnails";

              UI.sortableItems(cont,function(sortby){
                return this.sortValues[sortby];
              },{}); //add a sorting function to the container, but do not apply sort attributes

              return cont;
            },
            values: current_streams,
            add: {
              create: function(id){
                var stream = document.createElement("div");
                stream.className = "stream";
                stream.setAttribute("data-id",id);

                var elements = ["thumbnail","actions"];
                stream.elements = {};
                stream.elements.header = document.createElement("a");
                stream.elements.header.className = "header";
                stream.appendChild(stream.elements.header);
                for (var i in elements) {
                  var e = elements[i];
                  stream.elements[e] = document.createElement("div");
                  stream.elements[e].className = e;
                  stream.elements[e].raw = false;
                  stream.appendChild(stream.elements[e]);
                }
                if (id.indexOf("+") >= 0) {
                  stream.setAttribute("data-iswildcardstream","yes");
                  var wildparent = document.createElement("span");
                  wildparent.className = "wildparent";
                  var parts = id.split("+");
                  wildparent.appendChild(document.createTextNode(parts.shift()+"+"));
                  stream.elements.header.appendChild(wildparent);
                  stream.elements.header.appendChild(document.createTextNode(parts.join("+")));
                }
                else {
                  stream.setAttribute("data-iswildcardstream","no");
                  stream.elements.header.innerText = id;
                }
                stream.elements.header.addEventListener("click",function(){
                  UI.navto("Edit",id);
                });
                stream.elements.thumbnail.addEventListener("click",function(){
                  if (current_streams[id].isfolderstream) {
                    if (!current_streams[id].filesfound) {
                      UI.findFolderSubstreams(current_streams[id],function(result){
                        $.extend(current_streams,result);
                        $streams.update(current_streams);
                        current_streams[id].filesfound = true;
                        stream.setAttribute("data-showingsubstreams","yes");
                        stream.setAttribute("title","This is a folder stream: it points to a folder with media files inside.")
                      });
                    }
                    else {
                      UI.navto("Edit",id);
                    }
                  }
                  else {
                    UI.navto("Preview",id);
                  }
                });
                stream.elements.actions.appendChild($("<button>").text("Actions").click(function(e){
                  var pos = $(this).offset();
                  context_menu.fill(id,{pageX:pos.left,pageY:pos.top});
                  e.stopPropagation();
                })[0]);

                stream.insertBefore(
                  UI.modules.stream.status(id,{thumbnail:false,tags:"readonly"})[0],
                  stream.children[1]
                );

                stream.remove = function(){
                  if (this.parentNode) {
                    this.parentNode.removeChild(this);
                  }
                };

                stream.addEventListener("contextmenu",function(e){
                  e.preventDefault();
                  context_menu.fill(id,e);
                });

                return stream;
              },
              update: function(data){
                if ((data.online == 1) && (data.online != this.elements.thumbnail.raw)) {
                  var $thumb = UI.modules.stream.thumbnail(data.name,{clone:true}); 
                  this.elements.thumbnail.appendChild($thumb[0]);
                  this.elements.thumbnail.raw = data.online;
                }

                if (data.source !== this.raw_source) {
                  if (data.source === null) {
                    this.elements.header.classList.add("wildparent");
                    this.setAttribute("title","This stream has no configuration and will disappear once it goes offline.");
                  }
                  else {
                    if (this.raw_src === null) {
                      this.classList.remove("wildparent");
                    }

                    this.raw_source = data.source;

                    //is it a folder stream?
                    var inputs_f = UI.findInput("Folder");
                    this.setAttribute("title",data.source);
                    if (inputs_f) {
                      if (mist.inputMatch(inputs_f.source_match,data.source)) {
                        this.setAttribute("data-isfolderstream","yes");
                        data.isfolderstream = true;
                        this.setAttribute("title","This is a folder stream: it points to a folder with media files inside. Click to request its sub streams.");
                      }
                      else {
                        this.setAttribute("data-isfolderstream","no");
                        this.setAttribute("title",data.source);
                        data.isfolderstream = false;
                      }
                    }
                  }
                }

                //translate state integer to something to sort to
                //we want to see "Available" streams first, then by order of how far along it is in its boot sequence
                var state_map = [
                  0, //Offline
                  1, //Initializing
                  2, //Booting
                  3, //Waiting for data
                  5, //Available
                  4, //Shutting down
                  0 //Invalid state: treat as offline
                ]; 

                this.sortValues = {
                  name: data.name,
                  state: data.stats && (data.stats.length >= 2) ? (state_map.length > data.stats[1] ? state_map[data.stats[1]] : 0) : 0,
                  viewers: data.stats && (data.stats.length >= 3) ? data.stats[2] : 0,
                  inputs: data.stats && (data.stats.length >= 4) ? data.stats[3] : 0,
                  outputs: data.stats && (data.stats.length >= 5) ? data.stats[4] : 0
                };

              }
            },
            update: function(){
              this.sort();
              if (this.show_page) this.show_page();
            }
          });
          $main.append($streams);

          var sort_index = sortstreams.by;
          var sort_dir = {name: 1,viewers:-1,state:-1,inputs:-1,outputs:-1}; //for name, ascending is intuitive, but for viewers we prolably want to sort descending
          var sort_reverse = sort_dir[sort_index]*sortstreams.dir;

          $form.append(UI.buildUI([{
            label: "Sort streams by",
            help: "Choose by which attribute the streams listed below should be sorted",
            type: "select",
            select: [
              ["name","Stream name"],
              ["state","State (Online, offline, waiting etc.)"],
              ["viewers","Viewers"],
              ["inputs","Inputs"],
              ["outputs","Outputs"]
            ],
            value: sortstreams.by,
            "function": function(e){
              sort_index = $(this).getval();
              sortstreams.by = sort_index;
              mist.stored.set('sortstreams',sortstreams);

              if ($streams) $streams.sort(sort_index,sort_dir[sort_index]*sort_reverse);
            },
            "unit": $("<label>").append(
              $("<input>").attr("type","checkbox").prop("checked",sort_reverse == -1).change(function(){
                sort_reverse = $(this).is(":checked") ? -1 : 1;
                sortstreams.dir = sort_reverse*sort_dir[sort_index];
                mist.stored.set('sortstreams',sortstreams);

                if ($streams) $streams.sort(sort_index,sort_dir[sort_index]*sort_reverse);
              })
            ).append(
              $("<span>").text("Reverse")
            )
          }]).children());

        }
        else {
          var $table = $("<table>").addClass("streams");
          $main.append($table);
          $table.layout = {
            name: function(d,id){ 
              if (id != this.raw) {
                this.raw = id;
                var td = this;
                var a = $("<a>").addClass("clickable").text(d.name).click(function(){
                  if (($(td).attr("data-iswildcard") == "no") && ($(td).attr("data-isfolderstream") == "yes")) {
                    UI.navto("Edit",id);
                  }
                  else {
                    UI.navto("Preview",id);
                  }
                });
                if (id.indexOf("+") >= 0) {
                  var split = id.split("+");
                  var parentstream = split.shift();
                  var substream = split.join("+");
                  $(this).attr("data-iswildcard",parentstream+"+");
                  a.attr("title","This is a wildcard stream: its config is inherited from its parent: '"+parentstream+"'.").html(
                    $("<span>").addClass("wildparent").text(parentstream+"+")
                  ).append(
                    substream
                  );
                }
                else {
                  $(this).attr("data-iswildcard","no");
                }
                $(this).html(a);
              }
              if (d.source !== this.raw_src) {
                if (d.source === null) {
                  $(this).addClass("wildparent");
                  $(this).find("a").attr("title","This stream has no configuration and will disappear once it goes offline.")
                }
                else if (this.raw_src === null) {
                  $(this).removeClass("wildparent");
                  $(this).find("a").attr("title",id.indexOf("+") >= 0 ? "This is a wildcard stream: its config is inherited from its parent: '"+parentstream+"'." : false );
                }

                this.raw_src = d.source;
                //check if this is a folder stream
                if (mist.data.capabilities) {
                  var inputs_f = UI.findInput("Folder");
                  if (inputs_f) {
                    if (mist.inputMatch(inputs_f.source_match,d.source)) {
                      $(this).attr("data-isfolderstream","yes").attr("title","This is a folder stream: it points to a folder with media files inside. Click the '➕' to request its sub streams.");
                      d.isfolderstream = true;
                    }
                    else {
                      $(this).attr("data-isfolderstream","no").attr("title",d.source);
                      d.isfolderstream = false;
                    }
                  }
                }
              }
            },
            actions: function(d,streamname){
              if (this.raw) return;

              $(this).html(
                $("<button>").text("Actions").click(function(e){
                  var pos = $(this).offset();
                  context_menu.fill(streamname,{pageX:pos.left,pageY:pos.top});
                  e.stopPropagation();
                })
              );

              this.raw = true;
            },
            state: function(d){ 
              var state = $("<div>").attr("data-streamstatus",0).text("Inactive");
              if ("stats" in d) {
                if (this.raw == d.stats[1]) { return; }
                var s = ["Inactive","Initializing","Booting","Waiting for data","Available","Shutting down","Invalid state"];
                state = $("<div>").attr("data-streamstatus",d.stats[1]).text(s[d.stats[1]]);
                this.raw = d.stats[1];
              }
              $(this).html(state).addClass("activestream");
            },
            tags: function(d){
              //this is a dynamic element
              this.update(d);
            },
            viewers: function(d){
              var out = "";
              if ("stats" in d) {
                if (this.raw == d.stats[2]) { return; }
                out = d.stats[2];
                this.raw = d.stats[2];
                if (out == 0) out = "";
              }
              $(this).html(out);
            },
            inputs: function(d){
              var out = "";
              if ("stats" in d) {
                if (this.raw == d.stats[3]) { return; }
                out = d.stats[3];
                this.raw = d.stats[3];
                if (out == 0) out = "";
              }
              $(this).html(out);
            },
            outputs: function(d){
              var out = "";
              if ("stats" in d) {
                if (this.raw == d.stats[4]) { return; }
                out = d.stats[4];
                this.raw = d.stats[4];
                if (out == 0) out = "";
              }
              $(this).html(out);
            }
          };

          var $tr = $("<tr>").attr("data-sortby","name");
          $table.append($("<thead>").addClass("sticky").append($tr));
          var headers = {
            name: "Stream name",
            actions: ""
          };
          for (var i in $table.layout) {
            var label = i in headers ? headers[i] : UI.format.capital(i);
            var $th = $("<th>").text(label);
            if (label != "") {
              $th.attr("data-index",i);
            }
            $tr.append($th);
          }
          $streams = UI.dynamic({
            create: function(){
              var tbody = document.createElement("tbody");

              UI.sortableItems(tbody,function(sortby){
                return this._cells[sortby].raw;
              },{controls:$tr[0],sortsave:"sortstreams"});

              tbody.remove = function(){
                if (this.parentNode) {
                  this.parentNode.removeChild(this);
                }
              };
              return tbody;
            },
            values: current_streams,
            add: {
              create: function(streamname){
                var row = document.createElement("tr");
                row.setAttribute("data-id",streamname);

                row._cells = {};
                for (var i in $table.layout) {
                  var td = document.createElement("td");
                  td.setAttribute("data-index",i);
                  row._cells[i] = td;
                  row.append(td);
                }
                var tags_td = row._cells.tags;
                row._cells.tags = UI.modules.stream.tags({
                  streamname: streamname,
                  context_menu: context_menu,
                  onclick: function(e,id){
                    if (this.getAttribute("data-type") > 0) {
                      var $filter = $form.find(".field.filter");
                      if ($filter.getval() == "#"+id) {
                        var last = $filter.data("lastval");
                        $filter.setval(last ? last : "");
                      }
                      else {
                        $filter.data("lastval",$filter.getval());
                        $filter.setval("#"+id);
                      }
                    }
                  },
                  getStreamstatus: function(){
                    return this.closest("tr").querySelector("[data-streamstatus]").getAttribute("data-streamstatus");
                  }
                });
                tags_td.appendChild(row._cells.tags);

                row.addEventListener("contextmenu",function(e){
                  e.preventDefault();
                  context_menu.fill(streamname,e);
                });
                row.addEventListener("click",function(e){
                  if (current_streams[streamname].isfolderstream) {
                    if ("filesfound" in current_streams[streamname]) {
                      return;
                    }
                    else {
                      UI.findFolderSubstreams(current_streams[streamname],function(result){
                        $.extend(current_streams,result);
                        $streams.update(current_streams);
                        row.setAttribute("data-showingsubstreams","");
                      });
                    }

                    $streams.show_page(this); //ensure current cell is visible
                  }
                });

                row.remove = function(){
                  if (this.parentNode) {
                    this.parentNode.removeChild(this);
                  }
                };

                return row;
              },
              update: function(data,allValues){
                for (var i in $table.layout) {
                  $table.layout[i].call(this._cells[i],data,this._id);
                }
              }
            },
            update: function(){
              this.sort();
              if (this.show_page) this.show_page();
            }
          });
          $table.append($streams);
        }


        $streams.filter = function(str){
          if (str[0] == "#") {
            //filter tags
            str = str.slice(1);
            for (var i = 0; i < this.children.length; i++) {
              var item = this.children[i];
              if ((str in item._cells.tags.values) && (item._cells.tags.values[str] > 0)) {
                item.classList.remove("hidden");
              }
              else {
                item.classList.add("hidden");
              }
            }
          }
          else {
            //filter stream name
            str = str.toLowerCase();
            for (var i = 0; i < this.children.length; i++) {
              var item = this.children[i];
              if (item.getAttribute("data-id").toLowerCase().indexOf(str) >= 0) {
                item.classList.remove("hidden");
              }
              else {
                item.classList.add("hidden");
              }
            }
          }
          $streams.show_page();
        };

        $main.append(context_menu.ele);
        context_menu.fill = function(streamname,e){
          let settings = current_streams[streamname];
          function formatStreamname(streamname) {
            if (settings.source === null) {
              //this stream does not exist in config
              return "<span class=\"wildparent\">"+streamname+"</span><div class=\"description\">Unconfigured</div>";
            }
            if (streamname.indexOf("+") >= 0) {
              //it's a wildcard stream: highlight the base
              let split = streamname.split("+");
              return "<span class=\"wildparent\">"+split[0]+"+</span>"+split.slice(1).join("+");
            }
            return streamname;
          }
          var header = [
            $("<div>").addClass("header").html(formatStreamname(streamname))
          ];
          var gototabs = [
            [$("<span>").html("Edit "+(streamname.indexOf("+") < 0 ? "stream" : "<b>"+streamname.split("+")[0]+"</b>")),function(){ UI.navto("Edit",streamname); },"Edit","Change the settings of this stream."],
            ["Stream status",function(){ UI.navto("Status",streamname); },"Status","See more details about the status of this stream."],
            ["Preview stream",function(){ UI.navto("Preview",streamname); },"Preview","Watch the stream."],
            ["Embed stream",function(){ UI.navto("Embed",streamname); },"Embed","Get urls to this stream or get code to embed it on your website."]
          ];
          var actions = [
            ["Delete stream",function(){
              if (confirm('Are you sure you want to delete the stream "'+streamname+'"?')) {
                delete mist.data.streams[streamname];
                mist.send(function(d){
                  delete current_streams[streamname];
                  $streams.update(current_streams);
                },{deletestream: [streamname]});
              }
            },"trash","Remove this stream's settings."],
            ["Stop sessions",function(){
              if (confirm("Are you sure you want to disconnect all sessions (viewers, pushes and possibly the input) for the stream '"+streamname+"'?")) {
                mist.send(function(){
                  //done
                },{stop_sessions:streamname});
              }
            },"stop","Disconnect sessions for this stream. Disconnecting a session will kill any currently open connections (viewers, pushes and possibly the input). If the USER_NEW trigger is in use, it will be triggered again by any reconnecting connections."],
            ["Invalidate sessions",function(){
              if (confirm("Are you sure you want to invalidate all sessions for the stream '"+streamname+"'?\nThis will re-trigger the USER_NEW trigger.")) {
                mist.send(function(){
                  //done
                },{invalidate_sessions:streamname});
              }
            },"invalidate","Invalidate all the currently active sessions for this stream. This has the effect of re-triggering the USER_NEW trigger, allowing you to selectively close some of the existing connections after they have been previously allowed. If you don't have a USER_NEW trigger configured, this will not have any effect."],
            ["Nuke stream",function(){
              if (confirm("Are you sure you want to completely shut down the stream '"+streamname+"'?\nAll viewers will be disconnected.")) {
                mist.send(function(){
                  //done
                },{nuke_stream:streamname});
              }
            },"nuke","Shut down a running stream completely and/or clean up any potentially left over stream data in memory. It attempts a clean shutdown of the running stream first, followed by a forced shut down, and then follows up by checking for left over data in memory and cleaning that up if any is found."]
          ];
          if (settings.source === null) {
            gototabs.shift();
            gototabs.unshift([$("<span>").html("Create <b>"+streamname.split("+")[0]+"</b>"),function(){ UI.navto("Edit",streamname); },"Edit","Create this stream."]);
            actions.shift();
          }
          if (settings.isfolderstream) {
            gototabs.pop();
            gototabs.pop();
            actions = [actions[0]];
          }



          var menu = [header];
          if (current_streams[streamname].isfolderstream && !current_streams[streamname].filesfound) {
            menu.push([
              ["Scan folder for sub streams",function(){
                UI.findFolderSubstreams(current_streams[streamname],function(result){
                  $.extend(current_streams,result);
                  $streams.update(current_streams);
                });
              },"folder"]
            ]);
          }
          menu.push(gototabs);
          menu.push(actions);

          //let's not wake it up for this alone
          if (current_streams[streamname].online == 1) {
            menu.push(
              $("<aside>").append(UI.modules.stream.thumbnail(streamname))
            );
          }

          context_menu.show(menu,e);
          context_menu.ele.find("[tabindex]").first().focus();
        };

        


        UI.sockets.ws.active_streams.subscribe(function(type,data){
          if (type != "stream") return;
          var streamname = data[0];
          var streambase = streamname.split("+")[0];

          if (streambase in mist.data.streams) {
            if (streambase != streamname) {
              if  (!(streamname in current_streams)) {
                //it's a new wildcard stream
                current_streams[streamname] = $.extend({},mist.data.streams[streambase]);
                current_streams[streamname].name = streamname;
                if (current_streams[streambase].isfolderstream) {
                  current_streams[streamname].source += streamname.replace(streambase+"+");
                }
              }
              else if (data.slice(1).every((v)=>!v)) {
                //this wildcard stream no longer has any stats or tags
                //remove it from the table
                delete current_streams[streamname];
                $streams.update(current_streams);
                return;
              }
            }
            current_streams[streamname].stats = data;

            $streams.update(current_streams);
          }
          else if (streamname in current_streams) {
            current_streams[streamname].stats = data;
            $streams.update(current_streams);
          }
          else {
            //received information about unknown stream
            if (streamname) {
              //insert with default settings, uneditable
              current_streams[streamname] = {
                name: streamname,
                source: null,
                stats: data
              };
              $streams.update(current_streams);
            }
            else {
              console.log("Received information about unknown stream",streamname,data);
            }
          }

        });
        
        var $pagecontrol = UI.pagecontrol($streams,pagesize);
        $main.append($pagecontrol);

        //save selected page size
        $pagecontrol.elements.pagelength.change(function(){
          mist.stored.set("streams_pagesize",$(this).val());
        });

        break;
      }
      case 'Edit': {
        $main.append('Loading..');
        //renew capabilities and streamkeys
        mist.send(function(d){

          var editing = false;
          if (other != '') { editing = true; }

          if (!editing) {
            //new
            $main.html(
              $('<h2>').text('New Stream')
            );
            var saveas = {};
          }
          else {
            //editing
            var streamname = other.split("+")[0];
            var saveas;
            if (streamname in mist.data.streams) {
              saveas = $.extend({},mist.data.streams[streamname]);
            }
            else {
              //this stream does not exist yet, the user will want to create it
              //prefill the form
              saveas = {
                name: streamname,
                source: "push://"
              };
              if (UI.findStreamKeys(other).length) {
                //there are stream keys for the stream we are trying to create
                //preconfigure with required stream key
                saveas.source += "invalid,host";
              }
            }
            $main.html(
              UI.modules.stream.header(other,tab,streamname)
            );
            if (streamname != other) {
              $main.append(
                $("<div>").addClass("err_balloon").addClass("orange").css({position:"static",width:"54.65em",margin:"2em 0 3em"}).html("Note:<br>You are editing the settings of <b>"+streamname+"</b>, which is the parent of wildcard stream <b>"+other+"</b>. This will also affect other children of <b>"+streamname+"</b>.")
              );
            }
          }


          //find existing stream keys
          saveas.streamkeys = UI.findStreamKeys(other.split("+")[0]); //(even if source is not push://)
          if (saveas.source && saveas.source.slice(0,7) == "push://") {
            //if host "streamkey" is used, check the checkbox and clean from displayed source
            if (saveas.source.match(/push:\/\/[^:@\/]*/)?.[0] == "push://invalid,host") {
              saveas.streamkey_only = true;
              saveas.source = saveas.source.replace("push://invalid,host","push://");
            }
          }

          var filetypes = [];
          var $source_datalist = $("<datalist>").attr("id","source_datalist");
          var source_hinting = {};
          var $source_info = $("<div>").addClass("source_info");
          var dynamic_capa_rate_limit = false;
          var dynamic_capa_source = false;

          function addSourceHint(input) {
            let prefill = "source_prefill" in input ? input.source_prefill : [];
            if (typeof prefill == "string") {
              prefill = [prefill];
            }
            for (var j in prefill) {
              if (!(prefill[j] in source_hinting)) {
                source_hinting[prefill[j]] = [];
                $source_datalist.append(
                  $("<option>").val(prefill[j])
                );
              }
              source_hinting[prefill[j]].push(input);
            }
            //also add the "<INPUT NAME>://" type syntax to the source_hinting, but not to the prefill
            let input_override = input.name.toLowerCase()+":";
            if (!(input_override in source_hinting)) source_hinting[input_override] = [input];
          }
          for (var i in mist.data.capabilities.inputs) {
            for (var j in mist.data.capabilities.inputs[i].source_match) {
              filetypes.push(mist.data.capabilities.inputs[i].source_match[j]);
            }
            addSourceHint(mist.data.capabilities.inputs[i]);
          }
          Object.defineProperty(source_hinting,"prefills",{
            value: Object.keys(source_hinting).sort((a,b)=>{b.length-a.length}) //contain an array of the object keys, sorted by string length desc
          }); //not enumerable

          let $source_help = $("<div>").text("source_help");
          function showHint(source,cursorpos) {
            //show help text in the source help balloon and return type of input

            $source_help.html(
              $("<p>").html("Where MistServer can find the media data.<br>This help text will update as you type to provide more details.")
            ).append(
              $("<h3>").text("Video on Demand (VoD)")
            ).append(
              $("<p>").text("Please enter the path to your media file. This can be a file on your server (Use the 'browse'-button) or somewhere on the internet (enter the url).")
            ).append(
              $("<h3>").text("Live streaming")
            ).append(
              $("<h4>").text("Pulling from a device or server")
            ).append(
              $("<p>").html("If MistServer should pull the stream from somewhere - for example an IP camera - enter the protocol and address where it can be reached. This should be provided to you by the device or server.<br>For example: <b>rtsp://[user]:[password]@[hostname]</b>")
            ).append(
              $("<h4>").text("Pushing into MistServer")
            ).append(
              $("<p>").html("To set up MistServer to receive stream data from another application or server, enter <b>push://</b> and follow the instructions from there.")
            );

            var type = UI.findInputBySource(source)?.index;

            let syntaxes = {};
            for (let prefill of source_hinting.prefills) {
              if (source.startsWith(prefill)) {
                //the entered source starts with this prefill: gather syntaxes to show in the help
                let inputs = source_hinting[prefill];
                for (let input of inputs) {
                  if (type && (input.name == type)) {
                    //the source string already matches an input type: this will be printed seperately
                    
                  }

                  let syntax = "source_syntax" in input ? input.source_syntax : [];
                  if (typeof syntax == "string") syntax = [syntax];
                  if (syntax.filter((a)=>a.startsWith(input.name.toLowerCase()+":")).length == 0) {
                    //also insert generic syntax - if it has not already been included
                    syntax.push(input.name.toLowerCase()+":[address]");
                  }
                  for (s of syntax) {
                    //add this syntax only if it matches the used prefill
                    if (s.startsWith(prefill)) {
                      if (type && (input.name == type)) {
                        syntaxes["_match"+s] = [input.name];
                      }
                      else {
                        if (!(s in syntaxes)) syntaxes[s] = [];
                        syntaxes[s].push(input.name);
                      }
                    }
                  }
                }
                $source_help.html(
                  $("<p>").text("Where MistServer can find the media data.")
                );
                if (Object.keys(syntaxes).length) {
                  let $ul = $("<ul>").addClass("syntaxes");
                  $source_help.append($ul);
                  for (let syntax in syntaxes) {
                    let index = syntax;
                    let is_match = (syntax.slice(0,6) == "_match");
                    if (is_match) {
                      syntax = syntax.slice(6);
                    }

                    let $syntax = $("<span>"); 

                    //highlight cursor location in the syntax string
                    let syntaxregex = syntax.replace(/\//g,"\\/").replace(/\[.*?\]/g,function(str){
                      if (str[1].match(/[a-zA-Z0-9]/) === null) return "(\\"+str[1]+".*?)?";
                      return "(.*?)?";
                    })+"$";
                    let source_withcursor = source.substring(0,cursorpos) + "🐁🐀🐁" + source.substring(cursorpos)
                    let match = source_withcursor.match(syntaxregex);
                    let highlighted = syntax;
                    if ((match !== null) && (match.length > 1) && (typeof match[1] != "undefined")) {
                      //find which group contains the mouse cursor
                      for (let i = 1; i < match.length; i++) {
                        if (match[i].includes("🐁🐀🐁")) {
                          //make the same captured group bold
                          let n = 0;
                          highlighted = highlighted.replace(/(\[.*?\])/g,function(str){
                            n++;
                            if (n == i) return "<b>"+str+"</b>"; 
                            else return str;
                          })
                          break;
                        }
                      }
                    }
                    $syntax.html(highlighted);

                    //create a 'pretty' list of the inputs that apply for this syntax
                    let names = [];
                    for (let name of syntaxes[index]) {
                      let input = name in mist.data.capabilities.inputs ? mist.data.capabilities.inputs[name] : (name + ".exe" in mist.data.capabilities.inputs ? mist.data.capabilities.inputs[name + ".exe"] : null);
                      if (!input) return; //should not happen
                      names.push("source_name" in input ? input.source_name : name);
                    }
                    if (names.length >= 2) {
                      let last_two = names.splice(-2);
                      names.push(last_two.join(" or "));
                    }

                    let $help = $("<div>").addClass("description");
                    let $li = $("<li>").html(
                      $("<div>").text(is_match ? "Matched input type: " : "Input type: ").append(
                        $("<h4>").css("display","inline").text(names.join(", "))
                      )
                    ).append(
                      $("<div>").text("Syntax: ").append($syntax) 
                    ).append(
                      $help
                    );
                    
                    if (is_match) {
                      $ul.prepend($li.attr("data-icon","check"));
                    }
                    else {
                      $ul.append($li);
                    }

                    //gather syntax help
                    let help = [];
                    for (let i of syntaxes[index]) {
                      let input = i in mist.data.capabilities.inputs ? mist.data.capabilities.inputs[i] : (i + ".exe" in mist.data.capabilities.inputs ? mist.data.capabilities.inputs[i + ".exe"] : null);
                      if (!input) return; //should not happen

                      let h = input.desc;
                      if ("source_help" in input) h = input.source_help;
                      if (typeof h == "object") {
                        //an object is used to show a specific help for a specific syntax
                        if (syntax in h) {
                          h = h[syntax];
                        }
                        else if ("default" in h) {
                          h = h["default"];
                        }
                        else {
                          //probably an invalid syntax, revert to desc
                          h = input.desc;
                        }
                      }
                      if (!help.includes(h)) help.push(h);
                    }
                    for (let h of help) {
                      $help.append(
                        $("<div>").css("white-space","pre-line").text(h)
                      );
                    }
                  }
                }
                break;
              }
            }

            return type ? type : null;
          }

          var $inputoptions = $('<div>');

          function save(tab) {
            var send = {};

            if (!mist.data.streams) {
              mist.data.streams = {};
            }

            mist.data.streams[saveas.name] = saveas;
            if (saveas.source === null) saveas.source = "";

            send.addstream = {};
            send.addstream[saveas.name] = saveas;
            if (other != saveas.name) {
              delete mist.data.streams[other];
              send.deletestream = [other];
            }
            if ((saveas.stop_sessions) && (other != '')) {
              send.stop_sessions = other;
              delete saveas.stop_sessions;
            }

            if (saveas.source.slice(0,7) == "push://") {
              send.streamkey_del = [];
              send.streamkey_add = {};
              let old = UI.findStreamKeys(other.split("+")[0]);
              for (let key of old) {
                if (saveas.streamkeys.indexOf(key) < 0) {
                  //remove any stream keys that are no longer being saved
                  send.streamkey_del.push(key);
                }
              }
              for (let key of saveas.streamkeys) {
                //add any stream keys that don't exist yet or that are not set to saveas.name (possible when renaming stream, otherwise should cause form to be invalid)
                if (!mist.data.streamkeys || !(key in mist.data.streamkeys) || (mist.data.streamkeys[key] != saveas.name)) {
                  send.streamkey_add[key] = saveas.name;
                }
              }
              //when a stream is being renamed:
              //- any old keys still in the array will be overwritten, so that they now point to the new stream name
              //- when keys were removed from the form, they will also be removed from the old stream name
              if (saveas.streamkey_only) {
                //add "invalid,host" as host
                saveas.source = saveas.source.replace(/push:\/\/[^:@\/]*/,"push://invalid,host");
              }
              else {
                //remove invalid host if applicable
                saveas.source = saveas.source.replace("push://invalid,host","push://");
              }
            }

            var type = null;
            for (var i in mist.data.capabilities.inputs) {
              if (typeof mist.data.capabilities.inputs[i].source_match == 'undefined') { continue; }
              if (mist.inputMatch(mist.data.capabilities.inputs[i].source_match,saveas.source)) {
                type = i;
                break;
              }
            }
            if (type) {
              //sanatize saveas, remove options not in capabilities
              var input = mist.data.capabilities.inputs[type];
              for (var i in saveas) {
                if ((i == "name") || (i == "source") || (i == "stop_sessions") || (i == "processes") || (i == "tags")) { continue; }
                if (("optional" in input) && (i in input.optional)) { continue; }
                if (("required" in input) && (i in input.required)) { continue; }
                if ((i == "always_on") && ("always_match" in input) && (mist.inputMatch(input.always_match,saveas.source))) { continue; }
                delete saveas[i];
              }
            }

            mist.send(function(){
              delete mist.data.streams[saveas.name].online;
              delete mist.data.streams[saveas.name].error;
              UI.navto(tab,(tab == 'Preview' ? (other.indexOf("+") < 0 ? saveas.name : other) : ''));
            },send);


          }

          var $style = $('<style>').text('button.saveandpreview { display: none; }');
          var $livestreamhint = $('<span>').addClass("ih_balloon");
          var $processes = $('<div>');
          var newproc = {};
          var select = [];
          var $subtypecont = $("<div>");
          for (var i in mist.data.capabilities.processes) {
            select.push([i,(mist.data.capabilities.processes[i].hrn ? mist.data.capabilities.processes[i].hrn :mist.data.capabilities.processes[i].name)]);
          }
          if (select.length) {
            //if there are processes available
            var sublist = [{
              label: 'New process',
              type: 'select',
              select: select,
              value: select[0][0], //set the default type to the first process
              pointer: {
                main: newproc,
                index: "process"
              },
              "function": function(){
                var type = $(this).getval();
                if (type != null) {
                  var capabilities = mist.data.capabilities.processes[type];
                  var UIarr = [
                    $("<h4>").text(capabilities.name+" Process options")
                  ];
                  $subtypecont.html(UI.buildUI(UIarr.concat(mist.convertBuildOptions(capabilities,newproc))));
                }
              }
            },$subtypecont];
            $processes.append(UI.buildUI([
              $("<br>"),
              $("<h3>").text("Stream processes"),
              {
                label: "Stream processes",
                itemLabel: "stream process",
                type: "sublist",
                sublist: sublist,
                saveas: newproc,
                pointer: {
                  main: saveas,
                  index: "processes"
                }
              }
            ]));
          }
          var $form = UI.buildUI([
            {
              label: 'Stream name',
              type: 'str',
              validate: ['required','streamname'],
              pointer: {
                main: saveas,
                index: 'name'
              },
              help: 'Set the name this stream will be recognised by for players and/or stream pushing.'
            },{
              label: 'Source',
              type: 'browse',
              filetypes: filetypes,
              pointer: {
                main: saveas,
                index: 'source'
              },
              help: $source_help,
              'function': function(){
                var source = $(this).val();
                var $source_field = $(this);
                $style.remove();
                $livestreamhint.html('');
                let type = showHint(source,this.selectionStart);
                if (source == '') { return; }
                if (type === null) {
                  $inputoptions.html(
                    $('<h3>').text('Unrecognized input').addClass('red')
                  ).append(
                    $('<span>').text('Please edit the stream source.').addClass('red')
                  );

                  $source_info.html("");
                  return;
                }
                var input = mist.data.capabilities.inputs[type];

                var t = $source_info.find("div");
                if (t.length) {
                  t.removeClass("active");
                  t.filter("[data-source=\""+source+"\"]").addClass("active");
                }

                let $streamkeys = $(this).closest(".input_container").find(".itemgroup [name=\"streamkeys\"]").closest(".itemgroup");
                if (source.slice(0,7) == "push://") {
                  $streamkeys.show();
                }
                else {
                  $streamkeys.hide();
                }

                function update_input_options(source) {
                  var input_options = $.extend({},input);
                  if (input.dynamic_capa) {
                    input_options.desc = "Loading dynamic capabilities..";

                    //the capabilities for this input can change depending on the source string
                    if (!("dynamic_capa_results" in input) || (!(source in input.dynamic_capa_results))) {
                      dynamic_capa_source = source;

                      //we don't know the capabilities for this source string yet
                      if (dynamic_capa_rate_limit) {
                        //some other call is already in the waiting list, don't make a new one
                        return;
                      }
                      dynamic_capa_rate_limit = setTimeout(function(){
                        if (!("dynamic_capa_results" in input)) {
                          input.dynamic_capa_results = {};
                        }
                        input.dynamic_capa_results[dynamic_capa_source] = null; //reserve the space so we only make the call once
                        mist.send(function(d){
                          dynamic_capa_rate_limit = false;
                          input.dynamic_capa_results[dynamic_capa_source] = d.capabilities;
                          update_input_options(dynamic_capa_source);
                        },{capabilities:dynamic_capa_source});
                      },1e3); //one second rate limit


                    }
                    else {
                      //we know them, apply
                      delete input_options.desc;
                      if (input.dynamic_capa_results[source]) {
                        input_options = input.dynamic_capa_results[source];
                      }
                    }
                  }
                  $inputoptions.html(
                    $('<h3>').text("source_name" in input ? "Input options for "+(input.source_name).toLowerCase() : input.name+' Input options')
                  );                
                  var build = mist.convertBuildOptions(input_options,saveas);
                  if (('always_match' in input) && (mist.inputMatch(input.always_match,source))) {
                    build.push({
                      label: 'Always on',
                      type: 'checkbox',
                      help: 'Keep this input available at all times, even when there are no active viewers.',
                      pointer: {
                        main: saveas,
                        index: 'always_on'
                      },
                      value: (other == "" && ((type == "TSSRT") || (type == "TSRIST") || (type == "RTSP") || (type == "TS")) ? true : false) //for new streams, if the input is TSSRT TSRIST RTSP or TS(= tsudp), put always_on true by default
                    });
                  }
                  $inputoptions.append(UI.buildUI(build));
                  $source_info.html("");
                  if ((input.enum_static_prefix) && (source.slice(0,input.enum_static_prefix.length) == input.enum_static_prefix)) {
                    //this input can enumerate supported devices, and the source string matches the specified static prefix

                    function display_sources() {
                      //add to source info container
                      $source_info.html(
                        $("<p>").text("Possible sources for "+input.name+": (click to set)")
                      );
                      var sources = input.enumerated_sources[input.enum_static_prefix];
                      for (var i in sources) {
                        var v = sources[i].split(" ")[0];
                        $source_info.append(
                          $("<div>").attr("data-source",v).text(sources[i]).click(function(){
                            var t = $(this).attr("data-source");
                            $source_field.val(t).trigger("change");                          
                          }).addClass(v == source ? "active":"")
                        );
                      }

                    }

                    function apply_enumerated_sources() {
                      if ((!("enumerated_sources" in input)) || (!(input.enum_static_prefix in input.enumerated_sources))) {
                        if (!("enumerated_sources" in input)) { input.enumerated_sources = {}; }
                        input.enumerated_sources[input.enum_static_prefix] = []; //"reserve" the space so we won't make duplicate requests
                        setTimeout(function(){
                          //remove the reserved space so that we can collect new values
                          delete input.enumerated_sources[input.enum_static_prefix];
                        },10e3);
                        mist.send(function(d){
                          //save
                          if (!("enumerated_sources" in input)) { input.enumerated_sources = {}; }
                          input.enumerated_sources[input.enum_static_prefix] = d.enumerate_sources;

                          display_sources();

                        },{enumerate_sources:source});
                      }
                      else {
                        display_sources();
                      }
                    }
                    apply_enumerated_sources();
                  }
                }

                if (input.name == 'Folder') {
                  if (other.indexOf("+") < 0) { $main.append($style); }
                }

                let streamname = $main.find('[name=name]').val();
                UI.updateLiveStreamHint(
                  (other.indexOf("+") >= 0) && (streamname == other.split("+")[0]) ? other : streamname,
                  $main.find("[name=streamkey_only]").getval() ? $main.find('[name=source]')?.val()?.replace(/push:\/\/[^:@\/]*/,"push://invalid,host") : $main.find('[name=source]')?.val()?.replace("push://invalid,host","push://"),
                  $livestreamhint,
                  input,
                  $main.find("[name=streamkeys]").getval()
                );

                update_input_options(source);
              }
            },$source_datalist,$source_info,{
              label: "Persistent tags",
              type: "inputlist",
              help: "You can configure persistent tags that are applied to this stream when it starts. You can use tags to associate configuration or behavior with multiple streams.<br><br>Note that tags are not applied retroactively - if your stream is already active when you edit this field, changes will not take effect until the stream restarts.",
              pointer: {
                main: saveas,
                index: "tags"
              },
              input: {
                type: "str",
                prefix: "#"
              },
              validate: [function(val,me){
                val = val.join("");
                if (val.indexOf(" ") > -1) {
                  return {
                    msg: 'Spaces are not allowed in tag names.',
                    classes: ['red']
                  };
                }
                if (val.indexOf("#") > -1) {
                  return {
                    msg: 'You don\'t need to prefix these values with a #.',
                    classes: ['orange'],
                    "break": false
                  };
                }
              }]
            },{
              type: "group",
              label: "Permissions for stream input",
              options: [{
                label: "Require stream key",
                type: "checkbox",
                help: "Check this box to block pushes using the stream name instead of a stream key.",
                pointer: { main: saveas, index: "streamkey_only" },
                "function": function(){
                  $main.find("[name=source]").trigger("change");
                }
              },{
                type: "help",
                help: "Stream keys are a method to bypass all security and allow an incoming push for the given stream. If a token that matches a stream is used it will be accepted."
              },{
                label: "Stream keys",
                type: "inputlist",
                pointer: { main: saveas, index: "streamkeys" },
                help: "You may enter one or more stream keys. When none are entered, you can only push into this stream using the stream name.",
                "function": function(){
                  let streamname = $main.find('[name=name]').val();
                  UI.updateLiveStreamHint(
                    (other.indexOf("+") >= 0) && (streamname == other.split("+")[0]) ? other : streamname,
                    $main.find("[name=streamkey_only]").getval() ? $main.find('[name=source]')?.val()?.replace(/push:\/\/[^:@\/]*/,"push://invalid,host") : $main.find('[name=source]')?.val()?.replace("push://invalid,host","push://"),
                    $livestreamhint,
                    false,
                    $(this).getval()
                  );
                },
                input: {
                  type: "str",
                  clipboard: true,
                  maxlength: 256,
                  validate: [function(val,me){
                    if (mist.data.streamkeys && (val in mist.data.streamkeys) && (mist.data.streamkeys[val] != other)) {
                      //duplicates in the current field do not need to be tested - they're all for the same stream so it won't be an issue
                      return {
                        msg: "The key '"+val+"' is already in use (for the stream '"+mist.data.streamkeys[val]+"'). Duplicates are not allowed.",
                        classes: ["red"]
                      };
                    }
                    if (val.length && !val.match(/^[0-9a-z]+$/i)) {
                      return {
                        msg: "The key '"+val+"' contains special characters. We recommend not using these as some video streaming protocols do not accept them.",
                        classes: ["orange"],
                        "break": false
                      }
                    }
                  }],
                  unit: 
                    //NB: because unit is a reference to a DOMelement here, when a new input is created to be added to the list, this button will /move/ to the last input field, it will not be duplicated. In this case this works fine, because the Generate button only makes sense for the last, empty, input anyway.
                  $("<button>").text("Generate").click(function(){
                    let $field = $(this).closest(".field_container").find(".field");

                    function getRandomVals(n) {
                      function getRandomVal() {
                        const chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
                        return chars[Math.floor(Math.random()*chars.length)];
                      }
                      let str = "";
                      while (str.length < n) {
                        str += getRandomVal();
                      }
                      return str;
                    }

                    function apply() {
                      $field.setval(getRandomVals(32));
                      if ($field.data("validate")($field)) { 
                        //the listitem is invalid
                        apply();
                      }
                    }
                    apply();

                    $field.trigger("keyup");

                  }),
                }
              },$("<div>").addClass("livestreamhint").html(
                function(){
                  let basestream = other.split("+")[0];

                  let $c = $("<div>").addClass("description");
                  let $text = $("<span>");
                  function setText(){
                    let bystream = {};
                    //gather stream keys for base stream and children
                    for (const key in mist.data.streamkeys) {
                      const s = mist.data.streamkeys[key];
                      if (s.split("+")[0] == basestream) {
                        if (!(s in bystream)) {
                          bystream[s] = [];
                        }
                        bystream[s].push(key);
                      }
                    }
                    delete bystream[basestream];
                    $text.text(Object.keys(bystream).length ? "Additionally, a total of "+Object.values(bystream).map(function(v){return v.length}).reduce(function(sum,v){return sum+v})+" stream keys for "+Object.keys(bystream).length+" wildcards of \""+basestream+"\" have been configured." : "You can also add stream keys for wildcard streams here:");
                  }
                  setText();
                  $c.append(
                    $text
                  ).append(
                    $("<button>").css("float","right").attr("data-icon","key").text("Manage stream keys").click(function(){
                      let popup = UI.popup(UI.modules.streamkeys(basestream,function(){
                        //what to do after the add button is used
                        popup.show(UI.modules.streamkeys(basestream,arguments.callee));
                      }));
                      popup.element[0].addEventListener("close",function(){
                        //onclose update streamkeys field
                        mist.send(function(){
                          setText();
                          $main.find("[name=\"streamkeys\"]").setval(UI.findStreamKeys(basestream));
                        },{streamkeys: true})
                      });
                    })
                  );
                  return $c;
                }()
              )]
            },$livestreamhint,$('<br>'),{
              type: 'custom',
              custom: $inputoptions
            },$processes,{
              label: 'Stop sessions',
              type: 'checkbox',
              help: 'When saving these stream settings, kill this stream\'s current connections.',
              pointer: {
                main: saveas,
                index: 'stop_sessions'
              }
            },{
              type: 'buttons',
              buttons: [
                {
                  type: 'cancel',
                  label: 'Cancel',
                  'function': function(){
                    UI.navto('Streams');
                  }
                },{
                  type: 'save',
                  label: 'Save',
                  'function': function(){
                    save('Streams');
                  }
                },{
                  type: 'save',
                  label: 'Save and Preview',
                  'function': function(){
                    save('Preview');
                  },
                  classes: ['saveandpreview']
                }
              ]
            }
          ]);
          let $streamkeys = $form.find(".itemgroup [name=\"streamkeys\"]").closest(".itemgroup");
          if (saveas.source && saveas.source.slice(0,7) == "push://") {
            $streamkeys.show();
          }
          else {
            $streamkeys.hide();
          }

          $main.append($form);

          
          //if the form contents have been changed, set a flag on the header: only ask for confirm to navigate away if there have been changes
          $form.change(function(){
            var $h = $main.find(".header");
            if ($h.length) { $h.attr("data-changed","yes"); }
          });

          $main.find('[name=name]').keyup(function(){
            let streamname = $(this).val();
            UI.updateLiveStreamHint(
              (other.indexOf("+") >= 0) && (streamname == other.split("+")[0]) ? other : streamname,
              $main.find("[name=streamkey_only]").getval() ? $main.find('[name=source]')?.val()?.replace(/push:\/\/[^:@\/]*/,"push://invalid,host") : $main.find('[name=source]')?.val()?.replace("push://invalid,host","push://"),
              $livestreamhint,
              false,
              $main.find("[name=streamkeys]").getval()
            );
          }).trigger("keyup");
          $main.find('[name="source"]').attr("list","source_datalist");


        },{capabilities: true, streamkeys: true});
        break;
      }
      case 'Status': {
        if (other == '') { UI.navto('Streams'); return; }


        var $dashboard = $("<div>").addClass("dashboard");

        $main.html(
          UI.modules.stream.header(other,tab)
        ).append(
          $dashboard
        );

        var $findMist = UI.modules.stream.findMist(function(url,data){
          //MistServer was found! build dashboard

          $dashboard.append(UI.modules.stream.status(other,{status:false,stats:false}));
          $dashboard.append(UI.modules.stream.metadata(other));
          $dashboard.append(
            $("<section>").addClass("logcont").append(
              UI.modules.stream.logs(other)
            ).append(
              UI.modules.stream.accesslogs(other)
            )
          );
          $dashboard.append(UI.modules.stream.processes(other)); 
          $dashboard.append(UI.modules.stream.triggers(other,"Status"));
          $dashboard.append(UI.modules.stream.pushes(other));

        });
        $dashboard.append($findMist);
        $dashboard.append(UI.modules.stream.actions("Status",other)); //the actions can be used even if the http host is unknown
        $dashboard.append(UI.modules.stream.clients(other));

        break;
      }
      case 'Preview': {
        
        if (other == '') { UI.navto('Streams'); return; }
       
        var $dashboard = $('<div>').addClass("dashboard");
        var $status = $("<div>").text("Loading..");
        $main.html(
          UI.modules.stream.header(other,tab)
        ).append($status).append($dashboard);

        var $findMist = UI.modules.stream.findMist(function(url){
          //MistServer was found and data contains player.js, jQuery should already have added it to the page
          if (typeof mistplayers == "undefined") {
            throw "Player.js was not applied properly.";
          }

          $status.replaceWith(UI.modules.stream.status(other,{tags:false,thumbnail:false,status:false,stats:false}));

          window.mv = {};
          $preview = UI.modules.stream.preview(other,window.mv);
          $dashboard.append($preview);
          $dashboard.append(
            UI.modules.stream.playercontrols(window.mv,$preview)
          ).append(
            UI.modules.stream.logs(other)
          ).append(
            UI.modules.stream.metadata(other)
          );

        });

        $dashboard.append($findMist);


        break;
      }
      case 'Embed': {
        if (other == '') { UI.navto('Streams'); return; }
        
        $main.html(
          UI.modules.stream.header(other,tab)
        );
        
        var $embedlinks = $('<span>');
        $main.append(
          UI.modules.stream.findMist(function(url){
            $main.append(UI.modules.stream.embedurls(other,url,this.getUrls()));
          },false,true) //force full url search
        );

        break;
      }
      case 'Stream keys': {
        $main.html(
          $("<button>").attr("data-icon","Streams").css({width:"fit-content"}).text("Return to stream overview").click(function(){
            UI.navto("Streams");
          })
        ).append(UI.modules.streamkeys());

        break;
      }
      case 'Push':
        $main.html(
          $("<h2>").text("Pushes and recordings")
        ).append(
          UI.buildUI([{
            type: "help",
            help: "You can record streams to a file, or push streams to other servers, allowing them to broadcast your stream as well.<br>You can right click a push or stream name for actions."
          }])
        ).append(UI.modules.pushes({
          push_settings:true,
          collapsible: true,
          filter: true
        }));

        break;
      case 'Start Push':
        
        if (!('capabilities' in mist.data) || !('variable_list' in mist.data) || !('external_writer_list' in mist.data)) {
          $main.append('Loading MistServer capabilities..');
          mist.send(function(){
            UI.showTab('Start Push',other,prev);
          },{capabilities:true,variable_list:true,external_writer_list:true});
          return;
        }
        
        var allthestreams;
        function buildTheThings(edit,editid) {
          var o = other.split("_");
          other = o[0];
          if (o.length >= 2) { editid = o.slice(1).join("_"); }
          
          if ((typeof editid != "undefined") && (typeof edit == "undefined")) {
            mist.send(function(d){
              if (editid in d.auto_push) {
                buildTheThings(d.auto_push[editid],editid);
              }
              else {
                buildTheThings(); //not found: Create new push
              }
            },{push_auto_list: 1});
            return;
          }
          
          //retrieve a list of valid targets
          //var target_match = [];
          var file_match = [];
          var prot_match = [];
          var connector2target_match = {};
          var writer_protocols = [];
          for (var i in mist.data.capabilities.connectors) {
            var conn = mist.data.capabilities.connectors[i];
            if ('push_urls' in conn) {
              //target_match = target_match.concat(conn.push_urls);
              connector2target_match[i] = conn.push_urls;
              for (var j in conn.push_urls) {
                if (conn.push_urls[j][0] == "/") {
                  file_match.push(conn.push_urls[j]);
                }
                else {
                  prot_match.push(conn.push_urls[j]);
                }
              }
            }
          }
          if (mist.data.external_writer_list) {
            for (var i in mist.data.external_writer_list) {
              var writer = mist.data.external_writer_list[i];
              if (writer.length >= 3) {
                for (var j in writer[2]) {
                  writer_protocols.push(writer[2][j]+"://");
                }
              }
            }
          }
          if (mist.data.capabilities.internal_writers && mist.data.capabilities.internal_writers.length) {
            for (let value of mist.data.capabilities.internal_writers) {
              writer_protocols.push(value+"://");
            }
          }
          file_match.sort();
          prot_match.sort();
          
          var saveas = {params:{}};
          if (other == 'auto') {
            $main.find('h2').text('Add automatic push');
            saveas.enabled = true;
          }
          
          
          var params = []; //will contain all target url params as an array
          if (other == "auto") {
            saveas = Object.assign(saveas,{
              start_rule: [null,null,null],
              end_rule: [null,null,null]
            });
            if (typeof edit != "undefined") {
              saveas = Object.assign(saveas,edit);

              if (saveas.stream.indexOf("💤deactivated💤_") == 0) {
                saveas.enabled = false;
                saveas.stream = saveas.stream.slice(16);
              }

              var parts = saveas.target.split("?");
              if (parts.length > 1) {
                params = parts.pop(); //contains the part that comes after the ?, eg recstartunix=123&scheduletime=456
                saveas.target = parts.join("?"); //the rest of the url string can go back into the target
                params = params.split("&");
                for (var i in params) {
                  var param = params[i].split("=");
                  saveas.params[param.shift()] = param.join("=");
                }
              }
            }
          }
          var $additional_params = $("<div>").css("margin","1em 0");
          var $autopush = $("<div>");
          var push_parameters, full_list_of_push_parameters;
          if (other == "auto") {
            $autopush.css("margin","1em 0").html(UI.buildUI([
              $("<h3>").text("Automatic push options"),{
              label: "This push should be active",
              help: "When 'based on server time' is selected, a start and/or end timestamp can be configured. When it's 'based on a variable', the push will be activated while the specified variable matches the specified value.",
              type: "select",
              select: [["time","Based on server time"],["variable","Based on a variable"]],
              value: (saveas.start_rule[0] || saveas.end_rule[0] ? "variable" : "time"),
              classes: ["activewhen"],
              "function": function(){
                var $varbased = $autopush.find(".varbased").closest(".UIelement");
                var $timebased = $autopush.find(".timebased").closest(".UIelement");

                if ($(this).getval() == "time") {
                  $varbased.hide();
                  $timebased.css("display","");
                }
                else {
                  $timebased.hide();
                  $varbased.css("display","");
                  $autopush.find(".field.variableOperator").trigger("change");
                }
              }
            },
            $("<br>"),
            $("<span>").addClass("UIelement").append(
              $("<h3>").text("Start the push").addClass("varbased")
            ),{
              classes: ["varbased"],
              label: "Use this variable",
              type: "str",
              help: "This variable should be used to determine if this push should be started.",
              prefix: "$",
              datalist: Object.keys(mist.data.variable_list || []),
              pointer: { main: saveas.start_rule, index: 0 }
            },{
              classes: ["varbased","variableOperator"],
              label: "Comparison operator",
              type: "select",
              select: [
                [0,"is true"],
                [1,"is false"],
                [2,"=="],
                [3,"!="],
                [10,">  (numerical)"],
                [11,">= (numerical)"],
                [12,"<  (numerical)"],
                [13,"<= (numerical)"],
                [20,">  (lexical)"],
                [21,">= (lexical)"],
                [22,"<  (lexical)"],
                [23,"<= (lexical)"]
              ],
              value: 2,
              css: {display:"none"},
              help: "How would you like to compare this variable?",
              pointer: { main: saveas.start_rule, index: 1 },
              "function": function(){
                var $varvalue = $autopush.find(".field.startVariableValue").closest(".UIelement");
                if (Number($(this).getval()) < 2) {
                  $varvalue.hide();
                }
                else {
                  $varvalue.css("display","");
                }
              }
            },{
              classes: ["varbased","startVariableValue"],
              label: "Variable value",
              type: "str",
              help: "The variable will be compared with this value to determine if this push should be started.<br>You can also enter another variable here!",
              datalist: Object.values(mist.data.variable_list || []).map(function(a){return typeof a == "string" ? a : a[3]}).concat(Object.keys(mist.data.variable_list || []).map(function(a){ return "$"+a; })),
              pointer: { main: saveas.start_rule, index: 2 }
            },
              $("<span>").addClass("UIelement").append(
              $("<h3>").text("Stop the push").addClass("varbased")
            ),{
              classes: ["varbased"],
              label: "Use this variable",
              type: "str",
              help: "This variable should be used to determine if this push should be stopped.<br>You can leave this field blank if you do not want to have a stop condition. (You can always stop the push manually)",
              prefix: "$",
              datalist: Object.keys(mist.data.variable_list || []),
              pointer: { main: saveas.end_rule, index: 0 }
            },{
              classes: ["varbased"],
              label: "Comparison operator",
              type: "select",
              select: [
                [0,"is true"],
                [1,"is false"],
                [2,"=="],
                [3,"!="],
                [10,">  (numerical)"],
                [11,">= (numerical)"],
                [12,"<  (numerical)"],
                [13,"<= (numerical)"],
                [20,">  (lexical)"],
                [21,">= (lexical)"],
                [22,"<  (lexical)"],
                [23,"<= (lexical)"]
              ],
              value: 2,
              help: "How would you like to compare this variable?",
              pointer: { main: saveas.end_rule, index: 1 },
              "function": function(){
                var $varvalue = $autopush.find(".field.endVariableValue").closest(".UIelement");
                if (Number($(this).getval()) < 2) {
                  $varvalue.hide();
                }
                else {
                  $varvalue.css("display","");
                }
              }
            },{
              classes: ["varbased","endVariableValue"],
              label: "Variable value",
              type: "str",
              help: "The variable will be compared with this value to determine if this push should be stopped.<br>You can also enter another variable here!",
              datalist: Object.values(mist.data.variable_list || []).map(function(a){return typeof a == "string" ? a : a[3]}).concat(Object.keys(mist.data.variable_list || []).map(function(a){ return "$"+a; })),
              pointer: { main: saveas.end_rule, index: 2 }
            },{
              classes: ["timebased"],
              type: "unix",
              label: "Start time",
              min: 0,
              help: "The time where the push will become active. The default is to start immediately.",
              pointer: {
                main: saveas,
                index: "scheduletime"
              }
            },{
              classes: ["timebased"],
              type: "unix",
              label: "End time",
              min: 0,
              help: "The time where the push will stop. Defaults to never stop automatically.<br>Only makes sense for live streams.",
              pointer: {
                main: saveas,
                index: "completetime"
              }
            },{
              label: "Inhibitor",
              type: "inputlist",
              help: "Optional list of tags and/or stream names: if any of them identify the stream, this push will not start.",
              input: {
                type: "str",
                datalist: allthestreams,
                validate: [function(val,me){
                  if (val == "") return false;
                  if (val[0] == "#") {
                    //it's a tag, we're good
                    return false;
                  }
                  var shouldbestream = val.split('+');
                  shouldbestream = shouldbestream[0];
                  if (shouldbestream in mist.data.streams) {
                    return false;
                  }
                  return {
                    msg: "'"+shouldbestream+"' is not a stream name.",
                    classes: ['orange'],
                    "break": false
                  };
                }]
              },
              pointer: { main: saveas, index: "inhibit" }
            }],saveas));
            $autopush.find(".activewhen").trigger("change");
          }
          var build = [
            {
              label: 'Stream',
              type: 'str',
              help: 'This may either be a full stream name, a partial wildcard stream name, a full wildcard stream name, or a tag preceded by a # (e.g. #tag1).<br>For example, given the stream <i>a</i> you can use:\
              <ul>\
              <li><i>a</i>: the stream configured as <i>a</i></li>\
              <li><i>a+</i>: all streams configured as <i>a</i> with a wildcard behind it, but not <i>a</i> itself</li>\
              <li><i>a+b</i>: only the version of stream <i>a</i> that has wildcard <i>b</i></li>\
              </ul>',
              pointer: {
                main: saveas,
                index: 'stream'
              },
              validate: ['required',function(val,me){
                if (val[0] == "#") {
                  //it's a tag, we're good
                  return false;
                }
                var shouldbestream = val.split('+');
                shouldbestream = shouldbestream[0];
                if (shouldbestream in mist.data.streams) {
                  return false;
                }
                return {
                  msg: "'"+shouldbestream+"' is not a stream name.",
                  classes: ['orange'],
                  "break": false
                };
              }],
              datalist: allthestreams,
              value: prev[1] != "" ? prev[1] : "" //if we came here from a page where other exists (like the Status tab) prefill it, it's probably a stream
            },{
              label: 'Target',
              type: 'str',
              help: 'Where the stream will be pushed to.<br>\
              Valid push formats:\
              <ul>\
              <li>'+prot_match.join('</li><li>')+'</li>\
              </ul>\
              Valid file formats:\
              <ul>\
              <li>'+file_match.join('</li><li>')+'</li>\
              </ul>\
              '+(writer_protocols.length ? 'Additionally, the following protocols (from generic writers) may be used in combination with any of the above file formats:<ul><li>'+writer_protocols.join("</li><li>")+'</li></ul>' : "")+'\
              Valid text replacements:\
              <ul>\
              <li>$stream - inserts the stream name used to push to MistServer</li>\
              <li>$day - inserts the current day number</li><li>$month - inserts the current month number</li>\
              <li>$year - inserts the current year number</li><li>$hour - inserts the hour timestamp when stream was received</li>\
              <li>$minute - inserts the minute timestamp the stream was received</li>\
              <li>$seconds - inserts the seconds timestamp when the stream was received</li>\
              <li>$datetime - inserts $year.$month.$day.$hour.$minute.$seconds timestamp when the stream was received</li>\
              </ul>',
              pointer: {
                main: saveas,
                index: 'target'
              },
              validate: ['required',function(val,me){
                for (var i in prot_match) {
                  if (mist.inputMatch(prot_match[i],val)) {
                    return false;
                  }
                }
                for (var i in file_match) {
                  if (mist.inputMatch(file_match[i],val)) {
                    return false;
                  }
                  for (var j in writer_protocols) {
                    if (mist.inputMatch(writer_protocols[j]+file_match[i].slice(1),val)) {
                      return false;
                    }
                  }
                }

                return {
                  msg: 'Does not match a valid target.<br>Valid push formats:\
                  <ul>\
                  <li>'+prot_match.join('</li><li>')+'</li>\
                  </ul>\
                  Valid file formats:\
                  <ul>\
                  <li>'+file_match.join('</li><li>')+'</li>\
                  </ul>\
                  '+(writer_protocols.length ? 'Additionally, the following protocols may be used in combination with any of the above file formats:<ul><li>'+writer_protocols.join("</li><li>")+'</li></ul>' : ""),
                  classes: ['red']
                }
              }],
              "function": function(){
                //find what kind of target this is
                var match = false;
                var val = $(this).getval();
                for (connector in connector2target_match) {
                  for (var i in connector2target_match[connector]) {
                    if (mist.inputMatch(connector2target_match[connector][i],val)) {
                      match = connector;
                      break;
                    }
                    if (connector2target_match[connector][i][0] == "/") {
                      for (var j in writer_protocols) {
                        if (mist.inputMatch(writer_protocols[j]+connector2target_match[connector][i].slice(1),val)) {
                          match = connector;
                          break;
                        }
                      }
                    }
                  }
                }
                if ($(this).data("last_match") == match) { return; }
                $(this).data("last_match",match);
                if (!match) {
                  $additional_params.html(
                    $("<h4>").addClass("red").text("Unrecognized target.")
                  ).append(
                    $("<span>").text("Please edit the push target.")
                  );
                  return;
                }
                if (!("friendly" in mist.data.capabilities.connectors[match])) { mist.data.capabilities.connectors[match].friendly = mist.data.capabilities.connectors[match].name; }
                $additional_params.html($("<h3>").text(mist.data.capabilities.connectors[match].friendly.replace("over HTTP","")));
                push_parameters = $.extend({},mist.data.capabilities.connectors[match].push_parameters);
                full_list_of_push_parameters = {};
                function processPushParam(param,key) {
                  //filter out protocol only or file only options. This does not need to be dynamic as when the target changes, the whole $additional_params container is overwritten anyway
                  if (param.prot_only && String().match && (val.match(/.+\:\/\/.+/) === null)) { 
                    delete push_parameters[key];
                    return;
                  }
                  if (param.file_only && (val[0] != "/")) {
                    delete push_parameters[key];
                    return;
                  }
                  if (param.type == "group") {
                    for (var i in param.options) {
                      processPushParam(param.options[i],i);
                    }
                  }
                  else {
                    full_list_of_push_parameters[key] = param;
                  }
                }
                for (var i in mist.data.capabilities.connectors[match].push_parameters) {
                  processPushParam(mist.data.capabilities.connectors[match].push_parameters[i],i);
                }

                var capa = {
                  desc: mist.data.capabilities.connectors[match].desc.replace("over HTTP",""),
                  optional: push_parameters,
                  sort: "sort"
                };
                var capaform = mist.convertBuildOptions(capa,saveas.params);
                if (capaform[1].is("h4")) capaform.splice(1,1);

                //find left over url params that are not covered by this connector's capabilities
                var custom_params = [];
                for (var i in params) {
                  var p = params[i].split("=");
                  var name = p[0];
                  if (!(name in full_list_of_push_parameters)) {
                    custom_params.push(name+(p.length > 1 ? "="+p.slice(1).join("=") : ""));
                  }
                }

                capaform.push($("<br>"));
                capaform.push({
                  type: "inputlist",
                  label: "Custom url parameters",
                  value: custom_params,
                  classes: ["custom_url_parameters"],
                  input: {
                    type: "str",
                    placeholder: "name=value",
                    prefix: ""
                  },
                  help: "Any custom url parameters not covered by the parameters configurable above.",
                  pointer: {
                    main: saveas,
                    index: "custom_url_params"
                  }
                });

                $additional_params.append(UI.buildUI(capaform)); 
              }
            },other == "auto" ? {
              label: "Enabled",
              type: "checkbox",
              help: "When an automatic push is disabled, it will not start new pushes.",
              pointer: { 
                main: saveas,
                index: "enabled"
              }
            } : $(""),{
              label: "Notes",
              type: "textarea",
              rows: 4,
              pointer: {
                main: saveas,
                index: "x-LSP-notes"
              }
            },
            $additional_params,
            $autopush
          ];
          
          build.push({
            type: 'buttons',
            buttons: [{
              type: 'cancel',
              label: 'Cancel',
              'function': function(){
                if (prev[0] && prev[0] != tab) {
                  UI.navto(prev[0],prev[1]);
                }
                else {
                  UI.navto("Push");
                }
              }
            },{
              type: 'save',
              label: 'Save',
              preSave: function(){
                //is executed before the variables are saved

                if (other == "auto") {
                  var scheduletime = $("[name=\"scheduletime\"]").getval();
                  var startunix = $("[name=\"startunix\"]").getval();
                  if (typeof editid == "undefined") {
                    //we're adding a new auto push
                    if (scheduletime && (!startunix)) {
                      //schedultime is set, but startunix is not
                      $("[name=\"startunix\"]").setval(scheduletime);
                    }
                  }
                  else if (saveas.params.startunix && saveas.scheduletime) {
                    if ((scheduletime != saveas.scheduletime) && (startunix == saveas.params.startunix)) {
                      //scheduletime has changed, but startunix has not
                      $("[name=\"startunix\"]").setval(scheduletime);
                    }
                  }
                }

                
                //clean the object of old settings that may not be part of the current form 
                delete saveas.startVariableName;
                delete saveas.startVariableOperator;
                delete saveas.startVariableValue;
                delete saveas.endVariableName;
                delete saveas.endVariableOperator;
                delete saveas.endVariableValue;
                delete saveas.completetime;
                delete saveas.scheduletime;
                if (saveas.start_rule) saveas.start_rule.map(function(v){return null;});
                if (saveas.end_rule) saveas.end_rule.map(function(v){return null;});

              },
              'function': function(){
                if (other == "auto") {
                  if (!saveas.enabled) {
                    saveas.stream = "💤deactivated💤_"+saveas.stream;
                  }
                  delete saveas.enabled;
                }

                var params = saveas.params;
                for (var i in params) {
                  if ((params[i] === null) || (params[i] === false)) {
                    //remove any params that are set to null
                    delete params[i];
                  }
                  else if (!(i in full_list_of_push_parameters)) {
                    //remove any params that are not supported by this protocol (they will have been duplicated to saveas.custom_url_parameters if the user wanted to keep them)
                    delete params[i];
                  }
                }
                if (saveas.start_rule && (saveas.start_rule[0] === null)) {
                  delete saveas.start_rule;
                }
                if (saveas.end_rule && (saveas.end_rule[0] === null)) {
                  delete saveas.end_rule;
                }

                if (("start_rule" in saveas) || ("end_rule" in saveas)) {
                  saveas.scheduletime = 0;
                  saveas.completetime = 0;
                }
                if (Object.keys(params).length || (saveas.custom_url_params && saveas.custom_url_params.length)) {
                  var str = [];
                  for (var i in params) { //the MistServer settings as entered in "Optional parameters"
                    str.push(i+"="+params[i]);
                  }
                  for (var i in saveas.custom_url_params) { //the MistServer settings custom url parameters
                    str.push(saveas.custom_url_params[i]);
                  }
                  saveas.target += "?"+str.join("&");
                }
                delete saveas.params; //these are now part of the target url and we don't need them separately
                delete saveas.custom_url_params;
                
                var obj = {};
                if (other == "auto") {
                  if (editid) {
                    obj.push_auto_add = {};
                    obj.push_auto_add[editid] = saveas;
                  }
                  else {
                    obj.push_auto_add = saveas;
                  }
                }
                else {
                  obj.push_start = saveas;
                }
                
                mist.send(function(){
                  if (prev[0] && prev[0] != tab) {
                    UI.navto(prev[0],prev[1]);
                  }
                  else {
                    UI.navto("Push");
                  }
                },obj);
              }
            }]
          });
          
          $main.append(UI.buildUI(build));
        }
        
        //gather wildcard streams
        mist.send(function(d){
          allthestreams = d.active_streams;
          if (!allthestreams) { allthestreams = []; }

          var wildcards = [];
          for (var i in allthestreams) {
            if (allthestreams[i].indexOf('+') != -1) {
              wildcards.push(allthestreams[i].replace(/\+.*/,'')+'+');
            }
          }
          allthestreams = allthestreams.concat(wildcards);

          var browserequests = 0;
          var browsecomplete = 0;
          for (var i in mist.data.streams) {
            allthestreams.push(i);
            if (mist.inputMatch(UI.findInput('Folder').source_match,mist.data.streams[i].source)) {
              //browse all the things
              allthestreams.push(i+'+');
              mist.send(function(d,opts){
                var s = opts.stream;

                for (var i in d.browse.files) {
                  for (var j in mist.data.capabilities.inputs) {
                    if ((j.indexOf('Buffer') >= 0) || (j.indexOf('Folder') >= 0) || (j.indexOf('Buffer.exe') >= 0) || (j.indexOf('Folder.exe') >= 0)) { continue; }
                    if (mist.inputMatch(mist.data.capabilities.inputs[j].source_match,'/'+d.browse.files[i])) {
                      allthestreams.push(s+'+'+d.browse.files[i]);
                    }
                  }
                }

                browsecomplete++;

                if (browserequests == browsecomplete) {
                  //filter to only unique and sort
                  allthestreams = allthestreams.filter(function(e,i,arr){
                    return arr.lastIndexOf(e) === i;
                  }).sort();

                  buildTheThings();
                }
              },{browse:mist.data.streams[i].source},{stream: i});
              browserequests++;
            }
          }
          if (browserequests == browsecomplete) {
            //filter to only unique and sort
            allthestreams = allthestreams.filter(function(e,i,arr){
              return arr.lastIndexOf(e) === i;
            }).sort();

            buildTheThings();
          }
        },{
          active_streams:1
        });
        
        break;
      case 'Triggers':
        if (!('triggers' in mist.data.config) || (!mist.data.config.triggers)) {
          mist.data.config.triggers = {};
        }
        
        var $tbody = $('<tbody>');
        var $table = $('<table>').html(
          $('<thead>').addClass("sticky").html(
            $('<tr>').html(
              $('<th>').text('Trigger on').attr('data-sort-type','string').addClass('sorting-asc')
            ).append(
              $('<th>').text('Applies to').attr('data-sort-type','string')
            ).append(
              $('<th>').text('Handler').attr('data-sort-type','string')
            ).append(
              $('<th>')
            )
          )
        ).append($tbody);
        
        $main.append(
          UI.buildUI([{
            type: 'help',
            help: 'Triggers are a way to react to events that occur inside MistServer. These allow you to block specific users, redirect streams, keep tabs on what is being pushed where, etcetera. For full documentation, please refer to the developer documentation section on the MistServer website.'
          }])
        ).append(
          $('<button>').text('New trigger').click(function(){
            UI.navto('Edit Trigger');
          })
        ).append($table);
        $table.stupidtable();
        
        var triggers = mist.data.config.triggers
        for (var i in triggers) {
          for (var j in triggers[i]) {
            var t = triggerRewrite(triggers[i][j]);
            $tbody.append(
              $('<tr>').attr('data-index',i+','+j).append(
                $('<td>').text(i)
              ).append(
                $('<td>').text(('streams' in t ? t.streams.join(', ') : ''))
              ).append(
                $('<td>').text(t.handler)
              ).append(
                $('<td>').html(
                  $('<button>').text('Edit').click(function(){
                    UI.navto('Edit Trigger',$(this).closest('tr').attr('data-index'));
                  })
                ).append(
                  $('<button>').text('Delete').click(function(){
                    var index = $(this).closest('tr').attr('data-index').split(',');
                    if (confirm('Are you sure you want to delete this '+index[0]+' trigger?')) {
                      mist.data.config.triggers[index[0]].splice(index[1],1);
                      if (mist.data.config.triggers[index[0]].length == 0) {
                        delete mist.data.config.triggers[index[0]];
                      }
                      
                      mist.send(function(d){
                        UI.navto('Triggers');
                      },{config:mist.data.config});
                    }
                  })
                )
              )
            );
          }
        }
        
        break;
      case 'Edit Trigger':
        if (!('triggers' in mist.data.config) || (!mist.data.config.triggers)) {
          mist.data.config.triggers = {};
        }
        if (typeof mist.data.capabilities == 'undefined') {
          mist.send(function(d){
            UI.showTab(tab,other,prev);
          },{capabilities: true});
          $main.append('Loading..');
          return;
        }
        if (!other) {
          //new
          $main.html(
            $('<h2>').text('New Trigger')
          );
          var saveas = {};
        }
        else {
          //editing
          other = other.split(',');
          var source = triggerRewrite(mist.data.config.triggers[other[0]][other[1]]);
          var saveas = {
            triggeron: other[0],
            appliesto: source.streams,
            url: source.handler,
            async: source.sync,
            'default': source['default'],
            params: source.params
          };
        }
        
        var triggerSelect = [];
        for (var i in mist.data.capabilities.triggers) {
          var trigger = mist.data.capabilities.triggers[i];
          triggerSelect.push([i,i+": "+trigger.when]);
        }
        var $triggerdesc = $("<div>").addClass("desc");
        var $params_help = $("<div>");
        
        $main.append(UI.buildUI([{
          label: 'Trigger on',
          pointer: {
            main: saveas,
            index: 'triggeron'
          },
          help: 'For what event this trigger should activate.',
          type: 'select',
          select: triggerSelect,
          validate: ['required'],
          'function': function(){
            var v = $(this).getval();
            
            var info = mist.data.capabilities.triggers[v];
            $triggerdesc.html("");
            if (info) {
              function humanifyResponse(response) {
                switch (response) {
                  case "ignored":
                    return "No. The trigger will ignore the response of the handler.";
                    break;
                  case "always":
                    return "Yes. The trigger needs a response to proceed."
                    break;
                  case "when-blocking":
                    return "The trigger needs a response to proceed if it is configured to be blocking."
                    break;
                  default:
                    return response;
                  
                }
              }
              
              var b = [$("<h4>").text("Trigger properties"),{
                type: "help",
                help: "The trigger \"<i>"+v+"</i>\" has the following properties:"
              },{
                type: "span",
                label: "Triggers",
                value: info.when,
                help: "When this trigger is activated"
              }];
              if (info.payload != "") {
                b.push({
                  label: "Payload",
                  type: "textarea",
                  value: info.payload,
                  rows: info.payload.split("\n").length,
                  readonly: true,
                  clipboard: true,
                  help: "The information this trigger sends to the handler."
                });
              }
              b.push({
                type: "span",
                label: "Requires response",
                value: humanifyResponse(info.response),
                help: "Whether this trigger requires a response from the trigger handler"
              });
              b.push({
                type: "span",
                label: "Response action",
                value: info.response_action,
                help: "What this trigger will do with its handler's response"
              });
              $triggerdesc.append(UI.buildUI(b));
              
              if (info.stream_specific) {
                $('[name=appliesto]').closest('.UIelement').show();
              }
              else {
                $('[name=appliesto]').setval([]).closest('.UIelement').hide();
              }
              if (info.argument) {
                $('[name=params]').closest('.UIelement').show();
                $params_help.text(info.argument);
              }
              else {
                $('[name=params]').setval('').closest('.UIelement').hide();
              }
            }
            
          }
        },$triggerdesc,$("<h4>").text("Trigger settings"),{
          label: 'Applies to',
          pointer: {
            main: saveas,
            index: 'appliesto'
          },
          help: 'For triggers that can apply to specific streams, this value decides what streams they are triggered for.<br><br>Leave this list empty to apply the trigger to all streams.<br><br>These may either be a full stream name, a partial wildcard stream name, a full wildcard stream name, or a tag preceded by a # (e.g. #tag1).<br>For example, given the stream <i>a</i> you can use:\
              <ul>\
              <li><i>a</i>: the stream configured as <i>a</i></li>\
              <li><i>a+</i>: all streams configured as <i>a</i> with a wildcard behind it, but not <i>a</i> itself</li>\
              <li><i>a+b</i>: only the version of stream <i>a</i> that has wildcard <i>b</i></li>\
              </ul>',
          type: 'inputlist',
          datalist: Object.keys(mist.data.streams),
          value: (prev[1] != "" ? [prev[1]] : [])
        },$('<br>'),{
          label: 'Handler (URL or executable)',
          help: 'This can be either an HTTP URL or a full path to an executable.',
          pointer: {
            main: saveas,
            index: 'url'
          },
          validate: ['required'],
          type: 'str'
        },{
          label: 'Blocking',
          type: 'checkbox',
          help: 'If checked, pauses processing and uses the response of the handler. If the response does not start with 1, true, yes or cont, further processing is aborted. If unchecked, processing is never paused and the response is not checked.',
          pointer: {
            main: saveas,
            index: 'async'
          }
        },{
          label: 'Parameters',
          type: 'str',
          help: $("<div>").text('The extra data you want this trigger to use.').append($params_help),
          pointer: {
            main: saveas,
            index: 'params'
          }
        },{
          label: 'Default response',
          type: 'str',
          help: 'The default response in case the handler fails or is set to non-blocking.',
          placeholder: 'true',
          pointer: {
            main: saveas,
            index: 'default'
          }
        },{
          type: 'buttons',
          buttons: [
            {
              type: 'cancel',
              label: 'Cancel',
              'function': function(){
                if (prev[0] && prev[0] != tab) {
                  UI.navto(prev[0],prev[1]);
                }
                else {
                  UI.navto("Triggers");
                }
              }
            },{
              type: 'save',
              label: 'Save',
              'function': function(){
                if (other) {
                  //remove the old setting
                  mist.data.config.triggers[other[0]].splice(other[1],1);
                }
                
                var newtrigger = {
                  handler: saveas.url,
                  sync: (saveas.async ? true : false),
                  streams: (typeof saveas.appliesto == 'undefined' ? [] : saveas.appliesto),
                  params: saveas.params,
                  'default': saveas['default']
                };
                if (!("triggers" in mist.data.config) || (mist.data.config.triggers === null)) {
                  mist.data.config.triggers = {};
                }
                if (!(saveas.triggeron in mist.data.config.triggers)) {
                  mist.data.config.triggers[saveas.triggeron] = [];
                }
                mist.data.config.triggers[saveas.triggeron].push(newtrigger);
                
                mist.send(function(){
                  if (prev[0] && prev[0] != tab) {
                    UI.navto(prev[0],prev[1]);
                  }
                  else {
                    UI.navto("Triggers");
                  }
                },{config: mist.data.config});
              }
            }
          ]
        }]));
        $('[name=triggeron]').trigger('change');
        
        break;
      case 'Logs': {
        let $buttons = $("<div>").addClass("buttons");
        let $logs = UI.modules.logs();
        $buttons.append($logs.find("> button").first());
        $logs.find("> h3").remove();

        $main.append($buttons).append($logs);

        break;
      }
      case 'Statistics':
        var $UI = $('<span>').text('Loading..');
        $main.append($UI);
        
        var saveas = {
          graph: 'new'
        };
        var graphs = (mist.stored.get().graphs ? $.extend(true,{},mist.stored.get().graphs) : {});
        
        var thestreams = {};
        //let's not bother with folder streams, if they aren't active anyway
        for (var i in mist.data.streams) {
          thestreams[i] = true;
        }
        for (var i in mist.data.active_streams) {
          thestreams[mist.data.active_streams[i]] = true;
        }
        thestreams = Object.keys(thestreams).sort();
        var theprotocols = [];
        for (var i in mist.data.config.protocols) {
          theprotocols.push(mist.data.config.protocols[i].connector);
        }
        theprotocols.sort();
        
        mist.send(function(){
          //count the amount of CPU cores to calculate the load percentage in case we need it later
          UI.plot.datatype.templates.cpuload.cores = 0;
          for (var i in mist.data.capabilities.cpu) {
            UI.plot.datatype.templates.cpuload.cores += mist.data.capabilities.cpu[i].cores;
          }
          
          $UI.html(UI.buildUI([{
            type: 'help',
            help: 'Here you will find the MistServer stream statistics, you can select various categories yourself. All statistics are live: up to five minutes are saved.'
          },$('<h3>').text('Select the data to display'),{
            label: 'Add to',
            type: 'select',
            select: [['new','New graph']],
            pointer: {
              main: saveas,
              index: 'graph'
            },
            classes: ['graph_ids'],
            'function': function(){
              if (! $(this).val()) { return; }
              var $s = $UI.find('.graph_xaxis');
              var $id = $UI.find('.graph_id');
              if ($(this).val() == 'new') {
                $s.children('option').prop('disabled',false);
                $id.setval('Graph '+(Object.keys(graphs).length +1)).closest('label').show();
              }
              else {
                var xaxistype = graphs[$(this).val()].xaxis;
                $s.children('option').prop('disabled',true).filter('[value="'+xaxistype+'"]').prop('disabled',false);
                $id.closest('label').hide();
              }
              if ($s.children('option[value="'+$s.val()+'"]:disabled').length) {
                $s.val($s.children('option:enabled').first().val());
              }
              $s.trigger('change');
            }
          },{
            label: 'Graph id',
            type: 'str',
            pointer: {
              main: saveas,
              index: 'id'
            },
            classes: ['graph_id'],
            validate: [function(val,me){
              if (val in graphs) {
                return {
                  msg:'This graph id has already been used. Please enter something else.',
                  classes: ['red']
                }
              }
              return false;
            }]
          },{
            label: 'Axis type',
            type: 'select',
            select: [
              ['time','Time line']
            ],
            pointer: {
              main: saveas,
              index: 'xaxis'
            },
            value: 'time',
            classes: ['graph_xaxis'],
            'function': function(){
              $s = $UI.find('.graph_datatype');
              switch ($(this).getval()) {
                case 'coords':
                  $s.children('option').prop('disabled',true).filter('[value="coords"]').prop('disabled',false);
                  break;
                case 'time':
                  $s.children('option').prop('disabled',false).filter('[value="coords"]').prop('disabled',true);
                  break;
              }
              if ((!$s.val()) || ($s.children('option[value="'+$s.val()+'"]:disabled').length)) {
                $s.val($s.children('option:enabled').first().val());
                $s.trigger('change');
              }
            }
          },{
            label: 'Data type',
            type: 'select',
            select: [
              ['clients','Connections'],
              ['upbps','Bandwidth (up)'],
              ['downbps','Bandwidth (down)'],
              ['cpuload','CPU use'],
              ['memload','Memory load'],
              ['coords','Client location'],
              ['perc_lost','Lost packages'],
              ['perc_retrans','Re-transmitted packages']
            ],
            pointer: {
              main: saveas,
              index: 'datatype'
            },
            classes: ['graph_datatype'],
            'function': function(){
              $s = $UI.find('.graph_origin');
              switch ($(this).getval()) {
                case 'cpuload':
                case 'memload':
                  $s.find('input[type=radio]').not('[value="total"]').prop('disabled',true);
                  $s.find('input[type=radio][value="total"]').prop('checked',true);
                  break;
                default:
                  $s.find('input[type=radio]').prop('disabled',false);
                  break;
              }
            }
          },{
            label: 'Data origin',
            type: 'radioselect',
            radioselect: [
              ['total','All'],
              ['stream','The stream:',thestreams],
              ['protocol','The protocol:',theprotocols]
            ],
            pointer: {
              main: saveas,
              index: 'origin'
            },
            value: ['total'],
            classes: ['graph_origin']
          },{
            type: 'buttons',
            buttons: [{
              label: 'Add data set',
              type: 'save',
              'function': function(){
                //the graph options
                var graph;
                if (saveas.graph == 'new') {
                  graph = UI.plot.addGraph(saveas,$graph_c);
                  graphs[graph.id] = graph;
                  $UI.find('input.graph_id').val('');
                  $UI.find('select.graph_ids').append(
                    $('<option>').text(graph.id)
                  ).val(graph.id).trigger('change');
                }
                else {
                  graph = graphs[saveas.graph];
                }
                //the dataset options
                var opts = UI.plot.datatype.getOptions({
                  datatype: saveas.datatype,
                  origin: saveas.origin
                });
                graph.datasets.push(opts);
                UI.plot.save(graph);
                UI.plot.go(graphs);
              }
            }]
          }]));
          
          var $graph_c = $('<div>').addClass('graph_container');
          $main.append($graph_c);
          
          var $graph_ids = $UI.find('select.graph_ids');
          for (var i in graphs) {
            var graph = UI.plot.addGraph(graphs[i],$graph_c);
            $graph_ids.append(
              $('<option>').text(graph.id)
            ).val(graph.id);
            
            //the dataset options
            var datasets = [];
            for (var j in graphs[i].datasets) {
              var opts = UI.plot.datatype.getOptions({
                datatype: graphs[i].datasets[j].datatype,
                origin: graphs[i].datasets[j].origin
              });
              datasets.push(opts);
            }
            graph.datasets = datasets;
            graphs[graph.id] = graph;
          }
          $graph_ids.trigger('change');
          UI.plot.go(graphs);
          
          UI.interval.set(function(){
            UI.plot.go(graphs);
          },10e3);
          
        },{active_streams: true, capabilities: true});
        
        break;
      case 'Server Stats':
        if (typeof mist.data.capabilities == 'undefined') {
          mist.send(function(d){
            UI.showTab(tab);
          },{capabilities: true});
          $main.append('Loading..');
          return;
        }
        
        var $memory = $('<table>');
        var $loading = $('<table>');
        
        //build options for vheader table
        var cpu = {
          vheader: 'CPUs',
          labels: ['Model','Processor speed','Amount of cores','Amount of threads'],
          content: []
        };
        var cores = 0;
        for (var i in mist.data.capabilities.cpu) {
          var me = mist.data.capabilities.cpu[i];
          cpu.content.push({
            header: 'CPU #'+(Number(i)+1),
            body: [
              me.model,
              UI.format.addUnit(UI.format.number(me.mhz),'MHz'),
              me.cores,
              me.threads
            ]
          });
          cores += me.cores;
        }
        var $cpu = UI.buildVheaderTable(cpu);
        
        function buildstattables() {
          var mem = mist.data.capabilities.mem;
          var load = mist.data.capabilities.load;
          if (!("swapfree" in mem) && !("swaptotal" in mem)) {
            mem.swapfree = 0;
            mem.swaptotal = 0;
          }
          if (!("shmfree" in mem) && !("shmtotal" in mem)) {
            mem.shmfree = 0;
            mem.shmtotal = 0;
          }
          function createBar(total,used,cached) {
            if (total == 0) return "";
            return $("<div>").addClass("bargraph").append(
              $("<span>").attr("title","Used").addClass("used").text(Math.round(used/total*100)+"%").width((used/total*100)+"%")
            ).append( cached ? 
              $("<span>").attr("title","Cached").addClass("cached").width((cached/total*100)+"%")
              : "" )
          }
          var memory = {
            vheader: 'Memory',
            labels: ['','Used','Available','Total'],
            content: [
              {
                header: 'Physical memory',
                body: [
                  createBar(mem.total,mem.used,mem.cached),
                  $("<span>").append(UI.format.bytes(mem.used*1024*1024)).append(' ('+UI.format.addUnit(load.mem,'%')+')'),
                  $("<span>").append(
                    UI.format.bytes(mem.free*1024*1024)
                  ).append(
                    $("<div>").append(
                      "("
                    ).append(
                      UI.format.bytes(mem.cached*1024*1024)
                    ).append(
                      " cached)"
                    )
                  ),
                  UI.format.bytes(mem.total*1024*1024)
                ]
              },{
                header: 'Shared memory',
                body: [
                  createBar(mem.shmtotal,mem.shmtotal-mem.shmfree),
                  $("<span>").append(UI.format.bytes((mem.shmtotal-mem.shmfree)*1024*1024)).append(' ('+UI.format.addUnit(load.shm,'%')+')'),
                  UI.format.bytes(mem.shmfree*1024*1024),
                  UI.format.bytes(mem.shmtotal*1024*1024)
                ]
              },{
                header: 'Swap memory',
                body: [
                  createBar(mem.swaptotal,mem.swaptotal-mem.swapfree),
                  UI.format.bytes((mem.swaptotal-mem.swapfree)*1024*1024),
                  UI.format.bytes(mem.swapfree*1024*1024),
                  UI.format.bytes(mem.swaptotal*1024*1024)
                ]
              }
            ]
          };
          var nmem = UI.buildVheaderTable(memory);
          $memory.replaceWith(nmem);
          $memory = nmem;
          
          //CPU loading/total amount of cores is a percentage, over 1 means there are tasks waiting.
          var loading = {
            vheader: 'Load average',
            labels: ['CPU use','1 minute','5 minutes','15 minutes'],
            content: [{
              header: '&nbsp;',
              body: [
                UI.format.addUnit(UI.format.number(mist.data.capabilities.cpu_use/10),'%'),
                UI.format.number(load.one/100),
                UI.format.number(load.five/100),
                UI.format.number(load.fifteen/100)
              ]
            }]
          };
          var nload = UI.buildVheaderTable(loading);
          $loading.replaceWith(nload);
          $loading = nload;
        }
        
        buildstattables();
        $main.append(
          UI.buildUI([{
            type: 'help',
            help: 'You can find general server statistics here. Note that memory and CPU usage is for your entire machine, not just MistServer.'
          }])
        ).append(
          $('<table>').css('width','auto').addClass('nolay').append(
            $('<tr>').append(
              $('<td>').append($memory)
            ).append(
              $('<td>').append($loading)
            )
          ).append(
            $('<tr>').append(
              $('<td>').append($cpu).attr('colspan',2)
            )
          )
        );
        
        UI.interval.set(function(){
          mist.send(function(){
            buildstattables();
          },{capabilities: true});
        },30e3);
        
        break;
      case 'Email for Help':
        var config = $.extend({},mist.data);
        delete config.statistics;
        delete config.totals;
        delete config.clients;
        delete config.capabilities;
        
        config = JSON.stringify(config);
        config = 'Version: '+mist.data.config.version+"\n\nConfig:\n"+config;
        
        var saveas = {};
        
        $main.append(
          UI.buildUI([{
            type: 'help',
            help: 'You can use this form to email MistServer support if you\'re having difficulties.<br>A copy of your server config file will automatically be included.'
          },{
            type: 'str',
            label: 'Your name',
            validate: ['required'],
            pointer: {
              main: saveas,
              index: 'name'
            },
            value: mist.user.name
          },{
            type: 'email',
            label: 'Your email address',
            validate: ['required'],
            pointer: {
              main: saveas,
              index: 'email'
            }
          },{
            type: 'hidden',
            value: 'Integrated Help',
            pointer: {
              main: saveas,
              index: 'subject'
            }
          },{
            type: 'hidden',
            value: '-',
            pointer: {
              main: saveas,
              index: 'company'
            }
          },{
            type: 'textarea',
            rows: 20,
            label: 'Your message',
            validate: ['required'],
            pointer: {
              main: saveas,
              index: 'message'
            }
          },{
            type: 'textarea',
            rows: 20,
            label: 'Your config file',
            readonly: true,
            value: config,
            pointer: {
              main: saveas,
              index: 'configfile'
            }
          },{
            type: 'buttons',
            buttons: [{
              type: 'save',
              label: 'Send',
              'function': function(me){
                $(me).text('Sending..');
                $.ajax({
                  type: 'POST',
                  url: 'https://mistserver.org/contact?skin=plain',
                  data: saveas,
                  success: function(d) {
                    var $s = $('<span>').html(d);
                    $s.find('script').remove();
                    $main.html($s[0].innerHTML);
                  }
                });
              }
            }]
          }])
        );
        break;
      case 'Disconnect':
        mist.user.password = '';
        delete mist.user.authstring;
        delete mist.user.loggedin;
        mist.data = {};
        UI.sockets.http_host = null;
        sessionStorage.removeItem('mistLogin');

        mist.send(function(){
          UI.navto('Login');
        },{logout:true});
        
        break;
      default:
        $main.append($('<p>').text('This tab does not exist.'));
        break;
    }
    
    //focus on first empty field
    $main.find('.field').filter(function(){
      var val = $(this).getval();
      if (val == '') return true;
      if (val == null) return true;
      return false;
    }).each(function(){
      var a = [];
      if ($(this).is('input, select, textarea')) {
        a.push($(this));
      }
      else {
        a = $(this).find('input, select, textarea');
      }
      if (a.length) {
        $(a[0]).focus();
        return false;
      }
    });
  },
  dynamic: function(options,id){
    //required:
    //options.values = initial values
    //options.update = func(values), this -> element
    //options.create = func(), returns element
    //options.getEntries = func(data), convert input data to {id:child_data, ..} shape
    //options.add = func(id), -> this -> ele, returns child element, should have update func,
    //            = {create:()=>{},update:()=>{}}: pass dynamic creation options
    options = Object.assign({},options); //prevent overwriting if create returns either a dynamic object or a normal element            

    var ele = options.create(id);
    if (!ele) { return; }
    ele.raw = "";
    if (!options.getEntry) {
      options.getEntry = function(d,id){
        return (id in d ? d[id] : false);
      }
    }
    if (!options.getEntries) {
      options.getEntries = function(d) {
        return d;
      }
    }
    if (ele.update) {
      //it's most likely a nested dynamic element
      options.update = ele.update;
    }
    if (!options.update) {
      options.update = function(){};
    }
    if (typeof options.add == "object") {
      var addoptions = options.add;
      options.add = function(id){
        return UI.dynamic(addoptions,id);
      }
    }

    if (options.add) {

      ele._children = {};
      ele.add = function(id) {
        var child = options.add.call(ele,id);
        if (!child) { return; }
        if (typeof child.remove != "function") {
          child.remove = function(){
            var child = this;
            if (child instanceof jQuery) { child = child[0]; }
            if (child.parentNode) {
              child.parentNode.removeChild(child);
            }
            delete ele._children[id];
          };
        }
        else {
          var remove = child.remove;
          child.remove = function(){
            remove.apply(this,arguments);
            delete ele._children[id];
          };
        }

        child._id = id;
        ele._children[id] = child;
        if (child.customAdd) {
          child.customAdd(ele);
        }
        else {
          ele.append(child);
        }
      }
    }

    ele.update = function(values,allValues) {
      var entries = options.getEntries(values,ele._id);
      var raw = JSON.stringify(entries);
      if (this.raw == raw) {
        return;
      }
      this.values = entries;
      this.values_orig = values;


      if (options.add) {
        for (var id in entries) {
          if (!(id in this._children)) {
            this.add(id);
          }
          if (id in this._children) {
            this._children[id].update.call(this._children[id],entries[id],entries);
          }
        }
        for (var i in this._children) {
          if (!(i in entries)) {
            this._children[i].remove();
          }
        }
      }

      options.update.call(ele,entries,allValues);
      this.raw = raw;
    }

    if (options.values) ele.update.call(ele,options.values);
    
    return ele;
    
  },
  
  modules: {
    stream: {
      header: function(streamname,currenttab,parentstream){
        var $cont = $("<div>").addClass("header");
        var $nav = $("<ul>").addClass("tabnav");

        $cont.append(
          $("<div>").css("display","flex").css("align-items","baseline").append(
            $("<h2>").text("Stream \""+streamname+"\"")
          ).append(
            UI.modules.stream.status(streamname,{tags:false,thumbnail:false,livestreamhint:false})
          )
        ).append(
          $nav
        ).append(
          $("<h2>").text(
            currenttab == "Edit"  
            ? (parentstream in mist.data.streams ? "Edit \""+parentstream+"\"" : "Create \""+parentstream+"\"")
            : currenttab
          )
        );

        var tabs = [["Settings","Edit"],"Status","Preview","Embed"]
        
        var isFolderStream = false;
        if (streamname.indexOf("+") < 0) {
          var config = mist.data.streams[streamname];
          if (config?.source && (config.source.match(/^\/.+\/$/) || config.source.match(/^folder:.+$/))) {
            isFolderStream = true;
          }
        }
        if (isFolderStream) {
          tabs.pop();
          tabs.pop();
        }

        tabs.push(false);
        tabs.push(["Overview","Streams"]);

        function buildTab(name) {
          if (!name) {
            return $("<span>").addClass("spacer");
          }
          var label = name;
          if (typeof name != "string") {
            label = name[0];
            name = name[1];
          }
          return $("<li>").addClass("tab").addClass(
            name == currenttab ? "active" : false
          ).attr("tabindex",0).attr("data-icon",name).text(label).click(function(){
            if ((currenttab == "Edit") && ($cont.attr("data-changed"))) { //we're on the edit stream tab, and the fields have triggered an onchange
              if (!confirm("Your settings have not been saved. Are you sure you want to navigate away?")) {
                return;
              }
            }
            UI.navto(name,name == "Streams" ? "" : streamname);
          });
        }

        $nav.append(
          $("<span>").addClass("curtab-icon").attr("data-icon",currenttab)
        );
        for (var i in tabs) {
          $nav.append(
            buildTab(tabs[i])
          );
        }


        return $cont;
      },
      bigbuttons: function(streamname,currenttab){
        var $cont = $('<div>').addClass('bigbuttons');

        //wildcards streams don't have their own settings, but the edit page can deal with that nowadays
        $cont.append(
          $('<button>').text('Settings').addClass('settings').click(function(){
            UI.navto('Edit',streamname);
          })
        );

        //if this is not a wildcard stream, and is a folder input, only the status page is sensible
        //hardcoded to not require a capabilities call for these stupid buttons
        var isFolderStream = false;
        if (streamname.indexOf("+") < 0) {
          var config = mist.data.streams[streamname];
          if (config.source && (config.source.match(/^\/.+\/$/) || config.source.match(/^folder:.+$/))) {
            isFolderStream = true;
          }
        }
          


        if (currenttab != "Status") {
          $cont.append(
            $('<button>').text('Status').addClass('status').click(function(){
              UI.navto('Status',streamname);
            })
          );
        }
        if ((currenttab != "Preview") && !isFolderStream) {
          $cont.append(
            $('<button>').text('Preview').addClass('preview').click(function(){
              UI.navto('Preview',streamname);
            })
          );
        }
        if ((currenttab != "Embed") && !isFolderStream) {
          $cont.append(
            $('<button>').text('Embed').addClass('embed').click(function(){
              UI.navto('Embed',streamname);
            })
          );
        }

        $cont.append(
          $('<button>').addClass('cancel').addClass('return').text('Return').click(function(){
            UI.navto('Streams');
          })
        );

        return $cont;
      },
      findMist: function(on_success_callback,url_rewriter,fullsearch){
        var cont = $("<div>").addClass("findMist").hide();

        var debug = false; 
        //debug = true; //activates logging of found urls

        function makeUnique(ret) {
          var unique = [];
          for (var i = 0; i < ret.length; i++) {
            if (ret.indexOf(ret[i]) == i) { unique.push(ret[i]); }
          }
          return unique;
        }

        //find http output for info json
        //return an array with possible urls, try first url first
        function findHTTP(callback){
          //if (UI.sockets.http_host) { return callback([UI.sockets.http_host]); }
          var result = { HTTP: [], HTTPS: [] };
          var http = result.HTTP;
          var https = result.HTTPS;

          if (debug) console.log("Attempting to find urls to reach MistServer's HTTP output..");

          if (UI.sockets.http_host) {
            if (fullsearch) { 
              http.push(UI.sockets.http_host);
              https.push(UI.sockets.http_host);
              if (debug) console.log("Found previously used urls to MistServer's HTTP output, but performing a full search; adding it to both the result lists",UI.sockets.http_host);
            }
            else { 
              if (debug) console.log("Found a previously used url to MistServer's HTTP output, using that one",UI.sockets.http_host);
              return callback([UI.sockets.http_host]); 
            }
          }

          if (!mist.data.capabilities) {
            if (debug) console.log("I don't know MistServer's capabilities yet, retrieving those first.. brb")
            // Request capabilities first
            mist.send(function(){
              findHTTP(callback);
            },{capabilities:true});
            return;
          }



          var stored = mist.stored.get();
          if ("HTTPUrl" in stored) {
            if (debug) console.log("Found a previously valid HTTP url stored in MistServer's config, adding it to the HTTP list",stored.HTTPUrl);
            http.push(stored.HTTPUrl);
          }
          if ("HTTPSUrl" in stored) {
            if (debug) console.log("Found a previously valid HTTPS url stored in MistServer's config, adding it to the HTTPS list",stored.HTTPSUrl);
            https.push(stored.HTTPSUrl);
          }
          //scan http(s) protocol config
          for (var i in mist.data.config.protocols) {
            var connector = mist.data.config.protocols[i];
            var name = connector.connector;
            name = (""+name).replace(".exe","");
            switch (connector.connector) {
              case "HTTP": 
              case "HTTPS": {
                //pubadr
                if (connector.pubaddr) {
                  for (var j in connector.pubaddr) {
                    var protocol;
                    if (connector.pubaddr[j].slice(0,2) == "//") {
                      protocol = location.protocol;
                      //connector.pubaddr[j] = protocol+connector.pubaddr[j];
                      protocol = protocol.replace(":","").toUpperCase();
                    }
                    else {
                      protocol = connector.pubaddr[j].split(":")[0].toUpperCase();
                    }
                    if (protocol in result) {
                      result[protocol].push(connector.pubaddr[j]);
                      if (debug) console.log("Found a public address in the protocol config, adding it to the "+protocol+" list",connector.pubaddr[j]);
                    }
                    else {
                      console.warn("Unknown protocol in public address configuration for "+connector.connector+": ",connector.pubaddr[j]);
                    }
                  }
                }
                //port
                var port = connector.port;
                if (!port) { //retrieve default port
                  if (connector.connector in mist.data.capabilities.connectors) {
                    port = mist.data.capabilities.connectors[connector.connector].optional.port['default'];
                  }
                }
                if (port) {
                  var u = parseURL(mist.user.host,{port:port,pathname:'',protocol:name.toLowerCase()+":"}).full;
                  result[connector.connector].push(u);
                  if (debug) console.log("Found a port in the protocol config, adding it to the "+connector.connector+" list",u);
                }

                break;
              }
            }
          }

            
          if (!http.length) { 
            var u = parseURL(mist.user.host,{port:8080,pathname:''}).full;
            if (debug) console.log("Haven't found any HTTP urls yet, adding the default to the HTTP list",u);
            http.push(u);
          }
          if (!https.length) {
            if (debug) console.log("Haven't found any HTTPS urls yet, copying the HTTP list to HTTPS: it's worth a try",http);
            result.HTTPS = http;
            https = result.HTTPS;
          }

          http = makeUnique(http);
          https = makeUnique(https);

          cont.urls = result;
          if (debug) console.log("Full scan result:",result);
          var r = location.protocol == "https:" ? https : http;
          if (debug) console.log("Returning:",r);
          return callback(r);
        }

        function retrieveInfo(urls,attempt) {
          if (attempt >= urls.length) { return on_fail(urls); }
          try {
            if (!url_rewriter) {
              url_rewriter = function(hosturl){
                return hosturl + "player.js";
              };
            }
            url = url_rewriter(urls[attempt]);
            if (url.slice(0,2) == "//") {
              url = location.protocol + url;
            }
            if (url.slice(0,4) == "http") {
              $.ajax({
                type: "GET",
                url: url,
                success: function(d){
                  cont.hide();
                  on_success(urls[attempt],d);
                },
                error: function(jqXHR,textstatus,e){
                  retrieveInfo(urls,attempt+1);
                  if (debug) console.log("Could not reach "+url,e);
                }
              });
            }
            else if (url.slice(0,2) == "ws") {
              var ws = UI.websockets.create(url); //UI.websockets.create ensures websockets are closed when we navigate away
              ws.onopen = function(){
                ws.onerror = function(){};
                cont.hide();
                on_success(urls[attempt],ws);
              };
              ws.onerror = function(e){
                retrieveInfo(urls,attempt+1);
              };
            }
            else {
              throw "I'm not sure how to contact MistServer with this protocol. (at "+url+")";
            }
          }
          catch (e) { 
            if (debug) console.warn("Something went wrong while testing urls:",e,"attempt:",attempt,"url:",urls[attempt]);
            //on_fail(urls); 
            retrieveInfo(urls,attempt+1)
          }
        }
        function on_fail(urls) {
          if ((urls.length == 1) && (urls[0] == UI.sockets.http_host)) { 
            //reset the saved url and try searching again
            UI.sockets.http_host = null;
            findHTTP(function(result){
              urls = result;
              retrieveInfo(result,0);
            });
          }

          var $attempts;
          var $morehelp;
          if (urls.length) {
            $attempts = $("<div>");
            var $ul = $("<ul>");
            for (var i in urls) {
              var $li = $("<li>").html(
                $("<a>").attr("href",urls[i]).text(urls[i]).attr("target","MistURL")
              );
              $ul.append($li);
            }
            $attempts.html($ul);
            $morehelp = $("<div>").append(
              $("<span>").text("ℹ️").css("position","absolute").css("margin","1em 0 0 2em")
            ).append(
              $("<div>").css({border:"1px solid #bbb",padding:"0.5em 1em 0 3em",margin:"0 1em"}).append(
                $("<h2>").text("Why am I seeing this?")
              ).append(
                $("<p>").html("If you think one of the urls above should have worked, try clicking the link. <br>If something went wrong, your browser may give you more information about what it is. <br>If you see something like this:")
              ).append(
                $("<div>").css("margin-left","2em").html("<h1>Unsupported Media Type</h1>The server isn't quite sure what you wanted to receive from it.")
              ).append(
                $("<p>").html("then MistServer <i>can</i> be reached there but it couldn't be called from this page, possibly because of mixed content (if you're on an https website, your browser will refuse to load http content: create an https output) or CORS issues (if the HTTP output is accessible on a domain other than '"+location.origin+"' and that is not configured to allow cross-domain requests: check your proxy configuration).")
              )
            );

          }
          else {
            $attempts = $("<span>").text("I've not tried anything yet. I'm cluessless. Please help.")
          }

          //if request fails, allow user to enter url and save
          var save= {};
          var S = location.protocol == "https:" ? "S" : "";
          cont.show().html(
            $("<p>").text("Something went wrong: I could not locate MistServer's HTTP"+S+" output url. Where can it be reached?")
          ).append(
            $("<div>").addClass("description").append(
              $("<p>").text("I've attempted:")
            ).append(
              $attempts
            )          
          ).append(
            UI.buildUI([{
              label: "MistServer's HTTP"+S+" endpoint",
              type: "datalist",
              datalist: urls,
              help: "Please specify the url at which MistServer's HTTP"+S+" endpoint can be reached.",
              validate: ["required",function(val,me){
                if (val.length < 4) { return; }
                if (val.slice(0,4) != "http") {
                  return {
                    msg: "The url to MistServer should probably start with "+location.protocol+"//, for example: <br>"+parseURL(location.origin,{port:location.protocol == "https:" ? 4433 : 8080,pathname:""}).full,
                    "break": false,
                    classes: ['orange']
                  }
                }
                if (val.slice(0,5) != location.href.slice(0,5)) {
                  return {
                    msg: "It looks like you're attempting to connect to MistServer using "+parseURL(val).protocol+", while this page has been loaded over "+parseURL(location.href).protocol+". Your browser may refuse this because it is insecure.",
                    "break": false,
                    classes: ['orange']
                  }
                }
              }],
              pointer: { main: save, index: "url" }
            },{
              type: "buttons",
              buttons: [{
                type: "save",
                label: "Connect",
                "function": function(){
                  if (save.url[save.url.length-1] != "/") { urls.unshift(save.url+"/"); }
                  urls.unshift(save.url);
                  urls = makeUnique(urls);
                  retrieveInfo(urls,0);
                }
              }]
            }])
          ).append($morehelp);
        }


        function on_success(url,handler) {
          //this was the correct url, save
          
          if (url != UI.sockets.http_host) {
            UI.sockets.http_host = url;
            mist.stored.set("HTTP"+(url.slice(0,5) == "https" ? "S" : "")+"Url",url);
          }

          on_success_callback.call(cont,url,handler);
          
        }

        findHTTP(function(result){
          retrieveInfo(result,0);
        });

        cont.getUrls = function(kind){
          return cont.urls;
        }

        return cont;
      },
      livestreamhint: function(streamname){
        var $cont = $("<div>").addClass("livestreamhint");

        var settings = mist.data.streams[streamname.split("+")[0]];
        if (settings?.source && (settings.source.slice(0,1) != "/") && (!settings.source.match(/-exec:/))) {
          if ("streamkeys" in mist.data) {
            UI.updateLiveStreamHint(streamname,settings.source,$cont);
          }
          else {
            mist.send(function(){
              UI.updateLiveStreamHint(streamname,settings.source,$cont);
            },{streamkeys: true});
          }
        }
        else { 
          return false;
        }

        return $cont;
      }, 
      status: function(streamname,options){
        var defaultoptions = {
          livestreamhint: true,
          status: true,
          stats: true,
          tags: true, //use "readonly" to omit editing tags
          thumbnail: true
        };
        if (!options) {
          options = {};
        }
        options = Object.assign(defaultoptions,options);

        var activestream = {
          mode: 0,
          cont: $("<div>").addClass("activestream").attr("data-state",0),
          status: options.status ? $("<div>").attr("data-streamstatus",0).text("Inactive").hide() : false,
          viewers: options.stats ? $("<span>").attr("beforeifnotempty","Viewers: ") : false,
          inputs: options.stats ? $("<span>").attr("beforeifnotempty","Inputs: ") : false,
          outputs: options.stats? $("<span>").attr("beforeifnotempty","Outputs: ") : false,
          context_menu: false,
          tags: false,
          addtag: options.tags && (options.tags != "readonly") ? $("<div>").addClass("input_container").attr("title","Add a tag to this stream's current tags.\nNote that tags added here will not be applied when the stream restarts. To do that, add the tag through the stream settings.").css("display","block").html(
            $("<span>").attr("showifstate","0").text("Transient tags can be added here once the stream is active.")
          ).append(
            $("<input>").attr("showifstate","1").attr("placeholder","Tag name").on("keydown",function(e){
              switch (e.key) {
                case " ": {
                  e.preventDefault();
                  break;
                }
                case "Enter": {
                  $(this).parent().find("button").click();
                  break;
                }
              }
            })
          ).append(
            $("<button>").attr("showifstate","1").text("Add transient tag").click(function(){
              var addtag = {};
              var $me = $(this);
              var $input = $me.parent().find("input");
              var save = {tag_stream:{}};
              save.tag_stream[streamname] = $input.val();
              $me.text("Adding..");
              mist.send(function(d){
                $me.text("Add tag");
                $input.val("");
              },save);
            })
          ) : false,
          livestreamhint: options.livestreamhint ? UI.modules.stream.livestreamhint(streamname) : false
        };

        activestream.tags = false;
        if (options.tags) {
          if (options.tags != "readonly") {
            activestream.context_menu = new UI.context_menu();
          }
          activestream.tags = UI.modules.stream.tags({
            streamname: streamname,
            readonly: (options.tags == "readonly"),
            context_menu: activestream.context_menu,
            getStreamstatus: function(){
              return this.closest("[data-state]").getAttribute("data-state");
            }
          });
          activestream.tags.update(); //create initial data (retrieve configured tags from data.streams)
        }


        activestream.cont.html(
          activestream.status
        ).append(
          activestream.viewers
        ).append(activestream.inputs).append(activestream.outputs).append(activestream.tags ? activestream.tags : false)
        .append(activestream.addtag).append(
          options.thumbnail ? UI.modules.stream.thumbnail(streamname) : false
        ).append(
          activestream.livestreamhint
        );

        if (activestream.context_menu) {
          activestream.cont.css("position","relative");
          activestream.cont.append(activestream.context_menu.ele);
        }

        UI.sockets.ws.active_streams.subscribe(function(type,data){
          if (options.status) activestream.status.css("display","");
          if (type == "stream") {
            if (data[0] == streamname) {

              activestream.cont.attr("data-state",data[1]);

              var s = ["Inactive","Initializing","Booting","Waiting for data","Available","Shutting down","Invalid state"];
              if (options.status) activestream.status.attr("data-streamstatus",data[1]).text(s[data[1]]);
              if (options.stats) {
                activestream.viewers.text(data[2]);
                activestream.inputs.text(data[3] > 0 ? data[3] : "");
                activestream.outputs.text(data[4] > 0 ? data[4] : "");
              }
              if (options.tags) {
                activestream.tags.update({stats:data});
              }

              if (activestream.livestreamhint) {
                if (data[1] == 0) { activestream.livestreamhint.show(); }
                else { activestream.livestreamhint.hide(); }
              }

            }
          }
          else if (type == "error") {
            if (options.status) {
              activestream.status.attr("data-streamstatus",6).text(data)
            }
          }
        });

        return $("<section>").addClass("activestream").append(
          activestream.cont
        );
      },
      actions: function(currenttab,streamname){
        return $("<section>").addClass("actions").addClass("bigbuttons").html(
          $("<button>").text("Stop all sessions").attr("data-icon","stop").attr("title","Disconnect sessions for this stream. Disconnecting a session will kill any currently open connections (viewers, pushes and possibly the input). If the USER_NEW trigger is in use, it will be triggered again by any reconnecting connections.").click(function(){
            if (confirm("Are you sure you want to disconnect all sessions (viewers, pushes and possibly the input) from this stream?")) { 
              mist.send(function(){
                //done
              },{stop_sessions:streamname});
            }
          })
        ).append(
          $("<button>").text("Invalidate sessions").attr("data-icon","invalidate").attr("title","Invalidate all the currently active sessions for this stream. This has the effect of re-triggering the USER_NEW trigger, allowing you to selectively close some of the existing connections after they have been previously allowed. If you don't have a USER_NEW trigger configured, this will not have any effect.").click(function(){
            if (confirm("Are you sure you want to invalidate all sessions for the stream '"+streamname+"'?\nThis will re-trigger the USER_NEW trigger.")) { 
              mist.send(function(){
                //done
              },{invalidate_sessions:streamname});
            }
          })
        ).append(
          $("<button>").text("Nuke stream").attr("data-icon","nuke").attr("title","Shut down a running stream completely and/or clean up any potentially left over stream data in memory. It attempts a clean shutdown of the running stream first, followed by a forced shut down, and then follows up by checking for left over data in memory and cleaning that up if any is found.").click(function(){
            if (confirm("Are you sure you want to completely shut down the stream '"+streamname+"'?\nAll viewers will be disconnected.")) {
              mist.send(function(){
                UI.showTab(currenttab,streamname);
              },{nuke_stream:streamname});
            }
          })
        );
      },
      thumbnail: function(streamname,options){
        if (!UI.findOutput("JPG")) { return; }


        var image, stream;
        
        var jpg = $("<img>").hover(function(){
          if (image && stream) {
            $(this).attr("src",stream);
          }
        },function(){
          if (image && stream) {
            $(this).attr("src",image);
          }
        });

        UI.sockets.ws.info_json.subscribe(function(data){
          if (!data.source) return;
          //find mjpg and jpg urls
          for (var i in data.source) {
            if (data.source[i].type == "html5/image/jpeg") {
              var url = data.source[i].url;
              if (url.indexOf(".mjpg") > -1) { stream = url; }
              else { image = url; }
              if (image && stream) { break; }
            }
          }
          if (stream || image) {
            if (!stream) stream = image;
            else if (!image) image = stream;

            jpg.attr("src",image);
            if (clone) clone.attr("src",image);
          }
        },streamname);

        var clone;
        if (options && options.clone) clone = $("<img>").addClass("clone");
        
        return $("<section>").addClass("thumbnail").html(
          jpg
        ).append(clone);
      },
      metadata: function(streamname,options){
        var defaultoptions = {
          tracktable: true,
          tracktiming: true
        };
        if (!options) {
          options = {};
        }
        options = Object.assign(defaultoptions,options);

        var $meta = $("<div>").addClass("meta").text("Loading..");

        var $main = UI.dynamic({
          create: function(){
            var cont = $("<span>");

            cont.main = UI.dynamic({
              create: function(){
                var main = $("<div>").addClass("main").addClass("input_container");
                main.type = $("<span>");
                main.html(
                  $("<label>").append($("<span>").text("Type:")).append(main.type)
                );
                main.buffer = $("<span>");
                main.append(
                  $("<label>").attr("data-liveonly","").append($("<span>").text("Buffer window:")).append(main.buffer)
                );
                main.jitter = $("<span>");
                main.append(
                  $("<label>").attr("data-liveonly","").append($("<span>").text("Jitter:")).append(main.jitter)
                );
                if (options.tracktiming) {
                  main.timing = UI.dynamic({
                    create: function(){
                      var graph = $("<div>").addClass("tracktiming");

                      graph.box = $("<div>").addClass("boxcont");
                      graph.box.label = $("<span>").addClass("center");
                      graph.box.left = $("<span>").addClass("left");
                      graph.box.right = $("<span>").addClass("right");
                      graph.box.append(
                        $("<div>").addClass("box")
                      ).append(graph.box.label).append(graph.box.left).append(graph.box.right);

                      graph.append(graph.box);

                      graph.bounds = null;
                      graph.mstopos = function(value){
                        if (!this.bounds) { return 25; }

                        var pos = (value - this.bounds.firstms.min) / (this.bounds.lastms.max - this.bounds.firstms.min) * 100;

                        return pos;
                      };
                      graph.mstosize = function(value){
                        if (!this.bounds) { return 10; }
                        return value / (this.bounds.lastms.max - this.bounds.firstms.min) * 100
                      };

                      return graph;
                    },
                    add: {
                      create: function(id){
                        var row = $("<div>").addClass("track").attr("data-track",id);
                        row.label = $("<label>");
                        row.box = $("<div>").addClass("box");
                        row.jitter = $("<div>").addClass("jitter");
                        row.box.append(row.jitter);
                        row.left = $("<span>").addClass("left").attr("beforeifnotempty","-");
                        ;
                        row.right = $("<span>").addClass("right").attr("beforeifnotempty","+");
                        row.box.append(row.left);
                        row.box.append(row.right);
                        row.append(row.box).append(row.label);
                        return row;
                      },
                      update: function(values){
                        if (values.type != this.raw_type) {
                          this.attr("data-type",values.type);
                        }
                        if (values.type != this.raw_tracktype) {
                          this.attr("title",UI.format.capital(values.type));
                          this.raw_tracktype = values.type;
                        }
                        if (this.label.raw != values.idx+values.type+values.codec) {
                          this.label.text("Track "+values.idx+" ("+values.codec+")");
                          this.label.raw = values.idx+values.type+values.codec;
                          this.css("order",values.idx);
                        }
                        if (this.jitter.raw != values.jitter) {
                          this.jitter.attr("title","Jitter: "+values.jitter+"ms");
                          this.jitter.raw = values.jitter;
                        }

                        //always update: not just dependent on itself but also on the bounds
                        var lastmsindex = "nowms" in values ? "nowms" : "lastms";

                        var from = main.timing.bounds.firstms.max;
                        var to = main.timing.bounds.lastms.min;

                        this.box.css("left",main.timing.mstopos(values.firstms)+"%");
                        this.box.css("right",(100-main.timing.mstopos(values[lastmsindex]))+"%");
                        if (from >= to) {
                          //there is a gap
                          if (values.firstms > to) {
                            this.left.html(UI.format.duration((from - values.firstms)*1e-3,true))
                          }
                          else {
                            this.left.html(UI.format.duration((to - values.firstms)*1e-3,true))
                          }
                          if (values.lastms > from) {
                            this.right.html(UI.format.duration((values[lastmsindex] - from)*1e-3,true));
                          }
                          else {
                            this.right.html(UI.format.duration((values[lastmsindex] - to)*1e-3,true));
                          }
                        }
                        else {
                          this.left.html(UI.format.duration((from - values.firstms)*1e-3,true));
                          this.right.html(UI.format.duration((values[lastmsindex] - to)*1e-3,true));

                        }

                        //the jitter box is a child of the track box, so the width should be the percentage of the box width, which is lastms - firstms
                        this.jitter.width((values.jitter / (values[lastmsindex] - values.firstms) * 100)+"%");
                      }
                    },
                    getEntries: function(values){
                      var out = {};

                      //remove metadata tracks if applicable
                      var skipmeta = !main.includemeta.is(":checked");
                      for (var i in values) {
                        if (skipmeta && (values[i].type == "meta")) { continue; }
                        out[i] = values[i];
                      }
                      //find minimum and maximum values of first and lastms
                      var bounds = {
                        firstms: {
                          min: 1e24,
                          max: -1e24
                        },
                        lastms: {
                          min: 1e24,
                          max: -1e24
                        }
                      };
                      for (var i in out) {
                        var lastmsindex = "nowms" in out[i] ? "nowms" : "lastms";
                        bounds.firstms.min = Math.min(bounds.firstms.min,out[i].firstms);
                        bounds.firstms.max = Math.max(bounds.firstms.max,out[i].firstms);
                        bounds.lastms.min = Math.min(bounds.lastms.min,out[i][lastmsindex]);
                        bounds.lastms.max = Math.max(bounds.lastms.max,out[i][lastmsindex]);
                      }
                      main.timing.bounds = bounds;
                      for (var i in out) {
                        out[i].bounds = bounds; //ensure track updates when bounds update
                      }

                      return out;
                    },
                    update: function(values){
                      //the track rows have already been updated, now it's time for the box
                      //the box should be drawn from the maximum firstms to the minimum lastms
                      var from = main.timing.bounds.firstms.max;
                      var to = main.timing.bounds.lastms.min;

                      this.box.label.html(UI.format.duration(Math.abs(from - to)*1e-3,true));
                      this.box[0].style.setProperty("--ntracks",Object.keys(values).length);

                      if (from < to) {
                        //normal
                        this.box.css("margin-left",main.timing.mstopos(from)+"%");
                        this.box.css("margin-right",(100-main.timing.mstopos(to))+"%");
                        this.box.left.html(UI.format.duration(from*1e-3));
                        this.box.right.html(UI.format.duration(to*1e-3));
                        this.box.removeClass("gap");
                      }
                      else {
                        //gap!
                        this.box.css("margin-left",main.timing.mstopos(to)+"%");
                        this.box.css("margin-right",(100-main.timing.mstopos(from))+"%");
                        this.box.left.html(UI.format.duration(to*1e-3));
                        this.box.right.html(UI.format.duration(from*1e-3));
                        this.box.addClass("gap");
                      }

                      if (this.box.width() < 40) {
                        this.box.addClass("toosmall");
                      }
                      else {
                        this.box.removeClass("toosmall");
                      }
                    }
                  });
                  main.includemeta = $("<input>").attr("type","checkbox").click(function(){
                    cont.main.timing.update(cont.main.timing.values_orig);
                  });
                  main.append(
                    $("<label>").append(
                      $("<span>").text("Include metadata in graph:")
                    ).append(
                      main.includemeta
                    )
                  ).append(
                    $("<label>").append($("<span>").text("Track timing:"))
                  ).append(main.timing);
                }
                if (options.tracktable) {
                  main.tracks = UI.dynamic({
                    create: function(){
                      return $("<div>").addClass("tracks");
                    },
                    getEntries: function(tracks){
                      //sort into types
                      var out = {
                        audio: {},
                        video: {},
                        subtitle: {},
                        meta: {}
                      };
                      if (tracks) {
                        var sorted = Object.keys(tracks).sort(function(a,b){
                          return tracks[a].idx - tracks[b].idx;
                        });

                        for (var i in sorted) {
                          var track = tracks[sorted[i]];
                          var type = track.codec == "subtitle" ? "subtitle" : track.type;
                          out[type][sorted[i]] = track;
                          track.nth = Object.values(out[type]).length;
                        }
                      }
                      return out;
                    },
                    add: function(id){
                      var tt = UI.dynamic({
                        create: function(id){
                          var table = $("<table>").css("width","auto");
                          table.rows = [];
                          var headers = {
                            audio: {
                              vheader: 'Audio',
                              labels: ['Codec','Duration','Jitter','Avg bitrate','Peak bitrate','Channels','Samplerate','Language','Track index']
                            },
                            video: {
                              vheader: 'Video',
                              labels: ['Codec','Duration','Jitter','Avg bitrate','Peak bitrate','Size','Framerate','Language','Track index','Has B-Frames']
                            },
                            subtitle: {
                              vheader: 'Subtitles',
                              labels: ['Codec','Duration','Jitter','Avg bitrate','Peak bitrate','Language']
                            },
                            meta: {
                              vheader: 'Metadata',
                              labels: ['Codec','Duration','Jitter','Avg bitrate','Peak bitrate']
                            }
                          };
                          table.headers = headers[id].labels;
                          table.header = $("<tr>").addClass("header").append(
                            $("<td>").addClass("vheader").attr("rowspan",headers[id].labels.length+1).html($("<span>").text(headers[id].vheader))
                          ).append($("<td>"));
                          var tbody = $("<tbody>").append(table.header);
                          for (var i in headers[id].labels) {
                            var tr = $("<tr>").attr("data-label",headers[id].labels[i]).append($("<td>").text(headers[id].labels[i]+":"));
                            table.rows.push(tr);
                            tbody.append(tr);
                          }

                          table.append(tbody);
                          return table;
                        },
                        add: {
                          create: function(id,parent){
                            var header = $("<td>");
                            var tds = [];
                            for (var i in tt.headers) {
                              tds.push($("<td>"));
                            }

                            return {
                              header: header,
                              cells: tds,
                              customAdd: function(table){
                                table.header.append(this.header);
                                for (var i in this.cells) {
                                  table.rows[i].append(this.cells[i]);
                                }
                              }
                            };
                          },
                          update: function(track){
                            function getValues(track) {
                              function peakoravg (track,key) {
                                if ("maxbps" in track) {
                                  return UI.format.bits(track[key]*8,1);
                                }
                                else {
                                  if (key == "maxbps") {
                                    return UI.format.bits(track.bps*8,1);
                                  }
                                  return "unknown";
                                }
                              }
                              function displayDuration(track){
                                if ((track.firstms == 0) && (track.lastms == 0)) {
                                  return "« No data »";
                                }
                                var lastmsindex = "lastms";
                                if ("nowms" in track) {
                                  lastmsindex = "nowms";
                                }
                                var out;
                                out = UI.format.duration((track.lastms-track.firstms)/1000);
                                out += '<br><span class=description>'+UI.format.duration(track.firstms/1000)+' to '+UI.format.duration(track[lastmsindex]/1000)+'</span>';
                                return out;
                              }
                              var type = track.codec == "subtitle" ? "subtitle" : track.type;
                              switch (type) {
                                case 'audio':
                                  return {
                                    header: 'Track '+track.idx,
                                    body: [
                                      track.codec,
                                      displayDuration(track),
                                      UI.format.addUnit(UI.format.number(track.jitter),"ms"),
                                      peakoravg(track,"bps"),
                                      peakoravg(track,"maxbps"),
                                      track.channels,
                                      UI.format.addUnit(UI.format.number(track.rate),'Hz'),
                                      ('language' in track ? track.language : 'unknown'),
                                      track.nth
                                    ]
                                  };
                                  break;
                                case 'video':
                                  return {
                                    header: 'Track '+track.idx,
                                    body: [
                                      track.codec,
                                      displayDuration(track),
                                      UI.format.addUnit(UI.format.number(track.jitter),"ms"),
                                      peakoravg(track,"bps"),
                                      peakoravg(track,"maxbps"),
                                      UI.format.addUnit(track.width,'x ')+UI.format.addUnit(track.height,'px'),
                                      (track.fpks == 0 ? "variable" : UI.format.addUnit(UI.format.number(track.fpks/1000),'fps')),
                                      ('language' in track ? track.language : 'unknown'),
                                      track.nth,
                                      ("bframes" in track ? "yes" : "no")
                                    ]
                                  }
                                  break;
                                case 'subtitle':
                                  return {
                                    header: 'Track '+track.idx,
                                    body: [
                                      track.codec,
                                      displayDuration(track),
                                      UI.format.addUnit(UI.format.number(track.jitter),"ms"),
                                      peakoravg(track,"bps"),
                                      peakoravg(track,"maxbps"),
                                      ('language' in track ? track.language : 'unknown'),
                                      (track.nth)
                                    ]
                                  }
                                  break;
                                case "meta":
                                  return {
                                    header: 'Track '+track.idx,
                                    body: [
                                      track.codec,
                                      displayDuration(track),
                                      UI.format.addUnit(UI.format.number(track.jitter),"ms"),
                                      peakoravg(track,"bps"),
                                      peakoravg(track,"maxbps")
                                    ]
                                  }
                                  break;
                              }
                            }
                            var values = getValues(track);

                            if (this.header.raw != values.header) {  
                              this.header.text(values.header);
                              this.header.raw = values.header;
                            }
                            for (var i in this.cells) {
                              if (this.cells[i].raw != values.body[i]) {
                                this.cells[i].html(values.body[i]);
                                this.cells[i].raw = values.body[i];
                              }
                            }

                          }
                        },
                        update: function(){ 
                          if (Object.values(this._children).length) {
                            this.show();
                          }
                          else {
                            this.hide();
                          }
                        }
                      },id);
                      return tt;
                    },
                    update: function(){}
                  });
                  main.append(
                    $("<label>").append($("<span>").text("Track metadata:"))
                  ).append(main.tracks);
                }

                return main;

              },
              update: function(d){
                var main = this;
                if (main.type.raw != d.type) {
                  main.type.text(d.type == "live" ? "Live" : "Video on demand");
                  main.type.raw = d.type;
                }
                if (d.meta) {
                  if (main.buffer.raw != d.meta.buffer_window) {
                    main.buffer.html(d.meta.buffer_window ? UI.format.addUnit(UI.format.number(d.meta.buffer_window*1e-3),"s") : "N/A");
                    main.buffer.raw = d.meta.buffer_window;
                  }
                  if (main.jitter.raw != d.meta.jitter) {
                    main.jitter.html(d.meta.jitter ? UI.format.addUnit(UI.format.number(d.meta.jitter),"ms") : "N/A");
                    main.jitter.raw = d.meta.jitter;
                  }
                  main.tracks.update(d.meta.tracks);
                  main.timing.update(d.meta.tracks);
                }
              }
            });
            cont.audio = $("<table>").hide();
            cont.video = $("<table>").hide();
            cont.subtitle  = $("<table>").hide();
            cont.metadata = $("<table>").hide();


            return cont.append(cont.main).append(cont.audio).append(cont.video).append(cont.subtitle).append(cont.metadata);
          },
          update: function(info){
            this.main.update(info);
            if (this.raw_type != info.type) {
              this.attr("data-type",info.type);
              this.raw_type = info.type;
            }
          }
        });

        function ondata(data) {
          if ($main.freeze) { return; }

          if (data.error && (!$meta.rawdata || $meta.rawdata.type == "live")) {
            $meta.html("").attr("onempty",data.error+".");
          }
          else {
            if (($meta.rawdata && ($meta.rawdata.type == "live")) || !data.error) {
              $meta.rawdata = data;
            }
            if (!$main.parent().length) {
              //the interface doesn't show this yet
              $meta.html(
                $("<div>").append(
                  $("<button>").text("Open raw json").click(function(){
                    var tab = window.open(null, "Stream info json for "+streamname);
                    tab.document.write(
                      "<html><head><title>Raw stream metadata for '"+streamname+"'</title><meta http-equiv=\"content-type\" content=\"application/json; charset=utf-8\"></head><body><code><pre>"+JSON.stringify($meta.rawdata,null,2)+"</pre></code></body></html>"
                    ); 
                    tab.document.close(); // to finish loading the page
                  })
                ).append(
                  $("<button>").text("Freeze").attr("title","Pauze updating the stream metadata information below").click(function(){
                    if ($(this).text() == "Freeze") {
                      $(this).text("Frozen").css("background","#bbb");
                      $main.freeze = true;
                    }
                    else {
                      $(this).text("Freeze")[0].style.background = "";
                      $main.freeze = false;
                    }
                  })
                )
              ).append($main);
            }

            function buildTrackinfo(info) {
              if ($main.freeze) { return; } 

              var meta = info.meta;
              /*if ((!meta) || (!meta.tracks)) { 
                $main.html('No meta information available.');
                return;
              }*/
              $main.update(info);
            }
            buildTrackinfo(data);
          }

        }

        var firsttime = true;
        UI.sockets.ws.info_json.subscribe(function(d){
          ondata(d);

          if (firsttime && !d.error) { //activate the interval after the first message without error entry
            firsttime = false;
            if (d.type == "live") {
              //the websocket will not update values that change often, like firstms, lastms, etc
              //hax
              //this will. ugly but meh.
              UI.interval.set(function(){
                $.ajax({
                  type: "GET",
                  url: UI.sockets.http_host + "json_"+streamname+".js",
                  success: ondata
                });
              },5e3);
            }

          }
        },streamname);


        return $("<section>").addClass("meta").append(
          $("<h3>").text("Stream metadata")
        ).append(
          $meta
        );
      },
      processes: function(streamname){
        var $cont = $("<div>").attr("onempty","None.").addClass("processes");
        
        var layout = {
          "Process type:": function(d){ return $("<b>").text(d.process); },
          "Source:": function(d){
            var $s = $("<span>").text(d.source);
            if (d.source_tracks && d.source_tracks.length) {
              $s.append(
                $("<span>").addClass("description").text(" track "+(d.source_tracks.slice(0,-2).concat(d.source_tracks.slice(-2).join(" and "))).join(", "))
              );
            } 
            return $s;
          },
          "Sink:": function(d){
            var $s = $("<span>").text(d.sink);
            if (d.sink_tracks && d.sink_tracks.length) {
              $s.append(
                $("<span>").addClass("description").text(" track "+(d.sink_tracks.slice(0,-2).concat(d.sink_tracks.slice(-2).join(" and "))).join(", "))
              );
            } 
            return $s;
          },
          "Active for:": function(d){
            var since = new Date().setSeconds(new Date().getSeconds() - d.active_seconds);
            return $("<span>").append(
              $("<span>").html(UI.format.duration(d.active_seconds))
            ).append(
              $("<span>").addClass("description").text(" since "+UI.format.time(since/1e3))
            ); 
          },
          "Pid:": function(d,i){ return i; },
          "Logs:": function(d){
            var $logs = $("<div>").text("None.");
            if (d.logs && d.logs.length) {
              $logs.html("").addClass("description").addClass("logs").css({maxHeight: "6em", display: "flex", flexFlow: "column-reverse nowrap"});
              for (var i in d.logs) {
                var item = d.logs[i];
                $logs.prepend(
                  $("<div>").append(
                    UI.format.time(item[0])+' ['+item[1]+'] '+item[2]
                  )
                );
              }
            }
            return $logs;
          },
          "Additional info:": function(d){
            var $t;
            if (("ainfo" in d) && d.ainfo && Object.keys(d.ainfo).length) {
              $t = $("<table>");
              var capa = false;
              if (d.process in mist.data.capabilities.processes){
                capa = mist.data.capabilities.processes[d.process];
              }
              if (d.process+".exe" in mist.data.capabilities.processes){
                capa = mist.data.capabilities.processes[d.process+".exe"];
              }
              for (var i in d.ainfo) {
                var legend = false;
                if (capa && capa.ainfo && capa.ainfo[i]) {
                  legend = capa.ainfo[i];
                }
                if (!legend) {
                  legend = {
                    name: i
                  };
                }
                $t.append(
                  $("<tr>").append(
                    $("<th>").text(legend.name+":")
                  ).append(
                    $("<td>").html(d.ainfo[i]).append(legend.unit ? $("<span>").addClass("unit").text(legend.unit) : "")
                  )
                );
              }
            }
            else { $t = $("<span>").addClass("description").text("N/A"); }
            return $t;
          }
        };
        var $processes = UI.dynamic({
          create: function(){
            var $table = $("<table>");
            $table.rows = {};
            for (var i in layout) {
              var row = $("<tr>").append($("<th>").text(i).css("vertical-align","top"));
              $table.append(row);
              $table.rows[i] = row;
            }
            return $table;
          },
          add: {
            create: function(){
              var $cont = $("<div>").hide(); //dummy
              $cont.remove = function(){
                for (var i in this._children) {
                  this._children[i].remove();
                }
              };
              return $cont;
            },
            add: {
              create: function(i){
                return $("<td>").css("vertical-align","top");
              },
              update: function(d){
                this.html(d);
              }
            },
            getEntries: function(d,id){
              var out = {};
              for (var i in layout) {
                var v = layout[i](d,id);
                if (typeof v == "object") {
                  v = v.prop('outerHTML'); //unjQuerify the result so that raw (which jsons it) changes and isnt just '{"0":{},"length":1}'
                }
                out[i] = v;
              }
              return out;
            },
            update: function(d){
              //move children to table rows
              for (var i in this._children) {
                if (!this._children[i].moved) {
                  $processes.rows[i].append(this._children[i]);
                  this._children[i].moved = true;
                }
              }

              return;
            }
          },
          update: function(d){
            if (Object.keys(d).length) {
              if (!$processes.parent().length) {
                $processes.find("tr").first().addClass("header");
                $cont.append($processes);
              }
            }
            else {
              $processes.remove();
            }
          }
        });

        //the process stats update every 5 seconds internally
        UI.sockets.http.api.subscribe(function(d){
          if (d.proc_list) {
            $processes.update(d.proc_list);       
          }
        },{proc_list:streamname});
        

        return $("<section>").addClass("processes").append(
          $("<h3>").text("Processes")
        ).append(
          $cont
        );
      },
      triggers: function(streamname,tab){
        var $triggers = $("<div>").attr("onempty","None.").addClass("triggers");

        if (mist.data.config.triggers && Object.keys(mist.data.config.triggers).length) {
          var $table = $("<table>").append(
            $("<tbody>").append(
              $("<tr>").addClass("header type").append(
                $("<th>").text("Trigger name:")
              )
            ).append(
              $("<tr>").addClass("streams").append(
                $("<th>").text("Applies to:")
              )
            ).append(
              $("<tr>").addClass("handler").append(
                $("<th>").text("Handler:")
              )
            ).append(
              $("<tr>").addClass("blocking").append(
                $("<th>").text("Blocking:")
              )
            ).append(
              $("<tr>").addClass("response").append(
                $("<th>").text("Default response:")
              )
            ).append(
              $("<tr>").addClass("actions").append(
                $("<th>").text("Actions:")
              )
            )
          );
          $triggers.append($table);

          var count = 0;
          for (var trigger_type in mist.data.config.triggers) {
            var list = mist.data.config.triggers[trigger_type];
            for (var i in list) {
              var trigger = list[i];
              if (trigger.streams.length) { // there are streams specified
                if (trigger.streams.indexOf(streamname) < 0) { //this stream is not in the list
                  if (trigger.streams.indexOf(streamname.split("+")[0]) < 0) { //the wildcard root is not in the list
                    continue;
                  }
                }
              }
              count++;
              var $trs = $table.find("tr");
              for (var n = 0; n < $trs.length; n++) {
                var $td = $("<td>");
                switch (n) {
                  case 0: {
                    $td.append(
                      $("<b>").text(trigger_type)
                    );
                    break;
                  }
                  case 1: {
                    $td.text(trigger.streams.length ? trigger.streams.join(", ") : "All streams");
                    break;
                  }
                  case 2: {
                    $td.text(trigger.handler);
                    break;
                  }
                  case 3: {
                    $td.text(trigger.sync ? "Blocking" : "Non-blocking");
                    break;
                  }
                  case 4: {
                    $td.text(trigger['default'] ? trigger['default'] : "true");
                    break;
                  }
                  case 5: {
                    $td.addClass("buttons").attr("data-index",i).attr("data-type",trigger_type).append(
                      $("<button>").text("Edit").click(function(){
                        var $t = $(this).closest(".buttons");
                        UI.navto("Edit Trigger",$t.attr("data-type")+","+$t.attr("data-index"));
                      })
                    ).append(
                      trigger.streams.length > 1 
                      ? $("<button>").text("Remove from stream").click(function(){
                        var $t = $(this).closest(".buttons");
                        var type = $t.attr("data-type");
                        var streams = mist.data.config.triggers[type][$t.attr("data-index")].streams;
                        var i = streams.indexOf(streamname);
                        if (i >= 0) {
                          streams.splice(i,1);
                        }
                        else {
                          i = streams.indexOf(streamname.split("+")[0]);
                          if (i >= 0) { //should always be the case
                            streams.splice(i,1);
                          }
                        }
                        mist.send(function(){
                          UI.navto(tab,streamname);
                        },{config:mist.data.config});
                      })
                      : $("<button>").text("Remove").click(function(){
                        var $t = $(this).closest(".buttons");
                        var type = $t.attr("data-type");
                        if (confirm('Are you sure you want to delete this '+type+' trigger?')) {
                          mist.data.config.triggers[type].splice($t.attr("data-index"),1);
                          if (mist.data.config.triggers[type].length == 0) {
                            delete mist.data.config.triggers[type];
                          }

                          mist.send(function(d){
                            UI.navto(tab,streamname);
                          },{config:mist.data.config});
                        }
                      })
                    );
                    break;
                  }
                }
                $trs.eq(n).append($td);
              }

            }
          }
          if (count == 0) {
            $table.remove();
          }

        }

        return $("<section>").addClass("triggers").append(
          $("<h3>").text("Triggers")
        ).append(
          $("<button>").text("Add a trigger").click(function(){
            UI.navto("Edit Trigger");
          })
        ).append(
          $triggers
        );
      },
      pushes: function(streamname){
        return UI.modules.pushes({
          stream: streamname,
          logs: false,
          stop_pushes: false
        });
      },
      logs: function(streamname){
        return UI.modules.logs(streamname);
      },
      accesslogs: function(streamname){
        var $accesslogs = $("<div>").attr("onempty","None.").addClass("accesslogs");

        var tab = false;

        UI.sockets.ws.active_streams.subscribe(function(type,data){
          if (type == "access") {
            var scroll = ($accesslogs[0].scrollTop >= $accesslogs[0].scrollHeight - $accesslogs[0].clientHeight); //scroll to bottom unless scrolled elsewhere

            if (data[2] != "" && data[2] != streamname.split("+")[0]) { //filter out messages about other streams
              return;
            }


            //  [UNIX_TIMESTAMP, "session identifier", "stream name", "connector name", "hostname", SECONDS_ACTIVE, BYTES_UP_TOTAL, BYTES_DOWN_TOTAL, "tags"]

            var $up = $("<span>").html("↑").append(UI.format.bytes(data[6])); //bytes up
            if (data[6] != 0) {
              $up.append("(").append(
                UI.format.bits(data[6]*8/data[5],true) //avg bitrate
              ).append(")");
            }
            var $down = $("<span>").html("↓").append(UI.format.bytes(data[7])) //bytes down
            if (data[7] != 0) {
              $down.append("(").append(
                UI.format.bits(data[7]*8/data[5],true) //avg bitrate
              ).append(")");
            }

            var $msg = $("<div>").html(
              $("<span>").addClass("description").text(UI.format.dateTime(data[0]))
            ).append(
              $("<span>").attr("beforeifnotempty","Token: ").attr("title",data[1]).css("max-width","10em").text(data[1]) //session identifier
            ).append(
              $("<span>").attr("beforeifnotempty","Connector: ").text(data[3]) //connector name
            ).append(
              $("<span>").attr("beforeifnotempty","Hostname: ").text(data[4]) //host name
            ).append(
              $("<span>").attr("beforeifnotempty","Connected for: ").html(UI.format.duration(data[5])) //seconds active
            ).append(
              $up //bytes up
            ).append(
              $down //bytes down
            );
            $accesslogs.append($msg);

            if (scroll) $accesslogs[0].scrollTop = $accesslogs[0].scrollHeight; 

            if (tab) {
              try {
              var scroll = (tab.document.scrollingElement.scrollTop >= tab.document.scrollingElement.scrollHeight - tab.document.scrollingElement.clientHeight);
              tab.document.write($msg[0].outerHTML);
              if (scroll) tab.document.scrollingElement.scrollTop = tab.document.scrollingElement.scrollHeight;
              }
              catch (e) {}
            }

          }
          else if (type == "error") {
            var $msg = $("<div>").text(data);
            $accesslogs.append($msg);
          }
        });

        return $("<section>").addClass("accesslogs").append(
          $("<h3>").text("Access logs")
        ).append(
          $("<button>").text("Open raw").click(function(){
            tab = window.open("", "MistServer access logs for "+streamname);
            tab.document.write(
              "<html><head><title>MistServer access logs for '"+streamname+"'</title><meta http-equiv=\"content-type\" content=\"application/json; charset=utf-8\"><style>body{padding-left:2em;text-indent:-2em;}body>*>*:not(:last-child):not(:empty){padding-right:.5em;}.description{font-size:.9em;color:#777}[beforeifnotempty]:not(:empty):before{content:attr(beforeifnotempty);color:#777}</style></head><body>"
            );
            tab.document.write($accesslogs[0].innerHTML);
            tab.document.scrollingElement.scrollTop = tab.document.scrollingElement.scrollHeight;
          })
        ).append(
          $accesslogs
        );
      },
      preview: function(streamname,MistVideoObject){
        var $preview_cont = $('<section>').addClass("preview");
        if (!MistVideoObject) {
          window.mv = {};
          MistVideoObject = mv;
        }

        UI.sockets.http.player(function(){
          mistPlay(streamname,{
            target: $preview_cont[0],
            host: UI.sockets.http_host,
            skin: {
              inherit: "dev",
              colors:{accent:"var(--accentColor)"},
              blueprints: {
                protocol_mismatch_warning: function(){
                  var MistVideo = this;
                  var ele = document.createElement("div");
                  MistUtil.event.addListener(MistVideo.options.target,"initializeFailed",function(){
                    if (MistVideo.info && MistVideo.info.source) {
                      var msg = "";
                      var html = false;
                      if (MistVideo.info.source.length == 0) {
                        msg = "No stream sources were found.";
                      }
                      else {
                        var protomatch = false;
                        var myprot = location.protocol;
                        for (var i in MistVideo.info.source) {
                          var url = MistVideo.info.source[i].url;
                          url = url.replace(/^ws/,"http");
                          if (url.slice(0,myprot.length) == myprot) {
                            protomatch = true;
                            break;
                          }
                          if (url.slice(0,2) == "//") {
                            protomatch = true;
                            break;
                          }
                        }
                        if (!protomatch) {
                          myprot = myprot.replace(":","").toUpperCase();
                          msg = "No stream sources using "+myprot+" were found. You should configure "+myprot+" to play media from a "+myprot+" webpage.";
                          if (myprot == "HTTPS") {
                            html = document.createElement("a");
                            html.setAttribute("href","https://docs.mistserver.org/howto/https/");
                            html.setAttribute("target","_blank");
                            html.appendChild(document.createTextNode("Documentation about HTTPS"));
                          }
                        }
                      }
                      if (msg != "") {
                        ele.className = "err_balloon orange";
                        ele.style.position = "static";
                        ele.style.margin = "2em 0 3em";
                        ele.appendChild(document.createTextNode("Warning:"));
                        ele.appendChild(document.createElement("br"));
                        ele.appendChild(document.createTextNode(msg));
                        ele.appendChild(document.createElement("br"));
                        if (html) { 
                          ele.appendChild(html);
                          ele.appendChild(document.createElement("br"));
                        }
                        var button = document.createElement("button");
                        button.style.marginTop = "1em";
                        button.appendChild(document.createTextNode("Configure protocols"));
                        ele.appendChild(button);
                        button.addEventListener("click",function(){
                          UI.navto("Protocols");
                        });
                      }
                    }
                  },MistVideo.video);

                  return ele;
                }
              }
            },
            loop: true,
            MistVideoObject: MistVideoObject
          });

        },function(e){
          $preciew_cont.html(e);
        });

        return $preview_cont;
      },
      playercontrols: function(MistVideoObject,$video){
        var $controls = $("<section>").addClass("controls").addClass("mistvideo").addClass("input_container").html(
          $("<h3>").text("MistPlayer")
        ).append(
          $("<p>").text("Waiting for player..")
        );

        if (!$video) { $video = $(".dashboard"); }

        if (!$("link#devcss").length) {
          document.head.appendChild(
            $("<link>").attr("rel","stylesheet").attr("type","text/css").attr("href",UI.sockets.http_host+"skins/dev.css").attr("id","devcss")[0]
          );
        }

        function init() {
          var MistVideo = MistVideoObject.reference;
          function buildBlueprint(obj) {
            return MistVideo.UI.buildStructure.call(MistVideo.UI,obj); 
          }
          
          var name = buildBlueprint({
            "if": function(){
              return (this.playerName && this.source)
            },
            then: {
              type: "container",
              classes: ["mistvideo-description"],
              style: { display: "block" },
              children: [
                {type: "playername", style: { display: "inline" }},
                {type: "text", text: "is playing", style: {margin: "0 0.2em"}},
                {type: "mimetype"}
              ]
            }
          });
          var controls = buildBlueprint({
            type: "container",
            classes: ["mistvideo-column","mistvideo-devcontrols"],
            children: [
              {
                type: "protocol_mismatch_warning"
              },{
                type: "text",
                text: "Player control"
              },{
                type: "container",
                classes: ["mistvideo-devbuttons"],
                style: {"flex-wrap": "wrap"},
                children: [
                  {
                    "if": function(){ return !!(this.player && this.player.api); },
                    then: {
                      type: "button",
                      title: "Reload the video source",
                      label: "Reload video",
                      onclick: function(){
                        this.player.api.load();
                      }
                    }
                  },{
                    type: "button",
                    title: "Build MistVideo again",
                    label: "Reload player",
                    onclick: function(){
                      this.reload();
                    }
                  },{
                    type: "button",
                    title: "Switch to the next available player and source combination",
                    label: "Try next combination",
                    onclick: function(){
                      this.nextCombo();
                    }
                  }
                ]
              },
              {type:"forcePlayer"},
              {type:"forceType"},
              {type:"forceSource"}
            ]
          });
          var statistics = buildBlueprint({type:"decodingIssues", style: {"max-width":"30em","flex-flow":"column nowrap"}});
          var logs = buildBlueprint({type:"log"});
          var rawlogs = $("<button>").text("Open raw").css({display:"block",marginTop:"0.333em"}).click(function(){
            var streamname = MistVideo.stream;
            tab = window.open("", "Player logs for "+streamname);
            tab.document.write(
              "<html><head><title>Player logs for '"+streamname+"'</title><meta http-equiv=\"content-type\" content=\"application/json; charset=utf-8\"><style>.timestamp{color:#777;font-size:0.9em;}</style></head><body>"
            );
            tab.document.write(logs.lastChild.outerHTML);
            tab.document.scrollingElement.scrollTop = tab.document.scrollingElement.scrollHeight;
          });
          logs.insertBefore(rawlogs[0],logs.children[0]);

          $controls.html(
            $("<div>").append(
              $("<div>").append(
                $("<h3>").addClass("title").text("MistPlayer")
              ).append(name)
            ).append(controls).append(statistics)
          ).append(logs);
        }

        if (MistVideoObject && MistVideoObject.reference && MistVideoObject.reference.skin) {
          init();
        }
        $video[0].addEventListener("initialized",function(){ init(); });
        $video[0].addEventListener("initializedFailed",function(){ init(); });

        return $controls;
      },
      embedurls: function(streamname,misthost,urls){
        var cont = $("<section>").addClass("embedurls");

        var $datalist = $("<datalist>").attr("id","urlhints");
        var allurls = urls.HTTPS.concat(urls.HTTP);
        for (var i in allurls) {
          $datalist.append(
            $("<option>").val(allurls[i])
          );
        }

        var otherbase = otherhost.host ? otherhost.host : (allurls.length ? allurls[0] : misthost);

        cont.append(
          $('<span>').addClass('input_container').append(
            $('<label>').addClass('UIelement').append(
              $('<span>').addClass('label').text('Use base URL:')
            ).append(
              $('<span>').addClass('field_container').append(
                $('<input>').attr('type','text').addClass('field').val(otherbase).attr("list","urlhints")
              ).append(
                $datalist
              ).append(
                $('<span>').addClass('unit').append(
                  $('<button>').text('Apply').click(function(){
                    otherhost.host = $(this).closest('label').find('input').val();
                    if (otherhost.host.slice(-1) != "/") { otherhost.host += "/"; }
                    UI.navto('Embed',streamname);
                  })
                )
              )
            )
          )
        );

        var escapedstream = encodeURIComponent(streamname);
        var done = false;
        var defaultembedoptions = {
          forcePlayer: '',
          forceType: '',
          controls: true,
          autoplay: true,
          loop: false,
          muted: false,
          fillSpace: false,
          poster: '',
          urlappend: '',
          setTracks: {}
        };
        var embedoptions = $.extend({},defaultembedoptions);
        var stored = UI.stored.getOpts();
        if ('embedoptions' in stored) {
          embedoptions = $.extend(embedoptions,stored.embedoptions,true);
          if (typeof embedoptions.setTracks != 'object') {
            embedoptions.setTracks = {};
          }
        }
        var custom = {};
        switch (embedoptions.controls) {
          case 'stock':
            custom.controls = 'stock';
            break;
          case true:
            custom.controls = 1;
            break;
          case false:
            custom.controls = 0;
            break;
        }


        function embedhtml() {
          function randomstring(length){
            var s = '';
            function randomchar() {
              var n= Math.floor(Math.random()*62);
              if (n < 10) { return n; } //1-10
              if (n < 36) { return String.fromCharCode(n+55); } //A-Z
              return String.fromCharCode(n+61); //a-z
            }
            while (length--) { s += randomchar(); }
            return s;
          }
          function maybequotes(val) {
            switch (typeof val) {
              case 'string':
                if ($.isNumeric(val)) {
                  return val;
                }
                return '"'+val+'"';
              case 'object':
                return JSON.stringify(val);
              default:
                return val;
            }
            if (typeof val == 'string') {
              return 
            }
          }
          if (done) { 
            UI.stored.saveOpt('embedoptions',embedoptions); 
          }

          var target = streamname+'_'+randomstring(12);

          var options = ['target: document.getElementById("'+target+'")'];
          for (var i in embedoptions) {
            if (i == "prioritize_type") {
              if ((embedoptions[i]) && (embedoptions[i] != "")) {
                options.push("forcePriority: "+JSON.stringify({source:[["type",[embedoptions[i]]]]}));
              }
              continue;
            }
            if (i == "monitor_action") {
              if ((embedoptions[i]) && (embedoptions[i] != "")) {
                if (embedoptions[i] == "nextCombo") {
                  options.push("monitor: {\n"+
                    "          action: function(){\n"+
                    '            this.MistVideo.log("Switching to nextCombo because of poor playback in "+this.MistVideo.source.type+" ("+Math.round(this.vars.score*1000)/10+"%)");'+"\n"+
                    "            this.MistVideo.nextCombo();\n"+
                    "          }\n"+
                    "        }");
                }
              }
              continue;
            }
            if ((embedoptions[i] != defaultembedoptions[i]) && (embedoptions[i] != null) && ((typeof embedoptions[i] != 'object') || (JSON.stringify(embedoptions[i]) != JSON.stringify(defaultembedoptions[i])))) {
              options.push(i+': '+maybequotes(embedoptions[i]));
            }
          }

          var output = [];
          output.push('<div class="mistvideo" id="'+target+'">');
          output.push('  <noscript>');
          output.push('    <a href="'+otherbase+escapedstream+'.html'+'" target="_blank">');
          output.push('      Click here to play this video');
          output.push('    </a>');
          output.push('  </noscript>');
          output.push('  <script>');
          output.push('    var a = function(){');
          output.push('      mistPlay("'+streamname+'",{');
          output.push('        '+options.join(",\n        "));
          output.push('      });');
          output.push('    };');
          output.push('    if (!window.mistplayers) {');
          output.push('      var p = document.createElement("script");');

          if (urls.HTTPS.length) {
            output.push('      if (location.protocol == "https:") { p.src = "'+(parseURL(otherbase).protocol == "https://" ? otherbase : urls.HTTPS[0])+'player.js" } ');
            output.push('      else { p.src = "'+((parseURL(otherbase).protocol == "http://" ? otherbase : urls.HTTP[0]))+'player.js" } ');
          }
          else {
            output.push('      p.src = "'+otherbase+'player.js"');
          }

          //output.push('      p.src = "'+otherbase+'player.js"');
          output.push('      document.head.appendChild(p);');
          output.push('      p.onload = a;');
          output.push('    }');
          output.push('    else { a(); }');
          output.push('  </script>');
          output.push('</div>');

          return output.join("\n");
        }

        var emhtml = embedhtml(embedoptions);
        var $setTracks = $('<div>').text('Loading..').css('display','flex').css('flex-flow','column nowrap');
        var $protocolurls = $('<span>').text('Loading..').css("word-break","break-all");
        if (mistplayers) {
          var forcePlayerOptions = [['','Automatic']];
          for (var i in mistplayers) {
            forcePlayerOptions.push([i,mistplayers[i].name]);
          }
        }

        cont.append(UI.buildUI([
          $('<h3>').text('Urls'),
          {
            label: 'Stream info json',
            type: 'str',
            value: otherbase+'json_'+escapedstream+'.js',
            readonly: true,
            clipboard: true,
            help: 'Information about this stream as a json page.'
          },{
            label: 'Stream info script',
            type: 'str',
            value: otherbase+'info_'+escapedstream+'.js',
            readonly: true,
            clipboard: true,
            help: 'This script loads information about this stream into a mistvideo javascript object.'
          },{
            label: 'HTML page',
            type: 'str',
            value: otherbase+escapedstream+'.html',
            readonly: true,
            qrcode: true,
            clipboard: true,
            help: 'A basic html containing the embedded stream.'
          },$('<h3>').text('Embed code'),{
            label: 'Embed code',
            type: 'textarea',
            value: emhtml,
            rows: emhtml.split("\n").length+3,
            readonly: true,
            classes: ['embed_code'],
            clipboard: true,
            help: 'Include this code on your webpage to embed the stream. The options below can be used to configure how your content is displayed.'
          },$('<h4>').text('Embed code options (optional)').css('margin-top',0),{
            type: 'help',
            help: 'Use these controls to customise what this embedded video will look like.<br>Not all players have all of these options.'
          },{
            label: 'Prioritize type',
            type: 'select',
            select: [['','Automatic']],
            pointer: {
              main: embedoptions,
              index: 'prioritize_type'
            },
            classes: ['prioritize_type'],
            'function': function(){
              if (!done) { return; }
              embedoptions.prioritize_type = $(this).getval();
              $('.embed_code').setval(embedhtml(embedoptions));
            },
            help: 'Try to use this source type first, but full back to something else if it is not available.'
          },{
            label: 'Force type',
            type: 'select',
            select: [['','Automatic']],
            pointer: {
              main: embedoptions,
              index: 'forceType'
            },
            classes: ['forceType'],
            'function': function(){
              if (!done) { return; }
              embedoptions.forceType = $(this).getval();
              $('.embed_code').setval(embedhtml(embedoptions));
            },
            help: 'Only use this particular source.'
          },{
            label: 'Force player',
            type: 'select',
            select: forcePlayerOptions,
            pointer: {
              main: embedoptions,
              index: 'forcePlayer'
            },
            classes: ['forcePlayer'],
            'function': function(){
              if (!done) { return; }
              embedoptions.forcePlayer = $(this).getval();
              $('.embed_code').setval(embedhtml(embedoptions));
            },
            help: 'Only use this particular player.'
          },{
            label: 'Controls',
            type: 'select',
            select: [['1','MistServer Controls'],['stock','Player controls'],['0','None']],
            pointer: {
              main: custom,
              index: 'controls'
            },
            'function': function(){
              embedoptions.controls = ($(this).getval() == 1 );
              switch ($(this).getval()) {
                case 0:
                  embedoptions.controls = false;
                  break;
                case 1:
                  embedoptions.controls = true;
                  break;
                case 'stock':
                  embedoptions.controls = 'stock';
                  break;
              }
              $('.embed_code').setval(embedhtml(embedoptions));
            },
            help: 'The type of controls that should be shown.'
          },{
            label: 'Autoplay',
            type: 'checkbox',
            pointer: {
              main: embedoptions,
              index: 'autoplay'
            },
            'function': function(){
              embedoptions.autoplay = $(this).getval();
              $('.embed_code').setval(embedhtml(embedoptions));
            },
            help: 'Whether or not the video should play as the page is loaded.'
          },{
            label: 'Loop',
            type: 'checkbox',
            pointer: {
              main: embedoptions,
              index: 'loop'
            },
            'function': function(){
              embedoptions.loop = $(this).getval();
              $('.embed_code').setval(embedhtml(embedoptions));
            },
            help: 'If the video should restart when the end is reached.'
          },{
            label: 'Start muted',
            type: 'checkbox',
            pointer: {
              main: embedoptions,
              index: 'muted'
            },
            'function': function(){
              embedoptions.muted = $(this).getval();
              $('.embed_code').setval(embedhtml(embedoptions));
            },
            help: 'If the video should restart when the end is reached.'
          },{
            label: 'Fill available space',
            type: 'checkbox',
            pointer: {
              main: embedoptions,
              index: 'fillSpace'
            },
            'function': function(){
              embedoptions.fillSpace = $(this).getval();
              $('.embed_code').setval(embedhtml(embedoptions));
            },
            help: 'The video will fit the available space in its container, even if the video stream has a smaller resolution.'
          },{
            label: 'Poster',
            type: 'str',
            pointer: {
              main: embedoptions,
              index: 'poster'
            },
            'function': function(){
              embedoptions.poster = $(this).getval();
              $('.embed_code').setval(embedhtml(embedoptions));
            },
            help: 'URL to an image that is displayed when the video is not playing.'
          },{
            label: 'Video URL addition',
            type: 'str',
            pointer: {
              main: embedoptions,
              index: 'urlappend'
            },
            help: 'The embed script will append this string to the video url, useful for sending through params.',
            classes: ['embed_code_forceprotocol'],
            'function': function(){
              embedoptions.urlappend = $(this).getval();
              $('.embed_code').setval(embedhtml(embedoptions));
            }
          },{
            label: 'Preselect tracks',
            type: 'DOMfield',
            DOMfield: $setTracks,
            help: 'Pre-select these tracks.'
          },{
            label: 'Monitoring action',
            type: 'select',
            select: [['','Ask the viewer what to do'],['nextCombo','Try the next source / player combination']],
            pointer: {
              main: embedoptions,
              index: 'monitor_action'
            },
            'function': function(){
              embedoptions.monitor_action = $(this).getval();
              $('.embed_code').setval(embedhtml(embedoptions));
            },
            help: 'What the player should do when playback is poor.'
          },$('<h3>').text('Protocol stream urls'),$protocolurls
        ]));

        function displaySources(d,overwritebase) {
          var build = [];
          var $s_forceType = cont.find('.field.forceType');
          var $s_prioritizeType = cont.find('.field.prioritize_type');

          if (overwritebase) {
            overwritebase = overwritebase.replace(parseURL(overwritebase).protocol,"");
            build.push(
              $("<div>").addClass("orange").html("Warning: the provided base URL <a href=\""+otherbase+"\">"+otherbase+"</a> could not be reached. These links are my best guess but will probably not work properly.").css({margin:"0.5em 0",width:"45em","word-break":"normal"})
            );
          }

          for (var i in d.source) {
            var source = d.source[i];
            var human = UI.humanMime(source.type);

            var url = source.url;
            if (overwritebase) {
              url = parseURL(source.url).protocol + overwritebase + source.relurl;
            }


            //filter out the session token
            var tkn = url.match(/[\?\&]tkn=\d+\&?/);
            if (tkn) {
              tkn = tkn[0];
              url = url.replace(tkn,(tkn[0] == "?") && (tkn.slice(-1) == "&") ? "?" : (tkn.slice(-1) == "&" ? "&" : ""));
            }


            build.push({
              label: (human ? human+' <span class=description>('+source.type+')</span>' : UI.format.capital(source.type)),
              type: 'str',
              value: url,
              readonly: true,
              qrcode: true,
              clipboard: true
            });
            var human = UI.humanMime(source.type);
            if ($s_forceType.children("option[value=\""+source.type+"\"]").length == 0) {
              $s_forceType.append(
                $('<option>').text((human ? human+' ('+source.type+')' : UI.format.capital(source.type))).val(source.type)
              );
              $s_prioritizeType.append(
                $('<option>').text((human ? human+' ('+source.type+')' : UI.format.capital(source.type))).val(source.type)
              );
            }
          }
          $s_forceType.val(embedoptions.forceType);
          $s_prioritizeType.val(embedoptions.prioritize_type);
          $protocolurls.html(UI.buildUI(build));   
          done = true;
        }
        function displayTracks(msg) {
          var tracks = {};
          for (var i in msg.meta.tracks) {
            var t = msg.meta.tracks[i];
            if (t.codec == "subtitle") {
              t.type = "subtitle";
            }
            if ((t.type != 'audio') && (t.type != 'video') && (t.type != "subtitle")) { continue; }

            if (!(t.type in tracks)) {
              if (t.type == "subtitle") {
                tracks[t.type] = [];
              }
              else {
                tracks[t.type] = [[(''),"Autoselect "+t.type]];
              }
            }
            tracks[t.type].push([t.trackid,UI.format.capital(t.type)+' track '+(tracks[t.type].length+(t.type == "subtitle" ? 1 : 0))]);
          }
          $setTracks.html('');

          if (Object.keys(tracks).length) {
            $setTracks.closest('label').show();
            var trackarray = ["audio","video","subtitle"];
            for (var n in trackarray) {
              var i = trackarray[n];
              if (!tracks[i] || !tracks[i].length) { continue; }
              var $select = $('<select>').attr('data-type',i).css('flex-grow','1').change(function(){
                if ($(this).val() == '') {
                  delete embedoptions.setTracks[$(this).attr('data-type')];
                }
                else {
                  embedoptions.setTracks[$(this).attr('data-type')] = $(this).val();
                }
                $('.embed_code').setval(embedhtml(embedoptions));
              });
              $setTracks.append($select);
              if (i == "subtitle") {
                tracks[i].unshift(["","No "+i]);
              }
              else {
                tracks[i].push([-1,'No '+i]);
              }
              for (var j in tracks[i]) {
                $select.append(
                  $('<option>').val(tracks[i][j][0]).text(tracks[i][j][1])
                );
              }
              if (i in embedoptions.setTracks) {
                $select.val(embedoptions.setTracks[i]);
                if ($select.val() == null) {
                  $select.val('');
                  delete embedoptions.setTracks[i];
                  $('.embed_code').setval(embedhtml(embedoptions));
                }
              }
            }
          }
          else {
            $setTracks.closest('label').hide();
          }
        }

        function connect2info(baseurl) {
          UI.sockets.ws.info_json.subscribe(function(msg){
            if (msg.type == "error") {
              if (baseurl == otherbase) {
                //using otherbase, the info websocket could not be reached. Try again with misthost.
                connect2info(misthost);
              }
              else {
                throw msg;
              }
              return;
            }

            if ("source" in msg) {
              if (!done || msg.source.length) displaySources(msg,baseurl == otherbase ? false : otherbase);
            }
            if (("meta" in msg) && ("tracks" in msg.meta)) {
              displayTracks(msg);
            }

          },streamname,baseurl.replace(/^http/,"ws")+ "json_" + encodeURIComponent(streamname) + ".js",false,"?inclzero=1");

        }
        connect2info(otherbase);


        return cont;
      },
      tags: function(options){
        //returns dynamic element
        //call .update yourself :)
        //data format we're expecting:
        /*
          {
            tag1: 0, //this tag is in the config, but not in the active stream tags (it has been temp-removed)
            tag2: 1, //this tag is in the active stream tags, but not in the config (it is transient)
            tag3: 2  //this tag is both in the config and in the active stream tags
          }
        */
        options = Object.assign({
          streamname: false,
          readonly: false,
          context_menu: false,
          onclick: false,
          getStreamstatus: function(){ return 0; }
        },options);

        return UI.dynamic({
          create: function(){
            var ele = document.createElement("div");
            ele.classList.add("tags");
            return ele; 
          },
          add: {
            create: function(id){
              var tag = document.createElement("span");
              tag.classList.add("tag");
              tag.appendChild(document.createTextNode(id));

              tag.remove = function(){
                if (this.parentNode) {
                  this.parentNode.removeChild(this);
                }
              };

              if (options.onclick) tag.addEventListener("click",function(e) { 
                return options.onclick.apply(this,[e,id])
              });
              if (!options.readonly && options.context_menu) {
                tag.addEventListener("contextmenu",function(e){
                  e.stopPropagation();
                  e.preventDefault();
                  var type = this.values;
                  var stream = options.streamname;
                  var state = options.getStreamstatus.apply(this,arguments);

                  var menu = [];
                  if (state != 0) {
                    if (type == 0) {
                      menu.push([
                        "Re-activate tag",function(){
                          var save = {tag_stream:{}};
                          save.tag_stream[stream] = id;
                          mist.send(function(){
                            options.context_menu.hide();
                          },save);
                        },"➕",""
                      ]);
                    }
                    else {
                      menu.push([
                        "Untag stream",function(){
                          var save = {untag_stream:{}};
                          save.untag_stream[stream] = id;
                          mist.send(function(){
                            options.context_menu.hide();
                          },save);
                        },"➖","Remove this tag from this stream."+(type == 2 ? "\nIt will not be removed from the config, so it will return when the stream restarts." : "")
                      ]);
                    }
                  }
                  options.context_menu.show([[
                    $("<div>").addClass("header").append(
                      $("<div>").text("#"+id)
                    ).append(
                      $("<div>").addClass("description").text({
                        0: "Inactive tag",
                        1: "Transient tag",
                        2: "Persistent tag"
                      }[type]).attr("title",this.getAttribute("title"))
                    )
                  ],menu.length ? menu : null],e);
                });
              }

              return tag;
            },
            update: function(type){
              //can change type, not name
              var title = {
                0: "This tag is inactive: it is in the stream config, but it has been removed. It will return when the stream restarts.",
                1: "This tag is transient: it is not in the stream config. It will disappear when the stream becomes inactive.",
                2: "This tag is persistent: it is in the stream config. It will remain when the stream restarts."
              };

              if (this.raw !== type) {
                this.raw = type;
                this.setAttribute("data-type",type);
                this.setAttribute("title",title[type]);
              }
            }
          },
          getEntries: function(data){
            /* expects data to be like this: 
              {
                tags: [], //perma-tags as saved in config
                stats: [] //activestreams array
              }
              if tags is not set, it will attempt to read it from the stream config in mist.data.streams[streamname]
            */
            if (!data) { data = {}; }
            if (Array.isArray(data)) {
              data = {stats: data};
            }
            if (!("tags" in data)) {
              data.tags = [];
              var streambase = options.streamname.split("+")[0];
              if (streambase in mist.data.streams) {
                data.tags = mist.data.streams[streambase].tags;
              }
            }

            //data format we're creating:
            /*
                {
                  tag1: 0, //this tag is in the config, but not in the active stream tags (it has been temp-removed)
                  tag2: 1, //this tag is in the active stream tags, but not in the config (it is transient)
                  tag3: 2  //this tag is both in the config and in the active stream tags
                }
                */

            var out = {};
            for (var i in data.tags) {
              out[data.tags[i]] = 0; //semi-permanent tag saved in config
            }
            if (data.stats && (data.stats.length >= 6) && (data.stats[1] != 0)) {
              var transients = data.stats[5];
              if ((typeof transients == "string") && (transients != "")) { transients = transients.split(" "); }
              for (var i in transients) {
                if (transients[i] in out) {
                  out[transients[i]] = 2; //tag is both in config and in active stream tags
                }
                else {
                  out[transients[i]] = 1; //transient tag that disappears if stream is taken offline
                }
              }
            }
            else {
              for (var i in out) {
                out[i] = 2;
              }
            }
            //if (Object.keys(out).length) { console.warn(out,data) }

            return out;
          }
        });
      },
      clients: function(streamname){
        var layout = {
          Host: function(d){
            return d.host;
          },
          Protocol: function(d){
            return d.protocol.replace("INPUT:","");
          },
          Connected: function(d){
            return UI.format.duration(d.conntime);
          },
          "Data downloaded": function(d){
            return UI.format.bytes(d.down);
          },
          "Current bitrate": function(d){
            return UI.format.bits(d.downbps*8,true);
          },
          "Media time": function(d){
            return d.position ? UI.format.duration(d.position) : "";
          },
          "Packets received": function(d){
            return d.pktcount ? UI.format.number(d.pktcount,{round:false}) : ""
          },
          "Packets lost": function(d){
            return d.pktcount ? UI.format.number(d.pktlost,{round:false}) : ""
          },
          "Packets retransmitted": function(d){
            return d.pktcount ? UI.format.number(d.pktretransmit,{round:false}) : ""
          }

        };
        var $inputs = UI.dynamic({
          create: function(){
            var $thead = $("<thead>");
            var $tbody = $("<tbody>");
            var $table = $("<table>").append($thead).append($tbody);
            $table._rows = {};
            for (var i in layout) {
              var row = $("<tr>");
              
              var cell = $("<td>").text(i+":");
              row.append(cell);

              $table._rows[i]= row;
              $tbody.append(row);
            }
            $thead.append($tbody.children().first());
            return $table;
          },
          add: {
            create: function(){
              var out = {
                cells: {},
                customAdd: function(table){
                  for (var i in this.cells) {
                    table._rows[i].append(this.cells[i]);
                  }
                },
                remove: function(table){
                  for (var i in this.cells) {
                    this.cells[i].remove();
                    delete this.cells[i];
                  }
                }
              };
              for (var i in layout) {
                out.cells[i] = $("<td>");
              }
              return out;
            },
            update: function(d){
              for (var i in this.cells) {
                var value = layout[i].call(null,d);
                if (this.cells[i].raw != value) {
                  this.cells[i].html(value);
                  this.cells[i].raw = value;
                }
              }
            }
          },
          update: function(values){
            if (values.length) {
              if (!$inputs.parent().length) {
                $inputs._parent.append($inputs);
              }
            }
            else if ($inputs.parent()) {
              $inputs._parent = $inputs.parent();
              $inputs.remove();
            }
          }
        });

        UI.sockets.http.clients.subscribe(function(d){
          for (var i = d.data.length -1; i >= 0; i--) {
            if (d.data[i].stream != streamname) {
              d.data.splice(i,1);
            }
          }
          $inputs.update(d.data);
          //console.warn(d);
        },{streams:[streamname],protocols:["INPUT"]});
        
        
        return $("<section>").addClass("clients").append(
          $("<div>").addClass("inputs").append(
            $("<h3>").text("Current input")
          ).append(
            $inputs
          )
        )
      }
    },
    logs: function(streamname){
      var scroll = true;
      var $logs = $("<div>").attr("onempty","None.").attr("data-scrolling",scroll).addClass("logs").on("scroll",function(){
        //scroll to bottom unless scrolled elsewhere
        if (this.scrollTop + this.clientHeight >= this.scrollHeight - 5) {
          scroll = true;
        }
        else {
          scroll = false;
        }
        $logs.attr("data-scrolling",scroll);
      });

      var tab = false;

      UI.sockets.ws.active_streams.subscribe(function(type,data){
        if (type == "log") {
          if (streamname && (data[3] != "") && (data[3] != streamname.split("+")[0])) { //filter out messages about other streams
            return;
          }
          if (data[1] == "ACCS") { return; } //the access log has its own container

          var $msg = $("<div>").addClass("message").attr("data-debuglevel",data[1]).html(
            $("<span>").addClass("time").html(
              $("<span>").append(
                $("<span>").addClass("description").text("[")
              ).append(
                $("<span>").text(UI.format.dateTime(data[0]))
              ).append(
                $("<span>").addClass("description").text("] ")
              ).children()
            )
          ).append(
            $("<span>").addClass("binary").attr("title","binary name").text(data[5])
          ).append(
            $("<span>").addClass("stream").attr("title","stream name").html(
              data[3] ? $("<span>").append(
                $("<span>").addClass("description").text(":")
              ).append(
                $("<span>").text(data[3])
              ).children() : ""
            )
          ).append(
            $("<span>").addClass("pid").attr("title","pid").html(
              data[4] ? $("<span>").append(
                $("<span>").addClass("description").text(" (")
              ).append(
                $("<span>").text(data[4])
              ).append(
                $("<span>").addClass("description").text(")")
              ).children() : ""
            )
          ).append(
            $("<span>").addClass("debuglevel").attr("title","debug level").html(
              $("<span>").append(
                $("<span>").addClass("description").text(" [")
              ).append(
                $("<span>").text(data[1])
              ).append(
                $("<span>").addClass("description").text("]")
              ).children() 
            )
          ).append(
            $("<span>").addClass("message").text(" "+data[2])
          ).append(
            $("<span>").addClass("line").addClass("copy_but_hide").attr("title","line number").html(data[6] ? "&nbsp;("+data[6]+")" : "")
          );
          $logs.append($msg);

          if ($logs.children().length > 1000) {
            $logs.children().first().remove();
          }

          if (scroll) $logs[0].scrollTop = $logs[0].scrollHeight; 

          if (tab) {
            try {
              let s = (tab.document.scrollingElement.scrollTop >= tab.document.scrollingElement.scrollHeight - tab.document.scrollingElement.clientHeight);
              tab.document.write($msg[0].outerHTML);
              if (s) tab.document.scrollingElement.scrollTop = tab.document.scrollingElement.scrollHeight;
            }
            catch (e) {}
          }

        }
        else if (type == "error") {
          var $msg = $("<div>").text(data);
          $logs.append($msg);
        }
      });

      return $("<section>").addClass("logs").append(
        $("<h3>").text("MistServer logs")
      ).append(
        $("<button>").text("Open raw").click(function(){
          tab = window.open("", "MistServer logs"+(streamname ? " for "+streamname : ""));
          tab.document.write(
            "<html><head><title>MistServer logs"+(streamname ? " for '"+streamname+"'" : "")+"</title><meta http-equiv=\"content-type\" content=\"application/json; charset=utf-8\"><style>body{padding-left:2em;text-indent:-2em;font-family:monospace}.description,.message :is(.time,.binary,.pid,.line){font-size:.9em;color:#777}.message:is([data-debuglevel=\"WARN\"],[data-debuglevel=\"ERROR\"],[data-debuglevel=\"FAIL\"]){font-weight:bold;}</style></head><body>"
          );
          tab.document.write($logs[0].innerHTML);
          tab.document.scrollingElement.scrollTop = tab.document.scrollingElement.scrollHeight;
        })
      ).append(
        $logs
      ).append(
        $("<button>").addClass("down").attr("data-icon","down").attr("title","Snap to bottom").click(function(){
          scroll = true;
          $logs[0].scrollTop = $logs[0].scrollHeight;
        })
      );
    },
    pushes: function(options){
      if (!options) {
        options = {};
      }
      options = Object.assign({
        stream: false, //if stream is passed; filter the results to be only for that stream
        logs: true,
        stop_pushes: true,
        form: false,
        collapsible: false
      },options);

      var $pushes = $("<div>").addClass("pushes");
      $pushes.html("Loading..");

      mist.send(function(d){
        $pushes.html("");
        mist.data.push_list = d.push_list;

        var context_menu = new UI.context_menu();
        $pushes.append(context_menu.ele);

        if (options.push_settings) {
          $pushes.append(
            $("<section>").append(
              $('<h3>').text('Automatic push settings')
            ).append(
              UI.buildUI([
                {
                  type: "help",
                  help: "These settings only apply to automatic pushes."
                },{
                  label: 'Delay before retry',
                  unit: 's',
                  type: 'int',
                  min: 0,
                  help: 'How long the delay should be before MistServer retries an automatic push.<br>If set to 0, it does not retry.',
                  'default': 3,
                  pointer: {
                    main: d.push_settings,
                    index: 'wait'
                  }
                },{
                  label: 'Maximum retries',
                  unit: '/s',
                  type: 'int',
                  min: 0,
                  help: 'The maximum amount of retries per second (for all automatic pushes).<br>If set to 0, there is no limit.',
                  'default': 0,
                  pointer: {
                    main: d.push_settings,
                    index: 'maxspeed'
                  }
                },{
                  type: 'buttons',
                  buttons: [{
                    type: 'save',
                    label: 'Save',
                    'function': function(){
                      mist.send(function(d){
                        UI.navto('Push');
                      },{
                        push_settings: d.push_settings
                      })
                    }
                  }]
                }
              ])
            )
          );
        }

        if (options.filter) {
          $pushes.append(
            UI.buildUI([{
              label: "Filter the pushes below",
              classes: ["filter"],
              help: "Pushes that do not contain this text in their stream, target or notes will be hidden.",
              "function": function(e){
                var val = $(this).getval();
                var $tables = $pushes.find("table[data-pushtype]").each(function(i,table){
                  table.filter(val);
                })
              },
              css: {"margin-top":"3em"}
            }]).css( {"margin-bottom":0} )

          );
        }

        function buildPushCont(type,values) {
          function printPrettyComparison(a,b,c){
            var str = "";
            str += "$"+a+" ";
            switch (Number(b)) {
              case 0:  { str += "is true";  break; }
              case 1:  { str += "is false"; break; }
              case 2:  { str += "== "+c; break; }
              case 3:  { str += "!= "+c; break; }
              case 10: { str += "> (numerical) " +c; break; }
              case 11: { str += ">= (numerical) "+c; break; }
              case 12: { str += "< (numerical) " +c; break; }
              case 13: { str += "<= (numerical) "+c; break; }
              case 20: { str += "> (lexical) " +c; break; }
              case 21: { str += ">= (lexical) "+c; break; }
              case 22: { str += "< (lexical) " +c; break; }
              case 23: { str += "<= (lexical) "+c; break; }
              default: { str += "comparison operator unknown"; break; }
            }
            return str;
          }

          var layout = {
            "": function(push){
              var $cont = $("<label>").addClass("toggle-switch").append(
                $("<input>").attr("type","checkbox").prop("checked",!push.deactivated).change(function(){
                  if (push.deactivated) {
                    //push.stream has already been edited to have the normal (activated) stream name; just edit to the push object as is
                    var $me = $(this);
                    var o = {};
                    o[push.id] = push;
                    mist.send(function(d){
                      $table.update(d.auto_push);
                    },{
                      push_auto_add: o
                    });
                  }
                  else {
                    push.stream = "💤deactivated💤_"+push.stream;
                    var o = {};
                    o[push.id] = push;
                    mist.send(function(d){
                      $table.update(d.auto_push);
                    },{
                      push_auto_add: o
                    });
                  }
                })
              ).append(
                $("<div>").addClass("slider")
              ).append(
                $("<span>").text(push.deactivated ? "Disabled" : "Enabled").hide() //for sorting
              ).click(function(e){
                e.stopPropagation();
              })
              return $cont;
            },
            Stream: function(push){
              if (push.stream[0] == "#") return push.stream;
              return $("<a>").addClass("clickable").text(push.stream).click(function(e){
                UI.navto("Preview",push.stream);
                e.stopPropagation();
              }).on("contextmenu",function(e){
                e.preventDefault();
                e.stopPropagation();
                var streamname = push.stream;

                var header = [
                  $("<div>").addClass("header").text(streamname)
                ];
                var gototabs = [
                  [$("<span>").html("Edit "+(streamname.indexOf("+") < 0 ? "stream" : "<b>"+streamname.split("+")[0]+"</b>")),function(){ UI.navto("Edit",streamname); },"Edit","Change the settings of this stream."],
                  ["Stream status",function(){ UI.navto("Status",streamname); },"Status","See more details about the status of this stream."],
                  ["Preview stream",function(){ UI.navto("Preview",streamname); },"Preview","Watch the stream."]
                ];

                var menu = [header];
                menu.push(gototabs);

                context_menu.show(menu,e);

              });
            },
            Target: function(push){
              var $cont = $("<div>"); //temporary, to hold multiple children
              var t = push.target;
              if (type != "Automatic") {
                if (push.stats && push.stats.current_target) {
                  t = push.stats.current_target;
                }
                else {
                  t = push.resolved_target;
                }
              }
              //split url params
              t = t.split("?");
              var params = [];
              if (t.length > 1) params = t.pop().split("&");
              var main = t.join("?");
              $cont.append(
                $("<span>").text(main).attr("title",push.target)
              );
              if (params.length) {
                $cont.append(
                  $("<span>").addClass("param").text("?"+params[0])
                );
                for (var i = 1; i < params.length; i++) {
                  $cont.append(
                    $("<span>").addClass("param").text("&"+params[i])
                  );
                }
              }
              return $cont.children();
            },
            Conditions: false,
            Notes: false,
            Statistics: false,
            Actions: function(push,type) {
              var $cont = $("<div>").addClass("buttons");
              if (type == 'Automatic') {
                $cont.append(
                  $("<button>").text("Edit").click(function(e){
                    e.stopPropagation();
                    UI.navto("Start Push","auto_"+push.id);
                  })
                );
              }
              $cont.append(
                $('<button>').text((type == 'Automatic' ? 'Remove' : 'Stop')).click(function(e){
                  e.stopPropagation();

                  if (confirm("Are you sure you want to "+$(this).text().toLowerCase()+" this push?\n"+push.stream+' to '+push.target)) {
                    $(this).html(
                      $('<span>').addClass('red').text((type == 'Automatic' ? 'Removing..' : 'Stopping..'))
                    );
                    if (type == 'Automatic') {
                      var me = this;
                      mist.send(function(d){
                        $(me).text("Done.");
                        $table.update(d.auto_push);
                        //use the reply to update the automatic pushes list
                      },{push_auto_remove:push.id});
                    }
                    else {
                      mist.send(function(d){ 
                        //done
                      },{'push_stop':[push.id]});
                    }
                  };
                })
              );
              if (type == "Automatic") {
                if (options.stop_pushes) {
                  $cont.append(
                    $('<button>').text('Stop pushes').click(function(e){
                      e.stopPropagation();

                      var msg = "Are you sure you want to stop all pushes matching \n\""+push.stream+' to '+push.target+"\"?";
                      if (push.stream[0] == "#") {
                        msg = "Are you sure you want to stop all pushes to "+push.target+"\"?";
                      }
                      if (!push.deactivated && (d.push_settings.wait != 0)) {
                        msg += "\n\nRetrying is enabled. That means the push will probably just restart. You'll probably want to set 'Delay before retry' to 0, or disable the autopush first.";
                      }

                      if (confirm(msg)) {
                        var $button = $(this);
                        $button.text('Stopping pushes..');
                        //also stop the matching pushes
                        var pushIds = [];
                        var push_list = mist.data.push_list;
                        for (var i in push_list) {
                          if ((push.target == push_list[i][2]) && ((push.stream[0] == "#")  || (push.stream == push_list[i][1]))) {
                            pushIds.push(push_list[i][0]);
                            $pushes.find('table[data-pushtype="active"] tr[data-pushid='+push_list[i][0]+']').html(
                              $('<td colspan=99>').html(
                                $('<span>').addClass('red').text('Stopping..')
                              )
                            );
                          }
                        }

                        mist.send(function(){
                          $button.text('Stop pushes');
                        },{
                          push_stop: pushIds
                        });
                      }
                    })
                  );
                }
              }

              return $cont;
            }
          };
          if (options.stream) {
            delete layout.Stream;
          }
          if (type == "Automatic") {
            layout.Conditions = function(push){
              var $conditions = $("<div>");
              if ("scheduletime" in push) {
                $conditions.append(
                  $('<span>').text('schedule on '+(new Date(push.scheduletime*1e3)).toLocaleString())
                );
              }
              if ("completetime" in push) {
                $conditions.append(
                  $('<span>').text("complete on "+(new Date(push.completetime*1e3)).toLocaleString())
                );
              }
              if ("start_rule" in push) {
                $conditions.append(
                  $('<span>').text("starts if "+printPrettyComparison.apply(this,push.start_rule))
                );
              }
              if ("end_rule" in push) {
                $conditions.append(
                  $('<span>').text("stops if "+printPrettyComparison.apply(this,push.end_rule))
                );
              }
              return $conditions.children().length ? $conditions : "";
            }
            layout.Notes = function(push){
              if (("x-LSP-notes" in push) && (push["x-LSP-notes"])) {
                return push["x-LSP-notes"];
              }
              return "";
            };
          }
          else {
            delete layout[""];
            layout.Statistics = {
              create: function(){
                return $("<div>").addClass("statistics"); 
              },
              add: {
                create: function(id){
                  var $ele = $("<div>");
                  var labels = {
                    pid: "Pid: ",
                    latency: "Latency: ",
                    active_ms: "Active for: ",
                    bytes: "Data transferred: ",
                    mediatime: "Last sent timestamp:",
                    media_tx: "Media time transferred: ",
                    mediaremaining: "Media time until stream end: ",
                    pkt_retrans_count: "Packets retransmitted: ",
                    pkt_loss_count: "Packets lost: ",
                    tracks: "Tracks: "
                  };
                  if ((options.logs) && (id == "logs")) {
                    return UI.dynamic({
                      create: function(){
                        return $("<div>").addClass("logs");
                      },
                      add: {
                        create: function(){
                          return $("<div>");
                        },
                        update: function(item){
                          this.html(UI.format.time(item[0])+' ['+item[1]+'] '+item[2])
                        }
                      }
                    });
                  }
                  if (id in labels) {
                    return $ele.attr("beforeifnotempty",labels[id]);
                  }
                },
                update: function(val,allValues){
                  var me = this;
                  var formatting = {
                    pid: function(v){ return v;},
                    latency: function(v){
                      var $out = $("<span>").html(UI.format.addUnit(UI.format.number(v),"ms"));
                      $out.find(".unit").append(
                        $("<span>").addClass("info").text("i").hover(function(e){
                          UI.tooltip.show(e,
                            $("<div>").append(
                              $("<h3>").html("Latency: "+$out.html())
                            ).append(
                              $("<p>").text("This the difference between the last sent timestamp and the theoretically highest possible playback position. This is usually mostly jitter buffers.")
                            )
                          )
                        },function(e){
                          UI.tooltip.hide();
                        })
                      );
                      return $out;
                    },
                    active_ms: function(v) { return UI.format.duration(v*1e-3); },
                    bytes: UI.format.bytes,
                    mediatime: function(v){ return UI.format.duration(v*1e-3); },
                    media_tx: function(v){ return UI.format.duration(v*1e-3); },
                    mediatimestamp: function(v){ return UI.format.duration(v*1e-3); },
                    tracks: function(v){ return v.join(", "); },
                    pkt_retrans_count: function(v){
                      return UI.format.number(v || 0);
                    },
                    pkt_loss_count: function(v){
                      return UI.format.number(v || 0)+" ("+UI.format.addUnit(UI.format.number(allValues.pkt_loss_perc || 0),"%")+" over the last "+UI.format.addUnit(5,"s")+")";
                    }
                  };

                  if (this._id in formatting) {
                    this.html(formatting[this._id](val));
                  }

                }
              }
            }
          }

          var $cont = $("<div>").attr("onempty","None.");
          var $table;
          if (type == "Automatic"){
            $table = UI.dynamic({
              create: function(){
                var $table = $("<table>").attr("data-pushtype","auto");
                var $header = $("<tr>");
                $table.append(
                  $("<thead>").append($header)
                ).append(
                  $("<tbody>")
                );
                for (var i in layout) {
                  if (!layout[i]) continue;
                  var cell = $("<th>").addClass("header").text(i).attr("data-index",i);
                  $header.append(cell);
                }
                $header.find("[data-index=\"Actions\"]").removeAttr("data-index").text("");

                UI.sortableItems($table,function(sortby){
                  return $table._children[this.getAttribute("data-pushid")]._children[sortby].raw;
                },{
                  controls: $header[0],
                  sortby: "Stream" in layout ? "Stream" : "Target",
                  sortsave: "sort_autopushes",
                  container: $table[0].children[1] //tbody
                });

                $table[0].filter = function(str){
                  if (typeof str == "undefined") {
                    str = $pushes.find("input.filter").getval();
                    if (!str) str = "";
                  }
                  str = str.toLowerCase();
                  function match(value) {
                    if ((typeof value == "undefined") || (value === null)) return false;
                    return value.toLowerCase().indexOf(str) >= 0;
                  }

                  for (var i in $table._children) {
                    var item = $table._children[i];
                    var push = item.values;
                    if (match(push.stream) || match(push.target) || match(push["x-LSP-notes"])) {
                      item[0].classList.remove("hidden");
                    }
                    else {
                      item[0].classList.add("hidden");
                    }
                  }
                };

                return $table;
              },
              add: {
                create: function(id){
                  var $tr = $("<tr>").attr("data-pushid",id);
                  $tr._children = {};
                  for (var i in layout) {
                    if (!layout[i]) continue;
                    var $td = $("<td>").attr("data-index",i);
                    $tr._children[i] = $td;
                    $tr.append($td);
                    if (layout[i] instanceof jQuery) {
                      $td.html(layout[i].clone(true));
                    }
                    else if (typeof layout[i] == "object") {
                      $td.dynamic = UI.dynamic(layout[i]);
                      $td.html($td.dynamic);
                    }
                  }
                  $tr.click(function(e){
                    if (window.getSelection().toString().length) { return; }
                    var push = $table._children[id].values;
                    UI.navto("Start Push","auto_"+push.id);
                  });
                  $tr.on("contextmenu",function(e){
                    e.preventDefault();
                    var push = $table._children[id].values;

                    var $header = $("<div>").addClass("header");
                    if (!options.stream) {
                      $header.append(
                        $("<span>").text(push.stream)
                      ).append(
                        $("<span>").addClass("unit").text(" » ")
                      );
                    }
                    $header.append(
                      $("<span>").addClass("target").append(
                        $table._children[id]._children["Target"].children().clone()
                      )
                    );
                    if (push["x-LSP-notes"]) {
                      $header.append(
                        $("<div>").addClass("description").text(push["x-LSP-notes"])
                      );
                    }
                    var actions = [];
                    actions.push(["Edit",function(){
                      UI.navto("Start Push","auto_"+push.id);
                    },"Edit","Edit this automatic push"]);
                    actions.push(["Copy target",function(){
                      var me = this;
                      var text = push.target;
                      UI.copy(text).then(function(){
                        me._setText("Copied!")
                        setTimeout(function(){ context_menu.hide(); },300);
                      }).catch(function(e){
                        me._setText("Copy: "+e);
                        setTimeout(function(){ context_menu.hide(); },300);

                        var popup =  UI.popup(UI.buildUI([
                          $("<h1>").text("Copy push target"),{
                            type: "help",
                            help: "Automatic copying of the template to the clipboard failed ("+e+"). Instead you can manually copy from the field below."
                          },{
                            type: "str",
                            label: "Push target",
                            value: text,
                            rows: Math.ceil(text.length/50+2)
                          }
                        ]));
                        popup.element.find("textarea").select();
                      });

                    },"copy","Copy the full target url to the clipboard."]);
                    if (push.deactivated) {
                      actions.push(["Enable",function(){
                        //push.stream has already been edited to have the normal (activated) stream name; just edit to the push object as is
                        var $me = $(this);
                        var o = {};
                        o[push.id] = push;
                        mist.send(function(d){
                          $table.update(d.auto_push);
                          context_menu.hide();
                        },{
                          push_auto_add: o
                        });

                      },"wake","Enable this automatic push"]);
                    }
                    else {
                      actions.push(["Disable",function(){
                        push.stream = "💤deactivated💤_"+push.stream;
                        var o = {};
                        o[push.id] = push;
                        mist.send(function(d){
                          $table.update(d.auto_push);
                          context_menu.hide();
                        },{
                          push_auto_add: o
                        });
                      },"sleep","Disable this automatic push: it will not start new pushes but will remain listed"]);
                    }
                    actions.push(["Remove",function(){
                      if (confirm("Are you sure you want to "+$(this).text().toLowerCase()+" this push?\n"+push.stream+" to "+push.target)) {
                        $(this).html(
                          $("<span>").addClass("red").text("Removing..")
                        );
                        var me = this;
                        mist.send(function(d){
                          $(me).text("Done.");
                          $table.update(d.auto_push);
                          //use the reply to update the automatic pushes list
                          context_menu.hide();
                        },{push_auto_remove:push.id});
                      }
                    },"trash","Remove this automatic push."]);
                    if (options.stop_pushes) {
                      actions.push(["Stop pushes",function(e){
                        e.stopPropagation();

                        var msg = "Are you sure you want to stop all pushes matching \n\""+push.stream+' to '+push.target+"\"?";
                        if (push.stream[0] == "#") {
                          msg = "Are you sure you want to stop all pushes to "+push.target+"\"?";
                        }
                        if (!push.deactivated && (d.push_settings.wait != 0)) {
                          msg += "\n\nRetrying is enabled. That means the push will probably just restart. You'll probably want to set 'Delay before retry' to 0, or deactive the autopush first.";
                        }

                        if (confirm(msg)) {
                          var $button = $(this);
                          var $icon = $button.children().first();
                          $button.text('Stopping pushes..').prepend($icon);
                          //also stop the matching pushes
                          var pushIds = [];
                          var push_list = mist.data.push_list;
                          for (var i in push_list) {
                            if ((push.target == push_list[i][2]) && ((push.stream[0] == "#")  || (push.stream == push_list[i][1]))) {
                              pushIds.push(push_list[i][0]);
                              $pushes.find('table[data-pushtype="active"] tr[data-pushid='+push_list[i][0]+']').html(
                                $('<td colspan=99>').html(
                                  $('<span>').addClass('red').text('Stopping..')
                                )
                              );
                            }
                          }

                          mist.send(function(){
                            $button.text('Stop pushes').prepend($icon);
                            context_menu.hide();
                          },{
                            push_stop: pushIds
                          });
                        }
                      },"stop","Stop all active pushes "+(push.stream[0] == "#" ? "" : "matching '"+push.stream+"'")+" to '"+push.target+"'."]);
                      actions.push(["Stop & remove",function(e){
                        e.stopPropagation();

                        var msg = "Are you sure you want to stop all pushes matching \n\""+push.stream+' to '+push.target+"\",\n and then remove this automatic push?";
                        if (push.stream[0] == "#") {
                          msg = "Are you sure you want to stop all pushes to "+push.target+"\",\n and then remove this automatic push?";
                        }

                        if (confirm(msg)) {
                          var $button = $(this);
                          var $icon = $button.children().first();
                          $button.text('Stopping pushes..').prepend($icon);
                          //also stop the matching pushes
                          var pushIds = [];
                          var push_list = mist.data.push_list;
                          for (var i in push_list) {
                            if ((push.target == push_list[i][2]) && ((push.stream[0] == "#")  || (push.stream == push_list[i][1]))) {
                              pushIds.push(push_list[i][0]);
                              $pushes.find('table[data-pushtype="active"] tr[data-pushid='+push_list[i][0]+']').html(
                                $('<td colspan=99>').html(
                                  $('<span>').addClass('red').text('Stopping..')
                                )
                              );
                            }
                          }

                          mist.send(function(d){
                            $button.text('Stop pushes').prepend($icon);
                            $table.update(d.auto_push);
                            context_menu.hide();
                          },{
                            push_stop: pushIds,
                            push_auto_remove: push.id
                          });
                        }
                      },"stop_remove","Stop all active pushes "+(push.stream[0] == "#" ? "" : "matching '"+push.stream+"'")+" to '"+push.target+"' and then remove this automatic push."]);
                    }

                    var menu = [[$header],actions];

                    context_menu.show(menu,e);
                  });

                  return $tr;
                },
                update: function(push){
                  for (var i in layout) {
                    if (typeof layout[i] == "function") {
                      var newvalue = layout[i](push,type);
                      if (newvalue != this._children[i].raw) {
                        if (newvalue instanceof jQuery) {
                          this._children[i].raw = $("<div>").html(newvalue).prop("innerHTML");
                        }
                        else {
                          this._children[i].raw = newvalue;
                        }
                        this._children[i].html(newvalue);
                        if (i == "Notes") { this._children[i].attr("title",newvalue); }
                      }
                    }
                  }
                  this.sort();
                },
                getEntries: function(values){
                  out = Object.assign({},values);
                  out.deactivated = values.stream.indexOf("💤deactivated💤_") == 0;
                  if (out.deactivated) {
                    out.stream = out.stream.slice(16);
                  }
                  return out;
                }
              },
              update: function(values){
                var $table = this;
                $table.data("values",values);

                function isColumnUsed(column) {
                  for (var i in $table._children) {
                    var $row = $table._children[i];
                    if ($row._children[column].raw != "") {
                      return true;
                    }
                  }
                  return false;
                }

                if (Object.keys(values).length) {
                  if (!$table.parent().length) {
                    $cont.append($table);
                  }
                }
                else if ($table[0].parentNode) {
                  $table[0].parentNode.removeChild($table[0]);
                }
                $table.sort();
                if (options.filter) $table[0].filter();
              },
              values: values,
              getEntries: function(d){
                var out = {};
                var streamnameisbase = false;
                if (options.stream && (options.stream.split("+").length == 1)) {
                  streamnameisbase = true;
                }
                for (var i in d) {
                  var values = d[i];
                  values.id = i;
                  var sn = values.stream.replace("💤deactivated💤_","");
                  //filter out other streams
                  if (!options.stream || (sn == options.stream) || (!streamnameisbase && (sn.split("+")[0] == options.stream))) {
                    out[values.id] = values;
                  }
                }
                return out;
              }

            });
          }
          else {
            $table = UI.dynamic({
              create: function(){
                var $table = $("<table>").attr("data-pushtype","active");
                var $header = $("<tr>");
                $table.append(
                  $("<thead>").append($header)
                ).append(
                  $("<tbody>")
                );
                for (var i in layout) {
                  if (!layout[i]) continue;
                  var cell = $("<th>").addClass("header").text(i).attr("data-index",i);
                  $header.append(cell);
                }
                //it makes no sense to make these columns sortable
                $header.find("[data-index=\"Actions\"]").removeAttr("data-index").text("");

                //add sensible sorting by allowing to choose which stat to sort
                var stored = mist.stored.get()
                if (!("sort_pushes" in stored)) { stored.sort_pushes = {}; }
                var $whichStat = $("<select>").append(
                  $("<option>").text("Pid").val("pid")
                ).append(
                  $("<option>").text("Time active").val("active_seconds")
                ).append(
                  $("<option>").text("Transferred data").val("bytes")
                ).append(
                  $("<option>").text("Transferred media time").val("mediatime")
                ).val(stored.sort_pushes_statistics_type ? stored.sort_pushes_statistics_type : "pid").change(function(){
                  mist.stored.set("sort_pushes_statistics_type",$(this).val());
                  $table.sort("Statistics");
                });

                $header.find("[data-index=\"Statistics\"]").append($whichStat);

                UI.sortableItems($table,function(sortby){
                  if (sortby == "Statistics") {
                    var stats = $table._children[this.getAttribute("data-pushid")].values;
                    if (stats) stats = stats.stats;
                    var which = $whichStat.val();
                    if (stats && (which in stats)) { return stats[which]; }
                    return null;
                  }
                  return $table._children[this.getAttribute("data-pushid")]._children[sortby].raw;
                },{
                  controls: $header[0],
                  sortby: "Statistics", 
                  sortsave: "sort_pushes",
                  container: $table[0].children[1] //tbody
                });

                $table[0].filter = function(str){
                  if (typeof str == "undefined") {
                    str = $pushes.find("input.filter").getval();
                    if (!str) str = "";
                  }
                  str = str.toLowerCase(); 
                  function match(value) {
                    if ((typeof value == "undefined") || (value === null)) return false;
                    return value.toLowerCase().indexOf(str) >= 0;
                  }

                  for (var i in $table._children) {
                    var item = $table._children[i];
                    var push = item.values;
                    if (match(push.stream) || match(push.target) || match(push.resolved_target)) {
                      item[0].classList.remove("hidden");
                    }
                    else {
                      item[0].classList.add("hidden");
                    }
                  }
                }


                return $table;
              },
              add: {
                create: function(id){ 
                  var $tr = $("<tr>").attr("data-pushid",id);
                  $tr._children = {};
                  for (var i in layout) {
                    if (!layout[i]) continue;
                    var $td = $("<td>").attr("data-index",i);
                    $tr._children[i] = $td;
                    $tr.append($td);
                    if (layout[i] instanceof jQuery) {
                      $td.html(layout[i].clone(true));
                    }
                    else if (typeof layout[i] == "object") {
                      $td.dynamic = UI.dynamic(layout[i]);
                      $td.html($td.dynamic);
                    }
                  }

                  $tr.on("contextmenu",function(e){
                    e.preventDefault();
                    var push = $table._children[id].values;

                    var $header = $("<div>").addClass("header");
                    if (!options.stream) {
                      $header.append(
                        $("<span>").text(push.stream)
                      ).append(
                        $("<span>").addClass("unit").text(" » ")
                      );
                    }
                    $header.append(
                      $("<span>").addClass("target").append(
                        $table._children[id]._children["Target"].children().clone()
                      )
                    );
                    var actions = [];
                    actions.push(["Stop",function(){
                      if (confirm("Are you sure you want to "+$(this).text().toLowerCase()+" this push?\n"+push.stream+" to "+push.target)) {
                        $(this).html(
                          $("<span>").addClass("red").text("Stopping..")
                        );
                        mist.send(function(d){ 
                          //done
                        },{'push_stop':[push.id]});
                      }
                    },"trash","Stop this push."]);
                    var menu = [[$header],actions];

                    context_menu.show(menu,e);
                  });
                  return $tr;
                },
                update: function(push){
                  for (var i in layout) {
                    if (typeof layout[i] == "function") {
                      var newvalue = layout[i](push,type);
                      if (newvalue != this._children[i].raw) {
                        this._children[i].html(newvalue);
                        this._children[i].raw = newvalue;
                      }
                    }
                    else if ((i == "Statistics") && (layout[i])) {
                      var v = {
                        pid: push.id
                      };
                      if (push.stats) { v = Object.assign(v,push.stats); }
                      v.logs = push.logs;
                      this._children[i].dynamic.update(v);
                    }
                  }
                }
              },
              update: function(values){
                var $table = this;
                $table.data("values",values);
                if (Object.keys(values).length) {
                  if (!$table.parent().length) {
                    $cont.append($table);
                  }
                }
                else if ($table[0].parentNode) {
                  $table[0].parentNode.removeChild($table[0]);
                }
                $table.sort();
                if (options.filter) $table[0].filter();
              },
              values: values,
              getEntries: function(d){
                var out = {};
                var streamnameisbase = false;
                if (options.stream && (options.stream.split("+").length == 1)) {
                  streamnameisbase = true;
                }
                for (var i in d) {
                  var values = mist.convertPushArr2Obj(d[i]);
                  //filter out other streams
                  if (!options.stream || (values.stream == options.stream) || (!streamnameisbase && (values.stream.split("+")[0] == options.stream))) {
                    out[values.id] = values;
                  }
                }
                return out;
              }
            });
          }
          $cont.update = function(){ return $table.update.apply($table,arguments); }
          return $cont;
        }


        $pushes.append(
          $("<section>").append(
            $("<h3>").text("Automatic pushes")
          ).append(
            $("<div>").addClass("buttons").append(
              $("<button>").attr("data-icon","plus").text("Add an automatic push").click(function(){
                UI.navto("Start Push","auto");
              })
            )
          ).append(
            buildPushCont("Automatic",d.auto_push)
          )
        );

        var pushes_container = buildPushCont("Manual",d.push_list);
        $pushes.append(
          $("<section>").append(
            $("<h3>").text("Active pushes")
          ).append(
            $("<div>").addClass("buttons").append(
              $("<button>").attr("data-icon","plus").text("Start a push").click(function(){
                UI.navto("Start Push");
              })
            ).append(
              options.stop_pushes ? 
              $("<button>").attr("data-icon","stop").text("Stop all pushes listed below").click(function(){
                var pushIds = [];
                var $table = $pushes.find("table[data-pushtype=\"active\"]");
                var $rows = $table.find("tr[data-pushid]:visible");
                $rows.each(function(i,row){
                  pushIds.push($(this).attr("data-pushid"))
                });
                if (pushIds.length) {
                  if (confirm("Are you sure you want to stop "+pushIds.length+" push"+(pushIds.length > 1 ? "es?" : "?"))) {
                    $rows.each(function(){
                      $(this).html(
                        $('<td colspan=99>').html(
                          $('<span>').addClass('red').text('Stopping..')
                        )
                      )
                    });
                    mist.send(function(){
                      //done
                    },{
                      push_stop: pushIds
                    });
                  }
                }
              })
              : ""
            )
          ).append(
            pushes_container
          )
        );

        UI.sockets.http.api.subscribe(function(d){
          pushes_container.update(d.push_list);
          mist.data.push_list = d.push_list;
        },{push_list:1});

        if (options.collapsible) {
          $pushes.children("section").not(".context_menu").addClass("collapsible").addClass("expanded").each(function(){
            $(this).children().first().click(function(){
              $(this).closest("section.collapsible").toggleClass("expanded")
            });
          });
        }


      },{push_auto_list:1,push_list:1,push_settings:1});

      return $("<section>").addClass("pushes").append(
        $("<h3>").text("Pushes and recordings")
      ).append(
        $pushes
      );

    },
    streamkeys: function(streamname,onsave){
      if (!onsave) {
        onsave = function(){
          UI.showTab("Stream keys");
        }
      }

      let $c = $("<section>").text("Loading..");
      mist.send(function(d){ //request current stream keys + active streams
        var saveas = {};
        let current_streams = $.extend({},mist.data.streams);
        if (d.active_streams) {
          for (let streamname of d.active_streams) {
            var streambase = streamname.split("+")[0];

            if (streambase in mist.data.streams) {
              if ((streambase != streamname) && !(streamname in current_streams)) {
                //it's a new wildcard stream
                current_streams[streamname] = $.extend({},mist.data.streams[streambase]);
                current_streams[streamname].name = streamname;
              }
              current_streams[streamname].online = 1;
            }
          }
        }

        $c.html(
          $("<h1>").text("Manage stream keys"+(streamname ? " for '"+streamname+"'" : "")).css("margin-top",0)
        ).append(UI.buildUI([{
          type: "help",
          help: "Stream keys are a method to bypass all security and allow an incoming push for the given stream. If a token that matches a stream is used it will be accepted. This will even apply to vod or unconfigured streams, making them active as a live stream with default settings. <br>Note: A stream with source 'push://' and a stream key would accept both its stream name and stream key for input. To avoid this enable the 'Require stream key' option."
        },
        UI.buildUI([$("<h3>").text("Add stream key(s)"),{
            label: "For stream name",
            type: "str",
            pointer: { main: saveas, index: "stream" },
            prefix: streamname ? streamname+"+" : false,
            validate: streamname ? [] : ["required","streamname_with_wildcard",function(val){
              //notify if this stream does not exist
              if (!(val in current_streams)) {
                return {
                  msg: "This stream does not exist (yet). You can add a key for it anyway.",
                  "break": false
                };
              }
              //warn if this stream exists but does not have a push:// source
              if (current_streams[val].source?.slice(0,7) != "push://") {
                return {
                  msg: "It is not possible to push into this stream. Pushing to this stream will only work if it is not already active.",
                  classes: ["orange"],
                  "break": false
                };
              }

            }],
            help: "Enter the stream for which this stream key will be valid. You can enter a stream with wildcard. You can add stream keys for streams that do not exist yet.",
            datalist: function(){
              let out = [];
              for (const stream in current_streams) {
                if (current_streams[stream].source?.slice(0,7) == "push://") {
                  if (streamname) {
                    if (stream.split("+")[0] == streamname) {
                      out.push(stream.split("+").slice(1).join("+"));
                    }
                  }
                  else {
                    out.push(stream);
                  }
                }
              }
              return out;
            }()
          },{
            label: "Stream key(s)",
            type: "inputlist",
            classes: ["streamkeysinputlist"],
            pointer: { main: saveas, index: "keys" },
            validate: ["required"],
            help: "Enter one or more keys",
            input: {
              type: "str",
              clipboard: true,
              maxlength: 256,
              validate: [function(val,me){
                if (d.streamkeys && (val in d.streamkeys)) {
                  //duplicates in the current field do not need to be tested - they're all for the same stream so it won't be an issue
                  return {
                    msg: "The key '"+val+"' is already in use. Duplicates are not allowed.",
                    classes: ["red"]
                  };
                }
                if (val.length && !val.match(/^[0-9a-z]+$/i)) {
                  return {
                    msg: "The key '"+val+"' contains special characters. We recommend not using these as some video streaming protocols do not accept them.",
                    classes: ["orange"],
                    "break": false
                  }
                }
              }],
              unit: $("<button>").text("Generate").click(function(){
                let $field = $(this).closest(".field_container").find(".field");

                function getRandomVals(n) {
                  function getRandomVal() {
                    const chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
                    return chars[Math.floor(Math.random()*chars.length)];
                  }
                  let str = "";
                  while (str.length < n) {
                    str += getRandomVal();
                  }
                  return str;
                }

                function apply() {
                  $field.setval(getRandomVals(32));
                  if ($field.data("validate")($field)) { 
                    //the listitem is invalid
                    apply();
                  }
                }
                apply();

                $field.trigger("keyup");

              })
            }
          },{
            type: "buttons",
            buttons: [{
              type: "save",
              icon: "plus",
              label: "Add",
              "function": function(){
                let send = {};
                for (let key of saveas.keys) {
                  send[key] = streamname ? (saveas.stream ? streamname+"+"+saveas.stream : streamname) : saveas.stream;
                }
                mist.send(function(){
                  onsave();
                },{streamkey_add:send});
              }
            }]
          }]),
          $("<h3>").text("Current stream keys"+(streamname ? " for '"+streamname+"'" : "")),{
            type: "help",
            help: "Note: the stream status on this page does not update automatically."
          },{
            label: "Filter",
            help: "Only streams or keys that contain the text you enter here will be displayed.",
            "function": function(){
              $(this).closest(".input_container").find(".streamkeys")?.[0]?.filter($(this).getval());
            },
            css: { marginBottom: "3em" }
          },
          function(){
            const $cont = $("<div>").addClass("streamkeys");

            let bystreams = {};
            //group keys by stream
            for (key in d.streamkeys) {
              if (streamname) {
                if (d.streamkeys[key].split("+")[0] != streamname) continue;
              }
              if (!(d.streamkeys[key] in bystreams)) {
                bystreams[d.streamkeys[key]] = [];
              }
              bystreams[d.streamkeys[key]].push(key);
            }

            //sort streams
            let streams = Object.keys(bystreams).sort();
            function formatStream(streamname) {
              const $cont = $("<span>").addClass("clickable").text(streamname);
              if (streamname.indexOf("+") >= 0) {
                const split = streamname.split("+");
                $cont.html(
                  $("<span>").addClass("wildparent").text(split[0]+"+")
                ).append(split.slice(1));
              }
              $cont.on("contextmenu",function(e){
                e.preventDefault();

                const header = [
                  $("<div>").addClass("header").html($cont.html())
                ];
                const gototabs = [
                  [$("<span>").html("Edit "+(streamname.indexOf("+") < 0 ? "stream" : "<b>"+streamname.split("+")[0]+"</b>")),function(){ UI.navto("Edit",streamname); },"Edit","Change the settings of this stream."],
                  ["Stream status",function(){ UI.navto("Status",streamname); },"Status","See more details about the status of this stream."],
                  ["Preview stream",function(){ UI.navto("Preview",streamname); },"Preview","Watch the stream."]
                ];
                if (!(streamname.split("+")[0] in mist.data.streams)) {
                  //stream does not exist yet, change "Edit" to "Create"
                  gototabs.shift();
                  gototabs.unshift([$("<span>").html("Create "+(streamname.indexOf("+") < 0 ? "stream" : "<b>"+streamname.split("+")[0]+"</b>")),function(){ UI.navto("Edit",streamname); },"Edit","Create this stream."]);
                }

                const menu = [header];
                menu.push(gototabs);

                context_menu.show(menu,e);

              }).click(function(e){
                this.dispatchEvent(new MouseEvent("contextmenu",e));
                e.stopPropagation();
              });
              return $cont;
            }

            //display a container per stream
            for (let stream of streams) {
              $cont.append(
                $("<div>").addClass("stream").attr("data-stream",stream).append(
                  $("<div>").addClass("header").addClass("activestream").append(
                    function(){
                      let active = false;
                      if (d.active_streams?.indexOf(stream) >= 0) {
                        active = true;
                      }
                      return $("<div>").attr("data-streamstatus",active ? 4 : 0).attr("title",active ? "Active" : "Inactive");
                    }()
                  ).append(
                    formatStream(stream)
                  ).append(
                    function(){
                      let kind = "Unconfigured";
                      let base = stream.split("+")[0];
                      if (base in current_streams) {
                        if (current_streams[base].source.slice(0,7) != "push://") {
                          kind = "Using stream key will override configured source"
                        }
                        else if (current_streams[base].source.slice(0,16) == "push://invalid,host") {
                          kind = "Stream key only";
                        }
                        else {
                          kind = "Configured";
                        }
                      }
                      return $("<span>").text("("+kind+")").addClass("description").css("font-weight","normal")
                    }()
                  ).append(
                    $("<button>").attr("data-icon","trash").text("Delete all").click(function(){
                      if (confirm("Are you sure you want to remove all "+bystreams[stream].length+" keys of the stream '"+stream+"'?")) {
                        let $me = $(this);
                        mist.send(function(){
                          let $streamkeys = $me.closest(".streamkeys");
                          $me.closest(".stream").remove();
                          //update pagecontrol
                          $streamkeys[0].show_page();
                        },{
                          streamkey_del: Object.values(bystreams[stream])
                        });
                      }
                    })
                  )
                ).append(
                  function(){ 
                    const $cont = $("<div>").addClass("keys");
                    for (let key of bystreams[stream]) {
                      $cont.append(
                        $("<span>").addClass("key").attr("data-key",key).append(
                          $("<span>").text(key).addClass("clickable").on("contextmenu",function(e){
                            e.preventDefault();
                            let $me = $(this);

                            let raw = UI.updateLiveStreamHint(stream,"push://invalid,host","raw",false,[key]);
                            function createCopyEntry(label,text) {
                              return [
                                "Copy "+label,function(){
                                  UI.copy(text).then(()=>{
                                    this._setText("Copied!")
                                    setTimeout(function(){ context_menu.hide(); },300);
                                  }).catch((e)=>{
                                    this._setText("Copy: "+e);
                                    setTimeout(function(){ context_menu.hide(); },300);

                                    var popup =  UI.popup(UI.buildUI([
                                      $("<h1>").text("Copy to clipboard"),{
                                        type: "help",
                                        help: "Automatic copying failed ("+e+"). Instead you can manually copy from the field below."
                                      },{
                                        type: "str",
                                        label: "Text",
                                        value: text,
                                        rows: Math.ceil(text.length/50+2)
                                      }
                                    ]));
                                    popup.element.find("textarea").select();
                                  });
                                },"copy","Copy the "+label+" to the clipboard:\n"+text
                              ];
                            }

                            let fields = [];
                            fields.push(createCopyEntry("key",key));
                            for (const label in raw) {
                              if (Array.isArray(raw[label])) {
                                fields.push(createCopyEntry(label+" url",raw[label][0]));
                                //there will always only be a signle array entry in raw[label] as we have specified only the currently selected stream key
                              }
                              else if (label == "RTMP") {
                                fields.push(createCopyEntry("full RTMP url",raw.RTMP.full_url[0]));
                                fields.push(createCopyEntry("RTMP url (without key)",Object.keys(raw.RTMP.pairs)[0]));
                              }
                            }

                            context_menu.show([
                              [
                                $("<div>").addClass("header").append(
                                  $("<div>").text(key).css("font-family","monospace")
                                ).append(
                                  $("<div>").text("for ").append(formatStream(stream).removeClass("clickable"))
                                )
                              ],
                              fields, [
                                ["Delete this stream key", function(){
                                  if (confirm("Are you sure you want to remove the key '"+key+"'?")) {
                                    mist.send(function(){
                                      let $c = $me.closest(".keys");
                                      let $streamkeys = $c.closest(".streamkeys");
                                      $me.closest(".key").remove();
                                      if (!$c.children().length) {
                                        $c.closest(".stream").remove();
                                      }
                                      //update pagecontrol
                                      $streamkeys[0].show_page();
                                    },{
                                      streamkey_del: key
                                    });
                                  }

                                },"trash","Delete this stream key"]
                              ]
                            ],e);

                          }).click(function(e){
                            this.dispatchEvent(new MouseEvent("contextmenu",e));
                            e.stopPropagation();
                          })
                        ).append(
                          $("<button>").attr("data-icon","trash").attr("title","Delete").click(function(){
                            if (confirm("Are you sure you want to remove the key '"+key+"'?")) {
                              let $me = $(this);
                              mist.send(function(){
                                let $c = $me.closest(".keys");
                                let $streamkeys = $c.closest(".streamkeys");
                                $me.closest(".key").remove();
                                if (!$c.children().length) {
                                  $c.closest(".stream").remove();
                                }
                                //update pagecontrol
                                $streamkeys[0].show_page();
                              },{
                                streamkey_del: key
                              });
                            }
                          })
                        )
                      );
                    }
                    return $cont;
                  }()
                )
              );
            }
      
            $cont[0].filter = function(str){
              str = str.toLowerCase();

              //first, iterate over the streams and hide any that do not match
              for (var i = 0; i < this.children.length; i++) {
                var item = this.children[i];
                if (item.getAttribute("data-stream").toLowerCase().indexOf(str) >= 0) {
                  item.classList.remove("hidden");
                  item.classList.add("showAllKeys")
                }
                else {
                  item.classList.add("hidden");
                  item.classList.remove("showAllKeys");
                }
              }

              //now, iterate over the keys and hide any that do not match
              //if the key matches and its parent is hidden, show it
              const keys = this.querySelectorAll(".stream .keys .key");
              for (let key of keys) {
                if (key.getAttribute("data-key").toLowerCase().indexOf(str) >= 0) {
                  key.classList.remove("hidden");
                  key.closest(".stream").classList.remove("hidden");
                }
                else {
                  key.classList.add("hidden");
                }
              }

              $cont[0].show_page();
            }
      
            return $cont;
          }()
        ]));

        const $pagecontrol = UI.pagecontrol($c.find(".input_container > .streamkeys")[0],mist.stored.get()?.streamkeys_pagesize || 5);
        $c.append($pagecontrol);

        //save selected page size
        $pagecontrol.elements.pagelength.change(function(){
          mist.stored.set("streamkeys_pagesize",$(this).val());
        });

        const context_menu = new UI.context_menu();
        $c.append(context_menu.ele);

        $c.find(".field").first().focus();
      },{ streamkeys: true, active_streams: true, capabilities: true });
      return $c;
    }
  },
  sockets: {
    http_host: null,
    http: {
      api: {
        command: {},
        listeners: [],
        interval: false,
        get: function(){
          var me = this;
          mist.send(function(d){
            for (var i in me.listeners) {
              me.listeners[i](d);
            }
          },me.command);
        }, 
        init: function(){
          var me = this;
          me.get();
          me.interval = UI.interval.set(function(){
            me.get();
          },5e3);
        },
        subscribe: function(callback,command){
          this.command = Object.assign(this.command,command);
          if (!this.interval || !(this.interval in UI.interval.list)) {
            this.listeners = [];
            this.listeners.push(callback);
            this.init();
          }
          else {
            this.listeners.push(callback);
            this.get();
          }
        }
      },
      clients: {
        requests: [],
        listeners: [],
        interval: false,
        subscribe: function(callback,request){
          if (!this.interval || !(this.interval in UI.interval.list)) {
            this.listeners = [];
            this.requests = [];
          }

          if (!("time" in request)) { request.time = -3; }
          this.requests.push(request);
          this.listeners.push(callback);
          var me = this;

          if (this.listeners.length == 1) {
            //we only have to do this once because {clients: this.requests} is a pointer and the callback loops over this.listeners
            UI.sockets.http.api.subscribe(function(d){
              for (var i in d.clients) {
                var res = d.clients[i];
                //make it prettier
                var out = { data: [], time: res.time };
                for (var j in res.data) {
                  var entry = {};
                  for (var k in res.fields) {
                    var key = res.fields[k];
                    entry[key] = res.data[j][k];
                  }
                  out.data.push(entry);
                }

                me.listeners[i].call(null,out);
              }
            },{clients:this.requests});
            this.interval = UI.sockets.http.api.interval;
          }
          else {
            //execute the api call right now
            UI.sockets.http.api.get();
          }
        }
      },
      player: function(callback,errorCallback){
        if (!mistPlay) {
          if (!UI.sockets.http_host) {
            errorCallback("Could not find player.js: MistServer host unknown");
          }
          var url = UI.sockets.http_host+"player.js";
          $.ajax({
            type: "GET",
            url: url,
            success: function(d){
              callback();
            },
            error: function(){
              errorCallback("Error while retrieving player.js from "+url);
            }
          });
        }
        else { callback(); }
      }
    },
    ws: {
      info_json: {
        children: {},
        init: function(url){
          var ws = UI.websockets.create(url);
          this.children[url] = {
            ws: false,
            messages: [],
            listeners: []
          };
          this.children[url].ws = ws;
          var me = this;
          ws.onmessage = function(d){
            var data = JSON.parse(d.data);
            if (url in me.children) {
              me.children[url].messages.push(data);
              for (var i in me.children[url].listeners) {
                me.children[url].listeners[i](data);
              }
            }
          }
          ws.onerror = function(e){
            if (url in me.children) {
              for (var i in me.children[url].listeners) {
                me.children[url].listeners[i](e);
              }
            }
          };
          ws.cleanup = function(){
            //remove self from info_json.children[url]
            delete me.children[url];
            ws.onclose = function(){}; //remove onclose triggering nother remove of what could be a new instance
            ws.onmessage = function(){};
          }
          var close = ws.close;
          ws.close = function(){
            //to prevent race conditions, overwrite the websocket close function to remove itself from the list before closing is complete
            ws.cleanup();
            return close.apply(this,arguments);
          };
          ws.onclose = function(){
            ws.cleanup();
          };
        },
        subscribe: function(callback,streamname,url,params){
          if (!callback) { throw "Callback function not specified."; }
          if (!streamname && !url) { throw "Stream name not specified."; }
          if (!params) { params = ""; }
          if (!url) {
            if (!UI.sockets.http_host) {
              var me = this;
              var args = arguments;
              UI.modules.stream.findMist(function(url){
                me.subscribe.apply(me,args);
              });  
              return;
            }
            url = UI.sockets.http_host.replace(/^http/,"ws") + "json_" + encodeURIComponent(streamname) + ".js"+params;  
          }

          if (!(url in this.children) || (this.children[url].ws.readyState > 1)) {
            this.init(url); 
          }
          for (var i in this.children[url].messages) {
            callback(this.children[url].messages[i]); //replay history
          }
          this.children[url].listeners.push(callback);
        }
      },
      active_streams: {
        ws: false,
        listeners: [],
        messages: [],
        init: function(error_callback){
          var url = parseURL(mist.user.host);
          url = parseURL(mist.user.host,{pathname:url.pathname.replace(/\/api$/,"")+"/ws",search:"?logs=100&accs=100&streams=1"});
          var apiWs = UI.websockets.create(url.full.replace(/^http/,"ws"),error_callback);
          this.ws = apiWs;
          var me = this;
          apiWs.authState = 0;
          apiWs.onmessage = function(d){
            var da = JSON.parse(d.data);
            var type = da[0];
            var data = da[1];

            if (type == "auth") {
              if (data === true) { this.authState = 2; }
              else if (data === false) {
                this.send(JSON.stringify(["auth",{
                  password: MD5(mist.user.password+mist.user.authstring),
                  username: mist.user.name
                }]));
                this.authState = 1;
              }
              else if (typeof data == "object") {
                if ("challenge" in data) {
                  this.send(JSON.stringify(["auth",{
                    password: MD5(mist.user.password+data.challenge),
                    username: mist.user.name
                  }]));
                  this.authState = 1;
                }
                else if (("status" in data) && (data.status == "OK")) {
                  this.authState = 2;
                }
              }
              return;
            }
            me.messages.push([type,data]);
            for (var i in me.listeners){
              me.listeners[i](type,data);
            }
          }
          /*apiWs.onopen = function(){
          };
          apiWs.onclose = function(){
          };*/
          var close = apiWs.close;
          apiWs.close = function(){
            //to prevent race conditions, overwrite the websocket close function to remove itself from the list before closing is complete
            me.listeners = [];
            me.messages = [];
            me.ws = false;
            return close.apply(this,arguments);
          };
        },
        subscribe: function(callback){
          for (var i in this.messages) {
            callback(this.messages[i][0],this.messages[i][1]);
          }
          var me = this;
          if (!this.ws || (this.ws.readyState > 1)) { 
            this.init(function(){
              for (var i in me.listeners) {
                me.listeners[i]("error","An error occured: failed to connect to the active streams api websocket.");
              }
            }); 
          }
          this.listeners.push(callback);
        }
      }
    }
  }
};

if (!('origin' in location)) {
  location.origin = location.protocol+'//';
}
var host;
if (location.origin == 'file://') {
  host = 'http://localhost:4242/api';
}
else {
  host = location.origin+location.pathname.replace(/\/+$/, "")+'/api';
}
var mist = {
  data: {},
  user: {
    name: '',
    password: '',
    host: host
  },
  send: function(callback,sendData,opts){
    sendData = sendData || {};
    opts = opts || {};
    opts = $.extend(true,{
      timeOut: 30e3,
      sendData: sendData
    },opts);
    var data = {
      authorize: {
        password: (mist.user.authstring ? MD5(mist.user.password+mist.user.authstring) : ''),
        username: mist.user.name
      }
    };
    $.extend(true,data,sendData);
    log('Send',$.extend(true,{},sendData));
    var obj = {
      url: mist.user.host,
      type: 'POST',
      contentType: "application/json",
      data: JSON.stringify(data),
      dataType: 'json',
      crossDomain: true,
      timeout: opts.timeout*1000,
      async: true,
      error: function(jqXHR,textStatus,errorThrown){
        console.warn("connection failed :(",errorThrown);
        
        //connection failed
        delete mist.user.loggedin;
        
        if (!opts.hide) {
          switch (textStatus) {
            case 'timeout':
              textStatus = $('<i>').text('The connection timed out. ');
              break;
            case 'abort':
              textStatus = $('<i>').text('The connection was aborted. ');
              break;
            default:
              textStatus = $('<i>').text(textStatus+'. ').css('text-transform','capitalize');
          }
          $('#message').addClass('red').text('An error occurred while attempting to communicate with MistServer:').append(
            $('<br>')
          ).append(
            $("<span>").text(textStatus)
          ).append(
            $('<a>').text('Send server request again').click(function(){
              mist.send(callback,sendData,opts);
            })
          );
        }
        
        UI.navto('Login');
      },
      success: function(d,textstatus,request){
        log('Receive',$.extend(true,{},d),'as reply to',opts.sendData);
        mist.lastrequest = request; //store request so that we can read headers of it elsewhere (Status for example to look for X-Mst-Path)
        delete mist.user.loggedin;
        switch (d.authorize.status) {
          case 'OK':
            //communication succesful
            
            //fix the weird ass incomplete list stream shit
            if ('streams' in d) {
              if (d.streams) {
                if ('incomplete list' in d.streams) {
                  delete d.streams['incomplete list'];
                  $.extend(mist.data.streams,d.streams);
                }
                else {
                  mist.data.streams = d.streams;
                }
              }
              else {
                mist.data.streams = {};
              }
            }
            
            //remove everything we don't care about
            var save = $.extend({},d);
            var keep = ['config','capabilities','ui_settings','LTS','active_streams','browse','log','totals','bandwidth','variable_list','external_writer_list','streamkeys']; //streams was already copied above
            for (var i in save) {
              if (keep.indexOf(i) == -1) {
                delete save[i];
              }
            }
            if (("bandwidth" in data) && (!("bandwidth" in d))) {
              save.bandwidth = null;
            }
            if (opts.sendData.capabilities && (opts.sendData.capabilities !== true)) {
              //a specific type of capabilities was requested. Don't overwrite generic capability data
              delete save.capabilities;
            }
            
            $.extend(mist.data,save);
            
            mist.user.loggedin = true;
            UI.elements.connection.status.text('Connected').removeClass('red').addClass('green');
            UI.elements.connection.user_and_host.text(mist.user.name+' @ '+mist.user.host);
            UI.elements.connection.msg.removeClass('red').text('Last communication with the server at '+UI.format.time((new Date).getTime()/1000));
            
            
            if (d.log) {
              var lastlog = d.log[d.log.length-1];
              UI.elements.connection.msg.append($('<br>')).append(
                $("<span>").text(
                  'Last log entry: '+UI.format.time(lastlog[0])+' ['+lastlog[1]+'] '+lastlog[2]
                )
              );
            }
            if ('totals' in d) {
              
              //reformat to something more readable
              function reformat(main) {
                function insertZero(overridetime) {
                  if (typeof overridetime == 'undefined') {
                    overridetime = time;
                  }
                  
                  for (var j in main.fields) {
                    obj[main.fields[j]].push([time,0]);
                  }
                }
                
                var obj = {};
                for (var i in main.fields) {
                  obj[main.fields[i]] = [];
                }
                var insert = 0;
                var time;
                
                if (!main.data) {
                  //no data
                  time = (mist.data.config.time - 600)*1e3;
                  insertZero();
                  time = (mist.data.config.time - 15)*1e3;
                  insertZero();
                }
                else {
                  //leading 0?
                  if (main.start > (mist.data.config.time - 600)) {
                    time = (mist.data.config.time - 600)*1e3;
                    insertZero();
                    time = main.start*1e3;
                    insertZero();
                  }
                  else {
                    time = main.start*1e3;
                  }
                  
                  for (var i in main.data) { //i == time index
                    //obtain timestamp
                    if (i == 0) {
                      var time = main.start*1e3;
                      var interval_n = 0;
                    }
                    else {
                      time += main.interval[interval_n][1]*1e3; //increase time with delta
                      main.interval[interval_n][0]--; //amount of times to use delta
                      if (main.interval[interval_n][0] <= 0) {
                        interval_n++; //go to next interval
                        
                        //insert zeros between the intervals
                        //+= 2 in case the interval is only 1 long
                        if (interval_n < main.interval.length-1) { insert += 2; }
                      }
                    }
                    
                    if (insert % 2 == 1) {
                      //modulus in case the interval was only 1 long; prevents diagonal lines
                      insertZero();
                      insert--;
                    }
                    
                    for (var j in main.data[i]) { //j == field index
                      //write datapoint in format [timestamp,value]
                      obj[main.fields[j]].push([time,main.data[i][j]]);
                    }
                    
                    if (insert) {
                      insertZero();
                      insert--;
                    }
                  }
                  
                  //trailing 0?
                  if ((mist.data.config.time - main.end) > 20) {
                    insertZero();
                    time = (mist.data.config.time -15) * 1e3;
                    insertZero();
                  }
                }
                return obj;
              }
              function savereadable(streams,protocols,data){
                var obj = reformat(data);
                stream = (streams ? streams.join(' ') : 'all_streams');
                protocol = (protocols ? protocols.join('_') : 'all_protocols');
                
                if (!(stream in mist.data.totals)) {
                  mist.data.totals[stream] = {};
                }
                if (!(protocol in mist.data.totals[stream])) {
                  mist.data.totals[stream][protocol] = {};
                }
                $.extend(mist.data.totals[stream][protocol],obj);
              }
              
              mist.data.totals = {};
              if ('fields' in d.totals) {
                //only one totals object
                savereadable(sendData.totals.streams,sendData.totals.protocols,d.totals);
              }
              else {
                for (var i in d.totals) {
                  savereadable(sendData.totals[i].streams,sendData.totals[i].protocols,d.totals[i]);
                }
              }
            }
            
            if (callback) { callback(d,opts); }
            break;
          case 'CHALL':
            if (d.authorize.challenge == mist.user.authstring) {
              //invalid login details
              if (mist.user.password != '') {
                UI.elements.connection.msg.text('The credentials you provided are incorrect.').addClass('red');
              }
              UI.navto('Login');
            }
            else if (mist.user.password == '') {
              //credentials have not yet been entered
              UI.navto('Login');
            }
            else{
              //log in with new authstring
              mist.user.authstring = d.authorize.challenge;
              mist.send(callback,sendData,opts);
              
              //save the current settings to this session (note: session is renewed when a new tab or window is opened, but not through refreshes)
              var store = {
                host: mist.user.host,
                name: mist.user.name,
                password: mist.user.password
              };
              sessionStorage.setItem('mistLogin',JSON.stringify(store));
            }
            break;
          case 'NOACC':
            //go to create account
            UI.navto('Create a new account');
            break;
          case 'ACC_MADE':
            //the new account was created, now get the data
            delete sendData.authorize;
            mist.send(callback,sendData,opts);
            break;
          default:
            //connection failed
            UI.navto('Login');
        }
      }
    };
    
    if (!opts.hide) {
      UI.elements.connection.msg.removeClass('red').text('Data sent, waiting for a reply..').append(
        $('<br>')
      ).append(
        $('<a>').text('Cancel request').click(function(){
          jqxhr.abort();
        })
      );
    }
    
    var jqxhr = $.ajax(obj);
  },
  inputMatch: function(match,string){
    if (typeof match == 'undefined') { return false; }
    if (typeof match == 'string') {
      match = [match];
    }
    for (var s in match){
      var m = match[s];
      var str = string;
      if (m.slice(-1) == "?") {
        m = m.slice(0,-1);
        str = str.split("?")[0];
      }
      var query = m.replace(/[^\w\s]/g,'\\$&'); //prefix any special chars with a \
      query = query.replace(/\\\*/g,'.*'); //replace * with any amount of .*
      var regex = new RegExp('^(?:[a-zA-Z]\:)?'+query+'(?:\\?[^\\?]*)?$','i'); //case insensitive, and ignore everything after the last ?
      if (regex.test(str)){
        return true;
      }
    }
    return false;
  },
  convertBuildOptions: function(input,saveas) {
    var build = [];
    var type = ['required','optional'];
    if ('desc' in input) {
      build.push({
        type: 'help',
        help: input.desc
      });
    }
    
    function processEle(j,i,ele) {
      var obj = {
        label: UI.format.capital((ele.name ? ele.name : i)),
        pointer: {
          main: saveas,
          index: i
        },
        validate: []
      };
      if ((type[j] == 'required') && ((!('default' in ele)) || (ele['default'] == ''))) {
        obj.validate.push('required');
      }
      if ('default' in ele) {
        obj.placeholder = ele['default'];
        if (ele.type == "select") {
          for (var k in ele.select) {
            if (ele.select[k][0] == ele["default"]) {
              obj.placeholder = ele.select[k][1];
              break;
            }
          }
        }
      }
      if ('help' in ele) {
        obj.help = ele.help;
      }
      if ('unit' in ele) {
        obj.unit = ele.unit;
      }
      if ('placeholder' in ele) {
        obj.placeholder = ele.placeholder;
      }
      if ("datalist" in ele) {
        obj.datalist = ele.datalist; 
      }
      if ("type" in ele) {
        switch (ele.type) {
          case 'int':
            obj.type = 'int';
            if ("max" in ele) { obj.max = ele.max; }
            if ("min" in ele) { obj.min = ele.min; }
            break;
          case 'uint':
            obj.type = 'int';
            obj.min = 0;
            if ("max" in ele) { obj.max = ele.max; }
            if ("min" in ele) { obj.min = Math.max(obj.min,ele.min); }
            break;
          case 'radioselect':
            obj.type = 'radioselect';
            obj.radioselect = ele.radioselect;
            break;
          case 'select':
            obj.type = 'select';
            obj.select = ele.select.slice(0);
            if (obj.validate.indexOf("required") < 0) {
              obj.select.unshift(["",("placeholder" in obj ? "Default ("+obj.placeholder+")" : "" )]);
            }
            break;
          case 'sublist': {
            obj.type = 'sublist';
            //var subele = Object.assign({},ele);
            //delete subele.type;
            obj.saveas = {};
            obj.itemLabel = ele.itemLabel;
            obj.sublist = mist.convertBuildOptions(ele,obj.saveas);
            break;
          }
          case 'group': {
            obj.type = "group";
            obj.label = ele.name;
            obj.options = mist.convertBuildOptions({
              optional: ele.options
            },saveas);
            obj.options = obj.options.slice(1); //remove h4 "Optional parameters"
            break;
          }
          case 'bool': {
            obj.type = 'checkbox';
            break;
          }
          case 'unixtime': {
            obj.type = 'unix';
            break;
          }
          case 'inputlist': {
            obj.type = "inputlist";
            if ("input" in ele) obj.input = ele.input;
            break;
          }
          case 'json':
          case 'debug':
          case 'inputlist':
          case 'browse': {
            obj.type = ele.type;
            break;
          }
          default:
            obj.type = 'str';
            if ("minlength" in ele) { obj.minlength = ele.minlength; }
            if ("maxlength" in ele) { obj.maxlength = ele.maxlength; }
            break;
        }
      }
      else {
        obj.type = "checkbox";
      }
      if ("format" in ele) {
        switch (ele.format) {
          case "set_or_unset": {
            //whatever is set as 'postSave' will be called by the save button after validation and after the value has been saved to whatever is set as the pointer
            obj['postSave'] = function(){
              //if the value evaluates to false, remove it from the parent object
              var pointer = $(this).data('pointer');
              if (!pointer.main[pointer.index]) {
                delete pointer.main[pointer.index];
              }
            };

            break;
          }
        }
      }
      if ("influences" in ele) {
        obj["function"] = function(){
          var $cont = $(this).closest(".UIelement");
          var style = $cont.find("style");
          if (!style.length) {
            style = $("<style>").addClass("dependencies")[0];
            $cont.append(style);
          }
          else {
            style = style[0];
          }
          style.innerHTML = '.UIelement[data-dependent-'+i+']:not([data-dependent-'+i+'~="'+$(this).getval()+'"]) { display: none; }'+"\n";
          
          $(style).data("content",style.innerHTML);
          //enable all styles
          $("style.dependencies.hidden").each(function(){
            $(this).html($(this).data("content")).removeClass("hidden");
          });
          //disable "hidden" styles
          $(".UIelement:not(:visible) style.dependencies:not(.hidden)").each(function(){
            $(this).addClass("hidden");
            $(this).html("");
          });
        };
      }
      else if ("disable" in ele) {
        obj["function"] = function(){
          var $cont = $(this).closest(".input_container");
          var val = $(this).getval();
          for (var i = 0; i < ele.disable.length; i++) {
            var dependent = ele.disable[i];
            var $dependency = $cont.find(".field[name=\""+dependent+"\"]").closest(".UIelement");
            if ($dependency.length) {
              if (val == "") {
                $dependency[0].style.display = "";
              }
              else {
                $dependency.hide();
              }
            }
          }
        };
      } 
      if ("dependent" in ele) {
        obj.dependent = ele.dependent;
      }
      if ("value" in ele) {
        obj.value = ele.value;
      }
      if ("validate" in ele) {
        obj.validate = obj.validate.concat(ele.validate);
        if (ele.validate.indexOf("track_selector_parameter") > -1) {
          obj.help = "<div>"+ele.help+"</div>"+"<p>Track selector parameters consist of a string value which may be any of the following:</p> <ul><li><code>selector,selector</code>: Selects the union of the given selectors. Any number of comma-separated selector combinations may be used, they are evaluated one by one from left to right.</li> <li><code>selector,!selector</code>: Selects the difference of the given selectors. Specifically, all tracks part of the first selector that are not part of the second selector. Any number of comma-separated selector combinations may be used, they are evaluated one by one from left to right.</li> <li><code>selector,|selector</code>: Selects the intersection of the given selectors. Any number of comma-separated selector combinations may be used, they are evaluated one by one from left to right.</li> <li><code>none</code> or <code>-1</code>: Selects no tracks of this type.</li> <li><code>all</code> or <code>*</code>: Selects all tracks of this type.</li> <li>Any positive integer: Select this specific track ID. Does not apply if the given track ID does not exist or is of the wrong type. <strong>Does</strong> apply if the given track ID is incompatible with the currently active protocol or container format.</li> <li>ISO 639-1/639-3 language code: Select all tracks marked as the given language. Case insensitive.</li> <li>Codec string (e.g. <code>h264</code>): Select all tracks of the given codec. Case insensitive.</li> <li><code>highbps</code>, <code>maxbps</code> or <code>bestbps</code>: Select the track of this type with the highest bit rate.</li> <li><code>lowbps</code>, <code>minbps</code> or <code>worstbps</code>: Select the track of this type with the lowest bit rate.</li> <li><code>Xbps</code> or <code>Xkbps</code> or <code>Xmbps</code>: Select the single of this type which has a bit rate closest to the given number X. This number is in bits, not bytes.</li> <li><code>&gt;Xbps</code> or <code>&gt;Xkbps</code> or <code>&gt;Xmbps</code>: Select all tracks of this type which have a bit rate greater than the given number X. This number is in bits, not bytes.</li> <li><code>&lt;Xbps</code> or <code>&lt;Xkbps</code> or <code>&lt;Xmbps</code>: Select all tracks of this type which have a bit rate less than the given number X. This number is in bits, not bytes.</li> <li><code>max&lt;Xbps</code> or <code>max&lt;Xkbps</code> or <code>max&lt;Xmbps</code>: Select the one track of this type which has the highest bit rate less than the given number X. This number is in bits, not bytes.</li> <li><code>highres</code>, <code>maxres</code> or <code>bestres</code>: Select the track of this type with the highest pixel surface area. Only applied when the track type is video.</li> <li><code>lowres</code>, <code>minres</code> or <code>worstres</code>: Select the track of this type with the lowest pixel surface area. Only applied when the track type is video.</li> <li><code>XxY</code>: Select all tracks of this type with the given pixel surface area in X by Y pixels. Only applied when the track type is video.</li> <li><code>~XxY</code>: Select the single track of this type closest to the given pixel surface area in X by Y pixels. Only applied when the track type is video.</li> <li><code>&gt;XxY</code>: Select all tracks of this type with a pixel surface area greater than X by Y pixels. Only applied when the track type is video.</li> <li><code>&lt;XxY</code>: Select all tracks of this type with a pixel surface area less than X by Y pixels. Only applied when the track type is video.</li> <li><code>720p</code>, <code>1080p</code>, <code>1440p</code>, <code>2k</code>, <code>4k</code>, <code>5k</code>, or <code>8k</code>: Select all tracks of this type with the given pixel surface area. Only applied when the track type is video.</li> <li><code>surround</code>, <code>mono</code>, <code>stereo</code>, <code>Xch</code>: Select all tracks of this type with the given channel count. The 'Xch' variant can use any positive integer for 'X'. Only applied when the track type is audio.</li></ul>";
        }
        if (ele.validate.indexOf("track_selector") > -1) {
          obj.help = "<div>"+ele.help+"</div>"+"<p>A track selector is at least one track type (audio, video or subtitle) combined with a track selector parameter. For example: <code>audio=none&video=maxres</code>.<p>Track selector parameters consist of a string value which may be any of the following:</p> <ul><li><code>selector,selector</code>: Selects the union of the given selectors. Any number of comma-separated selector combinations may be used, they are evaluated one by one from left to right.</li> <li><code>selector,!selector</code>: Selects the difference of the given selectors. Specifically, all tracks part of the first selector that are not part of the second selector. Any number of comma-separated selector combinations may be used, they are evaluated one by one from left to right.</li> <li><code>selector,|selector</code>: Selects the intersection of the given selectors. Any number of comma-separated selector combinations may be used, they are evaluated one by one from left to right.</li> <li><code>none</code> or <code>-1</code>: Selects no tracks of this type.</li> <li><code>all</code> or <code>*</code>: Selects all tracks of this type.</li> <li>Any positive integer: Select this specific track ID. Does not apply if the given track ID does not exist or is of the wrong type. <strong>Does</strong> apply if the given track ID is incompatible with the currently active protocol or container format.</li> <li>ISO 639-1/639-3 language code: Select all tracks marked as the given language. Case insensitive.</li> <li>Codec string (e.g. <code>h264</code>): Select all tracks of the given codec. Case insensitive.</li> <li><code>highbps</code>, <code>maxbps</code> or <code>bestbps</code>: Select the track of this type with the highest bit rate.</li> <li><code>lowbps</code>, <code>minbps</code> or <code>worstbps</code>: Select the track of this type with the lowest bit rate.</li> <li><code>Xbps</code> or <code>Xkbps</code> or <code>Xmbps</code>: Select the single of this type which has a bit rate closest to the given number X. This number is in bits, not bytes.</li> <li><code>&gt;Xbps</code> or <code>&gt;Xkbps</code> or <code>&gt;Xmbps</code>: Select all tracks of this type which have a bit rate greater than the given number X. This number is in bits, not bytes.</li> <li><code>&lt;Xbps</code> or <code>&lt;Xkbps</code> or <code>&lt;Xmbps</code>: Select all tracks of this type which have a bit rate less than the given number X. This number is in bits, not bytes.</li> <li><code>max&lt;Xbps</code> or <code>max&lt;Xkbps</code> or <code>max&lt;Xmbps</code>: Select the one track of this type which has the highest bit rate less than the given number X. This number is in bits, not bytes.</li> <li><code>highres</code>, <code>maxres</code> or <code>bestres</code>: Select the track of this type with the highest pixel surface area. Only applied when the track type is video.</li> <li><code>lowres</code>, <code>minres</code> or <code>worstres</code>: Select the track of this type with the lowest pixel surface area. Only applied when the track type is video.</li> <li><code>XxY</code>: Select all tracks of this type with the given pixel surface area in X by Y pixels. Only applied when the track type is video.</li> <li><code>~XxY</code>: Select the single track of this type closest to the given pixel surface area in X by Y pixels. Only applied when the track type is video.</li> <li><code>&gt;XxY</code>: Select all tracks of this type with a pixel surface area greater than X by Y pixels. Only applied when the track type is video.</li> <li><code>&lt;XxY</code>: Select all tracks of this type with a pixel surface area less than X by Y pixels. Only applied when the track type is video.</li> <li><code>720p</code>, <code>1080p</code>, <code>1440p</code>, <code>2k</code>, <code>4k</code>, <code>5k</code>, or <code>8k</code>: Select all tracks of this type with the given pixel surface area. Only applied when the track type is video.</li> <li><code>surround</code>, <code>mono</code>, <code>stereo</code>, <code>Xch</code>: Select all tracks of this type with the given channel count. The 'Xch' variant can use any positive integer for 'X'. Only applied when the track type is audio.</li></ul>";
        }
      }
      
      return obj;
    }
    
    for (var j in type) {
      if (input[type[j]]) {
        build.push(
          $('<h4>').text(UI.format.capital(type[j])+' parameters')
        );
        var list = Object.keys(input[type[j]]); //array of the field names
        if ("sort" in input) {
          //sort by key input.sort
          list.sort(function(a,b){
            return (""+input[type[j]][a][input.sort]).localeCompare(input[type[j]][b][input.sort]);
          });
        }
        //loop over the list of field names
        for (var n in list) {
          var i = list[n];
          var ele = input[type[j]][i];
          if (Array.isArray(ele)) {
            for (var m in ele) {
              build.push(processEle(j,i,ele[m]));
            }
          }
          else {
            build.push(processEle(j,i,ele));
          }
        }
      }
    }
    return build;
  },
  convertPushArr2Obj: function(push,isAutoPush) {
    var out = {
      id: push[0],
      stream: push[1],
      target: push[2]
    };
    if (isAutoPush) {
      if (push.length >= 4) {
        if (push[3]) out.scheduletime = push[3];
        if (push.length >= 5) {
          if (push[4]) out.completetime = push[4];
          if (push.length >= 8) {
            if (push[5]) {
              out.start_rule([push[5],push[6],push[7]]);
            }
            if (push.length >= 11) {
              if (push[8]) {
                out.end_rule([push[8],push[9],push[10]]);
              }
            }
          }
        }
      }
    }
    else {
      //it's an active push
      if (push.length >= 4) {
        if (push[3]) out.resolved_target = push[3];
        if (push.length >= 5) {
          if (push[4]) out.logs = push[4];
          if (push.length >= 6) {
            if (push[5]) out.stats = push[5];
          }
        }
      }
    }
    return out;
  },
  stored: {
    get: function(){
      return mist.data.ui_settings || {};
    },
    set: function(name,val){
      var settings = this.get();
      var oldval = JSON.stringify(settings[name]);
      settings[name] = val;
      if (oldval != JSON.stringify(val)) {
        mist.send(function(){
          //done
        },{ui_settings: settings});
      }
    },
    del: function(name){
      delete mist.data.ui_settings[name];
      mist.send(function(){
        
      },{ui_settings: mist.data.ui_settings});
    }
  }
};

function log() {
  try {
    if (UI.debug) {
      var error = (new Error).stack;
      [].push.call(arguments,error);
    }
    [].unshift.call(arguments,'['+UI.format.time((new Date).getTime()/1000)+']');
    console.log.apply(console,arguments);
  } catch(e) {}
}

// setval and getval allow us to set and get values from custom input types
$.fn.getval = function(){
  var opts = $(this).data('opts');
  var val = $(this).val();
  if ((opts) && ('type' in opts)) {
    var type = opts.type;
    switch (type) { //exceptions only
      case 'int':
        if (val != "") { val = Number(val); }
        break;
      case 'span':
        val = $(this).html();
        break;
      case 'debug':
        val = $(this).val() == "" ? null : Number($(this).val());
        break;
      case 'checkbox':
        val = $(this).prop('checked');
        break;
      case 'radioselect':
        var $l = $(this).find('label > input[type=radio]:checked').parent();
        if ($l.length) {
          val = [];
          val.push($l.children('input[type=radio]').val());
          var $s = $l.children('select');
          if ($s.length) {
            val.push($s.val());
          }
        }
        else {
          val = '';
        }
        break;
      case 'checklist':
        val = [];
        $(this).find('.checklist input[type=checkbox]:checked').each(function(){
          val.push($(this).attr('name'));
        });
        /*if (val.length == opts.checklist.length) {
          val = [];
        }*/
        break;
      case "unix":
        if (val != "") {
          val = Math.round(new Date($(this).val()) / 1e3);
        }
        break;
      case "selectinput": 
        val = $(this).children("select").first().val();
        if (val == "CUSTOM") {
          //get value of input field
          val = $(this).children("label").first().find(".field_container").children().first().getval();
        }
        break;
      case "inputlist":
        val = [];
        $(this).find(".field").each(function(){
          if ($(this).getval() != "") {
            val.push($(this).getval());
          }
        });
        break;
      case "sublist":
        val = $(this).data("savelist");
        break;
      case "json":
        try {
          val = JSON.parse($(this).val());
        }
        catch (e) {
          val = null;
        }
        break;
      case "bitmask": {
        val = 0;
        $(this).find("input").each(function(){
          if ($(this).prop("checked")) {
            val += Number($(this).val());
          }
        });
        break;
      }
    }

    if (("factor" in opts) && (val !== null) && (val != "")) {
      val *= opts.factor;
    }
  }

  return val;
}
$.fn.setval = function(val,extraParameters){
  var opts = $(this).data('opts');
  if (opts && ("factor" in opts) && (Number(val) != 0)) {
    val /= opts.factor;
  }

  $(this).val(val);
  if ((opts) && ('type' in opts)) {
    var type = opts.type;
    switch (type) { //exceptions only
      case 'span':
        $(this).html(val).attr("title",typeof val == "string" ? val : "");
        break;
      case 'checkbox':
        $(this).prop('checked',val);
        break;
      case 'geolimited':
      case 'hostlimited':
        var subUI = $(this).closest('.field_container').data('subUI');
        if ((typeof val == 'undefined') || (val.length == 0)) {
          val = '-';
        }
        subUI.blackwhite.val(val.charAt(0));
        val = val.substr(1).split(' ');
        for (var i in val) {
          subUI.values.append(
            subUI.prototype.clone(true).val(val[i])
          );
        }
        subUI.blackwhite.trigger('change');
        break;
      case 'radioselect':
        if (typeof val == 'undefined') { return $(this); }
        var $l = $(this).find('label > input[type=radio][value="'+val[0]+'"]').prop('checked',true).parent();
        if (val.length > 1) {
          $l.children('select').val(val[1]);
        }
        break;
      case 'checklist':
        var $inputs = $(this).find('.checklist input[type=checkbox]').prop('checked',false);
        for (i in val) {
          $inputs.filter('[name="'+val[i]+'"]').prop('checked',true);
        }
        break;
      case "unix":
        if ((typeof val != "undefined") && (val != "") && (val !== null)) {
          var datetime = new Date(Math.round(val) * 1e3);
          datetime.setMinutes(datetime.getMinutes() - datetime.getTimezoneOffset()); //correct for the browser being a pain and converting to UTC
          datetime = datetime.toISOString();
          $(this).val(datetime.split("Z")[0]);
        }
        
        break;
      case "selectinput":
        //check if val is one of the select options
        if (val === null) {
          val = "";
        }
        var found = false;
        for (var i in opts.selectinput) {
          var compare;
          if (typeof opts.selectinput[i] == "string") {
            compare = opts.selectinput[i];
          }
          else if (typeof opts.selectinput[i][0] == "string") {
            compare = opts.selectinput[i][0];
          }
          if (compare == val) {
            $(this).children("select").first().val(val);
            found = true;
            break;
          }
        }
        if (!found) {
          $(this).children("label").first().find(".field_container").children().first().setval(val).trigger("change",extraParameters);
          $(this).children("select").first().val("CUSTOM").trigger("change",extraParameters);
        }
        break;
      case "inputlist": {
        if (typeof val == "string") { val = [val]; }
        //save all children, they will be removed after 
        //if e.input contains a reference to a DOMelement, removing it will destroy any eventListeners attached to it
        let $old = $(this).children();
        //add children for all the values
        for (var i in val) {
          var $newitem = $(this).data("newitem")();
          $newitem.find(".field").setval(val[i]);
          $(this).append($newitem);
        }
        $(this).append($(this).data("newitem")()); //add an empty input
        $old.remove(); 
        //any DOMelement in e.input should now be in the empty input added above, so we can safely remove the old inputs
        break;
      }
      case "sublist":
        var $field = $(this);
        var $curvals = $(this).children(".curvals");
        $curvals.html("");
        if (val && val.length) {
          for (var i in val) {
            var v = $.extend(true,{},val[i]);
            
            //don't display any keys that are set to null
            function removeNull(v) {
              for (var j in v) {
                if (j.slice(0,6) == "x-LSP-") {
                  delete v[j];
                }
                else if (typeof v[j] == "object") {
                  removeNull(v[j])
                }
              }
            }
            removeNull(v);
            
            $curvals.append(
              $("<div>").addClass("subitem").append(
                $("<span>").addClass("itemdetails").text(
                  (val[i]["x-LSP-name"] ? val[i]["x-LSP-name"] : JSON.stringify(v))
                ).attr("title",JSON.stringify(v,null,2))
              ).append(
                $("<button>").addClass("move").text("^").attr("title","Move item up").click(function(){
                  var i = $(this).parent().index();
                  if (i == 0) { return; }
                  var savelist = $field.getval();
                  savelist.splice(i - 1,0,savelist.splice(i,1)[0]);
                  $field.setval(savelist);
                })
              ).append(
                $("<button>").text("Edit").click(function(){
                  var index = $(this).parent().index();
                  var $field = $(this).closest(".field");
                  $field.data("build")(Object.assign({},$field.getval()[index]),index);
                })
              ).append(
                $("<button>").text("x").attr("title","Remove item").click(function(e){
                  var i = $(this).parent().index();
                  var savelist = $field.data("savelist");
                  savelist.splice(i,1);
                  $field.setval(savelist);
                  e.preventDefault(); //for some reason, if this is left out, the new item button gets activated when the last item is removed
                })
              )
            );
          }
        }
        else {
          $curvals.append("None.");
        }
        $field.data("savelist",val);
        break;
      case "json": {
        $(this).val(val === null ? "" : JSON.stringify(val,null,2));
        break;
      }
      case "bitmask": {
        var map = $(this).data("opts").bitmask;
        var $inputs = $(this).find("input");
        for (var i in map) {
          $el = $inputs.eq(i);
          if ((val & map[i][0]) == map[i][0]) {
            $el.attr("checked","checked");
          }
          else {
            $el.removeAttr("checked");
          }
        }
        break;
      }
    }
  }
  $(this).trigger('change',extraParameters);
  return $(this);
}
function parseURL(url,set) {
  var a;
  if ("URL" in window && (typeof window.URL == "function")) {
    try {
      a = new URL(url);
    }
    catch (e) {
      a = document.createElement('a');
      a.href = url;
    }
  }
  else {
    a = document.createElement('a');
    a.href = url;
  }
  if (set) {
    for (var i in set) {
      a[i] = set[i];
    }
  }
  return {
    full: a.href,
    protocol: a.protocol+'//',
    host: a.hostname,
    port: (a.port ? ':'+a.port : ''),
    pathname: a.pathname ? a.pathname : null,
    search: a.search ? a.search.replace(/^\?/,"") : null,
    searchParams : a.search ? a.searchParams : null
  };
}
function triggerRewrite(trigger) {
  if ((typeof trigger == 'object') && (typeof trigger.length == 'undefined')) { return trigger; }
  return obj = {
    handler: trigger[0],
    sync: trigger[1],
    streams: trigger[2],
    'default': trigger[3]
  };
}
