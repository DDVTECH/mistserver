$(function(){
  showTab('login');
});

var settings = {
  credentials:{},
  settings: {}
};

var debug = false;
function consolelog() {
  if (debug) {
    console.log.apply(console,arguments);
  }
}

var ih = false;

function confirmDelete(question){
  return confirm(question);
}
/**
* Add thousand seperator to a number
* @param number the number to format 
* @param seperator the seperator to use 
*/
function seperateThousands(number,seperator){
  if (isNaN(Number(number))) return number;
  number = number.toString().split('.');
  var regex = /(\d+)(\d{3})/;
  while (regex.test(number[0])) {
    number[0] = number[0].replace(regex,'$1'+seperator+'$2');
  }
  return number.join('.');
}
/**
* Format a date to mmm d yyyy, hh:mm:ss format
* @param date the date to format (timestamp in seconds)
*/
function formatDateLong(date){
  var d = new Date(date * 1000);
  
  return [
      nameMonth(d.getMonth()),
      d.getDate(),
      d.getFullYear()
  ].join(' ') + ', ' + formatTime(date);
}
function nameMonth(monthNum){
  months = Array('Jan','Feb','Mar','Apr','May','Jun','Jul','Aug','Sep','Oct','Nov','Dec');
  return months[monthNum];
}
function formatTime(date){
  var d = new Date(date * 1000);
  return [
    ('00' + d.getHours()).slice(-2),
    ('00' + d.getMinutes()).slice(-2),
    ('00' + d.getSeconds()).slice(-2)
  ].join(':');
}
/**
 * Format a time duration to something like "2 days, 00:00:00.000"
 * @param ms the duration to format in miliseconds
 */
function formatDuration(ms) {
  var secs = Math.floor(ms / 1000), mins = 0;
  ms = ms % 1000;
  if (secs >= 60) {
    mins = Math.floor(secs / 60);
    secs = secs % 60;
  }
  if (mins >= 60) {
    var hours = Math.floor(mins / 60);
    mins = mins % 60;
  }
  var string = ('00'+mins).slice(-2)+':'+('00'+secs).slice(-2)+'.'+('000'+ms).slice(-3);
  if (hours >= 24) {
    var days = Math.floor(hours / 24);
    hours = hours % 24;
  }
  if (hours > 0) {
    string = ('00'+hours).slice(-2)+':'+string;
  }
  if (days > 0) {
    string = days+' day'+(days > 1 ? 's' : '')+', '+string
  }
  return string;
}
/**
 * Capitalize the first letter
 * @param string the string
 */
function capFirstChar(string) {
  if (string.length <= 0) { return ''; }
  return string[0].toUpperCase() + string.slice(1);
}
/**
 * Flot tick generator for bandwidth
 * @param axis the axis
 */
function flotTicksBandwidthAxis(axis) {
  var range = axis.max - axis.min;
  var delta = range / 4;
  var start = axis.min;
  if (axis.max < 1024) {                                                                                                          // unit: bytes/s
    if      (delta > 100)       { delta = Math.floor(delta/100)*100;             start = Math.floor(start/100)*100;             } // to lowest 100 bytes/s
    else if (delta > 10)        { delta = Math.floor(delta/10)*10;               start = Math.floor(start/10)*10;               } // to lowest 10 bytes/s
  }
  else if (axis.max < 1048576) {                                                                                                  //unit: kiB/s
    if      (delta > 102400)    { delta = Math.floor(delta/102400)*102400;       start = Math.floor(start/102400)*102400;       } //to lowest 100 kiB/s
    else if (delta > 10240)     { delta = Math.floor(delta/10240)*10240;         start = Math.floor(start/10240)*10240;         } //to lowest 10 kiB/s
    else if (delta > 1024)      { delta = Math.floor(delta/1024)*1024;           start = Math.floor(start/1024)*1024;           } //to lowest 1 kiB/s
    else                        { delta = Math.floor(delta/102.4)*102.4;         start = Math.floor(start/102.4)*102.4;         } //to lowest 0.1 kiB/s
  }
  else {                                                                                                                          //unit: miB/s
    if      (delta > 104857600) { delta = Math.floor(delta/104857600)*104857600; start = Math.floor(start/104857600)*104857600; } //to lowest 100 miB/s
    else if (delta > 10485760)  { delta = Math.floor(delta/10485760)*10485760;   start = Math.floor(start/10485760)*10485760;   } //to lowest 10 miB/s
    else if (delta > 1048576)   { delta = Math.floor(delta/1048576)*1048576;     start = Math.floor(start/1048576)*1048576;     } //to lowest 1 miB/s
    else                        { delta = Math.floor(delta/104857.6)*104857.6;   start = Math.floor(start/104857.6)*104857.6;   } //to lowest 0.1 miB/s
  }
  var out = [];
  for (var i = start; i <= axis.max; i += delta) {
    out.push(i);
  }
  return out;
}
/**
 * Flot axis formatter for bandwidth
 * @param val the valuea
 * @param axis the axis
 */
function flotFormatBandwidthAxis(val,axis) {
  if (val < 0) { var sign = '-'; }
  else { var sign = ''; }
  val = Math.abs(val);
  
  if (val < 1024)      { return sign+Math.round(val)+' bytes/s'; }          // 0  bytes/s through 1023 bytes/s
  if (val < 10235)     { return sign+(val/1024).toFixed(2)+' kiB/s'; }      // 1.00 kiB/s through 9.99 kiB/s
  if (val < 102449)    { return sign+(val/1024).toFixed(1)+' kiB/s'; }      // 10.0 kiB/s through 99.9 kiB/s
  if (val < 1048064)   { return sign+Math.round(val/1024)+' kiB/s'; }       // 100  kiB/s through 1023 kiB/s
  if (val < 10480518)  { return sign+(val/1048576).toFixed(2)+' miB/s'; }   // 1.00 miB/s through 9.99 miB/s
  if (val < 104805172) { return sign+(val/1048576).toFixed(1)+' miB/s'; }   // 10.0 miB/s through 99.9 miB/s
  return sign+Math.round(val/1048576)+' miB/s';                             // 100  miB/s and up
}
/**
 * Converts the statistics data into something flot understands
 * @param stats the statistics.totals object
 * @param cumulative cumulative mode if true
 */
