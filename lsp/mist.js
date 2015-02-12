
$(function(){
  UI.elements = {
    menu: $('nav > .menu'),
    secondary_menu: $('nav.secondary_menu'),
    main: $('main'),
    connection: {
      status: $('#connection'),
      user_and_host: $('#user_and_host'),
      msg: $('#message')
    }
  };
  UI.buildMenu();
  UI.stored.getOpts();
  
  //check if username and host have been stored in the url
  if (location.hash) {
    var hash = location.hash.substring(1).split('@');
    var user = hash[0].split('&');
    mist.user.name = user[0];
    if (user[1]) { mist.user.host = user[1]; }
  }
  
  //check if we are logged in
  mist.send(function(d){
    //we're logged in
    $(window).trigger('hashchange');
  },{},{timeout: 5, hide: true});
  
});

$(window).on('hashchange', function(e) {
  var loc = location.hash.substring(1).split('@');
  if (!loc[1]) { loc[1] = ''; }
  var tab = loc[1].split('&');
  if (tab[0] == '') { tab[0] = 'Overview'; }
  UI.showTab(tab[0],tab[1]);
});

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
  interval: false,
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
  menu: [
    {
      Overview: {},
      Protocols: {},
      Streams: {},
      Preview: {},
      Limits: {
        LTSonly: true
      },
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
        link: 'http://mistserver.org/wiki/Category:Guides'
      },
      Tools: {
        submenu: {
          'Release notes': {
            link: 'http://mistserver.org/wiki/Mistserver_Changelog'
          },
          'Mist Shop': {
            link: 'http://mistserver.org/products'
          },
          'Email for Help': {},
          'Terms & Conditions': {
            link: 'http://mistserver.org/wiki/Mistserver_license'
          }
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
      if ('LTSonly' in button) {
        $button.addClass('LTSonly');
      }
      if ('link' in button) {
        $button.attr('href',button.link).attr('target','_blank');
      }
      else if (!('submenu' in button)) {
        $button.click(function(e){
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
        var $bc = $('<span>').addClass('button_container');
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
          $bc.append($b);
          switch (button.type) {
            case 'cancel':
              $b.addClass('cancel').click(button['function']);
              break;
            case 'save':
              $b.addClass('save').click(function(e){
                var $ic = $(this).closest('.input_container');
                //validate
                var error = false;
                $ic.find('.hasValidate').each(function(){
                  var vf = $(this).data('validate');
                  error = vf(this);
                  if (error) {
                    return false; //break loop
                  }
                });
                if (error) { return; } //validation failed
                
                //for all inputs
                $ic.find('.isSetting').each(function(){
                  var val = $(this).getval();
                  var pointer = $(this).data('pointer');
                  
                  if (val == '') {
                    if ('default' in $(this).data('opts')) {
                      val = $(this).data('opts')['default'];
                    }
                    else {
                      //this value was not entered
                      delete pointer.main[pointer.index];
                      return true; //continue
                    }
                  }
                  
                  //save
                  pointer.main[pointer.index] = val;
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
        $('<span>').addClass('label').html(e.label+':')
      );
      
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
            $field.attr('max',e.min);
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
            if (('LTSonly' in e) && (!mist.data.LTS)) {
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
              if (('LTSonly' in e) && (!mist.data.LTS)) {
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
        default:
          $field = $('<input>').attr('type','text');
      }
      $field.addClass('field').data('opts',e);
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
      if ('readonly' in e) {
        $field.attr('readonly','readonly');
        $field.click(function(){
          $(this).select();
        });
      }
      if ('rows' in e) {
        $field.attr('rows',e.rows);
      }
      if (('LTSonly' in e) && (!mist.data.LTS)) {
        $fc.addClass('LTSonly');
        $field.prop('disabled',true);
      }
      
      //additional field type code
      switch (e.type) {
        case 'browse':
          var $master = $('<div>').addClass('grouper').append($e);
          $c.append($master);
          
          
          var $browse_button = $('<button>').text('Browse');
          $fc.append($browse_button);
          $browse_button.click(function(){
            var $c = $(this).closest('.grouper');
            var $bc = $('<div>').addClass('browse_container');
            var $field = $c.find('.field');
            var $browse_button = $(this);
            
            var $path = $('<span>').addClass('field');
            var $choose_folder = $('<button>').text('Select this folder')
            var $folder_contents = $('<div>').addClass('browse_contents');
            var $folder = $('<a>').addClass('folder');
            var filetypes = $field.data('filetypes');
            
            $c.append($bc);
            $bc.append(
              $('<label>').addClass('UIelement').append(
                $('<span>').addClass('label').text('Current folder:')
              ).append(
                $('<span>').addClass('field_container').append($path).append(
                  $choose_folder
                )
              )
            ).append(
              $folder_contents
            );
            
            $choose_folder.click(function(){
              var src = $path.text()+'/';
              
              $field.setval(src);
              $browse_button.show();
              $bc.remove();
            });
            
            function browse(path){
              $folder_contents.text('Loading..');
              mist.send(function(d){
                $path.text(d.browse.path[0]);
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
                      
                      $field.setval(src);
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
          if (('LTSonly' in e) && (!mist.data.LTS)) {
            subUI.blackwhite.prop('disabled',true);
            subUI.prototype.prop('disabled',true);
          }
          subUI.values.append(subUI.prototype.clone(true));
          $fc.data('subUI',subUI).addClass('limit_list').append(subUI.blackwhite).append(subUI.values);
          break;
      }
      
      if ('pointer' in e) {
        $field.data('pointer',e.pointer).addClass('isSetting');
        var val = e.pointer.main[e.pointer.index];
        if (val != 'undefined') {
          $field.setval(val);
        }
      }
      if ('value' in e) {
        $field.setval(e.value);
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
                  if (val == '') {
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
                  if (parseInt(Number(val)) != val) {
                    return {
                      msg: 'Please enter an integer.',
                      classes: ['red']
                    };
                  }
                }
                break;
              case 'streamname':
                f = function(val,me) {
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
                  if (val.replace(/[^\da-z_]/g,'') != val) {
                    return {
                      msg: 'Special characters (except for underscores) are not allowed.',
                      classes: ['red']
                    };
                  }
                };
                break;
              default:
                f = function(){}
                break;
            }
          }
          fs.push(f);
        }
        $field.data('validate_functions',fs).data('help_container',$ihc).data('validate',function(me){
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
              $(me).focus();
              return true;
            }
          }
          return false;
        }).addClass('hasValidate').on('change keyup',function(){
          var f = $(this).data('validate');
          f($(this));
        });
        $field.trigger('change');
      }
      
      if ('function' in e) {
        $field.on('change keyup',e['function']);
        $field.trigger('change');
      }
    }
    $c.on('keydown',function(e){
      switch (e.which) {
        case 13:
          //enter
          $(this).find('button.save').trigger('click');
          break;
        case 27:
          //escape
          $(this).find('button.cancel').trigger('click');
          break;
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
  buildLimits: function(limits,tab,other){
    var $c = $('<div>');
    var $table = $('<table>');
    $c.append($table);
    var $thead = $('<tr>').append(
      $('<th>').text('Kind').attr('data-sort-type','string')
    ).append(
      $('<th>').text('Type').attr('data-sort-type','string')
    ).append(
      $('<th>').text('Value')
    ).append(
      $('<th>')
    );
    if (!(limits instanceof Array)) {
      $thead.prepend(
        $('<th>').text('Applies to').attr('data-sort-type','string').addClass('sorting-asc').addClass('applies_to')
      );
    }
    else {
      limits = {
        'noapply': limits
      };
    }
    $table.append(
      $('<thead>').append($thead)
    );
    var $tbody = $('<tbody>');
    $table.append($tbody);
    
    function buildrows(limit,apply) {
      var $tb = $('<tbody>');
      for (var i in limit) {
        var $tr = $('<tr>');
        
        $tr.data('pointer',apply.concat(i));
        
        var text = '';
        switch (apply[0]) {
          case 'server':
            text = 'The entire server';
            break;
          case 'stream':
            text = 'The stream "'+apply[1]+'"';
            break;
        }
        $tr.append(
          $('<td>').addClass('applies_to').text(text)
        );
        
        var name = limit[i].name;
        var value = limit[i].value;
        switch (limit[i].name) {
          case 'kbps_max':
            name = 'Maximum bandwidth';
            value = UI.format.bytes(value,true);
            break;
          case 'users':
            name = 'Maximum connected users';
            value = UI.format.number(value);
            break;
          case 'geo':
            name = 'Geolimited';
            var vals = value;
            var value = '<span class=unit>['+(value.charAt(0) == '-' ? 'Blacklist' : 'Whitelist')+']</span> ';
            vals = vals.substr(1).split(' ');
            var cs = [];
            for (var j in vals) {
              var c = UI.countrylist[vals[j]];
              cs.push(typeof c == 'undefined' ? vals[j] : c);
            }
            value += cs.join(', ');
            break;
          case 'host':
            name = 'Hostlimited';
            var vals = value;
            var value = '<span class=unit>['+(value.charAt(0) == '-' ? 'Blacklist' : 'Whitelist')+']</span> ';
            vals = vals.substr(1).split(' ');
            value += vals.join(', ');
            break;
        }
        
        $tr.append(
          $('<td>').text(limit[i].type).addClass('kind')
        ).append(
          $('<td>').text(name).addClass('type')
        ).append(
          $('<td>').html(value).addClass('value')
        ).append(
          $('<td>').css('text-align','right').html(
            $('<button>').text('Edit').click(function(){
              UI.navto('Edit Limit',$(this).closest('tr').data('pointer').join('^'));
              UI.returnTab = [tab,other];
            })
          ).append(
            $('<button>').text('Delete').click(function(){
              var $tr = $(this).closest('tr');
              var q = 'Are you sure you want to delete the '+$tr.find('.kind').text()+' '+$tr.find('.type').text()+' limit for '+$tr.find('.applies_to').text()+'?';
              if (confirm(q)) {
                var pointer = $tr.data('pointer');
                var index = pointer[pointer.length-1];
                var sendData = {};
                switch (pointer[0]) {
                  case 'server':
                    mist.data.config.limits.splice(index,1);
                    sendData = {config: mist.data.config};
                    break;
                  case 'stream':
                    mist.data.streams[pointer[1]].limits.splice(index,1);
                    if (mist.data.LTS == 1) {
                      var addstream = {};
                      addstream[pointer[1]] = mist.data.streams[pointer[1]];
                      sendData = {addstream: addstream};
                    }
                    else {
                      sendData = {streams: mist.data.streams};
                    }
                    break;
                }
                mist.send(function(d){
                  UI.navto(tab,other);
                },sendData);
              }
            })
          )
        );
        $tb.append($tr);
      }
      return $tb.children();
    }
    
    for (var i in limits) {
      switch (i) {
        case 'server':
          $tbody.append(
            buildrows(limits[i],['server'])
          );
          break;
        case 'streams':
          for (var j in limits[i]) {
            $tbody.append(
              buildrows(limits[i][j],['stream',j])
            );
          }
          break;
      }
    }
    
    $c.prepend(
      $('<button>').text('New limit').click(function(){
        UI.navto('Edit Limit');
        UI.returnTab = [tab,other];
      })
    );
    
    if ($tbody.children().length > 0) {
      $table.stupidtable();
    }
    else {
      $table.remove();
      $c.append($('<span>').text('No limits set.'));
    }
    return $c.children();
  },
  plot: {
    go: function(graphs) {
      //get plotdata
      //build request object
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
              switch (set.origin[0]) {
                case 'total':     reqobj['totals'].push({fields: [set.datatype]});                               break;
                case 'stream':    reqobj['totals'].push({fields: [set.datatype], streams: [set.origin[1]]});     break;
                case 'protocol':  reqobj['totals'].push({fields: [set.datatype], protocols: [set.origin[1]]});   break;
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
                  }
                }
              );
              
              //now the legend
              var $list = $('<div>').addClass('legend-list').addClass('checklist');
              graph.elements.legend.html(
                $('<h3>').text(graph.id)
              ).append($list);
              var plotdata = graph.plot.getOptions();
              for (var i in graph.datasets) {
                var $checkbox = $('<input>').attr('type','checkbox').data('index',i).click(function(){
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
                  $('<label>').html(
                    $checkbox
                  ).append(
                    $('<div>').addClass('series-color').css('background-color',graph.datasets[i].color)
                  ).append(
                    graph.datasets[i].label
                  )
                );
              }
              
              //and the tooltip
              graph.elements.plot.on('plothover',function(e,pos,item){
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
              });
              
              break;
            case 'coords':
              //TODO
              break;
          }
        }
      },reqobj)
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
        var variation = 75;
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
          label: 'CPU load',
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
            this.data.push([mist.data.config.time*1000,Math.min(100,mist.data.capabilities.load.one/this.cores)]);
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
      var multiplications = [1e-3,  1e3,   60,  60,   24,     7,1e9];
      var units =           ['ms','sec','min','hr','day','week'];
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
      if (num > 1e4) {
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
        case  2: $s.text('Inactive').addClass('orange'); break;
        default: $s.text(item.online);
      }
      if (typeof item.error != 'undefined') {
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
    var hash = location.hash.split('@');
    hash[0] = [mist.user.name,mist.user.host].join('&');
    hash[1] = [tab,other].join('&');
    location.hash = hash.join('@');
    $(window).trigger('hashchange');
    clearInterval(UI.interval);
  },
  showTab: function(tab,other) {
    UI.elements.menu.css('visibility','visible').find('.button').removeClass('active').filter(function(){
      if ($(this).find('.plain').text() == tab) { return true; }
    }).addClass('active');
    UI.elements.secondary_menu.html('');
    var $main = UI.elements.main;
    clearInterval(UI.interval);
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
        UI.elements.menu.css('visibility','hidden');
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
              index: 'password'
            }
          },{
            type: 'buttons',
            buttons: [{
              label: 'Login',
              type: 'save',
              'function': function(){
                mist.send(function(){
                  UI.navto('Overview');
                });
              }
            }]
          }
        ]));
        break;
      case 'Create a new account':
        UI.elements.menu.css('visibility','hidden');
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
              $('.match_password').not($(me)).trigger('change');
              return false;
            }],
            help: 'Enter your desired password. In the future, you will need this to access the Management Interface.',
            pointer: {
              main: mist.user,
              index: 'password'
            },
            classes: ['match_password']
          },{
            label: 'Repeat password',
            type: 'password',
            validate: ['required',function(val,me){
              if (val != $('.match_password').not($(me)).val()) {
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
                    new_password: mist.user.password
                  }
                });
              }
            }]
          }]));
        break;
      case 'Account created':
        UI.elements.menu.css('visibility','hidden');
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
        var $versioncheck = $('<span>').text('Loading..');
        var $streamsonline = $('<span>');
        var $viewers = $('<span>');
        var $servertime = $('<span>');
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
            value: $versioncheck,
            LTSonly: true
          },{
            type: 'span',
            label: 'Server time',
            value: $servertime
          },{
            type: 'span',
            label: 'Current streams',
            value: $streamsonline
          },{
            type: 'span',
            label: 'Current connections',
            value: $viewers
          },$('<br>'),{
            type: 'str',
            label: 'Human readable name',
            pointer: {
              main: mist.data.config,
              index: 'name'
            },
            help: 'You can name your MistServer here for personal use. You’ll still need to set host name within your network yourself.'
          },{
            type: 'debug',
            label: 'Debug level',
            pointer: {
              main: mist.data.config,
              index: 'debug'
            },
            help: 'You can set the amount of debug information MistServer saves in the log. A full reboot of MistServer is required before some components of MistServer can post debug information.'
          },{
            type: 'checkbox',
            label: 'Force JSON file save',
            pointer: {
              main: mist.data,
              index: 'save'
            },
            help: 'Tick the box in order to force an immediate save to the config.json MistServer uses to save your settings. Saving will otherwise happen upon closing MistServer. Don’t forget to press save after ticking the box.'
          },{
            type: 'buttons',
            buttons: [{
              type: 'save',
              label: 'Save',
              'function': function(){
                var send = {config: mist.data.config};
                if (mist.data.save) {
                  send.save = mist.data.save;
                }
                mist.send(function(){
                  UI.navto('Overview');
                },send)
              }
            }]
          }
        ]));
        if (mist.data.LTS) {
          function update_update() {
            if (!('uptodate' in mist.data.update)) {
              $versioncheck.text('Unknown');
              return;
            }
            else if (mist.data.update.error) {
              $versioncheck.addClass('red').text(mist.data.update.error);
              return;
            }
            else if (mist.data.update.uptodate) {
              $versioncheck.text('Your version is up to date.').addClass('green');
              return;
            }
            else {
              $versioncheck.addClass('red').text('Version outdated!').append(
                $('<button>').text('Update').css({'font-size':'1em','margin-left':'1em'}).click(function(){
                  mist.send(function(d){
                    UI.navto('Overview');
                  },{autoupdate: true});
                })
              );
            }
          }
          
          if ((!mist.data.update) || (!mist.data.update.lastchecked) || ((new Date()).getTime()-mist.data.update.lastchecked > 3600e3)) {
            if (!('update' in mist.data)) { mist.data.update = {}; }
            mist.data.update.lastchecked = (new Date()).getTime();
            mist.send(function(d){
              update_update();
            },{checkupdate: true});
          }
          else {
            update_update();
          }
        }
        else {
          $versioncheck.text('');
        }
        function updateViewers() {
          mist.send(function(d){
            enterStats()
          },{
            totals:{
              fields: ['clients'],
              start: -10
            },
            active_streams: true
          });
        }
        function enterStats() {
          if ('active_streams' in mist.data) {
            var active = (mist.data.active_streams ? mist.data.active_streams.length : 0)
          }
          else {
            var active = '?';
          }
          $streamsonline.text(active+' active, '+Object.keys(mist.data.streams).length+' configured');
          if (('totals' in mist.data) && ('all_streams' in mist.data.totals)) {
            var clients = mist.data.totals.all_streams.all_protocols.clients;
            clients = (clients.length ? UI.format.number(clients[clients.length-1][1]) : 0);
          }
          else {
            clients = 'Loading..';
          }
          $viewers.text(clients);
          $servertime.text(UI.format.dateTime(mist.data.config.time,'long'));
        }
        updateViewers();
        enterStats();
        UI.interval = setInterval(updateViewers,30e3);
        
        break;
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
          $('<button>').text('New protocol').click(function(){
            UI.navto('Edit Protocol');
          })
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
            $tbody.append(
              $('<tr>').data('index',i).append(
                $('<td>').text(protocol.connector)
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
                      mist.data.config.protocols.splice(index,1);
                      mist.send(function(d){
                        UI.navto('Protocols');
                      },{config: mist.data.config});
                    }
                  })
                )
              )
            );
          }
        }
        updateProtocols();
        UI.interval = setInterval(function(){
          mist.send(function(){
            updateProtocols();
          });
        },30e3);
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
        
        function buildProtocolSettings(kind) {
          var input = mist.data.capabilities.connectors[kind];
          var build = mist.convertBuildOptions(input,saveas);
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
                if (editing) {
                  mist.data.config.protocols[other] = saveas;
                }
                else {
                  mist.data.config.protocols.push(saveas);
                }
                mist.send(function(d){
                  UI.navto('Protocols');
                },{config: mist.data.config});
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
              if (typeof current[input.deps[i]] != 'undefined') {
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
          var select = [];
          for (var i in mist.data.capabilities.connectors) {
            select.push([i,i]);
          }
          var $cont = $('<span>');
          $main.append(UI.buildUI([{
            label: 'Protocol',
            type: 'select',
            select: select,
            'function': function(){
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
        $main.append(
          UI.buildUI([{
            type: 'help',
            help: 'Here you can create, edit or delete new and existing streams. Immidiately go to the stream preview or view the information available about the stream with the info button.'
          }])
        ).append(
          $('<button>').text('New stream').click(function(){
            UI.navto('Edit Stream');
          })
        ).append($table);
        $table.stupidtable();
        
        function buildStreamTable() {
          var i = 0;
          $tbody.html('');
          
          if (mist.data.LTS) {
            //insert active wildcard streams (should overwrite active folder wildcard streams)
            for (var i in mist.data.active_streams) {
              var streamsplit = mist.data.active_streams[i].split('+');
              if (streamsplit.length < 2) { continue; }
              if (streamsplit[0] in mist.data.streams) {
                var wcstream = createWcStreamObject(mist.data.active_streams[i],mist.data.streams[streamsplit[0]]);
                wcstream.online = 1; //it's in active_streams, so it's active. Go figure.
                allstreams[mist.data.active_streams[i]] = wcstream;
              }
            }
          }
          
          var streams = Object.keys(allstreams);
          streams.sort();
          for (var s in streams) {
            var streamname = streams[s];
            var stream = allstreams[streamname];
            
            var $viewers = $('<td>').css('text-align','right').html($('<span>').addClass('description').text('Loading..'));
            var v = 0;
            if ((typeof mist.data.totals != 'undefined') && (typeof mist.data.totals[streamname] != 'undefined')) {
              var data = mist.data.totals[streamname].all_protocols.clients;
              var v = (data.length ? data[data.length-1][1] : 0);
            }
            $viewers.html(UI.format.number(v));
            if ((v == 0) && (stream.online == 1)) {
              stream.online = 2;
            }
            var $buttons = $('<td>').css('text-align','right').css('white-space','nowrap');
            if (!stream.ischild) {
              $buttons.html(
                $('<button>').text('Edit').click(function(){
                  UI.navto('Edit Stream',$(this).closest('tr').data('index'));
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
            
            var streamnamelabel = $('<span>').text(streamname);
            if (stream.ischild) {
              streamnamelabel.css('padding-left','1em');
            }
            $tbody.append(
              $('<tr>').data('index',streamname).html(
                $('<td>').html(streamnamelabel).attr('title',streamname).addClass('overflow_ellipsis')
              ).append(
                $('<td>').text(stream.source).addClass('description')
              ).append(
                $('<td>').data('sort-value',stream.online).html(UI.format.status(stream))
              ).append(
                $viewers
              ).append(
                $('<td>').html(
                  $('<button>').text('Preview').click(function(){
                    UI.navto('Preview',$(this).closest('tr').data('index'));
                  })
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
              start: -10
            });
          }
          mist.send(function(){
            buildStreamTable();
          },{
            totals: totals,
            active_streams: true
          });
        }
        
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
        
        if (mist.data.LTS) {
          //insert folder streams
          var browserequests = 0;
          var browsecomplete = 0;
          for (var s in mist.data.streams) {
            if (mist.inputMatch(mist.data.capabilities.inputs.Folder.source_match,mist.data.streams[s].source)) {
              //this is a folder stream
              mist.send(function(){
                for (var i in mist.data.browse.files) {
                  for (var j in mist.data.capabilities.inputs) {
                    if ((j == 'Buffer') || (j == 'Folder')) { continue; }
                    if (mist.inputMatch(mist.data.capabilities.inputs[j].source_match,'/'+mist.data.browse.files[i])) {
                      var streamname = s+'+'+mist.data.browse.files[i];
                      allstreams[streamname] = createWcStreamObject(streamname,mist.data.streams[s]);
                    }
                  }
                }
                browsecomplete++;
                if (browserequests == browsecomplete) {
                  mist.send(function(){
                    updateStreams();
                  },{active_streams: true});
                  
                  UI.interval = setInterval(function(){
                    updateStreams();
                  },30e3);
                }
              },{browse:mist.data.streams[s].source});
              browserequests++;
            }
          }
          if (browserequests == 0) {
            mist.send(function(){
              updateStreams();
            },{active_streams: true});
            
            UI.interval = setInterval(function(){
              updateStreams();
            },30e3);
          }
        }
        else {
          mist.send(function(){
            updateStreams();
          },{active_streams: true});
          
          UI.interval = setInterval(function(){
            updateStreams();
          },30e3);
        }
        
        break;
      case 'Edit Stream':
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
            validate: ['required'],
            filetypes: filetypes,
            pointer: {
              main: saveas,
              index: 'source'
            },
            help: 'Set the stream source.<br>VoD: You can browse to the file or folder as a source or simply enter the path to the file.<br>Live: You’ll need to enter "push://IP" with the IP of the machine pushing towards MistServer. You can use "push://" to accept any source.<br>Pro only: use "push://(IP)@password" to set a password protection for pushes.<br>If you\'re unsure how to set the source properly please view our Live pushing guide at the tools section.',
            'function': function(){
              var source = $(this).val();
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
                  $('<h3>').text('Unrecognized input')
                ).append(
                  $('<span>').text('Please edit the stream source.')
                );
                return;
              }
              var input = mist.data.capabilities.inputs[type];
              $inputoptions.html(
                $('<h3>').text(input.name+' Input options')
              );
              var build = mist.convertBuildOptions(input,saveas);
              $inputoptions.append(UI.buildUI(build));
            }
          },$('<br>'),{
            type: 'custom',
            custom: $inputoptions
          },$('<br>'),$('<h3>').text('Encryption'),{
            type: 'help',
            help: 'To enable encryption, the licence acquisition url must be entered, as well as either the content key or the key ID and seed.<br>Unsure how you should fill in your encryption or missing your preferred encryption? Please contact us.'
          },{
            label: 'License acquisition url',
            type: 'str',
            LTSonly: true,
            pointer: {
              main: saveas,
              index: 'la_url'
            }
          },$('<br>'),{
            label: 'Content key',
            type: 'str',
            LTSonly: true,
            pointer: {
              main: saveas,
              index: 'contentkey'
            }
          },{
            type: 'text',
            text: ' - or - '
          },{
            label: 'Key ID',
            type: 'str',
            LTSonly: true,
            pointer: {
              main: saveas,
              index: 'keyid'
            }
          },{
            label: 'Key seed',
            type: 'str',
            LTSonly: true,
            pointer: {
              main: saveas,
              index: 'keyseed'
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
                  mist.data.streams[saveas.name] = saveas;
                  if (other != saveas.name) {
                    delete mist.data.streams[other];
                  }
                  
                  var send = {};
                  if (mist.data.LTS) {
                    send.addstream = {};
                    send.addstream[saveas.name] = saveas;
                    if (other != saveas.name) {
                      send.deletestream = [other];
                    }
                  }
                  else {
                    send.streams = mist.data.streams;
                  }
                  mist.send(function(){
                    UI.navto('Streams');
                  },send)
                }
              }
            ]
          }
        ]));
        
        if (editing) {
          var limits = {streams: {}};
          limits.streams[streamname] = mist.data.streams[streamname].limits;
          $main.append(
            $('<h3>').text('Limits')
          ).append(
            UI.buildLimits(limits,tab,other)
          );
        }
        
        break;
      case 'Preview':
        if (other == '') {
          $main.append('Loading..');
          function selectastream(select) {
            var saveas = {};
            select.sort();
            $main.html(
              $('<h2>').text(tab)
            ).append(UI.buildUI([
              {
                label: 'Select a stream',
                type: 'select',
                select: select,
                pointer: {
                  main: saveas,
                  index: 'stream'
                }
              },{
                type: 'buttons',
                buttons: [{
                  type: 'save',
                  label: 'Go',
                  'function': function(){
                    UI.navto(tab,saveas.stream);
                  }
                }]
              }
            ]));
            
            UI.elements.secondary_menu.html('').append(
              $('<a>').addClass('button').addClass('active').text('Choose stream').click(function(){
                UI.navto('Preview');
              })
            )
          }
          
          if (mist.data.LTS) {
            if (typeof mist.data.capabilities == 'undefined') {
              mist.send(function(d){
                UI.navto(tab,other);
              },{capabilities: true});
              $main.append('Loading..');
              return;
            }
            
            //insert folder streams
            var browserequests = 0;
            var browsecomplete = 0;
            var select = {};
            for (var s in mist.data.streams) {
              if (mist.inputMatch(mist.data.capabilities.inputs.Folder.source_match,mist.data.streams[s].source)) {
                //this is a folder stream
                mist.send(function(){
                  for (var i in mist.data.browse.files) {
                    for (var j in mist.data.capabilities.inputs) {
                      if ((j == 'Buffer') || (j == 'Folder')) { continue; }
                      if (mist.inputMatch(mist.data.capabilities.inputs[j].source_match,'/'+mist.data.browse.files[i])) {
                        select[s+'+'+mist.data.browse.files[i]] = true;
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
                        }
                      }
                      select = Object.keys(select);
                      select = select.concat(Object.keys(mist.data.streams));
                      select.sort();
                      selectastream(select);
                    },{active_streams: true});
                  }
                },{browse:mist.data.streams[s].source});
                browserequests++;
              }
            }
            if (browserequests == 0) {
              mist.send(function(){
                var select = [];
                for (var i in mist.data.active_streams) {
                  var split = mist.data.active_streams[i].split('+');
                  if ((split.length > 1) && (split[0] in mist.data.streams)) {
                    select[mist.data.active_streams[i]] = true;
                  }
                }
                if (mist.data.streams) { select = select.concat(Object.keys(mist.data.streams)); }
                if (select.length == 0) {
                  $main.html(
                    $('<h2>').text(tab)
                  ).append(
                    'Please set up a stream first.'
                  );
                  return;
                }
                select.sort();
                selectastream(select);
              },{active_streams: true});
            }
          }
          else {
            selectastream(Object.keys(mist.data.streams));
          }
          return;
        }
        
        $main.find('h2').append(' of "'+other+'"');
        
        var http_port = ':8080';
        for (var i in mist.data.config.protocols) {
          var protocol = mist.data.config.protocols[i];
          if ((protocol.connector == 'HTTP') || (protocol.connector == 'HTTP.exe')) {
            http_port = (protocol.port ? ':'+protocol.port : ':8080');
          }
        }
        
        //actual page
        var tabs = {};
        
        var $embedlinks = $('<span>').hide();
        tabs['Embed urls'] = $embedlinks;
        $main.append($embedlinks);
        function parseURL(url) {
          var pattern = /(https?)\:\/\/([^:\/]+)\:(\d+)?/i;
          var retobj = {protocol: '', host: '', port: ''};
          var results = url.match(pattern);
          if(results != null) {
            retobj.protocol = results[1];
            retobj.host = results[2];
            retobj.port = results[3];
          }
          return retobj;
        }
        var embedbase = 'http://'+parseURL(mist.user.host).host+http_port+'/';
        $embedlinks.append(
          $('<h3>').text('Embed urls')
        ).append(UI.buildUI([
          {
            label: 'Embed url',
            type: 'str',
            value: embedbase+'embed_'+other+'.js',
            readonly: true
          },{
            label: 'Embed code',
            type: 'textarea',
            value: '<div>'+"\n"+'   <script src="'+embedbase+'embed_'+other+'.js"></script>'+"\n"+'</div>',
            rows: 4,
            readonly: true
          },{
            label: 'Info url',
            type: 'str',
            value: embedbase+'info_'+other+'.js',
            readonly: true
          }
        ]));
        
        var $trackinfo = $('<span>').append(
          $('<h3>').text('Meta information')
        ).hide();
        tabs['Meta information'] = $trackinfo;
        var $tracktable = $('<span>');
        $trackinfo.append($tracktable);
        $main.append($trackinfo);
        function buildTrackinfo() {
          var meta;
          if (other in mist.data.streams) {
            meta = mist.data.streams[other].meta;
          }
          if (!meta) { 
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
          var audio = {
            vheader: 'Audio',
            labels: ['Codec','Duration','Average bitrate','Channels','Samplerate'],
            content: []
          };
          var video = {
            vheader: 'Video',
            labels: ['Codec','Duration','Average bitrate','Size','Framerate'],
            content: []
          };
          var keys = Object.keys(meta.tracks);
          keys.sort(function(a,b){
            a = a.split('_').pop();
            b = b.split('_').pop();
            return a-b;
          });
          for (var k in keys) {
            var i = keys[k];
            var track = meta.tracks[i];
            switch (track.type) {
              case 'audio':
                audio.content.push({
                  header: 'Track '+i.split('_').pop(),
                  body: [
                    track.codec,
                    UI.format.duration((track.lastms-track.firstms)/1000)+'<br><span class=description>'+UI.format.duration(track.firstms/1000)+' to '+UI.format.duration(track.lastms/1000)+'</span>',
                    UI.format.bytes(track.bps,1),
                    track.channels,
                    UI.format.addUnit(UI.format.number(track.rate),'Hz')
                  ]
                });
                break;
              case 'video':
                video.content.push({
                  header: 'Track '+i.split('_').pop(),
                  body: [
                    track.codec,
                    UI.format.duration((track.lastms-track.firstms)/1000)+'<br><span class=description>'+UI.format.duration(track.firstms/1000)+' to '+UI.format.duration(track.lastms/1000)+'</span>',
                    UI.format.bytes(track.bps,1),
                    UI.format.addUnit(track.width,'x ')+UI.format.addUnit(track.height,'px'),
                    UI.format.addUnit(UI.format.number(track.fpks/1000),'fps')
                  ]
                });
                break;
            }
          }
          var $audio = UI.buildVheaderTable(audio).css('width','auto');
          var $video = UI.buildVheaderTable(video).css('width','auto');
          build.push($('<span>').text('Tracks:'))
          build.push(
            $('<div>').css({
              'display': 'flex',
              'flex-flow': 'row wrap',
              'justify-content': 'center',
              'font-size': '0.9em'
            }).append($audio).append($video)
          );
          $tracktable.html(UI.buildUI(build));
        }
        buildTrackinfo();
        if ((other in mist.data.streams) && (!('meta' in mist.data.streams[other]))) {
          //try to refresh the meta information
          mist.send(function(){
            buildTrackinfo();
          });
        }
        
        //embedded video
        var $preview = $('<span>').hide();
        tabs['Preview'] = $preview;
        $main.append($preview);
        var $video = $('<div>').css('float','left').attr('data-forcesupportcheck','');
        var $protocols = $('<div>').css('float','left');
        $preview.append($video).append($protocols);
        
        if (UI.stored.vars.autoplay) {
          $video.attr('data-autoplay','');
        }
        
        function loadVideo() {
          $video.text('Loading..');
          $protocols.text('Loading..');
          
          // jQuery doesn't work -> use DOM magic
          var script = document.createElement('script');
          script.src = embedbase+'embed_'+other+'.js';
          script.onerror = function(){
            $video.text('Error loading "'+script.src+'".');
          };
          script.onload = function(){
            if (typeof mistvideo[other].error != 'undefined') {
              $video.text(mistvideo[other].error);
              return;
            }
            
            var vid = mistvideo[other];
            var $url = UI.buildUI([{
              label: 'Stream embed url',
              type: 'str',
              readonly: true,
              value: (vid.embedded ? vid.embedded.url : '')
            },{
              label: 'Autoplay (from now on)',
              type: 'checkbox',
              value: UI.stored.vars.autoplay,
              'function': function(){
                UI.stored.saveOpt('autoplay',$(this).getval());
              }
            }]);
            $url.find('.help_container').remove();
            $video.append($url);
            
            
            //protocol table
            var $table = $('<table>').css('font-size','0.9em').html(
              $('<thead>').html(
                $('<tr>').html(
                  $('<th>')
                ).append(
                  $('<th>').text('Type')
                ).append(
                  $('<th>').text('Priority')
                ).append(
                  $('<th>').text('Simul. tracks')
                ).append(
                  $('<th>').html('Your browser<br>support')
                )
              )
            );
            $protocols.html($table);
            var $tbody = $('<tbody>');
            $table.append($tbody);
            for (var i in vid.source) {
              var source = vid.source[i];
              var type = source.type.split('/');
              var humantype = type[0];
              switch (type.length) {
                case 1:
                  break;
                case 2:
                  humantype += ' v'+type[1];
                  if (type[0] == 'flash') {
                    switch (type[1]) {
                      case '7':
                        humantype = 'Progressive ('+humantype.charAt(0).toUpperCase()+humantype.slice(1)+')';
                        break;
                      case '10':
                        humantype = 'RTMP ('+humantype.charAt(0).toUpperCase()+humantype.slice(1)+')';
                        break;
                      case '11':
                        humantype = 'HDS ('+humantype.charAt(0).toUpperCase()+humantype.slice(1)+')';
                        break;
                    }
                  }
                  break;
                case 3:
                  switch (type[2]) {
                    case 'mp4':
                      humantype += ' MP4';
                      break;
                    case 'vnd.apple.mpegurl':
                      humantype += ' HLS';
                      break;
                    case 'vnd.ms-ss':
                      humantype += ' Smooth';
                      break;
                    default:
                      humantype = source.type;
                  }
                  break;
                default:
                  humantype = source.type;
              }
              humantype = UI.format.capital(humantype);
              var $tr = $('<tr>');
              $tbody.append($tr);
              $tr.html(
                $('<td>').html(
                  $('<input>').attr('type','radio').change(function(){
                    $video.attr('data-forcetype',$(this).val()).html('Loading embed..');
                    loadVideo();
                  }).val(source.type)
                )
              ).append(
                $('<td>').text(humantype)
              ).append(
                $('<td>').text(source.priority)
              ).append(
                $('<td>').text(source.simul_tracks+'/'+source.total_matches)
              ).append(
                $('<td>').text((source.browser_support ? 'yes' : 'no'))
              );
              if ((vid.embedded) && (vid.embedded.type == source.type)) {
                $tr.css('outline','1px solid rgba(0,0,0,0.5)');
                $tr.find('input[type=radio]').prop('checked',true);
              }
            }
          };
          $video.html('')[0].appendChild(script);
        }
        loadVideo();
        
        //navbar (tabs at the top)
        var $nav = UI.elements.secondary_menu;
        $nav.html('').append(
          $('<a>').addClass('button').text('Choose stream').click(function(){
            UI.navto('Preview');
          })
        ).append(
          $('<span>').addClass('separator')
        );
        var elements = ['Preview','Embed urls','Meta information'];
        var current = elements[0];
        for (var i in elements) {
          var $button = $('<a>').addClass('button').text(elements[i]).click(function(){
            $nav.find('.active').removeClass('active');
            $(this).addClass('active');
            for (i in tabs) {
              tabs[i].hide();
            }
            tabs[$(this).text()].show();
          });
          $nav.append($button);
          if (elements[i] == current) {
            $button.addClass('active');
            tabs[current].show();
          }
        }
        
        break;
      case 'Limits':
        $main.append(UI.buildUI([{
          type: 'help',
          help: 'Here you can see an overview of all the limits you currently have. Limits are an LTS only feature and you can simply add new limits by selecting new, by selecting edit you can edit the existing limit, by selecting delete you can delete the existing limit.'
        }]));
        var limits = {
          'server': mist.data.config.limits,
          'streams': {}
        };
        for (var i in mist.data.streams) {
          if ('limits' in mist.data.streams[i]) {
            limits.streams[i] = mist.data.streams[i].limits;
          }
        }
        $main.append(UI.buildLimits(limits,tab,other));
        break;
      case 'Edit Limit':
        var editing = false;
        if (other != '') { editing = true; }
        var saveas = {};
        var build = [{
          type: 'help',
          help: 'Here you can set the limit specifications, soft limits only warn while hard limits restrict access. Please feel free to set up limits according to your preferences.'
        }];
        if (UI.returnTab[0] == 'Overview') { UI.returnTab =  ['Limits']; }
        
        if (!editing) {
          $main.html(
            $('<h2>').text('New limit')
          );
          
          var thestreams = [];
          for (var i in mist.data.streams) {
            thestreams.push(i);
          }
          var select = [
            ['server','The entire server'],
            ['stream','The stream:',thestreams]
          ];
          if ((UI.returnTab[0] == 'Edit Stream') && (UI.returnTab[1])) {
            var value = ['stream',UI.returnTab[1]];
          }
          build.push({
            label: 'Applies to',
            type: 'radioselect',
            radioselect: select,
            pointer: {
              main: saveas,
              index: 'applies_to'
            },
            LTSonly: true,
            validate: ['required'],
            value: value
          });
        }
        else {
          var pointer = other.split('^');
          var text = tab;
          switch (pointer[0]) {
            case 'server':
              saveas = mist.data.config.limits[pointer[1]];
              text = 'For the entire server';
              break;
            case 'stream':
              saveas = mist.data.streams[pointer[1]].limits[pointer[2]];
              text = 'For the stream "'+pointer[1]+'"';
              break;
          }
          build.push({
            type: 'text',
            text: text
          });
        }
        
        build = build.concat([$('<br>'),{
          label: 'Kind',
          type: 'select',
          select: [
            ['soft','Soft'],
            ['hard','Hard']
          ],
          pointer: {
            main: saveas,
            index: 'type'
          },
          help: 'The server will not allow a hard limit to be passed. A soft limit can be used to set alerts.',
          LTSonly: true,
          validate: ['required']
        },{
          label: 'Value',
          pointer: {
            main: saveas,
            index: 'value'
          },
          classes: ['limit_value'],
          LTSonly: true
        },{
          label: 'Type',
          type: 'select',
          select: [
            ['kbps_max','Maximum bandwidth'],
            ['users','Maximum connected users'],
            ['geo','Geo limited'],
            ['host','Host limited']
          ],
          pointer: {
            main: saveas,
            index: 'name'
          },
          LTSonly: true,
          validate: ['required'],
          classes: ['limit_type'],
          'function': function(){
            var type = $(this).getval();
            var $lvalue = $(this).closest('.input_container').find('.limit_value');
            var $c = $lvalue.closest('label');
            var opts = $lvalue.data('opts');
            
            var nopts = opts;
            delete nopts.type;
            delete nopts.min;
            delete nopts.unit;
            nopts.validate = ['required'];
            
            switch (type) {
              case 'kbps_max':
                nopts.type = 'int';
                nopts.min = 1;
                nopts.unit = 'bytes/s'
                break;
              case 'users':
                nopts.type = 'int';
                nopts.min = 1;
                break;
              case 'geo':
                nopts.type = 'geolimited';
                break;
              case 'host':
                nopts.type = 'hostlimited';
                break;
            }
            var $nc = UI.buildUI([nopts]);
            $c.replaceWith($nc.children());
          }
        },{
          type: 'buttons',
          buttons: [
            {
              type: 'cancel',
              label: 'Cancel',
              'function': function(){
                UI.navto(UI.returnTab[0],UI.returnTab[1]);
              }
            },{
              type: 'save',
              label: 'Save',
              'function': function(){
                var send = {};
                if (editing) {
                  var pointer = other.split('^');
                  switch (pointer[0]) {
                    case 'server':
                      send = {config: {limits: mist.data.config.limits}};
                      break;
                    case 'stream':
                      if (mist.data.LTS) {
                        send = {addstream: {}};
                        send.addstream[pointer[1]] = mist.data.streams[pointer[1]];
                      }
                      else {
                        send = {streams: mist.data.streams};
                      }
                      break;
                  }
                }
                else {
                  var pointer = saveas.applies_to;
                  delete saveas.applies_to;
                  switch (pointer[0]) {
                    case 'server':
                      if (typeof mist.data.config.limits == 'undefined') {
                        mist.data.config.limits = [];
                      }
                      mist.data.config.limits.push(saveas);
                      send = {config: {limits: mist.data.config.limits}};
                      break;
                    case 'stream':
                      if (typeof mist.data.streams[pointer[1]].limits == 'undefined') {
                        mist.data.streams[pointer[1]].limits = [];
                      }
                      mist.data.streams[pointer[1]].limits.push(saveas);
                      if (mist.data.LTS) {
                        send = {addstream: {}};
                        send.addstream[pointer[1]] = mist.data.streams[pointer[1]];
                      }
                      else {
                        send = {streams: mist.data.streams};
                      }
                      break;
                  }
                }
                mist.send(function(){
                  UI.navto(UI.returnTab[0],UI.returnTab[1]);
                },send)
              }
            }
          ]
        }]);
        var $UI = UI.buildUI(build);
        $main.append($UI);
        //draw the type input after the value input, but show the value input last
        $UI.find('.limit_type').closest('label').after(
          $UI.find('.limit_value').closest('label')
        );
        $UI.find('.limit_type').trigger('change');
        
        break;
      case 'Logs':
        
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
            clearInterval(UI.interval);
            UI.interval = setInterval(function(){
              mist.send(function(){
                buildLogsTable();
              });
            },$(this).val()*1e3);
          }
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
          $('<table>').append($tbody)
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
            $tbody.append(
              $('<tr>').html(
                $('<td>').text(UI.format.dateTime(logs[index][0],'long')).css('white-space','nowrap')
              ).append(
                $('<td>').html(color(logs[index][1])).css('text-align','center')
              ).append(
                $('<td>').text(logs[index][2]).css('text-align','left')
              )
            );
          }
        }
        buildLogsTable();
        
        break;
      case 'Statistics':
        var $UI = $('<span>').text('Loading..');
        $main.append($UI);
        
        var saveas = {};
        var graphs = {};
        
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
              if ($(this).val() == 'new') {
                $s.children('option').prop('disabled',false);
              }
              else {
                var xaxistype = graphs[$(this).val()].xaxis;
                $s.children('option').prop('disabled',true).filter('[value="'+xaxistype+'"]').prop('disabled',false);
              }
              if ($s.children('option[value="'+$s.val()+'"]:disabled').length) {
                $s.val($s.children('option:enabled').first().val());
                $s.trigger('change');
              }
            }
          },{
            label: 'Axis type',
            type: 'select',
            select: [
              ['time','Time line'],
              ['coords','Geographical']
            ],
            pointer: {
              main: saveas,
              index: 'xaxis'
            },
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
              if ($s.children('option[value="'+$s.val()+'"]:disabled').length) {
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
              ['cpuload','CPU load'],
              ['memload','Memory load'],
              ['coords','Client location']
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
                if (saveas.graph == 'new') {
                  var graph = {
                    id: 'Graph '+(Object.keys(graphs).length+1),
                    xaxis: saveas.xaxis,
                    datasets: [],
                    elements: {
                      cont: $('<div>').addClass('graph'),
                      plot: $('<div>').addClass('plot'),
                      legend: $('<div>').addClass('legend')
                    }
                  }
                  graphs[graph.id] = graph;
                  graph.elements.cont.append(
                    graph.elements.plot
                  ).append(
                    graph.elements.legend
                  );
                  $graph_c.append(graph.elements.cont);
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
                UI.plot.go(graphs);
              }
            }]
          }]));
          $UI.find('.graph_xaxis').trigger('change');
          
          var $graph_c = $('<div>').addClass('graph_container');
          $main.append($graph_c);
          
        },{active_streams: true, capabilities: true});
        
        UI.interval = setInterval(function(){
          UI.plot.go(graphs);
        },10e3);
        
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
            labels: ['1 minute','5 minutes','15 minutes',''],
            content: [{
              header: 'Absolute',
              body: [
                UI.format.number(load.one/100),
                UI.format.number(load.five/100),
                UI.format.number(load.fifteen/100),
                ''
              ]
            },{
              header: 'Per core',
              body: [
                UI.format.addUnit(load.one/cores,'%'),
                UI.format.addUnit(load.five/cores,'%'),
                UI.format.addUnit(load.fifteen/cores,'%'),
                ''
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
        
        UI.interval = setInterval(function(){
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
            label: 'Your message',
            validate: ['required'],
            pointer: {
              main: saveas,
              index: 'message'
            }
          },{
            type: 'textarea',
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
                  url: 'http://mistserver.org/contact_us?skin=plain',
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
        
        UI.navto('Login');
        break;
      default:
        $main.append($('<p>').text('This tab does not exist.'));
        break;
    }
  }
};

var mist = {
  data: {},
  user: {
    name: '',
    password: '',
    host: 'http://localhost:4242/api'
  },
  send: function(callback,sendData,opts){
    sendData = sendData || {};
    opts = opts || {};
    opts = $.extend(true,{
      timeout: 30
    },opts);
    delete sendData.logs;
    var data = {
      authorize: {
        password: (mist.user.authstring ? MD5(MD5(mist.user.password)+mist.user.authstring) : ''),
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
          //connection failed
          
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
              textStatus
            ).append(
              $('<a>').text('Send server request again').click(function(){
                mist.send(callback,sendData,opts);
              })
            );
          }
          
          UI.navto('Login');
      },
      success: function(d){
        log('Received',$.extend(true,{},d));
        switch (d.authorize.status) {
          case 'OK':
            //communication succesfull
            
            $.extend(true,mist.data,d);
            
            //if streams/protocols have been deleted by someone else, make sure they're gone by overwriting
            mist.data.streams = d.streams;
            mist.data.config.protocols = d.config.protocols;
            mist.data.config.limits = d.config.limits
            
            mist.user.loggedin = true;
            UI.elements.connection.status.text('Connected').removeClass('red').addClass('green');
            UI.elements.connection.user_and_host.text(mist.user.name+' @ '+mist.user.host);
            UI.elements.connection.msg.removeClass('red').text('Last communication with the server at '+UI.format.time((new Date).getTime()/1000));
            
            //if this is LTS, get rid of the banner on menu buttons
            if (d.LTS) { UI.elements.menu.find('.LTSonly').removeClass('LTSonly'); }
            
            if (d.log) {
              var lastlog = d.log[d.log.length-1];
              UI.elements.connection.msg.append($('<br>')).append(
                'Last log entry: '+UI.format.time(lastlog[0])+' ['+lastlog[1]+'] '+lastlog[2]
              );
            }
            if ('totals' in d) {
              //reformat to something more readable
              function reformat(main) {
                function insertZero() {
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
                  time = mist.data.config.time*1e3;
                  insertZero();
                }
                else {
                  //leading 0?
                  if ((main.end - main.start) < 600) {
                    time = (main.end - 600)*1e3;
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
                        if (interval_n < main.interval.length-1) { insert = 2; } 
                      }
                    }
                    
                    if (insert == 1) {
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
                  if ((mist.data.config.time - main.end) > 5) {
                    insertZero();
                    time = mist.data.config.time * 1e3;
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
            
            if (callback) { callback(d); }
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
    
    var query = match.replace(/[^\w\s]/g,'\\$&'); //prefix any special chars with a \
    query = query.replace(/\\\?/g,'.').replace(/\\\*/g,'(?:.)*'); //replace ? with . and * with any amount of .
    var regex = new RegExp('^'+query+'$','i'); //case insensitive
    return regex.test(string);
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
    for (var j in type) {
      if (input[type[j]]) {
        build.push(
          $('<h4>').text(UI.format.capital(type[j])+' parameters')
        );
        for (var i in input[type[j]]) {
          var ele = input[type[j]][i];
          var obj = {
            label: UI.format.capital(ele.name),
            pointer: {
              main: saveas,
              index: i
            },
            validate: []
          };
          if ((type[j] == 'required') && (!('default' in ele))) {
            obj.validate.push('required');
          }
          if ('default' in ele) {
            obj.placeholder = ele['default'];
          }
          if ('help' in ele) {
            obj.help = ele.help;
          }
          if ('unit' in ele) {
            obj.unit = ele.unit;
          }
          switch (ele.type) {
            case 'int':
              obj.type = 'int';
              break;
            case 'uint':
              obj.type = 'int';
              obj.min = 0;
              break;
            case 'debug':
              obj.type = 'debug';
              break;
            case 'select':
              obj.type = 'select';
              obj.select = ele.select;
            case 'str':
            default:
              obj.type = 'str';
          }
          build.push(obj);
        }
      }
    }
    return build;
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
      case 'span':
        val = $(this).html();
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
    }
  }
  $(this).trigger('change');
  return $(this);
}