
         /**
          * Show a confirm dialog
          * @param question the question displayed
          */
         function confirmDelete(question)
         {
            return confirm(question);
         }


         /**
          * Format a date to mm/dd/yyyy hh:mm:ss format
          * @param date the date to format (timestamp)
          */
         function formatDate(date)
         {
            var d = new Date(date * 1000);
            // note: .getMonth() returns the month from 0-11
            return [
               ('00' + (d.getMonth()+1)).slice(-2),
               ('00' + d.getDate()).slice(-2),
               d.getFullYear()
            ].join('/') + ' ' + [
               ('00' + d.getHours()).slice(-2),
               ('00' + d.getMinutes()).slice(-2),
               ('00' + d.getSeconds()).slice(-2)
            ].join(':');
         }
         
          
          /**
          * Format a date to mmm dd /yyyy hh:mm:ss format
          * @param date the date to format (timestamp)
          */
          months = Array('Jan','Feb','Mar','Apr','May','Jun','Jul','Aug','Sep','Oct','Nov','Dec');
         function formatDateLong(date)
         {
            var d = new Date(date * 1000);
            
            return [
               months[d.getMonth()],
               ('00' + d.getDate()).slice(-2),
               d.getFullYear()
            ].join(' ') + ' ' + [
               ('00' + d.getHours()).slice(-2),
               ('00' + d.getMinutes()).slice(-2),
               ('00' + d.getSeconds()).slice(-2)
            ].join(':');
         }
         function nameMonth(monthNum) 
         {
            months = Array('Jan','Feb','Mar','Apr','May','Jun','Jul','Aug','Sep','Oct','Nov','Dec');
         }


         /**
          * Find out what kind of resource an URI is
          * @param uri the URI to check. If it start with a protocol (ebut not file://) return 'Live', else 'Recorded'
          */
         function TypeofResource(uri)
         {
            var protocol = /([a-zA-Z]+):\/\//.exec(uri);

            if(protocol === null || (protocol[1] && protocol[1] === 'file'))
            {
               return 'Recorded';
            }else{
               return 'Live';
            }
         }


         /**
          * convert a short limit name to a long one using the table above
          * @param name the short name of a limit
          */
         function shortToLongLimit(name)
         {
            var i;

            for(i = 0; i < ltypes.length; i++)
            {
               if(name == ltypes[i][0])
               {
                  return ltypes[i][1];
               }
            }

            return name;
         }


         /**
          * forse the server to save to the config file
          * @param callback function to call after the command is send
          */
         function forceJSONSave(callback)
         {
            // build the object to send to the server
            var data =
            {
               'authorize':
               {
                  'username': settings.credentials.username,
                  'password': (settings.credentials.authstring != "" ? MD5(MD5(settings.credentials.password) + settings.credentials.authstring) : "" )
               },
               'save': 1
            };

            // make the XHR call
            $.ajax(
            {
               'url': settings.server,
               'data':
               {
                  "command": JSON.stringify(data)
               },
               'dataType': 'jsonp',
               'timeout': 10000,
               'error': function(){},
               'success': function(){}
            });
         }


         /**
          * retrieves data from the server ;)
          * note: does not authenticate first. Assumes user is logged in.
          * @param callback the function to call when the data has been retrieved. This callback has 1 parameter, the data retrieved.
          */
         function getData(callback)
         {
            var data =
            {
               'authorize':
               {
                  'username': settings.credentials.username,
                  'password': (settings.credentials.authstring != "" ? MD5(MD5(settings.credentials.password) + settings.credentials.authstring) : "" )
               },
            'capabilities': {}
            };
            $.ajax(
            {
               'url': settings.server,
               'data':
               {
                  "command": JSON.stringify(data)
               },
               'dataType': 'jsonp',
               'timeout': 10000,
               'error': function(){},
               'success': function(d)
               {

                  var ret = $.extend(true,
                  {
                     "streams": {},
                     "capabilities": {},
                     "statistics": {}
                  }, d);
                  
                  //IE breaks if the console isn't opened, so keep commented when committing
                  //console.log('[651] RECV', ret);

                  if(callback)
                  {
                     callback(ret);
                  }
               }
            });
         }


         /**
          * retrieved the status and number of viewers from all streams
          * @param callback function that is called when the data is collected. Has one parameter, the data retrieved
          */
         function getStreamsData(callback)
         {

            getData(function(data)
            {
               var streams = {};   // streamID: [status, numViewers];
               var cnt = 0;

               for(var stream in data.streams)
               {
                  streams[stream] = [data.streams[stream].online, 0, data.streams[stream].error];
                  cnt++;
               }

               if(cnt === 0)
               {
                  return;   // if there are no streams, don't collect data and just return
               }

               for(stream in data.statistics)
               {
                  if(data.statistics[stream].curr)
                  {
                     for(var viewer in data.statistics[stream].curr)
                     {
                        streams[stream][1]++;
                     }
                  }
               }

               callback(streams);
            });
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

            if(results != null)
            {
               retobj.protocol = results[1];
               retobj.host = results[2];
               retobj.port = results[3];
            }

            return retobj;
         }

         /**
          * go figure.
          * @return true if there is a HTTP connector... and false if there isn't.
          */
         function isThereAHTTPConnector()
         {
            var i,
                len = (settings.settings.config.protocols ? settings.settings.config.protocols.length : 0);

            for(i = 0; i < len; i++)
            {
               if(settings.settings.config.protocols[i].connector == 'HTTP')
               {
                  return true;
               }
            }

            return false;
         }


         /**
          * retrieve port of the http connector
          * @return the port number
          */
         function getHTTPControllerPort()
         {
            var i,
                len = (settings.settings.config.protocols ? settings.settings.config.protocols.length : 0);

            for(i = 0; i < len; i++)
            {
               if(settings.settings.config.protocols[i].connector == 'HTTP')
               {
                  if (settings.settings.config.protocols[i].port == 0) {
                     return 8080;
                  }
                  else {
                     return settings.settings.config.protocols[i].port;
                  }
               }
            }

            return 0;
         }



         /**
          * retrieves the stream status (online and total number of streams) and viewer info (total number of viewers).
          * @param callback function that is called when data is retrieved. Has one parameter, the retrieved data.
          */
         function getStatData(callback)
         {
            getData(function(data)
            {
               var svr, viewer, ret,
                   numstr = 0,
                   numvwr = 0,
                   numtotstr = 0;

               for(svr in data.statistics)
               {
                  if(data.statistics[svr].curr)
                  {
                     for(viewer in data.statistics[svr].curr)
                     {
                        numvwr++;
                     }
                  }
               }

               for(svr in data.streams)
               {
                  numtotstr++;

                  if(data.streams[svr].online && data.streams[svr].online != 0 )
                  {
                     numstr++;
                  }
               }

               ret = {streams: [numstr, numtotstr], viewers: numvwr};
               callback(ret);
            });
         }


         /**
          * Connect to the server and retrieve the data
          * @param callback the function to call when connected. Has one parameter, an optional error string.
          */
         function loadSettings(callback)
         {
            // display 'loading, please wait' while retrieving data
            $('body').append( $('<div>').attr('id', 'shield').text('Loading, please wait...') );

            var errorstr = '',
                data = $.extend(settings.settings,
                {
                   'authorize':
                   {
                      'username': settings.credentials.username,
                      'password': (settings.credentials.authstring != "" ? MD5(MD5(settings.credentials.password) + settings.credentials.authstring) : "" )
                   }
                });

            delete data.log;   // don't send the logs back to the server
            delete data.statistics;   // same goes for the stats
            
            //IE breaks if the console isn't opened, so keep commented when committing
            //console.log('[763] SEND', data);

            $.ajax(
            {
               'url': settings.server,
               'data':
               {
                  "command": JSON.stringify(data)
               },
               'dataType': 'jsonp',

               'timeout': 5000,

               'error': function(jqXHR,textStatus,errorThrown)
               {
                  showTab('disconnect');
                  $('#shield').remove();   // remove loading display
                  alert('O dear! An error occurred while attempting to communicatie with the MistServer.\n\n'+textStatus+'\n'+errorThrown);
               },
               'success': function(d)
               {
                  $('#shield').remove();   // remove loading display

                  //IE breaks if the console isn't opened, so keep commented when committing
                  //console.log('[785] RECV', d);

                  if(d && d['authorize'] && d['authorize']['challenge'])
                  {
                     if (settings.credentials.authstring != d['authorize']['challenge'])
                     {
                        settings.credentials.authstring = d['authorize']['challenge'];
                        loadSettings(callback);
                        return;
                     }else{
                        errorstr = 'wrong credentials';
                     }
                  }else{
                     settings.settings = $.extend(true, {
                        "config":
                        {
                           "host": "",
                           "limits": [],
                           "name": "",
                           "protocols": [],
                           "status": "",
                           "version": ""
                        },
                        "streams": {},
                        "capabilities": {},
                        "log": {},
                        "statistics": {}
                     }, d)
                     if (settings.settings.LTS == 1) {
                        $('.LTSonly').show();
                     } else {
                        $('.LTSonly').hide();
                     }
                  }
                  if(callback)
                  {
                     callback(errorstr);
                  }
               }
            });
         }


         /**
          * Sets the page's header text (loging in, connected, disconnected), title and pretty colors (!)
          * @param state the state of the header. Possible are 'logingin', 'disconnected' or 'connected'.
          */
         function setHeaderState(state)
         {
            var text, cname, title;

            switch(state)
            {
               case 'logingin':        text = 'connecting...';    cname = 'loggingin';       title = 'connecting to ' + settings.server;     break;
               case 'disconnected':    text = 'disconnected';     cname = 'disconnected';    title = 'disconnected';                         break;
               case 'connected':       text = 'connected';        cname = 'connected';       title = 'connected to ' + settings.server;      break;
            }

            document.title = 'Mistserver Manager - ' + title;

            $('#header-connection').attr('class', cname);
            $('#header-connection').text(text);
            $('#header-host').text(settings.server.replace('HTTP://', ''));
         }



         /**
          * Formats the status property to a string (with colors!)
          * @param status, the status property of a stream
          */
         function formatStatus(status,text)
         {
            if(status == undefined)
            {
               return "<span>Unknown, checking...</span>";
            }
            if(text == undefined)
            {
              switch(status)
              {
                 case -1:   return "<span>Unknown, checking...</span>";         break;
                 case 0:    return "<span class='red'>Unavailable</span>";      break;
                 case 1:    return "<span class='green'>Active</span>";         break;
                 case 2:    return "<span class='orange'>Inactive</span>";      break;
                 default:   return "<span>"+status+"</span>";                   break;
              }
            }
            else
            {
              switch(status)
              {
                 case -1:   return "<span>Unknown, checking...</span>";         break;
                 case 0:    return "<span class='red'>"+text+"</span>";         break;
                 case 1:    return "<span class='green'>"+text+"</span>";       break;
                 case 2:    return "<span class='orange'>"+text+"</span>";      break;
                 default:   return "<span>"+text+"</span>";                     break;
              }
            }
         }