function convertStatisticsToFlotFormat(stats,islive) {
  var plotdata = [
    { label: 'Viewers', data: []},
    { label: 'Bandwidth (Up)', data: [], yaxis: 2},
    { label: 'Bandwidth (Down)', data: [], yaxis: 2}
  ];

  var oldtimestamp = 0;
  var i = 0, up = 0, down = 0;
  for (var timestamp in stats) {
    if (islive) {
      i++;
      up += stats[timestamp].up;
      down += stats[timestamp].down;
      //average over 5 seconds to prevent super spiky unreadable graph
      if ((i % 5) == 0) {
        plotdata[0].data.push([Number(timestamp)*1000,stats[timestamp].count]);
        plotdata[1].data.push([Number(timestamp)*1000,up/5]);
        plotdata[2].data.push([Number(timestamp)*1000,down/5]);
        up = 0;
        down = 0;
      }
    }
    else {
      var dt = timestamp - oldtimestamp;
      if (stats[oldtimestamp]) {
        var up = (stats[timestamp].up - stats[oldtimestamp].up)/dt;
        var down = (stats[timestamp].down - stats[oldtimestamp].down)/dt;
      }
      else {
        var up = stats[timestamp].up;
        var down = stats[timestamp].down;
      }
      plotdata[0].data.push([Number(timestamp)*1000,stats[timestamp].count]);
      plotdata[1].data.push([Number(timestamp)*1000,up]);
      plotdata[2].data.push([Number(timestamp)*1000,down]);
      oldtimestamp = timestamp; 
    }
  }
  for (var timestamp in stats) {
    var dt = timestamp - oldtimestamp;
    plotdata[0].data.push([Number(timestamp)*1000,stats[timestamp].count]);
    if (stats[oldtimestamp]) {
      var up = (stats[timestamp].up - stats[oldtimestamp].up)/dt;
      var down = (stats[timestamp].down - stats[oldtimestamp].down)/dt;
    }
    else {
      var up = stats[timestamp].up;
      var down = stats[timestamp].down;
    }
    plotdata[1].data.push([Number(timestamp)*1000,up]);
    plotdata[2].data.push([Number(timestamp)*1000,down]);
    oldtimestamp = timestamp;
  }
  return plotdata;
}
/**
* Check if an URL points to a live datastream or a recorded file
* @param url the url in question
*/
function isLive(url){
  var protocol = /([a-zA-Z]+):\/\//.exec(url);
  if ((protocol === null) || ((protocol[1]) && (protocol[1] === 'file'))) {
    return false;
  }
  else {
    return true;
  }
}
/**
* parses an url and returns the parts of it.
* @return object containing the parts of the URL: protocol, host and port.
*/
function parseURL(url)
{
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
/**
* Return html describing the status of the stream or protocol
* @param theStream the stream or protocol object in question
*/
function formatStatus(theStream) {
  if (theStream.online == undefined) {
    return $('<span>').text('Unknown, checking..');
  }
  if (theStream.error == undefined) {
    switch (theStream.online) {
      case -1: return $('<span>').text('Unknown, checking..');              break;
      case  0: return $('<span>').addClass('red').text('Unavailable');      break;
      case  1: return $('<span>').addClass('green').text('Active');         break;
      case  2: return $('<span>').addClass('orange').text('Inactive');      break;
      default: return $('<span>').text(theStream.online);                   break;
    }
  }
  else {
    switch (theStream.online) {
      case -1: return $('<span>').text('Unknown, checking..');              break;
      case  0: return $('<span>').addClass('red').text(theStream.error);    break;
      case  1: return $('<span>').addClass('green').text(theStream.error);  break;
      case  2: return $('<span>').addClass('orange').text(theStream.error); break;
      default: return $('<span>').text(theStream.error);                    break;
    }
  }
}
/**
* Apply entered data in input and select fields to their corresponding object adress
*/
function applyInput(){
  //check if all required fields have something in them
  $('#input-validation-info').remove();
  var error = false;
  $('input.isSetting.validate-required').each(function(){
    if ($(this).val() == '') {
      $(this).focus();
      $(this).parent().append(
        $('<div>').attr('id','input-validation-info').html(
          'This is a required field.'
        ).addClass('red')
      );
      error = true;
      return false;
    }
  });
  if (error == true) { return false; }
  
  
  //apply the inputs
  $('input.isSetting,select.isSetting').each(function(){
    
    var objpath = findObjPath($(this));
    
    if (($(this).val() == '') || (($(this).val() == 0) && ($(this).attr('type') == 'number'))) {
      eval('delete '+objpath+';');
    }
    else {
      eval(objpath+' = $(this).val();');
    }
  });
  return true;
}
/**
* Apply data from the settings object to the page elements
*/
//this function has been placed in footer.html
/**
 * Find the path to the setting for this element
 */
function findObjPath($element) {
  if ($element.attr('objpath')) {
    return 'settings.'+$element.attr('objpath');
  }
  else {
    return 'settings.'+$element.attr('id').replace(/-/g,'.');
  }
}

function ihAddBalloons() {
  var page = settings.ih.pages[settings.currentpage];
  if (!page) { return; }
  
  //something with pageinfo
  if (page.pageinfo) {
    $('#page').prepend(
      $('<div>').addClass('ih-balloon').addClass('pageinfo').html(page.pageinfo)
    );
  }
  
  for (inputid in page.inputs) {
    $('#'+inputid).parent().prepend(
      $('<div>').addClass('ih-balloon').addClass('inputinfo').attr('data-for',inputid).html(page.inputs[inputid]).hide()
    );
    $('#'+inputid).focus(function(){
      $('.ih-balloon[data-for='+$(this).attr('id')+']').show();
      $('.ih-balloon.pageinfo').hide();
    }).blur(function(){
      $('.ih-balloon[data-for='+$(this).attr('id')+']').hide();
      $('.ih-balloon.pageinfo').show();
    });
  }
  $('#page label').each(function(){
    $(this)
  });
}
function ihMakeBalloon(contents,forid) {
  return $('<div>').addClass('ih-balloon').attr('data-for',forid).html(contents).hide();
}

function getData(callBack,sendData,timeOut,doShield){
  timeOut = timeOut | 30000;
  var data = {};
  data.authorize = $.extend(true,{},settings.credentials);
  delete data.authorize.authstring;
  data.authorize.password = settings.credentials.authstring ? MD5(MD5(settings.credentials.password)+settings.credentials.authstring) : '';
  
  $.extend(true,data,sendData);
  
  consolelog('['+(new Date).toTimeString().split(' ')[0]+']','Sending data:',data);
  
  $('#message').removeClass('red').text('Data sent, waiting for a reply..').append(
    $('<br>')
  ).append(
    $('<a>').text('Cancel request').click(function(){
      jqxhr.abort();
    })
  );
  
  if (doShield) {
    $('body').append(
      $('<div>').attr('id', 'shield').text('Loading, please wait..')
    );
  }
  var obj = {
    'url': settings.server,
    'type': 'POST',
    'data': {
      'command': JSON.stringify(data)
    },
    'dataType': 'jsonp',
    'crossDomain': true,
    'timeout': timeOut,
    'error':function(jqXHR,textStatus,errorThrown){
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
          getData(callBack,sendData,timeOut,doShield);
        })
      );
      $('#shield').remove();
    },
    'success': function(returnedData){
      $('#message').text('Data received, processing..');
      
      consolelog('['+(new Date).toTimeString().split(' ')[0]+']','Received data:',returnedData);
      
      if ((returnedData) && (returnedData.authorize)) {
        if (returnedData.authorize.challenge){
          if (settings.credentials.authstring != returnedData.authorize.challenge){
            if (returnedData.authorize.status == 'ACC_MADE') {
              delete settings.credentials.new_password;
              delete settings.credentials.new_username;
              $('#page').html(
                $('<div>').addClass('description').text('Account made! Logging in..')
              );
            }
            else {
              $('#page').html(
                $('<div>').addClass('description').text('Logging in..')
              );
            }
            settings.credentials.authstring = returnedData.authorize.challenge;
            getData(callBack,sendData,timeOut);
            return;
          }
          else {
            $('#message').addClass('red').text('The credentials you provided are incorrect.');
            $('#connection').addClass('red').removeClass('green').text('Disconnected');
            showTab('login');
            $('#shield').remove();
            return;
          }
        }
        else if (returnedData.authorize.status == 'NOACC') {
          $('#message').addClass('red').text('The server does not have any accounts set.');
          $('#connection').addClass('green').removeClass('red').text('Connected');
          showTab('create new account');
          $('#shield').remove();
          return;
        }
        else {
          $('#connection').addClass('green').removeClass('red').text('Connected');
        }
      }
      
      if (callBack) {
        callBack(returnedData);
      }
      $('#message').text('Last communication with the server at '+formatTime((new Date).getTime()/1000));
      
      if (returnedData.log) {
        var lastlog = returnedData.log[returnedData.log.length-1];
        $('#message').append(
          $('<br>')
        ).append(
          $('<span>').text(
            'Last log entry: '+formatTime(lastlog[0])+' ['+lastlog[1]+'] '+lastlog[2]
          ).addClass(lastlog[1] == 'WARN' ? 'orange' : '')
        );
      }
      $('#shield').remove();
    }
  };
  
  var jqxhr = $.ajax(obj);
}

