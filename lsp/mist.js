$(function(){
  UI.elements = {
    menu: $('nav > .menu'),
    main: $('main'),
    header: $('header'),
    connection: {
      status: $('#connection'),
      user_and_host: $('#user_and_host'),
      msg: $('#message')
    }
  };
  UI.buildMenu();
  UI.stored.getOpts();
  
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

$(window).on('hashchange', function(e) {
  var loc = decodeURIComponent(location.hash).substring(1).split('@');
  if (!loc[1]) { loc[1] = ''; }
  var tab = loc[1].split('&');
  if (tab[0] == '') { tab[0] = 'Overview'; }
  UI.showTab(tab[0],tab[1]);
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
    clear: function(){
      if (typeof this.opts == 'undefined') {
        return;
      }
      clearInterval(this.opts.id);
      delete this.opts;
    },
    set: function(callback,delay){
      if (this.opts) {
        log('[interval]','Set called on interval, but an interval is already active.');
      }
      
      this.opts = {
        delay: delay,
        callback: callback
      };
      this.opts.id = setInterval(callback,delay);
    }
  },
  returnTab: ['Overview'],
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
  humanMime: function (type) {
    var human = false;
    switch (type) {
      case 'html5/application/vnd.apple.mpegurl':
        human = 'HLS (TS)';
        break;
      case "html5/application/vnd.apple.mpegurl;version=7":
        human = "HLS (CMAF)";
        break;
      case 'html5/video/webm':
        human = 'WebM';
        break;
      case 'html5/video/mp4':
        human = 'MP4';
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
      case 'webrtc':
        human = "WebRTC";
        break;
    }
    return human;
  },
  popup: {
    element: null,
    show: function(content) {
      this.element = $('<div>').attr('id','popup').append(
        $('<button>').text('Close').addClass('close').click(function(){
          UI.popup.element.fadeOut('fast',function(){
            UI.popup.element.remove();
            UI.popup.element = null;
          });
        })
      ).append(content);
      $('body').append(this.element);
    }
  },
  menu: [
    {
      Overview: {},
      General: {
        hiddenmenu: {
          "Edit variable": {},
          "Edit external writer": {}
        }
      },
      Protocols: {},
      Streams: {
        hiddenmenu: {
          Edit: {},
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
      Guides: {
        link: 'http://mistserver.org/documentation#Userdocs'
      },
      Tools: {
        submenu: {
          'Release notes': {
            link: 'http://mistserver.org/documentation#Devdocs'
          },
          'Mist Shop': {
            link: 'http://mistserver.org/products'
          },
          'Email for Help': {}
        }
      }
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
          UI.navto(j);
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
  buildUI: function(elements){
    /*elements should be an array of objects, the objects containing the UI element options 
     * (or a jQuery object that will be inserted isntead).
    
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
              $b.addClass('cancel').click(button['function']);
              break;
            case 'save':
              $b.addClass('save').click(function(e){
                var fn = $(this).data('opts')['preSave'];
                if (fn) { fn.call(this); }
                
                var $ic = $(this).closest('.input_container');
                
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
          if ('step' in e) {
            $field.attr('step',e.step);
          }
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
            [0,'0 - All debugging messages disabled'],
            [1,'1 - Messages about failed operations'],
            [2,'2 - Previous level, and error messages'],
            [3,'3 - Previous level, and warning messages'],
            [4,'4 - Previous level, and status messages for development'],
            [5,'5 - Previous level, and more status messages for development'],
            [6,'6 - Previous level, and verbose debugging messages'],
            [7,'7 - Previous level, and very verbose debugging messages'],
            [8,'8 - Report everything in extreme detail'],
            [9,'9 - Report everything in insane detail'],
            [10,'10 - All messages enabled']
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
          $field = $('<div>').addClass('inputlist');
          var newitem = function(){
            var $part;
            if ("input" in e) {
              $part = UI.buildUI([e.input]).find(".field_container");
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
              if ($(this).is(":last-child")) {
                if ($(this).find(".field").getval() != "") {
                  var $clone = $part.clone().keyup(keyup);
                  $clone.find(".field").setval("");
                  $(this).after($clone);
                }
                else if (e.which == 8) { //backspace
                  $(this).prev().find(".field").focus();
                }
              }
              else {
                if ($(this).find(".field").getval() == "") {
                  var $f = $(this).prev();
                  if (!$f.length) {
                    $f = $(this).next();
                  }
                  $f.find(".field").focus();
                  $(this).remove();
                }
              }
            };
            
            $part.keyup(keyup);
            return $part;
          };
          $field.data("newitem",newitem);
          
          $field.append($field.data("newitem"));
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
      $field.addClass('field').data('opts',e);
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
        $fc.append(
          $('<span>').addClass('unit').html(e.unit)
        );
      }
      if ('prefix' in e) {
        $fc.prepend(
          $('<span>').addClass('unit').html(e.prefix)
        );
      }
      if ('readonly' in e) {
        $field.attr('readonly','readonly');
        $field.click(function(){
          $(this).select();
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
              UI.popup.show(
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
            var $c = $(this).closest('.grouper');
            var $bc = $('<div>').addClass('browse_container');
            var $field = $c.find('.field').attr('readonly','readonly').css('opacity',0.5);
            var $browse_button = $(this);
            var $cancel = $('<button>').text('Stop browsing').click(function(){
                $browse_button.show();
                $bc.remove();
                $field.removeAttr('readonly').css('opacity',1);
            });
            
            var $path = $('<span>').addClass('field');
            
            var $folder_contents = $('<div>').addClass('browse_contents');
            var $folder = $('<a>').addClass('folder');
            var filetypes = $field.data('filetypes');
            
            $c.append($bc);
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
                if (mist.data.LTS) { $field.setval(d.browse.path[0]+'/'); }
                $folder_contents.html(
                  $folder.clone(true).text('..').attr('title','Folder up')
                );
                if (d.browse.subdirectories) {
                  d.browse.subdirectories.sort();
                  for (var i in d.browse.subdirectories) {
                    var f = d.browse.subdirectories[i];
                    $folder_contents.append(
                      $folder.clone(true).attr('title',$path.text()+seperator+f).text(f)
                    );
                  }
                }
                if (d.browse.files) {
                  d.browse.files.sort();
                  for (var i in d.browse.files) {
                    var f = d.browse.files[i];
                    var src = $path.text()+seperator+f;
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
                      
                      $field.setval(src).removeAttr('readonly').css('opacity',1);
                      $browse_button.show();
                      $bc.remove();
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
              var path = $path.text()+seperator+$(this).text();
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
            
            $browse_button.hide();
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
      
      if ('pointer' in e) {
        $field.data('pointer',e.pointer).addClass('isSetting');
        if (e.pointer.main) {
          var val = e.pointer.main[e.pointer.index];
          if (val != 'undefined') {
            $field.setval(val);
          }
        }
      }
      if ((($field.getval() == "") || ($field.getval() == null) || !("pointer" in e)) && ('value' in e)) {
        $field.setval(e.value);
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
      if ('help' in e) {
        $ihc.append(
          $('<span>').addClass('ih_balloon').html(e.help)
        );
        $field.on('focus mouseover',function(){
          $(this).closest('label').addClass('active');
        }).on('blur mouseout',function(){
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
                  if (!$(me)[0].validity.valid) {
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
                break;
              case 'streamname': {
                f = function(val,me) {
                  if (val == "") { return; }
                  
                  if (!isNaN(val.charAt(0))) {
                    return {
                      msg: 'The first character may not be a number.',
                      classes: ['red']
                    };
                  }
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
                  if (!isNaN(streampart.charAt(0))) {
                    return {
                      msg: 'The first character may not be a number.',
                      classes: ['red']
                    };
                  }
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
                  if (!isNaN(streampart.charAt(0))) {
                    return {
                      msg: 'The first character may not be a number.',
                      classes: ['red']
                    };
                  }
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
        $field.data('validate_functions',fs).data('help_container',$ihc).data('validate',function(me,focusonerror){
          if ((!$(me).is(":visible")) && (!$(me).is("input[type=\"hidden\"]"))) { return; }
          
          var val = $(me).getval();
          var fs = $(me).data('validate_functions');
          var $ihc = $(me).data('help_container');
          $ihc.find('.err_balloon').remove();
          for (var i in fs) {
            var error = fs[i](val,me);
            if (error) {
              $err = $('<span>').addClass('err_balloon').html(error.msg);
              for (var j in error.classes) {
                $err.addClass(error.classes[j]);
              }
              $ihc.prepend($err);
              if (focusonerror) { $(me).focus(); }
              return ((typeof error == "object") && ("break" in error) ? error["break"] : true);
            }
          }
          return false;
        }).addClass('hasValidate').on('change keyup',function(){
          var f = $(this).data('validate');
          f($(this));
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
    var $table = $('<table>').css('margin','0.2em');
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
          return UI.format.bytes(val,true);
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
          size = size * Math.pow(1024,exponent);
          
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
    duration: function(seconds) {
      //get the amounts
      var multiplications = [1e-3,  1e3,   60,  60,   24,     7,     52,1e9];
      var units =           ['ms','sec','min','hr','day','week','year'];
      var amounts = {};
      var left = seconds;
      for (var i in units) {
        left /= multiplications[i];
        var amount = Math.round(left % multiplications[Number(i)+1]);
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
        case 'year':
          $s.append(UI.format.addUnit(amounts.year,'years, ')).append(UI.format.addUnit(amounts.week,'wks'));
          break;
        case 'week':
          $s.append(UI.format.addUnit(amounts.week,'wks, ')).append(UI.format.addUnit(amounts.day,'days'));
          break;
        case 'day':
          $s.append(UI.format.addUnit(amounts.day,'days, ')).append(UI.format.addUnit(amounts.hr,'hrs'));
          break;
        default:
          $s.append(
            [
              ('0'+amounts.hr).slice(-2),
              ('0'+amounts.min).slice(-2),
              ('0'+amounts.sec).slice(-2)+(amounts.ms ? '.'+amounts.ms : '')
            ].join(':')
          );
          break;
      }
      return $s[0].innerHTML;
    },
    number: function(num) {
      if ((isNaN(Number(num))) || (num == 0)) { return num; }
      
      //rounding
      var sig = 3;
      var mult = Math.pow(10,sig - Math.floor(Math.log(num)/Math.LN10) - 1);
      num = Math.round(num * mult) / mult;
      
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
    bytes: function(val,persec){
      var suffix = ['bytes','KiB','MiB','GiB','TiB','PiB'];
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
      return UI.format.addUnit(UI.format.number(val),unit+(persec ? '/s' : ''));
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
  showTab: function(tab,other) {
    var $main = UI.elements.main;
    
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
    }
    
    //unload any video's that might still be playing
    if ((MistVideoObject) && (MistVideoObject.reference)) {
      MistVideoObject.reference.unload();
    }
    
    UI.interval.clear();
    $main.html(
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
            }
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
          $("<h3>").text("Write config now"),
          {
            type: "help",
            help: "Tick the box in order to force an immediate save to the config.json MistServer uses to save your settings. Saving will otherwise happen upon closing MistServer. Don\'t forget to press save after ticking the box."
          },{
            type: 'checkbox',
            label: 'Force configurations save',
            pointer: {
              main: s,
              index: 'save'
            }            
          },{
            type: 'buttons',
            buttons: [{
              type: 'save',
              label: 'Save',
              'function': function(){
                var save = {};
                
                if (s.save) {
                  save.save = s.save;
                }
                delete s.save;
                mist.send(function(){
                  UI.navto('Overview');
                },save)
              }
            }]
          }
        ]));
        if (mist.data.LTS) {
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
              setTimeout(function(){
                mist.send(function(d){
                  update_progress(d);
                },{update:true});
              },1e3);
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
        }
        else {
          $versioncheck.text('');
        }
        function updateViewers() {
          var request = {
            totals:{
              fields: ['clients'],
              start: -10
            },
            active_streams: true
          };
          if (!('cabailities' in mist.data)) {
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
                  $('<span>').append(UI.format.time(l[0]))
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

        var b = {limit:""};
        if ("bandwidth" in mist.data) {
          b = mist.data.bandwidth;
          if (b == null) { b = {}; }
          if (!b.limit) {
            b.limit = "";
          }
        }
        var $bitunit = $("<select>").html(
          $("<option>").val(1).text("bytes/s")
        ).append(
          $("<option>").val(1024).text("KiB/s")
        ).append(
          $("<option>").val(1048576).text("MiB/s")
        ).append(
          $("<option>").val(1073741824).text("GiB/s")
        );


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
                  $("<th>").text("Command")
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
                    typeof v == "string" ?
                    JSON.stringify(v) :
                    (v[2] > 0 ? JSON.stringify(v[3]) : "" )
                  )
                )
              ).append(
                $("<td>").text(typeof v == "string" ? "" : v[0])
              ).append(
                $("<td>").html(typeof v == "string" ? "Never" : (v[1] == 0 ? "Once" : UI.format.duration(v[1])))
              ).append(
                $("<td>").attr("title",
                  v[2] > 0 ?
                  (typeof v == "string" ? "" : "At "+UI.format.dateTime(new Date(v[2]),"long")) :
                  "Not yet"
                ).html(
                  typeof v == "string" ?
                  "" : 
                  (v[2] > 0 ? UI.format.duration(new Date().getTime()*1e-3 - v[2])+" ago" : "Not yet")
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
            $("<button>").text("New variable").click(function(){
              UI.navto("Edit variable","");
            })
          ),
          $variables
        ]));

        $main.append(UI.buildUI([
          $('<h3>').text("Load balancer"),
          {
            type: "help",
            help: "If you're using MistServer's load balancer, the information below is passed to it so that it can make informed decisions."
          },

          {
            type: "selectinput",
            label: "Server's bandwidth limit",
            selectinput: [
              ["","Default (1 gbps)"],
              [{
                label: "Custom",
                type: "int",
                min: 0,
                unit: $bitunit
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
                bandwidth.limit = (b.limit ? $bitunit.val() * b.limit : 0);
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


        var $uploaders = $("<div>").html("Loading..");
        $main.append(UI.buildUI([

          $('<h3>').text("External writers"),
          {
            type: "help",
            help: "When pushing a stream to a target unsupported by MistServer like S3 storage, an external writer can be provided which handles writing the media data to the target location. The writer will receive data over stdin and MistServer will print any info written to stdout and stderr as log messages."
          },
          $("<div>").addClass("button_container").css("text-align","right").html(
            $("<button>").text("New external writer").click(function(){
              UI.navto("Edit external writer","");
            })
          ),
          $uploaders

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
        },{external_writer_list:true});

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
              help: "What kind of variable is this? It can either be a static value that you can enter below, or a dynamic one that is returned by a command.",
              select: [
                ["value","Static value"],
                ["command","Dynamic through command"]
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
                      help: "The command that should be executed to retrieve the value for this variable.<br>For example:<br><code>/usr/bin/date +%A</code><br>There is a character limit of 511 characters.",
                      validate: ["required"],
                      pointer: {
                        main: saveas_dyn,
                        index: "target"
                      }
                    },{
                      type: "int",
                      min: 0,
                      max: 4294967295,
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
              build({
                name: other
              },(typeof v == "string" ? {
                value: v,
                type: "value"
              } : {
                target: v[0],
                interval: v[1],
                waitTime: v[4],
                type: "command"
              }));
            }
            else {
              $main.append('Variable "$'+other+'" does not exist.');
            }
          },{variable_list:true});
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
              if ((!('required' in mist.data.capabilities.connectors[toenable[i]])) || (Object.keys(mist.data.capabilities.connectors[toenable[i]].required).length == 0)) {
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
            $('<thead>').html(
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
      case 'Streams':
        if (!('capabilities' in mist.data)) {
          $main.html('Loading..');
          mist.send(function(){
            UI.navto(tab);
          },{capabilities: true});
          return;
        }
        
        var $switchmode = $('<button>');
        var $loading = $('<span>').text('Loading..');
        $main.append(
          UI.buildUI([{
            type: 'help',
            help: 'Here you can create, edit or delete new and existing streams. Go to stream preview or embed a video player on your website.'
          },
            $('<div>').css({
              width: '45.25em',
              display: 'flex',
              'justify-content':'flex-end'
            }).append(
              $switchmode
            ).append(
              $('<button>').text('Create a new stream').click(function(){
                UI.navto('Edit');
              })
            )
          ])
        ).append($loading);
        if (other == '') {
          var s = mist.stored.get();
          if ('viewmode' in s) {
            other = s.viewmode;
          }
        }
        $switchmode.text('Switch to '+(other == 'thumbnails' ? 'list' : 'thumbnail')+' view').click(function(){
          mist.stored.set('viewmode',(other == 'thumbnails' ? 'list' : 'thumbnails'));
          UI.navto('Streams',(other == 'thumbnails' ? 'list' : 'thumbnails'));
        });
        
        var allstreams = $.extend(true,{},mist.data.streams);
        function createWcStreamObject(streamname,parent) {
          var wcstream = $.extend({},parent);
          delete wcstream.meta;
          delete wcstream.error;
          wcstream.online = 2; //should either be available (2) or active (1)
          wcstream.name = streamname;
          wcstream.ischild = true;
          return wcstream;
        }
        
        function createPage(type,streams,folders) {
          $loading.remove();
          switch (type) {
            case 'thumbnails': {
              
              var $shortcuts = $('<div>').addClass('preview_icons');
              function selectastream(select,folders) {
                folders = folders || [];
                var saveas = {};
                select.sort();
                select.unshift('');
                $loading.remove();
                $main.append(
                  $('<h2>').text(tab)
                ).append(UI.buildUI([
                  {
                    label: 'Filter the streams',
                    type: 'datalist',
                    datalist: select,
                    pointer: {
                      main: saveas,
                      index: 'stream'
                    },
                    help: 'If you type something here, the box below will only show streams with names that contain your text.',
                    'function': function(){
                      var val = $(this).val();
                      $shortcuts.children().each(function(){
                        $(this).hide();
                        if ($(this).attr('data-stream').indexOf(val) > -1) {
                          $(this).show();
                        }
                      });
                    }
                  }
                ]));
                select.shift();
                
                
                $main.append(
                  $('<span>').addClass('description').text('Choose a stream below.')
                ).append($shortcuts);
                
                //if there is a JPG output, add actual thumnails \o/
                var thumbnails = false;
                ///\todo activate this code when the backend is ready
                /*
                if (UI.findOutput('JPG')) {
                  var jpgport = false;
                  //find the http port and make sure JPG is enabled
                  for (var i in mist.data.config.protocols) {
                    var protocol = mist.data.config.protocols[i];
                    if ((protocol.connector == 'HTTP') || (protocol.connector == 'HTTP.exe')) {
                      jpgport = (protocol.port ? ':'+protocol.port : ':8080');
                    }
                    if ((protocol.connector == 'JPG') || (protocol.connector == 'JPG.exe')) {
                      thumbnails = true;
                    }
                  }
                  if ((thumbnails) && (jpgport)) {
                    //now we get to use it as a magical function wheee!
                    jpgport = parseURL(mist.user.host).host+jpgport;
                    thumbnails = function(streamname) {
                      return 'http://'+jpgport+'/'+encodeURIComponent(streamname)+'.jpg';
                    }
                  }
                }
                */
                
                for (var i in select) {
                  var streamname = select[i];
                  var source = '';
                  var $delete = $('<button>').text('Delete').click(function(){
                    var streamname = $(this).closest('div').attr('data-stream');
                    if (confirm('Are you sure you want to delete the stream "'+streamname+'"?')) {
                      delete mist.data.streams[streamname];
                      var send = {};
                      send.deletestream = [streamname];
                      mist.send(function(d){
                        UI.navto('Streams');
                      },send);
                    }
                  });
                  var $edit = $('<button>').text('Settings').click(function(){
                    UI.navto('Edit',$(this).closest('div').attr('data-stream'));
                  });
                  var $preview = $('<button>').text('Preview').click(function(){
                    UI.navto('Preview',$(this).closest('div').attr('data-stream'));
                  });
                  var $embed = $('<button>').text('Embed').click(function(){
                    UI.navto('Embed',$(this).closest('div').attr('data-stream'));
                  });
                  var $image = $('<span>').addClass('image');
                  
                  if ((thumbnails) && (folders.indexOf(streamname) == -1)) {
                    //there is a JPG output and this isn't a folder
                    $image.append(
                      $('<img>').attr('src',thumbnails(streamname)).error(function(){
                        $(this).hide();
                      })
                    );
                  }
                  
                  //its a wildcard stream
                  if (streamname.indexOf('+') > -1) {
                    var streambits = streamname.split('+');
                    source = mist.data.streams[streambits[0]].source+streambits[1];
                    $delete = '';
                    $edit = '';
                    $image.addClass('wildcard');
                  }
                  else {
                    source = mist.data.streams[streamname].source;
                    //its a folder stream
                    if (folders.indexOf(streamname) > -1) {
                      $preview = '';
                      $embed = '';
                      $image.addClass('folder');
                    }
                  }
                  $shortcuts.append(
                    $('<div>').append(
                      $('<span>').addClass('streamname').text(streamname)
                    ).append(
                      $image
                    ).append(
                      $('<span>').addClass('description').text(source)
                    ).append(
                      $('<span>').addClass('button_container').append(
                        $edit
                      ).append(
                        $delete
                      ).append(
                        $preview
                      ).append(
                        $embed
                      )
                    ).attr('title',streamname).attr('data-stream',streamname)
                  );
                }
              }
              
              selectastream(streams,folders);
              break;
            }
            case 'list':
            default: {
              var $tbody = $('<tbody>').append($('<tr>').append('<td>').attr('colspan',6).text('Loading..'));
              var $table = $('<table>').html(
                $('<thead>').html(
                  $('<tr>').html(
                    $('<th>').text('Stream name').attr('data-sort-type','string').addClass('sorting-asc')
                  ).append(
                    $('<th>').text('Source').attr('data-sort-type','string')
                  ).append(
                    $('<th>').text('Status').attr('data-sort-type','int')
                  ).append(
                    $('<th>').css('text-align','right').text('Connections').attr('data-sort-type','int')
                  ).append(
                    $('<th>')
                  ).append(
                    $('<th>')
                  )
                )
              ).append($tbody);
              $main.append($table);
              $table.stupidtable();
              
              function buildStreamTable() {
                var i = 0;
                $tbody.html('');
                
                streams.sort();
                for (var s in streams) {
                  var streamname = streams[s];
                  var stream;
                  if (streamname in mist.data.streams) { stream = mist.data.streams[streamname]; }
                  else { stream = allstreams[streamname]; }
                  
                  var $viewers = $('<td>').css('text-align','right').html($('<span>').addClass('description').text('Loading..'));
                  var v = 0;
                  if ((typeof mist.data.totals != 'undefined') && (typeof mist.data.totals[streamname] != 'undefined')) {
                    var data = mist.data.totals[streamname].all_protocols.clients;
                    var v = 0;
                    //get the average value
                    if (data.length) {
                      for (var i in data) {
                        v += data[i][1];
                      }
                      v = Math.round(v / data.length);
                    }
                  }
                  $viewers.html(UI.format.number(v));
                  if ((v == 0) && (stream.online == 1)) {
                    stream.online = 2;
                  }
                  var $buttons = $('<td>').css('text-align','right').css('white-space','nowrap');
                  if ((!('ischild' in stream)) || (!stream.ischild)) {
                    $buttons.html(
                      $('<button>').text('Settings').click(function(){
                        UI.navto('Edit',$(this).closest('tr').data('index'));
                      })
                    ).append(
                      $('<button>').text('Delete').click(function(){
                        var index = $(this).closest('tr').data('index');
                        if (confirm('Are you sure you want to delete the stream "'+index+'"?')) {
                          delete mist.data.streams[index];
                          var send = {};
                          if (mist.data.LTS) {
                            send.deletestream = [index];
                          }
                          else {
                            send.streams = mist.data.streams;
                          }
                          mist.send(function(d){
                            UI.navto('Streams');
                          },send);
                        }
                      })
                    );
                  }
                  
                  var $streamnamelabel = $('<span>').text(stream.name);
                  if (stream.ischild) {
                    $streamnamelabel.css('padding-left','1em');
                  }
                  var $online = UI.format.status(stream);
                  var $preview = $('<button>').text('Preview').click(function(){
                    UI.navto('Preview',$(this).closest('tr').data('index'));
                  });
                  var $embed = $('<button>').text('Embed').click(function(){
                    UI.navto('Embed',$(this).closest('tr').data('index'));
                  });
                  if (('filesfound' in allstreams[streamname]) || (stream.online < 0)) {
                    $online.html('');
                    $preview = '';
                    $viewers.html('');
                    $embed = '';
                  }
                  $tbody.append(
                    $('<tr>').data('index',streamname).html(
                      $('<td>').html($streamnamelabel).attr('title',(stream.name == "..." ? "The results were truncated" : stream.name)).addClass('overflow_ellipsis')
                    ).append(
                      $('<td>').text(stream.source).attr('title',stream.source).addClass('description').addClass('overflow_ellipsis').css('max-width','20em')
                    ).append(
                      $('<td>').data('sort-value',stream.online).html($online)
                    ).append(
                      $viewers
                    ).append(
                      $('<td>').css('white-space','nowrap').html(
                        $preview
                      ).append(
                        $embed
                      )
                    ).append(
                      $buttons
                    )
                  );
                  i++;
                }
              }
              
              function updateStreams() {
                var totals = [];
                for (var i in mist.data.active_streams) {
                  totals.push({
                    streams: [mist.data.active_streams[i]],
                    fields: ['clients'],
                    start: -2
                  });
                }
                mist.send(function(){
                  $.extend(true,allstreams,mist.data.streams);
                  buildStreamTable();
                },{
                  totals: totals,
                  active_streams: true
                });
              }
              
              
              if (mist.data.LTS) {
                //insert folder streams
                var browserequests = 0;
                var browsecomplete = 0;
                for (var s in mist.data.streams) {
                  var inputs_f = mist.data.capabilities.inputs.Folder || mist.data.capabilities.inputs['Folder.exe'];
                  if (!inputs_f) { break; }
                  if (mist.inputMatch(inputs_f.source_match,mist.data.streams[s].source)) {
                    //this is a folder stream
                    allstreams[s].source += '*';
                    allstreams[s].filesfound = null;
                    mist.send(function(d,opts){
                      var s = opts.stream;
                      var matches = 0;
                      outer:
                      for (var i in d.browse.files) {
                        inner:
                        for (var j in mist.data.capabilities.inputs) {
                          if ((j.indexOf('Buffer') >= 0) || (j.indexOf('Buffer.exe') >= 0) || (j.indexOf('Folder') >= 0) || (j.indexOf('Folder.exe') >= 0)) { continue; }
                          if (mist.inputMatch(mist.data.capabilities.inputs[j].source_match,'/'+d.browse.files[i])) {
                            var streamname = s+'+'+d.browse.files[i];
                            allstreams[streamname] = createWcStreamObject(streamname,mist.data.streams[s]);
                            allstreams[streamname].source = mist.data.streams[s].source+d.browse.files[i];
                            
                            matches++;
                            if (matches >= 500) {
                              //stop retrieving more file names TODO properly display when this happens
                              allstreams[s+"+zzzzzzzzz"] = {
                                ischild: true,
                                name: "...",
                                online: -1
                              };
                              break outer;
                            }
                          }
                        }
                      }
                      if (('files' in d.browse) && (d.browse.files.length)) {
                        allstreams[s].filesfound = true;
                      }
                      else {
                        mist.data.streams[s].filesfound = false;
                      }
                      browsecomplete++;
                      if (browserequests == browsecomplete) {
                        mist.send(function(){
                          updateStreams();
                        },{active_streams: true});
                        
                        UI.interval.set(function(){
                          updateStreams();
                        },5e3);
                      }
                    },{browse:mist.data.streams[s].source},{stream: s});
                    browserequests++;
                  }
                }
                if (browserequests == 0) {
                  mist.send(function(){
                    updateStreams();
                  },{active_streams: true});
                  
                  UI.interval.set(function(){
                    updateStreams();
                  },5e3);
                }
              }
              else {
                mist.send(function(){
                  updateStreams();
                },{active_streams: true});
                
                UI.interval.set(function(){
                  updateStreams();
                },5e3);
              }
              break;
            }
          }
        }
        if (mist.data.LTS) {
          //browse into folder streams
          var browserequests = 0;
          var browsecomplete = 0;
          var select = {};
          var folders = [];
          for (var s in mist.data.streams) {
            var inputs_f = mist.data.capabilities.inputs.Folder || mist.data.capabilities.inputs['Folder.exe'];
            if (mist.inputMatch(inputs_f.source_match,mist.data.streams[s].source)) {
              //this is a folder stream
              folders.push(s);
              mist.send(function(d,opts){
                var s = opts.stream;
                var matches = 0;
                outer:
                for (var i in d.browse.files) {
                  inner:
                  for (var j in mist.data.capabilities.inputs) {
                    if ((j.indexOf('Buffer') >= 0) || (j.indexOf('Folder') >= 0)) { continue; }
                    if (mist.inputMatch(mist.data.capabilities.inputs[j].source_match,'/'+d.browse.files[i])) {
                      select[s+'+'+d.browse.files[i]] = true;
                      
                      matches++;
                      if (matches >= 500) {
                        //stop retrieving more file names
                        select[s+"+zzzzzzzzz"] = true;
                        break outer;
                      }
                    }
                  }
                }
                browsecomplete++;
                if (browserequests == browsecomplete) {
                  mist.send(function(){
                    for (var i in mist.data.active_streams) {
                      var split = mist.data.active_streams[i].split('+');
                      if ((split.length > 1) && (split[0] in mist.data.streams)) {
                        select[mist.data.active_streams[i]] = true;
                        allstreams[mist.data.active_streams[i]] = createWcStreamObject(mist.data.active_streams[i],mist.data.streams[split[0]]);
                      }
                    }
                    select = Object.keys(select);
                    select = select.concat(Object.keys(mist.data.streams));
                    select.sort();
                    createPage(other,select,folders);
                  },{active_streams: true});
                }
              },{browse:mist.data.streams[s].source},{stream: s});
              browserequests++;
            }
          }
          if (browserequests == 0) {
            mist.send(function(){
              //var select = [];
              for (var i in mist.data.active_streams) {
                var split = mist.data.active_streams[i].split('+');
                if ((split.length > 1) && (split[0] in mist.data.streams)) {
                  select[mist.data.active_streams[i]] = true;
                  allstreams[mist.data.active_streams[i]] = createWcStreamObject(mist.data.active_streams[i],mist.data.streams[split[0]]);
                }
              }
              select = Object.keys(select);
              if (mist.data.streams) { select = select.concat(Object.keys(mist.data.streams)); }
              select.sort();
              createPage(other,select);
            },{active_streams: true});
          }
        }
        else {
          createPage(other,Object.keys(mist.data.streams));
        }
        break;
      case 'Edit':
        if (typeof mist.data.capabilities == 'undefined') {
          mist.send(function(d){
            UI.navto(tab,other);
          },{capabilities: true});
          $main.append('Loading..');
          return;
        }
        
        
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
          var streamname = other;
          var saveas = mist.data.streams[streamname];
          $main.find('h2').append(' "'+streamname+'"');
        }
        
        var filetypes = [];
        for (var i in mist.data.capabilities.inputs) {
          filetypes.push(mist.data.capabilities.inputs[i].source_match);
        }
        var $inputoptions = $('<div>');
        
        function save(tab) {
          var send = {};
          
          if (!mist.data.streams) {
            mist.data.streams = {};
          }
          
          mist.data.streams[saveas.name] = saveas;
          if (other != saveas.name) {
            delete mist.data.streams[other];
          }
          
          send.addstream = {};
          send.addstream[saveas.name] = saveas;
          if (other != saveas.name) {
            send.deletestream = [other];
            }
          if ((saveas.stop_sessions) && (other != '')) {
            send.stop_sessions = other;
            delete saveas.stop_sessions;
          }
          mist.send(function(){
            delete mist.data.streams[saveas.name].online;
            delete mist.data.streams[saveas.name].error;
            UI.navto(tab,(tab == 'Preview' ? saveas.name : ''));
          },send);
          
          
        }
        
        var $style = $('<style>').text('button.saveandpreview { display: none; }');
        var $livestreamhint = $('<span>');
        function updateLiveStreamHint() {
          var streamname = $main.find('[name=name]').val();
          if (!streamname) { return; }
          var host = parseURL(mist.user.host);
          var source = $main.find('[name=source]').val();
          var passw = source.match(/@.*/);
          if (passw) { passw = passw[0].substring(1); }
          var ip = source.replace(/(?:.+?):\/\//,'');
          ip = ip.split('/');
          ip = ip[0];
          ip = ip.split(':');
          ip = ip[0];
          var custport = source.match(/:\d+/);
          if (custport) { custport = custport[0]; }
          
          
          var port = {};
          var trythese = ['RTMP','RTSP','RTMP.exe','RTSP.exe','TSSRT','TSSRT.exe'];
          for (var i in trythese) {
            if (trythese[i] in mist.data.capabilities.connectors) {
              port[trythese[i]] = mist.data.capabilities.connectors[trythese[i]].optional.port['default'];
            }
          }
          var defport = {
            RTMP: 1935,
            'RTMP.exe': 1935,
            RTSP: 554,
            'RTSP.exe': 554,
            TSSRT: -1,
            'TSSRT.exe': -1,
            TS: -1,
            'TS.exe': -1
          };
          for (var protocol in port) {
            for (var i in mist.data.config.protocols) {
              var p = mist.data.config.protocols[i];
              if (p.connector == protocol) {
                if ('port' in p) {
                  port[protocol] = p.port;
                }
                break;
              }
            }
            if (port[protocol] == defport[protocol]) { port[protocol] = ''; }
            else { port[protocol] = ':'+port[protocol]; }
          }
          port.TS = "";
          port["TS.exe"] = "";
          
          $livestreamhint.find('.field').closest('label').hide();
          for (var i in port) {
            var str;
            var useport = (custport ? custport : port[i]);
            switch(i) {
              case 'RTMP':
              case 'RTMP.exe':
                str = 'rtmp://'+host.host+useport+'/'+(passw ? passw : 'live')+'/';
                $livestreamhint.find('.field.RTMPurl').setval(str).closest('label').show();
                $livestreamhint.find('.field.RTMPkey').setval((streamname == '' ? 'STREAMNAME' : streamname)).closest('label').show();
                str += (streamname == '' ? 'STREAMNAME' : streamname);
                break;
              case 'TSSRT':
              case 'TSSRT.exe':
                str = 'srt://'+host.host+useport+'?streamname='+(streamname == '' ? 'STREAMNAME' : streamname);
                break;
              case 'RTSP':
              case 'RTSP.exe':
                str = 'rtsp://'+host.host+useport+'/'+(streamname == '' ? 'STREAMNAME' : streamname)+(passw ? '?pass='+passw : '');
                break;
              case 'TS':
              case 'TS.exe':
                str = 'udp://'+(ip == '' ? host.host : ip)+useport+'/';
                break;
            }
            $livestreamhint.find('.field.'+i.replace('.exe','')).setval(str).closest('label').show();
          }
        }
        
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
        $main.append(UI.buildUI([
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
            help: ("<p>\
                    Below is the explanation of the input methods for MistServer. Anything between brackets () will go to default settings if not specified.\
                  </p>\
                  <table class=valigntop>\
                    <tr>\
                      <th colspan=3><b>File inputs</b></th>\
                    </tr>\
                    <tr>\
                      <th>File</th>\
                      <td>\
                        Linux/MacOS:&nbsp;/PATH/FILE<br>\
                        Windows:&nbsp;/cygdrive/DRIVE/PATH/FILE\
                      </td>\
                      <td>\
                        For file input please specify the proper path and file.<br>\
                        Supported inputs are: DTSC, FLV, MP3. MistServer Pro has TS, MP4, ISMV added as input.\
                      </td>\
                    </tr>\
                      <th>\
                        Folder\
                      </th>\
                      <td>\
                        Linux/MacOS:&nbsp;/PATH/<br>\
                        Windows:&nbsp;/cygdrive/DRIVE/PATH/\
                      </td>\
                      <td>\
                        A folder stream makes all the recognised files in the selected folder available as a stream.\
                      </td>\
                    </tr>\
                    <tr><td colspan=3>&nbsp;</td></tr>\
                    <tr>\
                      <th colspan=3><b>Push inputs</b></th>\
                    </tr>\
                    <tr>\
                      <th>RTMP</th>\
                      <td>push://(IP)(@PASSWORD)</td>\
                      <td>\
                        IP is white listed IP for pushing towards MistServer, if left empty all are white listed.<br>\
                        PASSWORD is the application under which to push to MistServer, if it doesn\'t match the stream will be rejected. PASSWORD is MistServer Pro only.\
                      </td>\
                    </tr>\
                    <tr>\
                      <th>SRT</th>\
                      <td>push://(IP)</td>\
                      <td>\
                        IP is white listed IP for pushing towards MistServer, if left empty all are white listed.<br>\
                      </td>\
                    </tr>\
                    <tr>\
                      <th>RTSP</th>\
                      <td>push://(IP)(@PASSWORD)</td>\
                      <td>IP is white listed IP for pushing towards MistServer, if left empty all are white listed.</td>\
                    </tr>\
                    <tr>\
                      <th>TS</th>\
                      <td>tsudp://(IP):PORT(/INTERFACE)</td>\
                      <td>\
                        IP is the IP address used to listen for this stream, multi-cast IP range is: 224.0.0.0 - 239.255.255.255. If IP is not set all addresses will listened to.<br>\
                        PORT is the port you reserve for this stream on the chosen IP.<br>\
                        INTERFACE is the interface used, if left all interfaces will be used.\
                      </td>\
                    </tr>\
                    <tr><td colspan=3>&nbsp;</td></tr>\
                    <tr>\
                      <th colspan=3><b>Pull inputs</b></th>\
                    </tr>\
                    <tr>\
                      <th>DTSC</th>\
                      <td>dtsc://MISTSERVER_IP:PORT/(STREAMNAME)</td>\
                      <td>MISTSERVER_IP is the IP of another MistServer to pull from.<br>\
                        PORT is the DTSC port of the other MistServer. (default is 4200)<br>\
                        STREAMNAME is the name of the target stream on the other MistServer. If left empty, the name of this stream will be used.\
                      </td>\
                    </tr>\
                    <tr>\
                      <th>HLS</th>\
                      <td>http://URL/TO/STREAM.m3u8</td>\
                      <td>The URL where the HLS stream is available to MistServer.</td>\
                    </tr>\
                    <tr>\
                      <th>RTSP</th>\
                      <td>rtsp://(USER:PASSWORD@)IP(:PORT)(/path)</td>\
                      <td>\
                        USER:PASSWORD is the account used if authorization is required.<br>\
                        IP is the IP address used to pull this stream from.<br>\
                        PORT is the port used to connect through.<br>\
                        PATH is the path to be used to identify the correct stream.\
                      </td>\
                    </tr>\
                  </table>")
        ,
            'function': function(){
              var source = $(this).val();
              $style.remove();
              $livestreamhint.html('');
              if (source == '') { return; }
              var type = null;
              for (var i in mist.data.capabilities.inputs) {
                if (typeof mist.data.capabilities.inputs[i].source_match == 'undefined') { continue; }
                if (mist.inputMatch(mist.data.capabilities.inputs[i].source_match,source)) {
                  type = i;
                  break;
                }
              }
              if (type === null) {
                $inputoptions.html(
                  $('<h3>').text('Unrecognized input').addClass('red')
                ).append(
                  $('<span>').text('Please edit the stream source.').addClass('red')
                );
                return;
              }
              var input = mist.data.capabilities.inputs[type];
              $inputoptions.html(
                $('<h3>').text(input.name+' Input options')
              );
              var build = mist.convertBuildOptions(input,saveas);
              if (('always_match' in mist.data.capabilities.inputs[i]) && (mist.inputMatch(mist.data.capabilities.inputs[i].always_match,source))) {
                build.push({
                  label: 'Always on',
                  type: 'checkbox',
                  help: 'Keep this input available at all times, even when there are no active viewers.',
                  pointer: {
                    main: saveas,
                    index: 'always_on'
                  },
                  value: (other == "" && ((i == "TSSRT") || (i == "TSRIST")) ? true : false) //for new streams, if the input is TSSRT or TSRIST, put always_on true by default
                });
              }
              $inputoptions.append(UI.buildUI(build));
              if (input.name == 'Folder') {
                $main.append($style);
              }
              else if (['Buffer','Buffer.exe','TS','TS.exe'].indexOf(input.name) > -1) {
                var fields = [$("<br>"),$('<span>').text('Configure your source to push to:')];
                switch (input.name) {
                  case 'Buffer':
                  case 'Buffer.exe':
                    fields.push({
                      label: 'RTMP full url',
                      type: 'span',
                      clipboard: true,
                      readonly: true,
                      classes: ['RTMP'],
                      help: 'Use this RTMP url if your client doesn\'t ask for a stream key'
                    });
                    fields.push({
                      label: 'RTMP url',
                      type: 'span',
                      clipboard: true,
                      readonly: true,
                      classes: ['RTMPurl'],
                      help: 'Use this RTMP url if your client also asks for a stream key'
                    });
                    fields.push({
                      label: 'RTMP stream key',
                      type: 'span',
                      clipboard: true,
                      readonly: true,
                      classes: ['RTMPkey'],
                      help: 'Use this key if your client asks for a stream key'
                    });
                    fields.push({
                      label: 'SRT',
                      type: 'span',
                      clipboard: true,
                      readonly: true,
                      classes: ['TSSRT']
                    });
                    fields.push({
                      label: 'RTSP',
                      type: 'span',
                      clipboard: true,
                      readonly: true,
                      classes: ['RTSP']
                    });
                    break;
                  case 'TS':
                  case 'TS.exe':
                    if (source.charAt(0) == "/") {
                      fields = [];
                    }
                    else {
                      fields.push({
                        label: 'TS',
                        type: 'span',
                        clipboard: true,
                        readonly: true,
                        classes: ['TS']
                      });
                    }
                    break;
                }
                $livestreamhint.html(UI.buildUI(fields));
                updateLiveStreamHint();
              }
            }
          },{
            label: 'Stop sessions',
            type: 'checkbox',
            help: 'When saving these stream settings, kill this stream\'s current connections.',
            pointer: {
              main: saveas,
              index: 'stop_sessions'
            }
          },$livestreamhint,$('<br>'),{
            type: 'custom',
            custom: $inputoptions
          },$processes,
          {
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
        ]));
        
        $main.find('[name=name]').keyup(function(){
          updateLiveStreamHint();
        });
        updateLiveStreamHint();
        
        break;
      case 'Preview':
        
        if (other == '') { UI.navto('Streams'); }
        
        var parsed = parseURL(mist.user.host);
        var http_protocol = parsed.protocol;
        var http_host = parsed.host;
        var http_port = ':8080';
        var embedbase = http_protocol+http_host+http_port+'/';
        for (var i in mist.data.config.protocols) {
          var protocol = mist.data.config.protocols[i];
          if ((protocol.connector == 'HTTP') || (protocol.connector == 'HTTP.exe')) {
            if (protocol.pubaddr && protocol.pubaddr.length) {
              if (typeof protocol.pubaddr == "string") {
                embedbase = protocol.pubaddr.replace(/\/$/,'')+"/";
              }
              else if (protocol.pubaddr.length) {
                embedbase = protocol.pubaddr[0].replace(/\/$/,'')+"/";
              }
            }
            else {
              http_port = (protocol.port ? ':'+protocol.port : ':8080');
              embedbase = http_protocol+http_host+http_port+'/';
            }
            break;
          }
        }
        
        var $cont = $('<div>').css({
          'display':'flex',
          'flex-flow':'row wrap',
          'flex-shrink':1,
          'min-width':'auto'
        });
        var $edit = '';
        if (other.indexOf('+') == -1) {
          $edit = $('<button>').text('Settings').addClass('settings').click(function(){
            UI.navto('Edit',other);
          });
        }
        $main.html(
          $('<div>').addClass('bigbuttons').append($edit).append(
            $('<button>').text('Embed').addClass('embed').click(function(){
              UI.navto('Embed',other);
            })
          ).append(
            $('<button>').addClass('cancel').addClass('return').text('Return').click(function(){
              UI.navto('Streams');
            })
          )
        ).append(
          $('<h2>').text('Preview of "'+other+'"')
        ).append($cont);
        var escapedstream = encodeURIComponent(other);
        var $preview_cont = $('<div>').css("flex-shrink","1").css("min-width","auto").css("max-width","100%");
        $cont.append($preview_cont);
        var $title = $('<div>');
        var $video = $('<div>').text('Loading player..').css("max-width","100%").css("flex-shrink","1").css("min-width","auto");
        var $controls = $('<div>').addClass('controls');
        $preview_cont.append($video).append($title).append($controls);//.append($switches);
        
        if (!$("link#devcss").length) {
          $main.append(
            $("<link>").attr("rel","stylesheet").attr("type","text/css").attr("href",embedbase+"skins/dev.css").attr("id","devcss")
          );
        }
        
        function initPlayer(streamname) {
          if ((tab != "Preview") || (!other) || (other == "") || (streamname != other)) {
            return;
          }
          
          function afterInit() {
            var MistVideo = MistVideoObject.reference;
            
            $controls.html("");
            
            $controls.append(MistVideo.UI.buildStructure({
              type: "container",
              classes: ["mistvideo-column"],
              style: { flexShrink: 1 },
              children: [
                {
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
                },
                {type:"decodingIssues", style: {"max-width":"30em","flex-flow":"column nowrap"}},
                {
                  type: "container",
                  classes: ["mistvideo-column","mistvideo-devcontrols"],
                  children: [
                    {
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
                },
                {type:"log"}
              ]
            }));
          }
          $video[0].addEventListener("initialized",afterInit);
          $video[0].addEventListener("initializeFailed",afterInit);
          
          var options = {
            target: $video[0],
            host: embedbase,
            skin: "dev",
            loop: true,
            MistVideoObject: MistVideoObject
          };
          MistVideoObject.reference = mistPlay(streamname,options);
        }
        
        
        //load the player js
        function loadplayer() {
          $title.text('');
          var script = document.createElement('script');
          $main.append(script);
          script.src = embedbase+'player.js';
          script.onerror = function(){
            $video.html(
              $("<p>").append('Failed to load <a href="'+embedbase+'player.js">'+embedbase+'player.js</a>.')
            ).append(
              $("<p>").append("Please check if you've activated the HTTP protocol, if your http port is blocked, or if you're trying to load HTTPS on an HTTP page.")
            ).append(
              $('<button>').text('Reload').css('display','block').click(function(){
                loadplayer();
              })
            );
          };
          script.onload = function(){
            initPlayer(other);
            $main[0].removeChild(script);
          };
          
          //allow destroying while this script is loading
          MistVideoObject.reference = {
            unload: function(){
              script.onload = function(){
                if (this.parentElement) {
                  this.parentElement.removeChild(this);
                }
              };
            }
          };
        }
        loadplayer();
        
        //load the meta information
        var $trackinfo = $('<div>').append(
          $('<h3>').text('Meta information')
        );
        var $tracktable = $('<span>').text('Loading..');
        $trackinfo.append($tracktable);
        var $processinfo = $("<div>").addClass("process_info");
        $trackinfo.append($processinfo);
        $cont.append($trackinfo);
        function buildTrackinfo(info) {
          var meta = info.meta;
          if ((!meta) || (!meta.tracks)) { 
            $tracktable.html('No meta information available.');
            return;
          }
          
          var build = [];
          build.push({
            label: 'Type',
            type: 'span',
            value: (meta.live ? 'Live' : 'Pre-recorded (VoD)')
          });
          if ('format' in meta) {
            build.push({
              label: 'Format',
              type: 'span',
              value: meta.format
            });
          }
          if (meta.live) {
            build.push({
              label: 'Buffer window',
              type: 'span',
              value: UI.format.addUnit(meta.buffer_window,'ms')
            });
          }
          var tables = {
            audio: {
              vheader: 'Audio',
              labels: ['Codec','Duration','Avg bitrate','Peak bitrate','Channels','Samplerate','Language','Track index',""],
              content: []
            },
            video: {
              vheader: 'Video',
              labels: ['Codec','Duration','Avg bitrate','Peak bitrate','Size','Framerate','Language','Track index','Has B-Frames'],
              content: []
            },
            subtitle: {
              vheader: 'Subtitles',
              labels: ['Codec','Duration','Avg bitrate','Peak bitrate','Language','Track index',"","",""],
              content: []
            },
            meta: {
              vheader: 'Metadata',
              labels: ['Codec','Duration','Avg bitrate','Peak bitrate',"","","","",""],
              content: []
            }
          }
          var keys = Object.keys(meta.tracks);
          keys.sort(function(a,b){
            a = a.split('_').pop();
            b = b.split('_').pop();
            return a-b;
          });
          function peakoravg (track,key) {
            if ("maxbps" in track) {
              return UI.format.bytes(track[key],1);
            }
            else {
              if (key == "maxbps") {
                return UI.format.bytes(track.bps,1);
              }
              return "unknown";
            }
          }
          var trackindex = {audio:1,video:1,subtitle:1};
          for (var k in keys) {
            var i = keys[k];
            var track = meta.tracks[i];
            switch (track.type) {
              case 'audio':
                tables.audio.content.push({
                  header: 'Track '+i.split('_').pop(),
                  body: [
                    track.codec,
                    UI.format.duration((track.lastms-track.firstms)/1000)+'<br><span class=description>'+UI.format.duration(track.firstms/1000)+' to '+UI.format.duration(track.lastms/1000)+'</span>',
                    peakoravg(track,"bps"),
                    peakoravg(track,"maxbps"),
                    track.channels,
                    UI.format.addUnit(UI.format.number(track.rate),'Hz'),
                    ('language' in track ? track.language : 'unknown'),
                    (trackindex.audio),
                    ""
                  ]
                });
                trackindex.audio++;
                break;
              case 'video':
                tables.video.content.push({
                  header: 'Track '+i.split('_').pop(),
                  body: [
                    track.codec,
                    UI.format.duration((track.lastms-track.firstms)/1000)+'<br><span class=description>'+UI.format.duration(track.firstms/1000)+' to '+UI.format.duration(track.lastms/1000)+'</span>',
                    peakoravg(track,"bps"),
                    peakoravg(track,"maxbps"),
                    UI.format.addUnit(track.width,'x ')+UI.format.addUnit(track.height,'px'),
                    UI.format.addUnit(UI.format.number(track.fpks/1000),'fps'),
                    ('language' in track ? track.language : 'unknown'),
                    (trackindex.video),
                    ("bframes" in track ? "yes" : "no")
                  ]
                });
                trackindex.video++
                break;
              case 'meta':
              case 'subtitle':
                if ((track.codec == "subtitle") || (track.type == "subtitle")) {
                  tables.subtitle.content.push({
                    header: 'Track '+i.split('_').pop(),
                    body: [
                      track.codec,
                      UI.format.duration((track.lastms-track.firstms)/1000)+'<br><span class=description>'+UI.format.duration(track.firstms/1000)+' to '+UI.format.duration(track.lastms/1000)+'</span>',
                      peakoravg(track,"bps"),
                      peakoravg(track,"maxbps"),
                      ('language' in track ? track.language : 'unknown'),
                      (trackindex.subtitle),
                      "","",""
                    ]
                  });
                  trackindex.subtitle++
                  break;
                }
                else {
                  tables.meta.content.push({
                    header: 'Track '+i.split('_').pop(),
                    body: [
                      track.codec,
                      UI.format.duration((track.lastms-track.firstms)/1000)+'<br><span class=description>'+UI.format.duration(track.firstms/1000)+' to '+UI.format.duration(track.lastms/1000)+'</span>',
                      peakoravg(track,"bps"),
                      peakoravg(track,"maxbps"),
                      "","","","",""
                    ]
                  });
                  break;
                }
            }
          }
          var tracktypes = ['audio','video','subtitle','meta'];
          var $c = $('<div>').css({
            'display': 'flex',
            'flex-flow': 'row wrap',
            /*'justify-content': 'center',*/
            'font-size': '0.9em'
            /*'min-width': 'max-content'*/
          });
          for (var i in tracktypes) {
            if (tables[tracktypes[i]].content.length) {
              $c.append(UI.buildVheaderTable(tables[tracktypes[i]]).css('width','auto'));
            }
          }
          build.push($('<span>').text('Tracks:'))
          build.push($c);
          $tracktable.html(UI.buildUI(build));
        }
        
        if (escapedstream != ""){
          $.ajax({
            type: 'GET',
            url: embedbase+'json_'+escapedstream+'.js',
            success: function(d) {
              buildTrackinfo(d);
            },
            error: function(){
              $tracktable.html('Error while retrieving stream info.');
            }
          });
          function updateProcessInfo() {
            var req = {proc_list: other};
            if (!mist.data.capabilities) { req.capabilities = true; }
            mist.send(function(data){
              if (data.proc_list) {
                var $processes = $("<table>").css("width","auto");
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
                          $("<span>").text(UI.format.duration(d.active_seconds))
                        ).append(
                          $("<span>").addClass("description").text(" since "+UI.format.time(since/1e3))
                        ); 
                      },
                      "Pid:": function(d,i){ return i; },
                      "Logs:": function(d){
                        var $logs = $("<div>").text("None.");
                        if (d.logs && d.logs.length) {
                          $logs.html("").addClass("description").css({overflow: "auto", maxHeight: "6em", display: "flex", flexFlow: "column-reverse nowrap"});
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
                        if (d.ainfo && Object.keys(d.ainfo).length) {
                          $t = $("<table>");
                          for (var i in d.ainfo) {
                            var legend = mist.data.capabilities.processes[d.process].ainfo[i];
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
                $processinfo.html(
                  $("<h4>").text("Stream processes")
                ).append($processes);
                for (var i in layout) {
                  var $tr = $("<tr>");
                  $processes.append($tr);
                  $tr.append(
                    $("<th>").text(i).css("vertical-align","top")
                  );
                  for (var j in data.proc_list) {
                    $out = layout[i](data.proc_list[j],j);
                    $tr.append($("<td>").html($out).css("vertical-align","top"));
                  }
                }
              }
            },req);
          }
          UI.interval.set(function(){
            updateProcessInfo();
          },5e3);
          updateProcessInfo();
        }
        
        break;
      case 'Embed':
        if (other == '') { UI.navTo('Streams'); }
        
        var $edit = '';
        if (other.indexOf('+') == -1) {
          $edit = $('<button>').addClass('settings').text('Settings').click(function(){
            UI.navto('Edit',other);
          });
        }
        $main.html(
          $('<div>').addClass('bigbuttons').append($edit).append(
            $('<button>').text('Preview').addClass('preview').click(function(){
              UI.navto('Preview',other);
            })
          ).append(
            $('<button>').addClass('cancel').addClass('return').text('Return').click(function(){
              UI.navto('Streams');
            })
          )
        ).append(
          $('<h2>').text('Embed "'+other+'"')
        );
        
        var $embedlinks = $('<span>');
        $main.append($embedlinks);
        
        var escapedstream = encodeURIComponent(other);
        var parsed = parseURL(mist.user.host);
        var http_protocol = parsed.protocol;
        var http_host = parsed.host;
        var http_port = ':8080';
        var https_port;
        var puburls = {};
        var embedbase = {
          http: http_protocol+http_host+http_port+"/"
        };
        for (var i in mist.data.config.protocols) {
          var protocol = mist.data.config.protocols[i];
          if ((protocol.connector == 'HTTP') || (protocol.connector == 'HTTP.exe')) {
            if (protocol.pubaddr) {
              if (typeof protocol.pubaddr == "string") {
                embedbase.http = protocol.pubaddr.replace(/\/$/,'')+"/";
              }
              else if (protocol.pubaddr.length) {
                embedbase.http = protocol.pubaddr[0].replace(/\/$/,'')+"/";
              }
              puburls.http = protocol.pubaddr;
            }
            else {
              http_port = (protocol.port ? ':'+protocol.port : ':8080');
              embedbase.http = http_protocol+http_host+http_port+"/";
            }
          }
          else if ((protocol.connector == 'HTTPS') || (protocol.connector == 'HTTPS.exe')) {
            if (protocol.pubaddr && protocol.pubaddr.length) {
              if (typeof protocol.pubaddr == "string") {
                embedbase.https = protocol.pubaddr.replace(/\/$/,'')+"/";
              }
              else if (protocol.pubaddr.length) {
                embedbase.https = protocol.pubaddr[0].replace(/\/$/,'')+"/";
              }
              puburls.https = protocol.pubaddr;
            }
            else {
              https_port = (protocol.port ? ':'+protocol.port : ':4433');
              embedbase.https = "https://"+http_host+https_port+"/";
            }
            
          }
        }
        
        
        var otherbase = embedbase.http;
        var other_split = {
          http: embedbase.http
        };
        if ("https" in embedbase) {
          other_split.https = embedbase.https;
        }
        if ((otherhost.host) || (otherhost.https)) {
          otherbase = (otherhost.https && (https_port) ? 'https://' : 'http://')+(otherhost.host ? otherhost.host : parsed.host)+(otherhost.https && (https_port) ? https_port : http_port)+'/';
          if (otherhost.host) {
            if (!("http" in puburls)) {
              other_split.http = parseURL(other_split.http,{hostname:otherhost.host}).full;
            }
            if (("https" in other_split) && (!("https" in puburls))) {
              other_split.https = parseURL(other_split.https,{hostname:otherhost.host}).full;
            }
          }
          otherbase = (otherhost.https ? other_split.https : other_split.http);
        }
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
          
          var target = other+'_'+randomstring(12);
          
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
          output.push('    <a href="'+(otherhost.https ? other_split.https : other_split.http)+escapedstream+'.html'+'" target="_blank">');
          output.push('      Click here to play this video');
          output.push('    </a>');
          output.push('  </noscript>');
          output.push('  <script>');
          output.push('    var a = function(){');
          output.push('      mistPlay("'+other+'",{');
          output.push('        '+options.join(",\n        "));
          output.push('      });');
          output.push('    };');
          output.push('    if (!window.mistplayers) {');
          output.push('      var p = document.createElement("script");');
          if (("https" in embedbase) && (parseURL(embedbase.http).protocol != "https://")) {
            output.push('      if (location.protocol == "https:") { p.src = "'+other_split.https+'player.js" } ');
            output.push('      else { p.src = "'+other_split.http+'player.js" } ');
          }
          else {
            output.push('      p.src = "'+otherbase+'player.js"');
          }
          output.push('      document.head.appendChild(p);');
          output.push('      p.onload = a;');
          output.push('    }');
          output.push('    else { a(); }');
          output.push('  </script>');
          output.push('</div>');
          
          return output.join("\n");
        }
        
        var $protocolurls = $('<span>').text('Loading..');
        var emhtml = embedhtml(embedoptions);
        var $setTracks = $('<div>').text('Loading..').css('display','flex').css('flex-flow','column nowrap');
        
        
        var $usehttps = '';
        if ('https' in embedbase) {
          $usehttps = UI.buildUI([{
            label: 'Use HTTPS',
            type: 'checkbox',
            'function': function(){
              if ($(this).getval() != otherhost.https) {
                otherhost.https = $(this).getval();
                UI.navto('Embed',other);
              }
            },
            value: otherhost.https
          }]).find('label');
          
        }
        $embedlinks.append(
          $('<span>').addClass('input_container').append(
            $('<label>').addClass('UIelement').append(
              $('<span>').addClass('label').text('Use a different host:')
            ).append(
              $('<span>').addClass('field_container').append(
                $('<input>').attr('type','text').addClass('field').val((otherhost.host ? otherhost.host : parsed.host))
              ).append(
                $('<span>').addClass('unit').append(
                  $('<button>').text('Apply').click(function(){
                    otherhost.host = $(this).closest('label').find('input').val();
                    UI.navto('Embed',other);
                  })
                )
              )
            )
          ).append($usehttps)
        ).append(UI.buildUI([
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
            select: [['','Automatic']],
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
        
        $.ajax({
          type: 'GET',
          url: otherbase+'json_'+escapedstream+'.js?inclzero=1',
          success: function(d) {
            
            var build = [];
            var $s_forceType = $embedlinks.find('.field.forceType');
            var $s_prioritizeType = $embedlinks.find('.field.prioritize_type');
            for (var i in d.source) {
              var source = d.source[i];
              var human = UI.humanMime(source.type);
              
              build.push({
                label: (human ? human+' <span class=description>('+source.type+')</span>' : UI.format.capital(source.type)),
                type: 'str',
                value: source.url,
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
            
            $setTracks.html('');
            var tracks = {};
            if (d.meta) { 
              for (var i in d.meta.tracks) {
                var t = d.meta.tracks[i];
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
            }
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
            
            done = true;
          },
          error: function(){
            $protocolurls.html('Error while retrieving stream info.');
            $setTracks.closest('label').hide();
            embedoptions.setTracks = {};
          }
        });
        
        var script = document.createElement('script');
        script.src = embedbase.http+'player.js';
        document.head.appendChild(script);
        script.onload = function(){
          var $s_forcePlayer = $embedlinks.find('.field.forcePlayer');
          for (var i in mistplayers) {
            $s_forcePlayer.append(
              $('<option>').text(mistplayers[i].name).val(i)
            );
          }
          
          document.head.removeChild(this);
        };
        script.onerror = function(){
          document.head.removeChild(this);
        };
        break;
      case 'Push':
        var $c = $('<div>').text('Loading..'); //will contain everything
        $main.append($c);
        
        mist.send(function(d){
          $c.html(UI.buildUI([{
            type: 'help',
            help: 'You can push streams to files or other servers, allowing them to broadcast your stream as well.'
          }]));
          
          
          var push_settings = d.push_settings;
          if (!push_settings) { push_settings = {}; }
          
          var $push = $('<table>').append(
            $('<tr>').append(
              $('<th>').text('Stream')
            ).append(
              $('<th>').text('Target')
            ).append(
              $('<th>')
            ).append(
              $('<th>')
            )
          );
          var $autopush = $push.clone();
          //check if push ids have been stopped untill the answer is yes
          function checkgone(ids) {
            setTimeout(function(){
              mist.send(function(d){
                var gone = false;
                if (('push_list' in d) && (d.push_list) && (d.push_list.length)) {
                  gone = true;
                  for (var i in d.push_list) {
                    if (ids.indexOf(d.push_list[i][0]) > -1) {
                      gone = false;
                      break;
                    }
                  }
                }
                else {
                  gone = true;
                }
                if (gone) {
                  for (var i in ids) {
                    $push.find('tr[data-pushid='+ids[i]+']').remove();
                  }
                }
                else {
                  checkgone();
                }
              },{push_list:1});
            },1e3);
          }
          var showing = false;
          function buildTr(push,type) {
            var $target = $('<span>');
            var $logs = $("<span>");
            if ((type == "Automatic") && (push.length >= 4)) {

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

              $target.append(
                $('<span>').text(push[2])
              );
              if (push[3]) {
                $target.append(
                  $('<span>').text(', schedule on '+(new Date(push[3]*1e3)).toLocaleString())
                );
              }
              if ((push.length >= 5) && (push[4])) {
                $target.append(
                  $('<span>').text(", complete on "+(new Date(push[4]*1e3)).toLocaleString())
                );
              }
              if ((push.length >= 8) && (push[5])) {
                $target.append(
                  $('<span>').text(", starts if "+printPrettyComparison(push[5],push[6],push[7]))
                );
              }
              if ((push.length >= 11) && (push[8])) {
                $target.append(
                  $('<span>').text(", stops if "+printPrettyComparison(push[8],push[9],push[10]))
                );
              }

            }
            else if ((push.length >= 4) && (push[2] != push[3])) {
              $target.append(
                $('<span>').text(push[2])
              ).append(
                $('<span>').html('&#187').addClass('unit').css('margin','0 0.5em')
              ).append(
                $('<span>').text(push[3])
              );
            }
            else {
              $target.append(
                $('<span>').text(push[2])
              );
            }
            var $buttons = $('<td>').append(
              $('<button>').text((type == 'Automatic' ? 'Remove' : 'Stop')).click(function(){
                if (confirm("Are you sure you want to "+$(this).text().toLowerCase()+" this push?\n"+push[1]+' to '+push[2])) {
                  var $tr = $(this).closest('tr');
                  $tr.html(
                    $('<td colspan=99>').html(
                      $('<span>').addClass('red').text((type == 'Automatic' ? 'Removing..' : 'Stopping..'))
                    )
                  );
                  if (type == 'Automatic') {
                    var a = push.slice(1);
                    mist.send(function(){
                      $tr.remove();
                    },{'push_auto_remove':[a]});
                  }
                  else {
                    mist.send(function(d){
                      checkgone([push[0]]);
                    },{'push_stop':[push[0]]});
                  }
                }
              })
            );
            if (type == 'Automatic') {
              $buttons.prepend(
                $("<button>").text("Edit").click(function(){
                  UI.navto("Start Push","auto_"+($(this).closest("tr").index()-1));
                })
              );
              $buttons.append(
                $('<button>').text('Stop pushes').click(function(){
                  if (confirm("Are you sure you want to stop all pushes matching \n\""+push[1]+' to '+push[2]+"\"?"+(push_settings.wait != 0 ? "\n\nRetrying is enabled. You'll probably want to set that to 0." : ''))) {
                    var $button = $(this);
                    $button.text('Stopping pushes..');
                    //also stop the matching pushes
                    var pushIds = [];
                    for (var i in d.push_list) {
                      //                streamname                        target
                      if ((push[1] == d.push_list[i][1]) && (push[2] == d.push_list[i][2])) {
                        pushIds.push(d.push_list[i][0]);
                        $push.find('tr[data-pushid='+d.push_list[i][0]+']').html(
                          $('<td colspan=99>').html(
                            $('<span>').addClass('red').text('Stopping..')
                          )
                        );
                      }
                    }
                    
                    mist.send(function(){
                      $button.text('Stop pushes');
                      checkgone(pushIds);
                    },{
                      push_stop: pushIds,
                      push_settings: {wait: 0}
                    });
                  }
                })
              );
            }
            else {
              //it's a non-automatic push
              if (push.length >= 6) {
                var stats = push[5];
                $logs.append(
                  $("<div>").append(
                    "Active for: "+UI.format.duration(stats.active_seconds)
                  )
                ).append(
                  $("<div>").append(
                    "Data transfered: "+UI.format.bytes(stats.bytes)
                  )
                ).append(
                  $("<div>").append(
                    "Media time transfered: "+UI.format.duration(stats.mediatime*1e-3)
                  )
                );
                if ("pkt_retrans_count" in stats) {
                  $logs.append(
                    $("<div>").append(
                      "Packets retransmitted: "+UI.format.number(stats.pkt_retrans_count || 0)
                    )
                  );
                }
                if ("pkt_loss_count" in stats) {
                  $logs.append(
                    $("<div>").append(
                      "Packets lost: "+UI.format.number(stats.pkt_loss_count || 0)+" ("+UI.format.addUnit(UI.format.number(stats.pkt_loss_perc || 0),"%")+" over the last "+UI.format.addUnit(5,"s")+")"
                    )
                  );
                }
              }
              if (push.length >= 5) {
                //there are logs
                for (var i in push[4]) {
                  var item = push[4][i];
                  $logs.append(
                    $("<div>").append(
                      UI.format.time(item[0])+' ['+item[1]+'] '+item[2]
                    )
                  );
                }
              }
              
            }
            return $('<tr>').css("vertical-align","top").attr('data-pushid',push[0]).append(
              $('<td>').text(push[1])
            ).append(
              $('<td>').append($target.children())
            ).append(
              $("<td>").addClass("logs").append($logs.children())
            ).append(
              $buttons
            );
          }
          
          if ('push_list' in d) {
            for (var i in d.push_list) {
              $push.append(buildTr(d.push_list[i],'Manual'));
            }
          }
          if ('push_auto_list' in d) {
            for (var i in d.push_auto_list) {
              var a = d.push_auto_list[i].slice();
              a.unshift(-1);
              $autopush.append(buildTr(a,'Automatic'));
            }
          }
          
          $c.append(
            $('<h3>').text('Automatic push settings')
          ).append(
            UI.buildUI([
              {
                label: 'Delay before retry',
                unit: 's',
                type: 'int',
                min: 0,
                help: 'How long the delay should be before MistServer retries an automatic push.<br>If set to 0, it does not retry.',
                'default': 3,
                pointer: {
                  main: push_settings,
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
                  main: push_settings,
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
                      push_settings: push_settings
                    })
                  }
                }]
              }
            ])
          ).append(
            $('<h3>').text('Automatic push settings')
          ).append(
            $('<button>').text('Add an automatic push').click(function(){
              UI.navto('Start Push','auto');
            })
          );
          if ($autopush.find('tr').length == 1) {
            $c.append(
              $('<div>').text('No automatic pushes have been configured.').addClass('text').css('margin-top','0.5em')
            );
          }
          else {
            $c.append($autopush);
          }
          
          $c.append(
            $('<h3>').text('Pushes')
          ).append(
            $('<button>').text('Start a push').click(function(){
              UI.navto('Start Push');
            })
          );
          if ($push.find('tr').length == 1) {
            $c.append(
              $('<div>').text('No pushes are active.').addClass('text').css('margin-top','0.5em')
            );
          }
          else {
            var streams = [];
            var targets = [];
            var $select_streams = $('<select>').css('margin-left','0.5em').append(
              $('<option>').text('Any stream').val('')
            );
            var $select_targets = $('<select>').css('margin-left','0.5em').append(
              $('<option>').text('Any target').val('')
            );
            for (var i in d.push_list) {
              if (streams.indexOf(d.push_list[i][1]) == -1) {
                streams.push(d.push_list[i][1]);
              }
              if (targets.indexOf(d.push_list[i][2]) == -1) {
                targets.push(d.push_list[i][2]);
              }
            }
            streams.sort();
            targets.sort();
            for (var i in streams) {
              $select_streams.append(
                $('<option>').text(streams[i])
              );
            }
            for (var i in targets) {
              $select_targets.append(
                $('<option>').text(targets[i])
              );
            }
            
            $c.append(
              $('<button>').text('Stop all pushes').click(function(){
                var push_list = [];
                for (var i in d.push_list) {
                  push_list.push(d.push_list[i][0]);
                }
                if (push_list.length == 0) { return; }
                if (confirm('Are you sure you want to stop all pushes?')) {
                  mist.send(function(d){
                    checkgone(push_list);
                  },{push_stop:push_list});
                  $push.find('tr:not(:first-child)').html(
                    $('<td colspan=99>').append(
                      $('<span>').addClass('red').text('Stopping..')
                    )
                  );
                  $(this).remove();
                }
                
              })
            ).append(
              $('<label>').css('margin-left','1em').append(
                $('<span>').text('Stop all pushes that match: ').css('font-size','0.9em')
              ).append(
                $select_streams
              ).append(
                $('<span>').css('margin-left','0.5em').text('and').css('font-size','0.9em')
              ).append(
                $select_targets
              ).append(
                $('<button>').css('margin-left','0.5em').text('Apply').click(function(){
                  var s = $select_streams.val();
                  var t = $select_targets.val();
                  
                  if ((s == '') && (t == '')) { return alert('Looks like you want to stop all pushes. Maybe you should use that button?'); }
                  var pushes = {};
                  
                  for (var i in d.push_list) {
                    if  (((s == '') || (d.push_list[i][1] == s)) && ((t == '') || (d.push_list[i][2] == t))) {
                      pushes[d.push_list[i][0]] = d.push_list[i];
                    }
                  }
                  
                  if (Object.keys(pushes).length == 0) {
                    return alert('No matching pushes.');
                  }
                  
                  var msg = 'Are you sure you want to stop these pushes?'+"\n\n";
                  for (var i in pushes) {
                    msg += pushes[i][1]+' to '+pushes[i][2]+"\n";
                  }
                  if (confirm(msg)) {
                    pushes = Object.keys(pushes);
                    mist.send(function(d){
                      checkgone(pushes);
                    },{'push_stop':pushes});
                    for (var i in pushes) {
                      $push.find('tr[data-pushid='+pushes[i]+']').html(
                        $('<td colspan=99>').html(
                          $('<span>').addClass('red').text('Stopping..')
                        )
                      );
                    }
                  }
                })
              )
            ).append($push);
          }
          
          UI.interval.set(function(){
            mist.send(function(d){
              var $header = $push.find("tr").first();
              $push.empty();
              $push.append($header);
              for (var i in d.push_list) {
                $push.append(buildTr(d.push_list[i]));
              }
            },{push_list:1});
          },5e3);
          
        },{push_settings:1,push_list:1,push_auto_list:1});
        
        break;
      case 'Start Push':
        
        if (!('capabilities' in mist.data) || !('variable_list' in mist.data) || !('external_writer_list' in mist.data)) {
          $main.append('Loading Mist capabilities..');
          mist.send(function(){
            UI.navto('Start Push',other);
          },{capabilities:1,variable_list:true,external_writer_list:true});
          return;
        }
        
        var allthestreams;
        function buildTheThings(edit) {
          var editid = false;
          var o = other.split("_");
          other = o[0];
          if (o.length == 2) { editid = o[1]; }
          
          if ((editid !== false) && (typeof edit == "undefined")) {
            mist.send(function(d){
              buildTheThings(d.push_auto_list[editid]);
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
          file_match.sort();
          prot_match.sort();
          
          if (other == 'auto') {
            $main.find('h2').text('Add automatic push');
          }
          
          
          var saveas = {params:{}};
          var params = []; //will contain all target url params as an array
          if ((other == "auto") && (typeof edit != "undefined")) {
            saveas = {
              "stream": edit[0],
              "target": edit[1],
              "params": {}
            };
            
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
            
            if (edit.length >= 3) { saveas.scheduletime = edit[2] != 0 ? edit[2] : null; }
            if (edit.length >= 4) { saveas.completetime = edit[3] != 0 ? edit[3] : null; }
            if (edit.length >= 5) { saveas.startVariableName     = edit[4] != '' ? edit[4] : null; }
            if (edit.length >= 6) { saveas.startVariableOperator = edit[5] != '' ? edit[5] : null; }
            if (edit.length >= 7) { saveas.startVariableValue    = edit[6] != '' ? edit[6] : null; }
            if (edit.length >= 8) { saveas.endVariableName       = edit[7] != '' ? edit[7] : null; }
            if (edit.length >= 9) { saveas.endVariableOperator   = edit[8] != '' ? edit[8] : null; }
            if (edit.length >= 10){ saveas.endVariableValue      = edit[9] != '' ? edit[9] : null; }

          }
          var $additional_params = $("<div>").css("margin","1em 0");
          var $autopush = $("<div>");
          var push_parameters;
          if (other == "auto") {
            $autopush.css("margin","1em 0").html(UI.buildUI([{
              label: "This push should be active",
              help: "When 'based on server time' is selected, a start and/or end timestamp can be configured. When it's 'based on a variable', the push will be activated while the specified variable matches the specified value.",
              type: "select",
              select: [["time","Based on server time"],["variable","Based on a variable"]],
              value: (saveas.startVariableName || saveas.endVariableName ? "variable" : "time"),
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
                  $autopush.find("[name=\"startVariableOperator\"]").trigger("change");
                  $autopush.find("[name=\"endVariableOperator\"]").trigger("change");
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
              pointer: { main: saveas, index: "startVariableName" }
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
              css: {display:"none"},
              help: "How would you like to compare this variable?",
              pointer: { main: saveas, index: "startVariableOperator" },
              "function": function(){
                var $varvalue = $autopush.find("[name=\"startVariableValue\"]").closest(".UIelement");
                if (Number($(this).getval()) < 2) {
                  $varvalue.hide();
                }
                else {
                  $varvalue.css("display","");
                }
              }
            },{
              classes: ["varbased"],
              label: "Variable value",
              type: "str",
              help: "The variable will be compared with this value to determine if this push should be started.<br>You can also enter another variable here!",
              datalist: Object.values(mist.data.variable_list || []).map(function(a){return typeof a == "string" ? a : a[3]}).concat(Object.keys(mist.data.variable_list || []).map(function(a){ return "$"+a; })),
              pointer: { main: saveas, index: "startVariableValue" }
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
              pointer: { main: saveas, index: "endVariableName" }
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
              pointer: { main: saveas, index: "endVariableOperator" },
              "function": function(){
                var $varvalue = $autopush.find("[name=\"endVariableValue\"]").closest(".UIelement");
                if (Number($(this).getval()) < 2) {
                  $varvalue.hide();
                }
                else {
                  $varvalue.css("display","");
                }
              }
            },{
              classes: ["varbased"],
              label: "Variable value",
              type: "str",
              help: "The variable will be compared with this value to determine if this push should be stopped.<br>You can also enter another variable here!",
              datalist: Object.values(mist.data.variable_list || []).map(function(a){return typeof a == "string" ? a : a[3]}).concat(Object.keys(mist.data.variable_list || []).map(function(a){ return "$"+a; })),
              pointer: { main: saveas, index: "endVariableValue" }
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
            }],saveas));
            $autopush.find(".activewhen").trigger("change");
          }
          var build = [{
            label: 'Stream name',
            type: 'str',
            help: 'This may either be a full stream name, a partial wildcard stream name, or a full wildcard stream name.<br>For example, given the stream <i>a</i> you can use:\
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
            datalist: allthestreams
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
              if (!match) {
                $additional_params.html(
                  $("<h4>").addClass("red").text("Unrecognized target.")
                ).append(
                  $("<span>").text("Please edit the push target.")
                );
                return;
              }
              $additional_params.html($("<h3>").text(mist.data.capabilities.connectors[match].friendly.replace("over HTTP","")));
              push_parameters = {};
              //filter out protocol only or file only options. This does not need to be dynamic as when the target changes, the whole $additional_params container is overwritten anyway
              for (var i in mist.data.capabilities.connectors[match].push_parameters) {
                var param = mist.data.capabilities.connectors[match].push_parameters[i];
                if (param.prot_only && String().match && (val.match(/.+\:\/\/.+/) === null)) { continue; }
                if (param.file_only && (val[0] != "/")) { continue; }
                push_parameters[i] = param;
              }

              var capa = {
                desc: mist.data.capabilities.connectors[match].desc.replace("over HTTP",""),
                optional: push_parameters,
                sort: "sort"
              };
              var capaform = mist.convertBuildOptions(capa,saveas.params);

              //find left over url params that are not covered by this connector's capabilities
              var custom_params = [];
              for (var i in params) {
                var p = params[i].split("=");
                var name = p[0];
                if (!(name in push_parameters)) {
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
          },
          $autopush,
          $additional_params];
          
          
          
          /*
          if (other == "auto") { //options only for automatic pushes
            
            build.push($("<h4>").text("Optional parameters"),{
              type: "unix",
              label: "Schedule time",
              min: 0,
              help: "The time where the push will become active. The default is to start immediately.",
              pointer: {
                main: saveas,
                index: "scheduletime"
              }
            },{
              type: "unix",
              label: "Recording start time",
              min: 0,
              help: "Where in the media buffer the recording will start. Defaults to the most recently received keyframe.<br>Only makes sense for live streams.",
              pointer: {
                main: saveas,
                index: "recstartunix"
              }
            },{
              type: "unix",
              label: "Complete time",
              min: 0,
              help: "The time where the push will stop. Defaults to never stop automatically.<br>Only makes sense for live streams.",
              pointer: {
                main: saveas,
                index: "completetime"
              }
            });
            
          }*/
          
          build.push({
            type: 'buttons',
            buttons: [{
              type: 'cancel',
              label: 'Cancel',
              'function': function(){
                UI.navto('Push');
              }
            },{
              type: 'save',
              label: 'Save',
              preSave: function(){
                //is executed before the variables are saved
                
                //clean the object of old settings that may not be part of the current form 
                delete saveas.startVariableName;
                delete saveas.startVariableOperator;
                delete saveas.startVariableValue;
                delete saveas.endVariableName;
                delete saveas.endVariableOperator;
                delete saveas.endVariableValue;
                delete saveas.completetime;
                delete saveas.scheduletime;
              },
              'function': function(){
                var params = saveas.params;
                for (var i in params) {
                  if (params[i] === null) {
                    //remove any params that are set to null
                    delete params[i];
                  }
                  else if (!(i in push_parameters)) {
                    //remove any params that are not supported by this protocol (they will have been duplicatec to saveas.custom_url_parameters if the user wanted to keep them)
                    delete params[i];
                  }
                }
                if (saveas.startVariableName || saveas.endVariableName) {
                  saveas.scheduletime = 0;
                  saveas.completetime = 0;
                }
                if (saveas.startVariableName === null) {
                  delete saveas.startVariableName;
                  delete saveas.startVariableOperator;
                  delete saveas.startVariableValue;
                }
                if (saveas.endVariableName === null) {
                  delete saveas.endVariableName;
                  delete saveas.endVariableOperator;
                  delete saveas.endVariableValue;
                }
                if (saveas.scheduletime) {
                  params["recstartunix"] = saveas.scheduletime;
                }
                if (Object.keys(params).length || (saveas.custom_url_params && saveas.custom_url_params.length)) {
                  var append = "?";
                  var curparams = saveas.target.split("?");
                  if (curparams.length > 1) {
                    append = "&";
                    curparams = curparams[curparams.length-1];
                    curparams = curparams.split("&");
                    for (var i in curparams) {
                      var key = curparams[i].split("=")[0];
                      if (key in params) { delete params[key]; }
                    }
                  }
                  if (Object.keys(params).length || (saveas.custom_url_params && saveas.custom_url_params.length)) {
                    var str = [];
                    for (var i in params) {
                      str.push(i+"="+params[i]);
                    }
                    for (var i in saveas.custom_url_params) {
                      str.push(saveas.custom_url_params[i]);
                    }
                    append += str.join("&");
                    saveas.target += append;
                  }
                }
                delete saveas.params; //these are now part of the target url and we don't need them seperately
                delete saveas.custom_url_params;
                
                var obj = {};
                obj[(other == 'auto' ? 'push_auto_add' : 'push_start')] = saveas;
                if ((typeof edit != "undefined") && ((edit[0] != saveas.stream) || (edit[1] != saveas.target))) {
                  obj.push_auto_remove = [edit];
                }
                
                mist.send(function(){
                  UI.navto('Push');
                },obj);
              }
            }]
          });
          
          $main.append(UI.buildUI(build));
        }
        
        if (mist.data.LTS) {
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
        }
        else {
          allthestreams = Object.keys(mist.data.streams);
          buildTheThings();
        }
        
        break;
      case 'Triggers':
        if (!('triggers' in mist.data.config) || (!mist.data.config.triggers)) {
          mist.data.config.triggers = {};
        }
        
        var $tbody = $('<tbody>');
        var $table = $('<table>').html(
          $('<thead>').html(
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
            UI.navto(tab,other);
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
          help: 'For triggers that can apply to specific streams, this value decides what streams they are triggered for. (none checked = always triggered)',
          type: 'checklist',
          checklist: Object.keys(mist.data.streams)
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
                UI.navto('Triggers');
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
                if (!("triggers" in mist.data.config)) {
                  mist.data.config.triggers = {};
                }
                if (!(saveas.triggeron in mist.data.config.triggers)) {
                  mist.data.config.triggers[saveas.triggeron] = [];
                }
                mist.data.config.triggers[saveas.triggeron].push(newtrigger);
                
                mist.send(function(){
                  UI.navto('Triggers');
                },{config: mist.data.config});
              }
            }
          ]
        }]));
        $('[name=triggeron]').trigger('change');
        
        break;
      case 'Logs':
        var $refreshbutton = $('<button>').text('Refresh now').click(function(){
          $(this).text('Loading..');
          mist.send(function(){
            buildLogsTable();
            $refreshbutton.text('Refresh now');
          });
        }).css('padding','0.2em 0.5em').css('flex-grow',0);
        
        $main.append(UI.buildUI([{
          type: 'help',
          help: 'Here you have an overview of all edited settings within MistServer and possible warnings or errors MistServer has encountered. MistServer stores up to 100 logs at a time.'
        },{
          label: 'Refresh every',
          type: 'select',
          select: [
            [10,'10 seconds'],
            [30,'30 seconds'],
            [60,'minute'],
            [300,'5 minutes']
          ],
          value: 30,
          'function': function(){
            UI.interval.clear();
            UI.interval.set(function(){
              mist.send(function(){
                buildLogsTable();
              });
            },$(this).val()*1e3);
          },
          help: 'How often the table below should be updated.'
        },{
          label: '..or',
          type: 'DOMfield',
          DOMfield: $refreshbutton,
          help: 'Instantly refresh the table below.'
        }]));
        
        $main.append(
          $('<button>').text('Purge logs').click(function(){
            mist.send(function(){
              mist.data.log = [];
              UI.navto('Logs');
            },{clearstatlogs:true});
          })
        );
        var $tbody = $('<tbody>').css('font-size','0.9em');
        $main.append(
          $('<table>').addClass('logs').append($tbody)
        );
        
        function color(string){
          var $s = $('<span>').text(string);
          switch (string) {
            case 'WARN':
              $s.addClass('orange');
              break;
            case 'ERROR':
            case 'FAIL':
              $s.addClass('red');
              break;
          }
          return $s;
        }
        function buildLogsTable(){
          var logs = mist.data.log;
          if (!logs) { return; }
          
          if ((logs.length >= 2) && (logs[0][0] < logs[logs.length-1][0])){
            logs.reverse();
          }
          
          $tbody.html('');
          for (var index in logs) {
            var $content = $('<span>').addClass('content');
            var split = logs[index][2].split('|');
            for (var i in split) {
              $content.append(
                $('<span>').text(split[i])
              );
            }
            
            $tbody.append(
              $('<tr>').html(
                $('<td>').text(UI.format.dateTime(logs[index][0],'long')).css('white-space','nowrap')
              ).append(
                $('<td>').html(color(logs[index][1])).css('text-align','center')
              ).append(
                $('<td>').html($content).css('text-align','left')
              )
            );
          }
        }
        buildLogsTable();
        
        break;
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
            UI.navto(tab);
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
          
          var memory = {
            vheader: 'Memory',
            labels: ['Used','Cached','Available','Total'],
            content: [
              {
                header: 'Physical memory',
                body: [
                  UI.format.bytes(mem.used*1024*1024)+' ('+UI.format.addUnit(load.memory,'%')+')',
                  UI.format.bytes(mem.cached*1024*1024),
                  UI.format.bytes(mem.free*1024*1024),
                  UI.format.bytes(mem.total*1024*1024)
                ]
              },{
                header: 'Swap memory',
                body: [
                  UI.format.bytes((mem.swaptotal-mem.swapfree)*1024*1024),
                  UI.format.addUnit('','N/A'),
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
        sessionStorage.removeItem('mistLogin');
        
        UI.navto('Login');
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
      data: {command:JSON.stringify(data)},
      dataType: 'jsonp',
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
      success: function(d){
        log('Receive',$.extend(true,{},d),'as reply to',opts.sendData);
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
            var keep = ['config','capabilities','ui_settings','LTS','active_streams','browse','log','totals','bandwidth','variable_list','external_writer_list']; //streams was already copied above
            for (var i in save) {
              if (keep.indexOf(i) == -1) {
                delete save[i];
              }
            }
            if (("bandwidth" in data) && (!("bandwidth" in d))) {
              save.bandwidth = null;
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
      var query = match[s].replace(/[^\w\s]/g,'\\$&'); //prefix any special chars with a \
      query = query.replace(/\\\*/g,'.*'); //replace * with any amount of .*
      var regex = new RegExp('^(?:[a-zA-Z]\:)?'+query+'(?:\\?[^\\?]*)?$','i'); //case insensitive, and ignore everything after the last ?
      if (regex.test(string)){
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
            if (obj.validate.indexOf("required") >= 0) {
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
          case 'bool': {
            obj.type = 'checkbox';
            break;
          }
          case 'unixtime': {
            obj.type = 'unix';
            break;
          }
          case 'json':
          case 'debug':
          case 'inputlist': {
            obj.type = ele.type;
            break;
          }
          default:
            obj.type = 'str';
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
  stored: {
    get: function(){
      return mist.data.ui_settings || {};
    },
    set: function(name,val){
      var settings = this.get();
      settings[name] = val;
      mist.send(function(){
        
      },{ui_settings: settings});
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
  }
  return val;
}
$.fn.setval = function(val){
  var opts = $(this).data('opts');
  $(this).val(val);
  if ((opts) && ('type' in opts)) {
    var type = opts.type;
    switch (type) { //exceptions only
      case 'span':
        $(this).html(val);
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
          $(this).children("label").first().find(".field_container").children().first().setval(val);
          $(this).children("select").first().val("CUSTOM").trigger("change");
        }
        break;
      case "inputlist":
        if (typeof val == "string") { val = [val]; }
        for (var i in val) {
          var $newitem = $(this).data("newitem")();
          $newitem.find(".field").setval(val[i]);
          $(this).append($newitem);
        }
        $(this).append($(this).children().first()); //put empty input last
        break;
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
  $(this).trigger('change');
  return $(this);
}
function parseURL(url,set) {
  var a = document.createElement('a');
  a.href = url;
  if (set) {
    for (var i in set) {
      a[i] = set[i];
    }
  }
  return {
    full: a.href,
    protocol: a.protocol+'//',
    host: a.hostname,
    port: (a.port ? ':'+a.port : '')
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