/**
 * Build a HTML limit row
 * @param l the limit data
 * @return a (jQuery'd) HTML Element
 */
function BuildLimitRow(l)
{
   l['type'] = l['type'] || 'soft';
   l['name'] = l['name'] || 'kbps_max';
   l['val']  = l['val']  || 0;

   var i, j, lv, db,
       output = $("<tr>").addClass("limits_row"),
       selects = [$("<select class=\"limits_type\">"), $("<select class=\"limits_name\">")],
       options =
       [
          [
             ['soft', 'Softlimit', 'Allow this limit to be passed. (Usefull for alerts)'],
             ['hard', 'Hardlimit','Do not allow this limit to be passed.']
          ],
          [
             ['kbps_max', 'Current bandwidth', 'In bytes/s. Refuses new connections after current bandwidth limit is reached.'],
             ['users', 'Concurrent users','Maximum concurrent users.'],
             ['geo', 'Geolimited', 'Either a blacklist or whitelist containing country codes.'],
             ['host', 'Hostlimited', 'Either a blacklist or whitelist containing hosts seperated by spaces.']
          ],
          ['type', 'name']
       ];
       /*
       Limits that are currently not in use but may return later:
             ['kb_total', 'Total bandwidth','Total bandwidth in bytes.'],
             ['duration', 'Duration', 'Maximum duration a user may be connected in seconds.'],
             ['str_kbps_min', 'Minimum bitrate','Minimum bitrate in bytes/s.'],
             ['str_kbps_max', 'Maximum bitrate','Maximum bitrate in bytes/s.']
       */

   for(i = 0; i < 2; i++)
   {

      for(j = 0; j < options[i].length; j++)
      {
         selects[i].append(
            $('<option>').val(options[i][j][0]).text(options[i][j][1]).data('desc',options[i][j][2])
         );
      }
      selects[i].val(l[options[2][i]]);
      output.append($('<td>').html(selects[i]));
   }

   lv = $('<td>').addClass('limit_input_container');
   BuildLimitRowInput(lv,l);
   
   appliesto = $('<td>');
   var appliesselect = $('<select>').attr('class', 'new-limit-appliesto').append( $('<option>').attr('value', 'server').text('Server') );
   for(var strm in settings.settings.streams) {
    appliesselect.append(
      $('<option>').attr('value', strm).text(settings.settings.streams[strm].name)
    );
   }
   if (l.appliesto) {
      appliesselect.val(l.appliesto);
   }
   appliesto.append(appliesselect);
     
   db = $('<td>').html(
     $('<button>').addClass('limit_delete_button').text('Delete').click(function(){$(this).parent().parent().remove();})
   );
   
   output.append(lv).append(appliesto).append(db);
   
   return output;
}