function getWikiData(url,callBack) {
  var wikiHost = 'http://rework.mistserver.org'; //must be changed when rework goes live
  
  $('#message').removeClass('red').text('Connecting to the MistServer wiki..').append(
    $('<br>')
  ).append(
    $('<a>').text('Cancel request').click(function(){
      jqxhr.abort();
    })
  );
  
  var obj = {
    'url': wikiHost+url,
    'type': 'GET',
    'crossDomain': true,
    'data': {
      'skin': 'plain'
    },
    'error':function(jqXHR,textStatus,errorThrown){
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
      $('#message').addClass('red').text('An error occurred while attempting to communicate with the MistServer wiki:').append(
        $('<br>')
      ).append(
        textStatus
      ).append(
        $('<a>').text('Send server request again').click(function(){
          getWikiData(url,callback);
        })
      );
    },
    'success': function(returnedData){
      $('#message').text('Wiki data received');
      
      //convert to DOM elements
      //returnedData = $.parseHTML(returnedData);
      returnedData = $(returnedData);
      
      //fix broken slash-links in the imported data
      returnedData.find('a[href]').each(function(){
        if ((this.hostname == '') || (this.hostname == undefined)) {
          $(this).attr('href',wikiHost+$(this).attr('href'));
        }
        if (!$(this).attr('target')) {
          $(this).attr('target','_blank');
        }
      }).find('img[src]').each(function(){
        var a = $('<a>').attr('href',$(this).attr('src'));
        if ((a.hostname == '') || (a.hostname == undefined)) {
          $(this).attr('src',wikiHost+$(this).attr('src'));
        }
      });
      
      consolelog('['+(new Date).toTimeString().split(' ')[0]+']','Received wiki data:',returnedData);
      
      if (callBack) {
        callBack(returnedData);
      }
      $('#message').text('Last communication with the MistServer wiki at '+formatTime((new Date).getTime()/1000));
      
    }
  };
  
  var jqxhr = $.ajax(obj);
}

function saveAndReload(tabName){
  var sendData = $.extend(true,{},settings.settings);
  delete sendData.logs;
  delete sendData.statistics;
  if (settings.credentials.authstring) {
    sendData.capabilities = true;
    sendData = $.extend(true,{conversion: {'encoders': true, 'status': true}},sendData);
  }
  getData(function(returnedData){
    settings.settings = returnedData;
    if (tabName) { showTab(tabName); }
  },sendData,0,true);
}
var countrylist = {'AF':'Afghanistan','AX':'&Aring;land Islands','AL':'Albania','DZ':'Algeria','AS':'American Samoa','AD':'Andorra',
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
};

function updateOverview() {
  getData(function(data){
    var viewers = 0;
    var streams = 0;
    var streamsOnline = 0;
    
    if (data.clients && data.clients.data) {
      viewers = data.clients.data.length;
    }
    
    for (var index in data.streams) {
      streams++;
      if ((data.streams[index].online) && (data.streams[index].online != 0)) {
        streamsOnline++;
      }
    }
    
    $('#cur_streams_online').text(streamsOnline+'/'+streams+' online');
    $('#cur_num_viewers').text(seperateThousands(viewers,' '));
    $('#settings-config-time').text(formatDateLong(data.config.time));
    
    settings.settings.statistics = data.statistics;
  },{clients: {}});
}
function updateProtocols() {
  getData(function(data){
    if (!data.config.protocols) { data.config.protocols = []; }
    if (data.config.protocols.length != $('#protocols-tbody').children().length) {
      saveAndReload('protocols');
      return;
    }
    for (var index in data.config.protocols) {
      $('#status-of-'+index).html(formatStatus(data.config.protocols[index]))
    }
  });
}

function displayProtocolSettings(theProtocol) {
  var capabilities = settings.settings.capabilities.connectors[theProtocol.connector];
  if (!capabilities) {
    return '';
  }
  var settingsList = [];
  for (var index in capabilities.required) {
    if ((theProtocol[index]) && (theProtocol[index] != '')) {
      settingsList.push(index+': '+theProtocol[index]);
    }
    else {
      if (capabilities.required[index]['default']) {
        settingsList.push(index+': '+capabilities.required[index]['default']);
      }
    }
  }
  for (var index in capabilities.optional) {
    if ((theProtocol[index]) && (theProtocol[index] != '')) {
      settingsList.push(index+': '+theProtocol[index]);
    }
    else {
      if (capabilities.optional[index]['default']) {
        settingsList.push(index+': '+capabilities.optional[index]['default']);
      }
    }
  }
  return settingsList.join(', ');
}
function buildProtocolFields(selectedProtocol,objpath,streamName) {
  data = settings.settings.capabilities.connectors[selectedProtocol];
  
  $('#protocol-description').html(data.desc);
  
  if (data.deps) {
    $ul = $('<ul>');
    $('#protocol-description').append('<br>Dependencies:').append($ul);
    var dependencies = data.deps.split(',');
    for (var index in dependencies) {
      if ($.inArray(dependencies[index],currentConnectors) < 0) {
        $ul.append(
          $('<li>').text(dependencies[index]).append(
            $('<span>').text(' (Not yet configured)').addClass('red')
          )
        );
      }
      else {
        $ul.append(
          $('<li>').text(dependencies[index]).append(
            $('<span>').text(' (Configured)').addClass('green')
          )
        );
      }
    }
  }
  
  $('#protocol-fields').html('');
  if (data.required) {
    $('#protocol-fields').append(
      $('<p>').text('Required parameters')
    ).append(
      buildProtocolParameterFields(data.required,true,objpath)
    );
  }
  if (data.optional) {
    $('#protocol-fields').append(
      $('<p>').text('Optional parameters')
    ).append(
      buildProtocolParameterFields(data.optional,false,objpath)
    );
  }
  
  if ((streamName != '_new_') && (selectedProtocol == settings.settings.config.protocols[streamName].connector)) {
    enterSettings();
  }
}

