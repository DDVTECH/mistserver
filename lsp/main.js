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
    
    if ($(this).val() == '') {
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
          $('<span>').text('Last log entry: '+formatTime(lastlog[0])+' ['+lastlog[1]+'] '+lastlog[2])
        );
      }
      $('#shield').remove();
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
    
    for (var index in data.statistics) {
      if (data.statistics[index].curr) {
        for (viewer in data.statistics[index].curr) {
          viewers++;
        }
      }
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
  });
}
function updateProtocols() {
  getData(function(data){
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
  getData(function(data){
    for (var index in data.streams) {
      $('#status-of-'+index).html(formatStatus(data.streams[index]))
    }
    for (var index in data.statistics) {
      var viewers = 0;
      if (data.statistics[index].curr) {
        for (var jndex in data.statistics[index].curr) {
          viewers++;
        }
      }
      $('#viewers-of-'+index).text(seperateThousands(viewers,' '));
    }
  });
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
        str = 'Only trigger on connections not from:<br>'
      }
      else {
        str = 'Only trigger on connections from:<br>'
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
        str = 'Only trigger on connections not from:<br>'
      }
      else {
        str = 'Only trigger on connections from:<br>'
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
    $('<option>').val('-').text('Only trigger on connections from..')
  ).append(
    $('<option>').val('+').text('Only trigger on connections not from..')
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
          $field.clone().val(limitValue[index])
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
  var $videocodec = $('<select>').attr('id',objpath+'-video-codec').addClass('isSetting');
  var $audiocodec = $('<select>').attr('id',objpath+'-audio-codec').addClass('isSetting');
  
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
          $('#video-settings-container').hide();
          $('#video-settings-container').find('input,select').val('');
        }
        else {
          $('#video-settings-container').show();
        }
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
          $('#audio-settings-container').hide();
          $('#audio-settings-container').find('input,select').val('');
        }
        else {
          $('#audio-settings-container').show();
        }
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
        $('<td>').text(settings.settings.capabilities.load.one+'%').css('text-align','right')
      )
    ).append(
      $('<tr>').html(
        $('<td>').text('5 minutes:')
      ).append(
        $('<td>').text(settings.settings.capabilities.load.five+'%').css('text-align','right')
      )
    ).append(
      $('<tr>').html(
        $('<td>').text('15 minutes:')
      ).append(
        $('<td>').text(settings.settings.capabilities.load.fifteen+'%').css('text-align','right')
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

$(function(){
  $('#menu div.button').click(function(){
    if ((settings.settings.LTS != 1) && ($(this).hasClass('LTS-only'))) { return; }
    showTab($(this).text().toLowerCase());
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