/**
 * Build a HTML limit row input field
 * @param target where the input field should be inserted
 * @param l the limit data
 */
 function BuildLimitRowInput(target,l) {
  if (!l.val) { l.val=0; }
  console.log(l);
  switch (l.name) {
    case 'geo':
      var countrylist = [['AF','Afghanistan'],['AX','&Aring;land Islands'],['AL','Albania'],['DZ','Algeria'],['AS','American Samoa'],['AD','Andorra'],['AO','Angola'],['AI','Anguilla'],['AQ','Antarctica'],['AG','Antigua and Barbuda'],['AR','Argentina'],['AM','Armenia'],['AW','Aruba'],['AU','Australia'],['AT','Austria'],['AZ','Azerbaijan'],['BS','Bahamas'],['BH','Bahrain'],['BD','Bangladesh'],['BB','Barbados'],['BY','Belarus'],['BE','Belgium'],['BZ','Belize'],['BJ','Benin'],['BM','Bermuda'],['BT','Bhutan'],['BO','Bolivia, Plurinational State of'],['BQ','Bonaire, Sint Eustatius and Saba'],['BA','Bosnia and Herzegovina'],['BW','Botswana'],['BV','Bouvet Island'],['BR','Brazil'],['IO','British Indian Ocean Territory'],['BN','Brunei Darussalam'],['BG','Bulgaria'],['BF','Burkina Faso'],['BI','Burundi'],['KH','Cambodia'],['CM','Cameroon'],['CA','Canada'],['CV','Cape Verde'],['KY','Cayman Islands'],['CF','Central African Republic'],['TD','Chad'],['CL','Chile'],['CN','China'],['CX','Christmas Island'],['CC','Cocos (Keeling) Islands'],['CO','Colombia'],['KM','Comoros'],['CG','Congo'],['CD','Congo, the Democratic Republic of the'],['CK','Cook Islands'],['CR','Costa Rica'],['CI','C&ocirc;te d\'Ivoire'],['HR','Croatia'],['CU','Cuba'],['CW','Cura&ccedil;ao'],['CY','Cyprus'],['CZ','Czech Republic'],['DK','Denmark'],['DJ','Djibouti'],['DM','Dominica'],['DO','Dominican Republic'],['EC','Ecuador'],['EG','Egypt'],['SV','El Salvador'],['GQ','Equatorial Guinea'],['ER','Eritrea'],['EE','Estonia'],['ET','Ethiopia'],['FK','Falkland Islands (Malvinas)'],['FO','Faroe Islands'],['FJ','Fiji'],['FI','Finland'],['FR','France'],['GF','French Guiana'],['PF','French Polynesia'],['TF','French Southern Territories'],['GA','Gabon'],['GM','Gambia'],['GE','Georgia'],['DE','Germany'],['GH','Ghana'],['GI','Gibraltar'],['GR','Greece'],['GL','Greenland'],['GD','Grenada'],['GP','Guadeloupe'],['GU','Guam'],['GT','Guatemala'],['GG','Guernsey'],['GN','Guinea'],['GW','Guinea-Bissau'],['GY','Guyana'],['HT','Haiti'],['HM','Heard Island and McDonald Islands'],['VA','Holy See (Vatican City State)'],['HN','Honduras'],['HK','Hong Kong'],['HU','Hungary'],['IS','Iceland'],['IN','India'],['ID','Indonesia'],['IR','Iran, Islamic Republic of'],['IQ','Iraq'],['IE','Ireland'],['IM','Isle of Man'],['IL','Israel'],['IT','Italy'],['JM','Jamaica'],['JP','Japan'],['JE','Jersey'],['JO','Jordan'],['KZ','Kazakhstan'],['KE','Kenya'],['KI','Kiribati'],['KP','Korea, Democratic People\'s Republic of'],['KR','Korea, Republic of'],['KW','Kuwait'],['KG','Kyrgyzstan'],['LA','Lao People\'s Democratic Republic'],['LV','Latvia'],['LB','Lebanon'],['LS','Lesotho'],['LR','Liberia'],['LY','Libya'],['LI','Liechtenstein'],['LT','Lithuania'],['LU','Luxembourg'],['MO','Macao'],['MK','Macedonia, the former Yugoslav Republic of'],['MG','Madagascar'],['MW','Malawi'],['MY','Malaysia'],['MV','Maldives'],['ML','Mali'],['MT','Malta'],['MH','Marshall Islands'],['MQ','Martinique'],['MR','Mauritania'],['MU','Mauritius'],['YT','Mayotte'],['MX','Mexico'],['FM','Micronesia, Federated States of'],['MD','Moldova, Republic of'],['MC','Monaco'],['MN','Mongolia'],['ME','Montenegro'],['MS','Montserrat'],['MA','Morocco'],['MZ','Mozambique'],['MM','Myanmar'],['NA','Namibia'],['NR','Nauru'],['NP','Nepal'],['NL','Netherlands'],['NC','New Caledonia'],['NZ','New Zealand'],['NI','Nicaragua'],['NE','Niger'],['NG','Nigeria'],['NU','Niue'],['NF','Norfolk Island'],['MP','Northern Mariana Islands'],['NO','Norway'],['OM','Oman'],['PK','Pakistan'],['PW','Palau'],['PS','Palestine, State of'],['PA','Panama'],['PG','Papua New Guinea'],['PY','Paraguay'],['PE','Peru'],['PH','Philippines'],['PN','Pitcairn'],['PL','Poland'],['PT','Portugal'],['PR','Puerto Rico'],['QA','Qatar'],['RE','R&eacute;union'],['RO','Romania'],['RU','Russian Federation'],['RW','Rwanda'],['BL','Saint Barth&eacute;lemy'],['SH','Saint Helena, Ascension and Tristan da Cunha'],['KN','Saint Kitts and Nevis'],['LC','Saint Lucia'],['MF','Saint Martin (French part)'],['PM','Saint Pierre and Miquelon'],['VC','Saint Vincent and the Grenadines'],['WS','Samoa'],['SM','San Marino'],['ST','Sao Tome and Principe'],['SA','Saudi Arabia'],['SN','Senegal'],['RS','Serbia'],['SC','Seychelles'],['SL','Sierra Leone'],['SG','Singapore'],['SX','Sint Maarten (Dutch part)'],['SK','Slovakia'],['SI','Slovenia'],['SB','Solomon Islands'],['SO','Somalia'],['ZA','South Africa'],['GS','South Georgia and the South Sandwich Islands'],['SS','South Sudan'],['ES','Spain'],['LK','Sri Lanka'],['SD','Sudan'],['SR','Suriname'],['SJ','Svalbard and Jan Mayen'],['SZ','Swaziland'],['SE','Sweden'],['CH','Switzerland'],['SY','Syrian Arab Republic'],['TW','Taiwan, Province of China'],['TJ','Tajikistan'],['TZ','Tanzania, United Republic of'],['TH','Thailand'],['TL','Timor-Leste'],['TG','Togo'],['TK','Tokelau'],['TO','Tonga'],['TT','Trinidad and Tobago'],['TN','Tunisia'],['TR','Turkey'],['TM','Turkmenistan'],['TC','Turks and Caicos Islands'],['TV','Tuvalu'],['UG','Uganda'],['UA','Ukraine'],['AE','United Arab Emirates'],['GB','United Kingdom'],['US','United States'],['UM','United States Minor Outlying Islands'],['UY','Uruguay'],['UZ','Uzbekistan'],['VU','Vanuatu'],['VE','Venezuela, Bolivarian Republic of'],['VN','Viet Nam'],['VG','Virgin Islands, British'],['VI','Virgin Islands, U.S.'],['WF','Wallis and Futuna'],['EH','Western Sahara'],['YE','Yemen'],['ZM','Zambia'],['ZW','Zimbabwe']];
      var entrylist = l.val.toString();
      
      //build the template country selectbox
      var geoselect = $('<select>').addClass('limit_listentry');
      geoselect.append(
        $('<option>').val('').text('[Select a country]')
      );
      for (i in countrylist) {
        geoselect.append(
          $('<option>').val(countrylist[i][0]).html(countrylist[i][1])
        );
      }
      
      //build the blacklist or whitelist selectbox
      var selectbox = $('<select>').addClass('limit_listtype');
      selectbox.append($('<option>').val('-').text('Blacklist'));
      selectbox.append($('<option>').val('+').text('Whitelist'));
      if (entrylist.charAt(0) == '+') {
        selectbox.val('+');
      }
      else {
        selectbox.val('-');
      }
      var inputfields = $('<table>').addClass('limit_inputfields').append(
        $('<tbody>').append(
          $('<tr>').append(
            $('<td>').append(
              selectbox
            )
          )
        )
      );
      entrylist = entrylist.substring(1).split(' ');
      
      var firstfieldused = false;
      //insert selectboxes with currently set geolimits
      for (i in entrylist) {
        if (entrylist[i] == "") { continue; }
        //make a new country selectbox based on the template
        var newgeoselect = geoselect.clone();
        newgeoselect.val(entrylist[i]);

        if (firstfieldused) {
          inputfields.children('tbody').append(
            $('<tr>').append(
              $('<td>')
            ).append(
              $('<td>').html(newgeoselect)
            )
          );
        }
        else {
          inputfields.children('tbody').children('tr').append(
            $('<td>').html(newgeoselect)
          );
          firstfieldused = true;
        }
      }
      if (!firstfieldused) {
        inputfields.children('tbody').children('tr').append(
          $('<td>').html(geoselect.clone())
        );
      }
      
      //add a button to insert country selectboxes
      var addbutton = $('<button>').text('Add country selectbox').click(function(){
        $(this).parent('td').parent('tr').before(
          $('<tr>').append(
            $('<td>')
          ).append(
            $('<td>').html(geoselect.clone())
          )
        )
      });
      
      inputfields.children('tbody').append(
        $('<tr>').append(
          $('<td>')
        ).append(
          $('<td>').html(addbutton)
        )
      );
      
      target.html(inputfields);
    break;
    case 'host':
    case 'time':
      var entrylist = l.val.toString();
      
      //build the blacklist or whitelist selectbox
      var selectbox = $('<select>').addClass('limit_listtype');
      selectbox.append($('<option>').val('-').text('Blacklist'));
      selectbox.append($('<option>').val('+').text('Whitelist'));
      if (entrylist.charAt(0) == '+') {
        selectbox.val('+');
      }
      else {
        selectbox.val('-');
      }
      target.html(
        $('<span>').addClass('limit_inputfields').append(
          selectbox
        )
      );
      entrylist = entrylist.substring(1);
      
      //add inputfield
      target.children('.limit_inputfields').append(
        $('<input>').attr('type','text').addClass('limit_listentry').val(entrylist)
      );
    break;
    default:
      target.html($("<input type=\"text\" class=\"limits_val\">").val(l["val"]));
    break;
  }
}