function buildProtocolParameterFields(data,required,objpath) {
  var $container = $('<div>');
  for (var index in data) {
    var $label = $('<label>').text(data[index].name+':').attr('title',data[index].help+'.').attr('for','protocol-fieldname')
    var $input = $('<input>').attr('type','text').attr('id','protocol-'+index).attr('objpath',objpath+'.'+index).addClass('isSetting');
    switch (data[index].type) {
      case 'int':
      case 'uint':
        $input.addClass('validate-positive-integer');
        break;
      case 'str':
      default:
        
        break;
    }
    if (required) {
      $input.addClass('validate-required');
    }
    if (data[index]['default']) {
      $input.attr('placeholder',data[index]['default'])
    }
    $label.append($input);
    $container.append($label);
  }
  return $container.html();
}
function updateStreams() {
  var streamlist = [];
  for (var stream in settings.settings.streams) {
    streamlist.push(stream);
  }
  getData(function(data){
    var datafields = {};
    for (var index in data.clients.fields) {
      datafields[data.clients.fields[index]] = index;
    }
    var viewers = {};
    for (var index in data.clients.data) {
      if (viewers[data.clients.data[index][datafields['stream']]]) {
        viewers[data.clients.data[index][datafields['stream']]]++;
      }
      else {
        viewers[data.clients.data[index][datafields['stream']]] = 1;
      }
    }
    for (var index in data.streams) {
      $('#status-of-'+index).html(formatStatus(data.streams[index]))
      $('#viewers-of-'+index).text(seperateThousands(viewers[index],' '));
    }

    settings.settings.statistics = data.statistics;
  },{clients:{}});
}
function filterTable() {
  var displayRecorded = $('#stream-filter-recorded').is(':checked');
  var displayLive = $('#stream-filter-live').is(':checked');
  
  $('#streams-tbody').children().each(function(){
    var type = $(this).children('.isLive').text();
    if (type == 'Live') {
      if (displayLive) {
        $(this).show();
      }
      else {
        $(this).hide();
      }
    }
    else {
      if (displayRecorded) {
        $(this).show();
      }
      else {
        $(this).hide();
      }
    }
  });
}
function limitShortToLong(shortLimit) {
  switch (shortLimit) {
    case 'kbps_max': return 'Maximum bandwidth';       break;
    case 'users'   : return 'Maximum connected users'; break;
    case 'geo'     : return 'Geo-limited';             break;
    case 'host'    : return 'Host-limited';            break;
  }
}
function limitValueFormat(theLimit) {
  switch (theLimit.name) {
    case 'kbps_max':
      return seperateThousands(theLimit.value,' ')+' bytes/s';
      break;
    case 'users':
      return theLimit.value;
      break;
    case 'geo':
      var str;
      if (theLimit.value.charAt(0) == '+') {
        str = 'Whitelist<br>'
      }
      else {
        str = 'Blacklist<br>'
      }
      var values = theLimit.value.substr(1).split(' ');
      for (var index in values) {
        values[index] = countrylist[values[index]];
      }
      return str+values.join(', ')
      break;
    case 'host':
      var str;
      if (theLimit.value.charAt(0) == '+') {
        str = 'Whitelist<br>'
      }
      else {
        str = 'Blacklist<br>'
      }
      return str+theLimit.value.substr(1).split(' ').join(', ');
      break;
    default:
      return '';
      break;
  }
}
function makeLimitValue() {
  var values = [];
  $('#field_container').children('input,select').each(function(){
    if ($(this).val()) {
      values.push($(this).val());
    }
  });
  
  $('#limit-value').val($('#limit-list-type').val()+values.join(' '));
}

function changeLimitName(limitValue) {
  if (!limitValue) {
    limitValue = '';
  }
  $('#limit-value').val(limitValue).removeClass('validate-positive-integer').parent().children('.unit').text('');
  $('#detailed-settings').html('');
  $('#limit-value-label').show();
  var $listtype = $('<select>').attr('id','limit-list-type').html(
    $('<option>').val('-').text('Blacklist')
  ).append(
    $('<option>').val('+').text('Whitelist')
  ).change(function(){
    makeLimitValue();
  });
  switch ($('#limit-name').val()) {
    case 'kbps_max':
      $('#limit-value').addClass('validate-positive-integer').parent().children('.unit').text('[bytes/s]');
      break;
    case 'users':
      $('#limit-value').addClass('validate-positive-integer');
      break;
    case 'geo':
      $('#limit-value-label').hide();
      var $field = $('<select>').html(
        $('<option>').val('').text('[Select a country]')
      ).bind('change',function(){
        makeLimitValue();
      });
      for (var index in countrylist) {
        $field.append(
          $('<option>').val(index).html(countrylist[index])
        );
      }
      $('#detailed-settings').html(
        $('<label>').attr('for','limit-list-type').text('List type:').append(
          $listtype
        )
      ).append(
        $('<span>').addClass('pretend-label').attr('id','field_container').text('Values:').css('overflow','hidden')
      );
      // ^  can't actually be a label because issues arise with multiple inputs in one label
      
      switch (limitValue.charAt(0)) {
        case '+': $listtype.val('+'); break;
        case '-': $listtype.val('-'); break;
      }
      limitValue = limitValue.substr(1).split(' ');
      for (var index in limitValue) {
        $('#field_container').append(
          $field.clone(true).val(limitValue[index])
        )
      }
      $('#field_container').append(
        $field.clone(true)
      );
      $('#detailed-settings').append(
        $('<button>').text('Add country list element').click(function(){
          $('#field_container').append(
            $field.clone(true)
          );
        })
      );
      makeLimitValue();
    break;
    case 'host':
      $('#limit-value-label').hide();
      var $field = $('<input>').attr('type','text').bind('change',function(){
        makeLimitValue();
      });
      $('#detailed-settings').html(
        $('<label>').attr('for','limit-list-type').text('List type:').append(
          $listtype
        )
      ).append(
        $('<span>').addClass('pretend-label').attr('id','field_container').text('Values:').css('overflow','hidden')
      );
      // ^  can't actually be a label because issues arise with multiple inputs in one label
      
      switch (limitValue.charAt(0)) {
        case '+': $listtype.val('+'); break;
        case '-': $listtype.val('-'); break;
      }
      limitValue = limitValue.substr(1).split(' ');
      for (var index in limitValue) {
        $('#field_container').append(
          $field.clone(true).val(limitValue[index])
        )
      }
      $('#field_container').append(
        $field.clone(true)
      );
      $('#detailed-settings').append(
        $('<button>').text('Add host list element').click(function(){
          $('#field_container').append(
            $field.clone(true)
          );
        })
      );
      makeLimitValue();
    break;
  }
}
function formatConversionStatus(status) {
  if (typeof status == 'string') {
    if (status == 'Conversion successful') {
      return $('<span>').addClass('green').text(status);
    }
    else {
      return $('<span>').addClass('red').text(status);
    }
  }
  else {
    return $('<span>').addClass('orange').text('Converting.. ('+status.progress+'%)');
  }
}

function updateConversions() {
  getData(function(data){
    for (var index in data.conversion.status) {
      $('#conversion-status-of-'+index).html(formatConversionStatus(data.conversion.status[index]))
    }
    settings.settings.conversion.status = data.conversion.status;
  },{conversion: {status: true}});
}
function conversionDirQuery(query){
  defaults.conversion.inputdir = query;
  $('#query-status').text('Searching for input files in "'+query+'"..');
  $('#conversion-input-file').html('');
  $('#conversion-details').html('');
  var objpath = 'settings-conversion-convert-_new_';
  var theFiles = {};
  getData(function(data){
    if (data.conversion.query) {
      var $inputfile = $('<select>').attr('id',objpath+'-input').addClass('isSetting').addClass('validate-required').change(function(){
        conversionSelectInput(theFiles);
      });
      dir = query.replace(/\/$/,'')+'/';
      $('#conversion-input-file').html(
        $('<p>').text('Files from "'+dir+'"')
      ).append(
        $('<div>').addClass('description').text('Next, select the file you wish to convert.')
      ).append(
        $('<label>').text('Input file:').attr('for',objpath+'-input').addClass('isSetting').append(
          $inputfile
        )
      );
      
      var filesFound = {total:0,valid:0}
      for (var index in data.conversion.query) {
        filesFound.total++;
        if ((data.conversion.query[index]) && (index.substr(-5) != '.dtsc')) {
          filesFound.valid++;
          $inputfile.append(
            $('<option>').text(index).val(dir+index)
          );
          theFiles[index] = data.conversion.query[index];
        }
      }
      $('#query-status').text(filesFound.total+' file(s) found, of which '+filesFound.valid+' are valid.');
      $inputfile.children().sort(function(a,b){
        return ($(a).text().toLowerCase() > $(b).text().toLowerCase()) ? 1 : -1;
      }).appendTo($inputfile);
      
      if (filesFound.valid > 0) {
        conversionSelectInput(theFiles);
      }
    }
    else {
      $('#query-status').text('No files found in "'+query+'".');
    }
  },{conversion:{query:{path:query}}},60000); //1 minute timeout
}

