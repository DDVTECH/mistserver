
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
                  return settings.settings.config.protocols[i].port;
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
                     if (settings.settings.LTS != 1) {
                        $('.LTSonly').remove();
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