/**
 * At the logs page, refreshes data for the logs
 */
 function getLogsdata(){
   getData(function(data){
     settings.settings.log = data.log;
     
     $('#page table').remove();
     $('#page').append(buildLogsTable());
   });
}
/**
 * At the logs page, builds a table to display the logs
 */
function buildLogsTable(){
   $table = $('<table>');
   $table.html("<thead><th>Date<span class='theadinfo'>(MM/DD/YYYY)</span></th><th>Type</th><th>Message</th></thead>");
   $tbody = $('<tbody>');
   
   if(!settings.settings.log)
   {
      return;  // no logs, so just bail
   }
   var i, cur, $tr,
       logs = settings.settings.log,
       len = logs.length;
       
   if(len >= 2 && settings.settings.log[0][0] < settings.settings.log[len - 1][0])
   {
      logs.reverse();
   }
   
   $tbody.html('');
   
   for(i = 0; i < len; i++)
   {
      cur = settings.settings.log[i];
      
      $tr = $('<tr>').append(
         $('<td>').text(formatDate(cur[0]))
      ).append(
         $('<td>').text(cur[1])
      ).append(
         $('<td>').text(cur[2])
      );
      
      $tbody.append($tr);
   }
   
   $table.append($tbody);
   return $table;
}

/**
 * Tooltip creator - creates a tooltip near the cursor
 @params: - position: the object returned by the hover or click event
          - appendto: the jquery element to which the tooltip should be appended
          - contents: the content of the tooltip
 */
function showTooltip(position,appendto,contents,debug) {
  if (position == null) { position = { pageX: 0, pageY:0 } }
  if (appendto == null) { appendto = $('body'); }
  if (contents == null) { contents = 'Empty'; }
  var css = {};
  if (position.pageX > window.innerWidth / 2) {
    css.right = window.innerWidth - position.pageX + 10;
  }
  else {
    css.left = position.pageX - appendto.offset().left + 10;
  }
  if (position.pageY > window.innerHeight / 2) {
    css.bottom = window.innerHeight - position.pageY + 10;
  }
  else {
    css.top = position.pageY - appendto.offset().top + 10;
  }
  $("<div>").attr('id','tooltip').html(
    contents
  ).css(css).appendTo(appendto).fadeIn("fast");
  if (debug) { console.log('Tooltip.. position:',position,'appendto:',appendto,'contents:',contents,'css:',css); }
}
function removeTooltip() {
   $('#tooltip').remove();
}