function conversionSelectInput(theFiles) {
  var objpath = 'settings-conversion-convert-_new_';
  var index = $('#'+objpath+'-input option:selected').text();
  var filename = index.split('.');
  filename.splice(-1);
  filename = filename.join('.');
  theFile = theFiles[index];
  theFile = $.extend(true,{audio:{samplerate:''},video:{fpks:'',width:'',height:''}},theFile);
  theFile.video.fps = theFile.video.fpks ? theFile.video.fpks/1000 : '';
  var $encoders = $('<select>').attr('id',objpath+'-encoder').addClass('isSetting').addClass('validate-required').change(function(){
    $videocodec.html(
      $('<option>').text('Current').val('')
    );
    for (var codec in settings.settings.conversion.encoders[$(this).val()].video) {
      $videocodec.append(
        $('<option>').val(codec).text(settings.settings.conversion.encoders[$(this).val()].video[codec]+' ('+codec+')')
      );
    }
    $audiocodec.html(
      $('<option>').text('Current').val('')
    );
    for (var codec in settings.settings.conversion.encoders[$(this).val()].audio) {
      $audiocodec.append(
        $('<option>').val(codec).text(settings.settings.conversion.encoders[$(this).val()].audio[codec]+' ('+codec+')')
      );
    }
  });
  var $videocodec = $('<select>').attr('id',objpath+'-video-codec').addClass('isSetting').change(function(){
    if ($(this).val() == '') {
      $('#video-settings-container').find('input,select').not($(this)).val('').removeClass('isSetting').parent().hide();
    }
    else {
      $('#video-settings-container').find('input,select').not($(this)).addClass('isSetting').parent().show();
    }
  });
  var $audiocodec = $('<select>').attr('id',objpath+'-audio-codec').addClass('isSetting').change(function(){
    if ($(this).children(':selected').text().substr(0,4) == 'mp3 ') {
      $('#settings-conversion-convert-_new_-audio-samplerate').val('44100').attr('disabled','disabled');
    }
    else {
      $('#settings-conversion-convert-_new_-audio-samplerate').removeAttr('disabled');
      if ($(this).val() == '') {
        $('#audio-settings-container').find('input,select').not($(this)).val('').removeClass('isSetting').parent().hide();
      }
      else {
        $('#audio-settings-container').find('input,select').not($(this)).addClass('isSetting').parent().show();
      }
    }
  });
  
  $('#conversion-details').html(
    $('<p>').text('Conversion settings for "'+index+'"')
  ).append(
    $('<div>').addClass('description').text('Finally, select the file properties you wish to convert to.')
  ).append(
    $('<label>').text('Output directory:').attr('for',objpath+'-outputdir').append(
      $('<input>').attr('type','text').attr('id',objpath+'-outputdir').val(dir).attr('placeholder','/path/to/output/folder').addClass('isSetting').addClass('validate-required')
    )
  ).append(
    $('<label>').text('Output filename:').attr('for',objpath+'-output').append(
      $('<input>').attr('type','text').attr('id',objpath+'-output').val(filename+'.dtsc').attr('placeholder','name_of_output.file').addClass('isSetting').addClass('validate-required')
    )
  ).append(
    $('<label>').text('Encoder:').attr('for',objpath+'-encoder').append(
      $encoders
    )
  ).append(
    $('<label>').text('Include video:').attr('for',objpath+'-video').append(
      $('<input>').attr('type','checkbox').attr('id',objpath+'-video').attr('checked','checked').change(function(){
        if (!$(this).is(':checked')) {
          $('#video-settings-container').hide().find('input,select').val('').removeClass('isSetting');
        }
        else {
          $('#video-settings-container').show().find('input,select').addClass('isSetting');
        }
        $videocodec.trigger('change');
      })
    )
  ).append(
    $('<span>').attr('id','video-settings-container').html(
      $('<label>').text('Video codec:').attr('for',objpath+'-video-codec').append(
        $videocodec
      )
    ).append(
      $('<label>').text('Video FPS:').attr('for',objpath+'-video-fps').append(
        $('<input>').attr('type','text').attr('id',objpath+'-video-fps').attr('placeholder',theFile.video.fps).addClass('isSetting').addClass('validate-positive-number')
      )
    ).append(
      $('<label>').text('Video width:').attr('for',objpath+'-video-width').append(
        $('<span>').addClass('unit').text('[px]')
      ).append(
        $('<input>').attr('type','text').attr('id',objpath+'-video-width').attr('placeholder',theFile.video.width).addClass('isSetting').addClass('validate-positive-integer')
      )
    ).append(
      $('<label>').text('Video height:').attr('for',objpath+'-video-height').append(
        $('<span>').addClass('unit').text('[px]')
      ).append(
        $('<input>').attr('type','text').attr('id',objpath+'-video-height').attr('placeholder',theFile.video.height).addClass('isSetting').addClass('validate-positive-integer')
      )
    )
  ).append(
    $('<label>').text('Include audio:').attr('for',objpath+'-audio').append(
      $('<input>').attr('type','checkbox').attr('id',objpath+'-audio').attr('checked','checked').change(function(){
        if (!$(this).is(':checked')) {
          $('#audio-settings-container').hide().find('input,select').val('').removeClass('isSetting');
        }
        else {
          $('#audio-settings-container').show().find('input,select').addClass('isSetting');
        }
        $audiocodec.trigger('change');
      })
    )
  ).append(
    $('<span>').attr('id','audio-settings-container').html(
      $('<label>').text('Audio codec:').attr('for',objpath+'-audio-codec').append(
        $audiocodec
      )
    ).append(
      $('<label>').text('Audio sample rate:').attr('for',objpath+'-audio-samplerate').append(
        $('<span>').addClass('unit').text('[Hz]')
      ).append(
        $('<input>').attr('type','text').attr('id',objpath+'-audio-samplerate').attr('placeholder',seperateThousands(theFile.audio.samplerate,' ')).addClass('isSetting').addClass('validate-positive-integer')
      )
    )
  ).append(
    $('<button>').text('Save').addClass('enter-to-submit').click(function(){
      if (!settings.settings.conversion.convert) {
        settings.settings.conversion.convert = {};
      }
      settings.settings.conversion.convert._new_= {};
      if ($('#'+objpath+'-video').is(':checked')) {
        settings.settings.conversion.convert._new_.video = {};
      }
      if ($('#'+objpath+'-audio').is(':checked')) {
        settings.settings.conversion.convert._new_.audio = {};
      }
      
      applyInput();
      
      var extension = settings.settings.conversion.convert._new_.output.split('.');
      if (extension[extension.length-1] != 'dtsc') {
        extension.push('dtsc');
        settings.settings.conversion.convert._new_.output = extension.join('.');
      }
      settings.settings.conversion.convert._new_.output = settings.settings.conversion.convert._new_.outputdir.replace(/\/$/,'')+'/'+settings.settings.conversion.convert._new_.output;
      delete settings.settings.conversion.convert._new_.outputdir;
      if ((settings.settings.conversion.convert._new_.video) && (settings.settings.conversion.convert._new_.video.fps)) {
        settings.settings.conversion.convert._new_.fpks = Math.floor(settings.settings.conversion.convert._new_.fps * 1000);
      }
      
      
      
      settings.settings.conversion.convert['c_'+(new Date).getTime()] = settings.settings.conversion.convert._new_;
      delete settings.settings.conversion.convert._new_;
      saveAndReload('conversion');
    })
  ).append(
    $('<button>').text('Cancel').addClass('escape-to-cancel').click(function(){
      showTab('conversion');
    })
  );
  
  for (var encoder in settings.settings.conversion.encoders) {
    $encoders.append(
      $('<option>').text(encoder).val(encoder)
    );
  }
  $encoders.trigger('change');
  $audiocodec.trigger('change');
  $videocodec.trigger('change');
}
function buildLogsTable(){
  var logs = settings.settings.log;
  if ((logs.length >= 2) && (logs[0][0] < logs[logs.length-1][0])){
    logs.reverse();
  }
  
  var $tbody = $('<tbody>');
  for (var index in logs) {
    $tbody.append(
      $('<tr>').html(
        $('<td>').text(formatDateLong(logs[index][0]))
      ).append(
        $('<td>').text(logs[index][1])
      ).append(
        $('<td>').text(logs[index][2])
      )
    );
  }
  
  return $('<table>').attr('id','logs-table').html(
    $('<thead>').html(
      $('<tr>').html(
        $('<th>').text('Date')
      ).append(
        $('<th>').text('Type')
      ).append(
        $('<th>').text('Message')
      )
    )
  ).append($tbody);
}
function fillServerstatsTables(data) {
  if (data.capabilities.mem) {
    $('#stats-physical-memory').html(
      $('<tr>').html(
        $('<td>').text('Used:')
      ).append(
        $('<td>').html(seperateThousands(data.capabilities.mem.used,' ')+' MiB<br>('+data.capabilities.load.memory+'%)').css('text-align','right')
      )
    ).append(
      $('<tr>').html(
        $('<td>').text('Cached:')
      ).append(
        $('<td>').text(seperateThousands(data.capabilities.mem.cached,' ')+' MiB').css('text-align','right')
      )
    ).append(
      $('<tr>').html(
        $('<td>').text('Available:')
      ).append(
        $('<td>').text(seperateThousands(data.capabilities.mem.free,' ')+' MiB').css('text-align','right')
      )
    ).append(
      $('<tr>').html(
        $('<td>').text('Total:')
      ).append(
        $('<td>').text(seperateThousands(data.capabilities.mem.total,' ')+' MiB').css('text-align','right')
      )
    );
    $('#stats-swap-memory').html(
      $('<tr>').html(
        $('<td>').text('Used:')
      ).append(
        $('<td>').text(seperateThousands(data.capabilities.mem.swaptotal-data.capabilities.mem.swapfree,' ')+' MiB').css('text-align','right')
      )
    ).append(
      $('<tr>').html(
        $('<td>').text('Available:')
      ).append(
        $('<td>').text(seperateThousands(data.capabilities.mem.swapfree,' ')+' MiB').css('text-align','right')
      )
    ).append(
      $('<tr>').html(
        $('<td>').text('Total:')
      ).append(
        $('<td>').text(seperateThousands(data.capabilities.mem.swaptotal,' ')+' MiB').css('text-align','right')
      )
    );
  }
  if (data.capabilities.load) {
    $('#stats-loading').html(
      $('<tr>').html(
        $('<td>').text('1 minute:')
      ).append(
        $('<td>').text(settings.settings.capabilities.load.one/100+'%').css('text-align','right')
      )
    ).append(
      $('<tr>').html(
        $('<td>').text('5 minutes:')
      ).append(
        $('<td>').text(settings.settings.capabilities.load.five/100+'%').css('text-align','right')
      )
    ).append(
      $('<tr>').html(
        $('<td>').text('15 minutes:')
      ).append(
        $('<td>').text(settings.settings.capabilities.load.fifteen/100+'%').css('text-align','right')
      )
    );
  }
}

function updateServerstats() {
  getData(function(data){
    fillServerstatsTables(data);
    settings.settings.capabilities = data.capabilities;
  },{capabilities:true});
}

function buildstreamembed(streamName,embedbase) {
  $('#liststreams .button.current').removeClass('current')
  $('#liststreams .button').filter(function(){
    return $(this).text() == streamName;
  }).addClass('current');
  
  $('#subpage').html(
    $('<div>').addClass('input_container').html(
      $('<label>').text('The info embed URL is:').append(
        $('<input>').attr('type','text').attr('readonly','readonly').val(embedbase+'info_'+streamName+'.js')
      )
    ).append(
      $('<label>').text('The embed URL is:').append(
        $('<input>').attr('type','text').attr('readonly','readonly').val(embedbase+'embed_'+streamName+'.js')
      )
    ).append(
      $('<label>').text('The embed code is:').css('overflow','hidden').append(
        $('<textarea>').val('<div>\n  <script src="'+embedbase+'embed_'+streamName+'.js"></' + 'script>\n</div>')
      )
    )
  ).append(
    $('<span>').attr('id','listprotocols').text('Loading..')
  ).append(
    $('<p>').text('Preview:')
  ).append(
    $('<div>').attr('id','preview-container').attr('data-forcesupportcheck',1)
  ).append(
    $('<div>').addClass('input_container').append(
      $('<label>').text('Stream embed url:').append(
        $('<input>').attr('type','text').attr('readonly','readonly').attr('id','streamurl')
      )
    )
  );
  
  // jQuery doesn't work -> use DOM magic
  var script = document.createElement('script');
  script.src = embedbase+'embed_'+streamName+'.js';
  script.onerror = function(){
    $('#preview-container').text('Failed to load embed script.');
  };
  script.onload = function(){
    if (typeof mistvideo[streamName].error != 'undefined') {
      $('#preview-container').text(mistvideo[streamName].error);
    }
    else {
      var priority = mistvideo[streamName].source;
      if (typeof priority != 'undefined') {
        $radio = $('<input>').attr('type','radio').attr('name','forcetype').attr('title','The embed type that is being used.').change(function(){
          $('#preview-container').attr('data-forcetype',$(this).val()).html('');
          $(this).closest('table').find('tr.outline').removeClass('outline');
          $(this).closest('tr').addClass('outline');
          $('#streamurl').val(mistvideo[streamName].source[$(this).val()].url)
          
          var script = document.createElement('script');
          script.src = embedbase+'embed_'+streamName+'.js';
          script.onload = function(){
            
          };
          document.getElementById('preview-container').appendChild( script );
        });
        priority.sort(function(a,b){
          return b.priority - a.priority;
        });
        var $table = $('<table>').html(
          $('<tr>').html(
            $('<th>')
          ).append(
            $('<th>').text('Type')
          ).append(
            $('<th>').text('Priority')
          ).append(
            $('<th>').text('Simul. tracks')
          ).append(
            $('<th>').text('Browser support')
          )
        );
        for (var i in priority) {
          var type = priority[i].type.split('/');
          var humantype = type[0];
          switch (type.length) {
            case 1:
              break;
            case 2:
              humantype += ' v'+type[1];
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
                  humantype = priority[i].type;
              }
              break;
            default:
              humantype = priority[i].type;
          }
          humantype = humantype.charAt(0).toUpperCase()+humantype.slice(1);
          if (priority[i].browser_support) {
            bsup = 'yes';
          }
          else {
            bsup = 'no';
          }
          $table.append(
            $('<tr>').html(
              $('<td>').html(
                $radio.clone(true).attr('data-name',priority[i].type).val(i)
              )
            ).append(
              $('<td>').text(humantype)
            ).append(
              $('<td>').addClass('align-center').text(priority[i].priority)
            ).append(
              $('<td>').text(priority[i].simul_tracks+'/'+priority[i].total_matches)
            ).append(
              $('<td>').text(bsup)
            )
          );
        }
        $('#listprotocols').html($table);
        $table.find('[name=forcetype][data-name="'+mistvideo[streamName].embedded.type+'"]').attr('checked','checked').closest('tr').addClass('outline');
        $('#streamurl').val(mistvideo[streamName].embedded.url)
      }
      else {
        $('#listprotocols').html('No data in info embed file.');
      }
    }
  }
  document.getElementById('preview-container').appendChild( script );
}

$(function(){
  $('#logo > a').click(function(){
    if ($.isEmptyObject(settings.settings)) {
      showTab('login')
    }
    else {
      showTab('overview');
    }
  });
  $('#menu div.button').click(function(e){
    //if ((settings.settings.LTS != 1) && ($(this).hasClass('LTS-only'))) { return; }
    showTab($(this).text().toLowerCase());
    e.stopPropagation();
  })
  $('body').on('keydown',function(e){
    switch (e.which) {
      case 13:
        //the return key
        $(document.activeElement).parents('.input_container').children('.enter-to-submit').trigger('click');
      break;
      case 27:
        //the escape key
        $('.escape-to-cancel').trigger('click');
      break;
    }
  });
  $('#page').on('input','.validate-positive-integer',function(e){
    var curpos = this.selectionStart;       //store the cursor position
    var v = $(this).val();                  //validate the current value
    v = v.replace(/[^\d]/g,'');
    if (Number(v) == 0) {
      v = '';
    }
    
    $('#input-validation-info').remove();
    if (v != $(this).val()) {
      $(this).parent().append(
        $('<div>').attr('id','input-validation-info').html(
          'Invalid input.<br>Input in this field must be a positive nonzero integer.'
        ).addClass('red')
      );
    }
    
    $(this).val(v);
    this.setSelectionRange(curpos,curpos);  //set the cursor to its original position
  });
  $('#page').on('input','.validate-positive-number',function(e){
    var curpos = this.selectionStart;       //store the cursor position
    var v = $(this).val();                  //validate the current value
    v = v.replace(/[^\d\.]/g,'');
    v = v.split('.');
    if (v.length > 1) {
      var decimal = v.splice(1).join('');
      v.push(decimal);
    }
    v = v.join('.');
    if (Number(v) == 0) {
      v = '';
    }
    
    $('#input-validation-info').remove();
    if (v != $(this).val()) {
      $(this).parent().append(
        $('<div>').attr('id','input-validation-info').html(
          'Invalid input.<br>Input in this field must be a positive nonzero number.'
        ).addClass('red')
      );
    }
    
    $(this).val(v);
    this.setSelectionRange(curpos,curpos);  //set the cursor to its original position
  });
  $('#page').on('input','.validate-lowercase-alphanumeric_-firstcharNaN',function(e){
    var curpos = this.selectionStart;
    var v = $(this).val();
    v = v.toLowerCase(); //make everything lowercase
    v = v.replace(/[^\da-z_]/g,'');      //remove any chars except for digits, lowercase letters and underscores
    v = v.replace(/^\d+/,'');            //remove any digits at the front of the string
    
    $('#input-validation-info').remove();
    if (v != $(this).val()) {
      $(this).parent().append(
        $('<div>').attr('id','input-validation-info').html(
          'Invalid input.<br>Input in this field must be:<ul><li>lowercase</li><li>alphanumeric or underscore</li><li>the first character can\'t be numerical</li></ul>'
        ).addClass('red')
      );
    }
    
    $(this).val(v);
    this.setSelectionRange(curpos,curpos);
  });
  
  $('.expandbutton').click(function(){
    $(this).toggleClass('active');
  });
  
  
  $('#ih-button').click(function(){
    $('.ih-balloon').remove();
    if (!ih) {
      getWikiData('/wiki/Integrated_Help',function(data){
        settings.ih = { 
          raw: data.find('#mw-content-text').contents(),
          pages: {}
        }
        settings.ih.raw.filter('.page[data-pagename]').each(function(){
          var pagename = $(this).attr('data-pagename').replace(' ','_');
          settings.ih.pages[pagename] = {
            raw: $(this).contents(),
            pageinfo: $(this).find('.page-description').html(),
            inputs: {}
          }
          $(this).children('.input-description[data-inputid]').each(function(){
            settings.ih.pages[pagename].inputs[$(this).attr('data-inputid')] = $(this).html();
          });
        });
        consolelog('New integrated help data:',settings.ih);
        ihAddBalloons();
      });
    }
    ih = !ih;
    $(this).toggleClass('active');
  });
  
});

$(window).on('hashchange', function(e) {
  if (ignoreHashChange) { 
    ignoreHashChange = false;
    return; 
  }
  var loc = location.hash.split('&')[1].split('@');
  if (loc[1]) {
    showTab(loc[0],loc[1]);
  }
  else {
    showTab(loc[0]);
  }
  ignoreHashChange = false;
});

function localStorageSupported() {
  //does this browser support it?
  try {
    return 'localStorage' in window && window['localStorage'] !== null;
  } catch (e) {
    return false;
  }
}

/* functions for the statistics graphs */

function getPlotData(graphs) {
  var reqobj = {
    totals: []
  };
  for (var g in graphs) {
    for (var d in graphs[g].datasets) {
      var set = graphs[g].datasets[d];
      switch (set.type) {
        case 'clients':
        case 'upbps':
        case 'downbps':
          switch (set.cumutype) {
            case 'all':       reqobj['totals'].push({fields: [set.type]});                               break;
            case 'stream':    reqobj['totals'].push({fields: [set.type], streams: [set.stream]});        break;
            case 'protocol':  reqobj['totals'].push({fields: [set.type], protocols: [set.protocol]});    break;
          }
          set.sourceid = reqobj['totals'].length-1;
          break;
        case 'cpuload':
        case 'memload':
          reqobj['capabilities'] = {};
          break;
      }
    }
  }
  getData(function(data){
    for (var j in graphs) {
      for (var i in graphs[j].datasets) {
        graphs[j].datasets[i] = findDataset(graphs[j].datasets[i],data);
      }
      drawGraph(graphs[j]);
    }
  },reqobj);
}

function findDataset(dataobj,sourcedata) {
  var now = sourcedata.config.time;
  switch (dataobj.type) {
    case 'cpuload':
      //remove any data older than 10 minutes
      var removebefore = false;
      for (var i in dataobj.data) {
        if (dataobj.data[i][0] < (now-600)*1000) {
          removebefore = Number(i)+1;
        }
      }
      if (removebefore !== false) {
        dataobj.data.splice(0,removebefore);
      }
      dataobj.data.push([now*1000,sourcedata.capabilities.load.one/100]);
      break;
    case 'memload':
      //remove any data older than 10 minutes
      var removebefore = false;
      for (var i in dataobj.data) {
        if (dataobj.data[i][0] < (now-600)*1000) {
          removebefore = Number(i)+1;
        }
      }
      if (removebefore !== false) {
        dataobj.data.splice(0,removebefore);
      }
      dataobj.data.push([now*1000,sourcedata.capabilities.load.memory]);
      break;
    case 'upbps':
    case 'downbps':
    case 'clients':
      //todo: depending on the stream..
      if (!sourcedata.totals || !sourcedata.totals[dataobj.sourceid] || !sourcedata.totals[dataobj.sourceid].data) {
        dataobj.data.push([(now-600)*1000,0]);
        dataobj.data.push([now*1000,0]);
      }
      else {
        var fields = {};
        for (var index in sourcedata.totals[dataobj.sourceid].fields) {
          fields[sourcedata.totals[dataobj.sourceid].fields[index]] = index;
        }
        var time = sourcedata.totals[dataobj.sourceid].start;
        dataobj.data = [];
        if (time > now-590) {
          //prepend data with 0 
          dataobj.data.push([(now-600)*1000,0]);
          dataobj.data.push([time*1000-1,0]);
        }
        var index = 0;
        dataobj.data.push([[time*1000,sourcedata.totals[dataobj.sourceid].data[index][fields[dataobj.type]]]]);
        for (var i in sourcedata.totals[dataobj.sourceid].interval) {
          if ((i % 2) == 1) {
            //fill gaps with 0
            time += sourcedata.totals[dataobj.sourceid].interval[i][1];
            dataobj.data.push([time*1000,0]);
          }
          else {
            for (var j = 0; j < sourcedata.totals[dataobj.sourceid].interval[i][0]; j++) {
              time += sourcedata.totals[dataobj.sourceid].interval[i][1];
              index++;
              dataobj.data.push([time*1000,sourcedata.totals[dataobj.sourceid].data[index][fields[dataobj.type]]]);
            }
            if (i < sourcedata.totals[dataobj.sourceid].interval.length-1) {
              dataobj.data.push([time*1000+1,0]);
            }
          }
        }
        if (now > time + 10) {
          //append data with 0
          dataobj.data.push([time*1000+1,0]);
          dataobj.data.push([now*1000,0]);
        }
      }
      break;
    case 'coords':
      //retrieve data
      //format [lat,long]
      
      //testing data
      dataobj.data = [[-54.657438,-65.11675],[49.725719,-1.941553],[-34.425464,172.677617],[76.958669,68.494178],[0,0]];
      break;
  }
  
  
  return dataobj;
}

function drawGraph(graph){
  var datasets = graph.datasets;
  if (datasets.length < 1) { 
    $('#'+graph.id).children('.graph,.legend').html('');
    return; 
  }
  switch (graph.type) {
    case 'coords':
      plotsets = [];
      for (var d in datasets) {
        //put backend data into the correct projection
        data = datasets[d].data;
        //correct latitude according to the Miller cylindrical projection
        for (var i in  data) {
          var lat = data[i][0];
          var lon = data[i][1];
          //to radians
          lat = Math.PI * lat / 180;
          var y = 1.25 * Math.log(Math.tan(0.25 * Math.PI + 0.4 * lat));
          data[i] = [lon,y];
        }
        plotsets.push({data:data});
      }
      //make sure the plot area has the correct height/width ratio
      if ($('#'+graph.id+' .graphbackground').length == 0) {
        var parent = $('#'+graph.id+' .graph');
        var mapheight = 2450;
        var mapwidth = 3386.08;
        parent.height(parent.width()*mapheight/mapwidth);
        var placeholder = $('<div>').addClass('graphforeground')
        parent.html(
          $('<img>').attr('src','map.svg').addClass('graphbackground').width(parent.width).height(parent.height())
        ).append(
          placeholder
        );
      }
      else {
        var placeholder = $('#'+graph.id+' .graphforeground');
      }
      plot = $.plot(
        placeholder,
        plotsets,
        {
          legend: {show: false},
          xaxis: {
            show: false,
            min: -170,
            max: 190
          },
          yaxis: {
            show: false,
            min: -2.248101053,
            max: 2.073709536
          },
          series: {
            lines: {show: false},
            points: {show: true}
          },
          grid: {
            hoverable: true,
            margin: 0,
            border: 0,
            borderWidth: 0,
            minBorderMargin: 0
          }
        }
      );
      break;
      case 'time':
      case 'default':
        var yaxes = [];
        var yaxesTemplates = {
          percentage: {
            name: 'percentage',
            color: 'black',
            display: false,
            tickColor: 0,
            tickDecimals: 0,
            tickFormatter: function(val,axis){
              return val.toFixed(axis.tickDecimals) + '%';
            },
            tickLength: 0,
            min: 0
          },
          amount: {
            name: 'amount',
            color: 'black',
            display: false,
            tickColor: 0,
            tickDecimals: 0,
            tickFormatter: function(val,axis){
              return seperateThousands(val.toFixed(axis.tickDecimals),' ');
            },
            tickLength: 0,
            min: 0
          },
          bytespersec: {
            name: 'bytespersec',
            color: 'black',
            display: false,
            tickColor: 0,
            tickDecimals: 1,
            tickFormatter: function(val,axis){
              var suffix = ['bytes','KiB','MiB','GiB','TiB','PiB'];
              if (val == 0) { 
                val = val+' '+suffix[0];
              }
              else {
                var exponent = Math.floor(Math.log(Math.abs(val)) / Math.log(1024));
                if (exponent < 0) {
                  val = val.toFixed(axis.tickDecimals)+' '+suffix[0];
                }
                else {
                  val = Math.round(val / Math.pow(1024,exponent) * Math.pow(10,axis.tickDecimals)) / Math.pow(10,axis.tickDecimals) +' '+suffix[exponent];
                }
              }
              return val + '/s';
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
        };
        var xaxistemplates = {
          time: {
            name: 'time',
            mode: 'time',
            timezone: 'browser',
            ticks: 5
          }
        }
        var plotsets = [];
        for (var i in datasets) {
          if (datasets[i].display) {
            if (yaxesTemplates[datasets[i].yaxistype].display === false) {
              yaxes.push(yaxesTemplates[datasets[i].yaxistype]);
              yaxesTemplates[datasets[i].yaxistype].display = yaxes.length;
            }
            datasets[i].yaxis = yaxesTemplates[datasets[i].yaxistype].display;
            datasets[i].color = Number(i);
            plotsets.push(datasets[i]);
          }
        }
        if (yaxes[0]) { yaxes[0].color = 0; }
        plot = $.plot(
          $('#'+graph.id+' .graph'),
                      plotsets,
                      {
                        legend: {show: false},
                      xaxis: xaxistemplates[graph.type],
                      yaxes: yaxes,
                      grid: {
                        hoverable: true,
                      borderWidth: {top: 0, right: 0, bottom: 1, left: 1},
                      color: 'black',
                      backgroundColor: {colors: ['#fff','#ededed']}
                      }
                      }
        );
        break;
  } //end of graph type switch
  $('#'+graph.id+' .legend').html(
    $('<div>').addClass('legend-list').addClass('checklist')
  );
  var plotdata = plot.getOptions();
  for (var i in datasets) {
    var $checkbox = $('<input>').attr('type','checkbox').data('dataset-index',i).click(function(){
      if ($(this).is(':checked')) {
        datasets[$(this).data('dataset-index')].display = true;
      }
      else {
        datasets[$(this).data('dataset-index')].display = false;
      }
      drawGraph($(this).parents('.graph-item'));
    });
    if (datasets[i].display) {
      $checkbox.attr('checked','checked');
    }
    $('#'+graph.id+' .legend-list').append(
      $('<label>').html(
        $checkbox
      ).append(
        $('<div>').addClass('series-color').css('background-color',plotdata.colors[datasets[i].color % plotdata.colors.length])
      ).append(
        datasets[i].label
      )
    );
  }
  if (datasets.length > 0) {
    $('#'+graph.id+' .legend').append(
      $('<button>').text('Clear all').click(function(){
        var graph = graphs[$(this).parents('.graph-item').attr('id')];
        graph.datasets = [];
        drawGraph(graph);
      }).css({'float':'none'})
    );
  }
}
