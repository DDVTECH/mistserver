
			/**
			 * Local settings page
			 * DDVTECH
			 * for more information, see http://mistserver.org/index.php?title=Stand-alone_Configuration_Page
			 */


			/**
			 * Store a local copy of the data send by the server.
			 * server is the URL, credentials hold the username, password and authstring and settings is the local copy.
			 */
         var settings =
         {
            server: '',
            credentials:
            {
               username: "",
               password: "",
               authstring: ""
            },
            settings: {}
         };


			/**
			 *  Table for long/short limit names
			 */
         var ltypes =
         [
            ['kb_total', 'Total bandwidth'],
            ['kbps_max', 'Current bandwidth'],
            ['users', 'Concurrent users'],
            ['streams', 'Cocurrent streams'],
            ['geo', 'Geolimited'],
            ['host', 'Hostlimited'],
            ['time', 'Timelimited'],
            ['duration', 'Duration'],
            ['str_kbps_min', 'Minimum bitrate'],
            ['str_kbps_max', 'Maximum bitrate']
         ];


			/**
			 *  When the page loads, fix the menu and show the correct tab
			 */
         $(document).ready(function()
         {
            $('#nav').children().each(function()
            {
               $(this).click(function()
               {
                  // remove currently selected' class
                  $('#nav').children().each(function()
                  {
                     $(this).attr('class', '');
                  });

                  // select this one
                  $(this).attr('class', 'selected');

                  // show correct tab
                  showTab($(this).text());
               });
            });

            // show login 'tab' and hide menu
            showTab('login');
            $('#nav').css('visibility', 'hidden');
         });



         // used on the overview and streams page. cleared when switching to another 'tab'.
         var sinterval = null;

         // what kind of streams should be displayed? Format is [recorded, live];
         var streamsdisplay = [true, true];



			/**
			 * Display a certain page. It contains a (giant) switch-statement, that builds a page depending on the tab requested
			 * @param name the name of the tab
			 * @param streamname only used when editing streams, the name of the edited (or new) stream. Also used with the 'embed' tab
			 */
         function showTab(name, streamname)
         {
            // clear page and refresh interval
            $('#page').html('');
            clearInterval(sinterval);

            switch(name)
            {
               case 'login':

                  var host = $('<input>').attr('type', 'text').attr('placeholder', 'HTTP://' + (location.host == '' ? 'localhost:4242' : location.host) + '/api');
                  var user = $('<input>').attr('type', 'text').attr('placeholder', 'USERNAME');
                  var pass = $('<input>').attr('type', 'password').attr('placeholder', 'PASSWORD');
                  var conn = $('<button>').click(function()
                  {
                     // get login info
                     settings.credentials.username = user.val();
                     settings.credentials.password = pass.val();
                     settings.server = host.val() == '' ? host.attr('placeholder') : host.val().toLowerCase();

                     // save username, URL in address
                     location.hash = user.val() + '@' + settings.server;

                     // try to login
                     setHeaderState('logingin');

                     loadSettings(function(errorstr)
                     {
                        if(errorstr == '')
                        {
                           setHeaderState('connected');

                           $('#nav').css('visibility', 'visible');

                           showTab('overview');

                           // show overview as current tab - this only happens when logging out and then in
                           $('#nav').children().each(function()
                           {
                              if($(this).text() != 'overview')
                              {
                                 $(this).attr('class', '');
                              }else{
                                 $(this).attr('class', 'selected');
                              }
                           });

                        }else{
                           setHeaderState('disconnected');
                           $('#header-host').text('');
                        }
                     });
                  }).text('login');

                  $('#page').append(
                     $('<div>').attr('id', 'login').append(host).append(user).append(pass).append(conn)
                  );

                  // if we 'enter' in host, user or pass we should try to login.
                  function hand(e)
                  {
                     if(e.keyCode == 13)
                     {
                        conn.trigger('click');  // conn = login button
                     }
                  }

                  host.keypress(hand);
                  user.keypress(hand);
                  pass.keypress(hand);

                  // retrieve address hash from URL
                  var adr = location.hash.replace('#', '').split('@');

                  if(adr.length == 2)
                  {
                     // put it in the page
                     host.val(adr[1]);
                     user.val(adr[0]);
                  }

                  break;




               case 'overview':

                  $('#page').append(
                     $('<div>').attr('id', 'editserver').append(
                        $('<label>').attr('for', 'config-host').text('host').append(
                           $('<input>').attr('type', 'text').attr('placeholder', 'HOST').attr('id', 'config-host').attr('value', settings.settings.config.host)
                        )
                     ).append(
                        $('<label>').attr('for', 'config-name').text('name').append(
                           $('<input>').attr('type', 'text').attr('placeholder', 'NAME').attr('id', 'config-name').attr('value', settings.settings.config.name)
                        )
                     ).append(
                        $('<label>').text('version').append(
                           $('<span>').text(settings.settings.config.version)
                        )
                     ).append(
                        $('<label>').text('time').append(
                           $('<span>').text( formatDate(settings.settings.config.time) )
                        )
                     ).append(
                        $('<label>').text('Streams').append(
                           $('<span>').attr('id', 'cur_streams_online').text('retrieving data...')
                        )
                     ).append(
                        $('<label>').text('Viewers').append(
                           $('<span>').attr('id', 'cur_num_viewers').text('retrieving data...')
                        )
                     )
                  );

                  function showStats()
                  {
                     getStatData(function(data)
                     {
                        $('#cur_streams_online').html('').text(data.streams[0] + ' of ' + data.streams[1] + ' online');
                        $('#cur_num_viewers').html('').text(data.viewers);
                     });
                  }

						// refresh the stream status + viewers
                  sinterval = setInterval(function()
                  {
                     showStats();
                  }, 10000);

                  showStats();


                  $('#editserver').append(
                     $('<button>').attr('class', 'floatright').click(function()
                     {
                        var host = $('#config-host').val();
                        var name = $('#config-name').val();

                        settings.settings.config.host = host;
                        settings.settings.config.name = name;

                        loadSettings(function()
                        {
                           showTab('overview');
                        });
                     }).text( 'save' )
                  );


                  var forcesave = $('<div>').attr('id', 'forcesave');

                  forcesave.append(
                     $('<p>').text('Click the button below to force an immediate settings save. This differs from a regular save to memory and file save on exit by saving directly to file while operating. This may slow server processes for a short period of time.')
                  ).append(
                     $('<button>').click(function()
                     {
                       if(confirmDelete('Are you sure you want to force a JSON save?') == true)
                       {
                          forceJSONSave();
                       }
                     }).text( 'force save to JSON file' )
                  );

                  $('#page').append(forcesave);

                  break;




               case 'protocols':

                  $table = $('<table>');
                  $table.html("<thead><th>Protocol</th><th>Port</th><th>Interface</th><th></th></thead>");
                  $tbody = $('<tbody>');

                  var tr, i, protocol,
                      len = (settings.settings.config.protocols ? settings.settings.config.protocols.length : 0);

                  $tbody.html('');

                  for(i = 0; i < len; i++)
                  {
                     protocol = settings.settings.config.protocols[i];  // local copy

                     tr = $('<tr>').attr('id', 'protocol-' + i);

                     tr.append( $('<td>').text( protocol.connector ) );
                     tr.append( $('<td>').text( protocol.port ) );

                     tr.append( $('<td>').text( protocol['interface'] ) );  // interface is a reserved JS keyword

                     tr.append( $('<td>').attr('class', 'center').append( $('<button>').click(function()
		                 {
		                    if(confirmDelete('Are you sure you want to delete this protocol?') == true)
		                    {
		                       var id = $(this).parent().parent().attr('id').replace('protocol-', '');
		                       settings.settings.config.protocols.splice(id, 1);
		                       $(this).parent().parent().remove();
		                       loadSettings();
		                    }
		                 }).text('delete') ) );

                     $tbody.append(tr);
                  }


                  // add new protocol!
                  $nprot = $('<tr>').attr('class', 'outsidetable');
                  // protocol select
                  $pname = $('<select>').attr('id', 'new-protocol-name');
                  $pname.append( $('<option>').attr('value', 'HTTP').text('HTTP') );
                  $pname.append( $('<option>').attr('value', 'HTTPDynamic').text('HTTPDynamic') );
                  $pname.append( $('<option>').attr('value', 'HTTPProgressive').text('HTTPProgressive') );
                  $pname.append( $('<option>').attr('value', 'HTTPSmooth').text('HTTPSmooth') );
                  $pname.append( $('<option>').attr('value', 'RTMP').text('RTMP') );

                  $nprot.append( $('<td>').append($pname) );
                  // the port value
                  $nprot.append( $('<td>').append( $('<input>').attr('type', 'number').attr('id', 'new-protocol-val') ) );

                  // interface
                  $nprot.append( $('<td>').append( $('<input>').attr('type', 'text').attr('id', 'new-protocol-interface') ) );

                  $nprot.append(
                     $('<td>').attr('class', 'center').append(
                        $('<button>').click(function()
                        {
                           if($('#new-protocol-val').val() == '')
                           {
                              $('#new-protocol-val').focus();
                              return;
                           }

                           if(!settings.settings.config.protocols)
                           {
                              settings.settings.config.protocols = [];
                           }

                           var nobj =
                           {
                              connector: $('#new-protocol-name :selected').val(),
                              port: Math.abs($('#new-protocol-val').val())
                           };

                           nobj['interface'] = $('#new-protocol-interface').val();

                           settings.settings.config.protocols.push(nobj);

                           loadSettings(function()
                           {
                              showTab('protocols');
                           });

                        }).text('add new')
                     )
                  );

                  $tbody.append($nprot);
                  $table.append($tbody);
                  $('#page').append($table);

                  break;







               case 'streams':

                  // the filter element containr
                  $div = $('<div>').attr('id', 'streams-filter');

						// filters the table. uses the streamsdisplay
                  function filterTable()
                  {
                     $('#streams-list-tbody').children().each(function(k, v)
                     {
                        var type = $($(v).children()[0]).text().toLowerCase();

                        $(v).show();

                        if(type == 'recorded' && streamsdisplay[0] == false)
                        {
                           $(v).hide();
                        }

                        if(type == 'live' && streamsdisplay[1] == false)
                        {
                           $(v).hide();
                        }
                     });
                  }

                  function filterOn(event, elem)
                  {
                     if(event.target.id == '')
                     {
                        return;  // label click goes bubbles on checkbox, so ignore it
                     }

                     var what = $(elem).text();

                     if(what == 'recorded')
                     {
                        streamsdisplay[0] = !streamsdisplay[0];
                        $('#stream-filter-recorded').attr('checked', streamsdisplay[0]);
                     }else{
                        streamsdisplay[1] = !streamsdisplay[1];
                        $('#stream-filter-live').attr('checked', streamsdisplay[1]);
                     }

                     filterTable();
                  }

                  $div.append(
                     $('<label>').attr('for', 'stream-filter-recorded').text('recorded').append(
                        $('<input>').attr('type', 'checkbox').attr('id', 'stream-filter-recorded').attr('checked', streamsdisplay[0])
                     ).click(function(event)
                     {
                        filterOn(event, this);
                     })
                  );
                  $div.append(
                     $('<label>').attr('for', 'stream-filter-live').text('live').append(
                        $('<input>').attr('type', 'checkbox').attr('id', 'stream-filter-live').attr('checked', streamsdisplay[1])
                     ).click(function(event)
                     {
                        filterOn(event, this);
                     })
                  );

                  $('#page').append($div);

						// refresh every streams' data (status and viewer count)
						function refreshStreams()
						{
							getStreamsData(function(streams)
							{
								for(stream in streams)
								{
									if( $('stream-' + stream) )
									{
										var row = $('#stream-' + stream);
										var status = streams[stream][0];

										$(row.children()[3]).html( formatStatus(status) );

										$(row.children()[4]).text(streams[stream][1]);
									}
								}
							});
						};

						sinterval = setInterval(function()
						{
							refreshStreams();
						}, 10000);

						refreshStreams();

                  $table = $('<table>');
                  $table.html("<thead><th>Type</th><th>Embed</th><th>Name</th><th>Status</th><th>Viewers</th><th>Edit</th></thead>");
                  $tbody = $('<tbody>');

                  var stream, cstr, $tr;

                  $tbody.html('').attr('id', 'streams-list-tbody');

                  for(stream in settings.settings.streams)
                  {
                     var cstr = settings.settings.streams[stream];

                     $tr = $('<tr>').attr('id', 'stream-' + stream);

                     $tr.append( $('<td>').text( TypeofResource( cstr.channel.URL ) ) );

                     $tr.append( $('<td>').append( $('<button>').text('embed').click(function()
                     {
                        var sname = $(this).parent().parent().attr('id').replace('stream-', '');
                        showTab('embed', sname);
                     }) ) );   // end function, end click(), end append(), end append(). Huzzah jQuery.

                     $tr.append( $('<td>').text(cstr.name) );

							$tr.append( $('<td>').html( formatStatus( cstr.online ) ) );

                     var cviewers = 0;

                     if(settings.settings.statistics && settings.settings.statistics[stream])
                     {
                        if(settings.settings.statistics[stream] && settings.settings.statistics[stream].curr)
                        {
                           for(viewer in settings.settings.statistics[stream].curr)
                           {
                              cviewers++;
                           }
                        }
                     }else{
                        cviewers = 0;
                     }

                     $tr.append( $('<td>').text( cviewers ) );

                     $tr.append( $('<td>').append( $('<button>').text('edit').click(function()
                     {
                        var sname = $(this).parent().parent().attr('id').replace('stream-', '');

                        showTab('editstream', sname);
                     }) ) );   // end function, end click, end append, end append.

                     $tbody.append($tr);
                  }

                  $table.append($tbody);
                  $('#page').append($table);

                  // on page load, also filter with the (users' defined) stream filter
                  filterTable();

                  $('#page').append(
                     $('<button>').attr('class', 'floatright').click(function()
                     {
                        showTab('editstream', 'new');
                     }).text('add new')
                  );

                  break;







               case 'editstream':

                  var sdata, title;

                  if(streamname == 'new')
                  {
                     sdata =
                     {
                        name: '',
                        channel:
                        {
                           URL: ''
                        },
                        limits: [],
                        preset:
                        {
                           cmd: ''
                        }
                     };
                     title = 'add new stream';
                  }else{
                     sdata = settings.settings.streams[streamname];
                     title = 'edit stream "' + sdata.name + '"';
                  }

                  $('#page').append( $('<p>').text(title) );

                  $('#page').append(
                     $('<div>').attr('id', 'editserver').append(
                        $('<label>').attr('for', 'stream-edit-name').text('name').append(
                           $('<input>').attr('type', 'text').attr('placeholder', 'NAME').attr('id', 'stream-edit-name').attr('value', sdata.name)
                        )
                     ).append(
                        $('<label>').attr('for', 'stream-edit-source').text('source').append(
                           $('<input>').attr('type', 'text').attr('placeholder', 'SOURCE').attr('id', 'stream-edit-source').attr('value', sdata.channel.URL).keyup(function()
									{
									   var text = $(this).val();

										if(text.charAt(0) == '/' || text.substr(0, 7) == 'push://')
										{
											$('#stream-edit-preset').val('');
											$('#stream-edit-preset').hide();
											$('#stream-edit-preset-label').hide();
										}else{
											$('#stream-edit-preset').show();
											$('#stream-edit-preset-label').show();
										}
									})
                        )
                     ).append(
                        $('<label>').attr('id', 'stream-edit-preset-label').attr('for', 'stream-edit-preset').text('preset').append(
                           $('<input>').attr('type', 'text').attr('placeholder', 'PRESET').attr('id', 'stream-edit-preset').attr('value', sdata.preset.cmd)
                        )
                     )
                  );

						// if the source is push or file, don't do a preset
					   var text = $('#stream-edit-source').val();

						if(text.charAt(0) == '/' || text.substr(0, 7) == 'push://')
						{
							$('#stream-edit-preset').hide();
							$('#stream-edit-preset-label').hide();
						}else{
							$('#stream-edit-preset').show();
							$('#stream-edit-preset-label').show();
						}


                  $('#editserver').append(
                     $('<button>').attr('class', 'floatright').click(function()
                     {
                        if(streamname == 'new')
                        {
                           showTab('streams');
                        }else{
                           if(confirmDelete('Are you sure you want to delete the stream "' + settings.settings.streams[streamname].name + '"?') == true)
                           {
                              delete settings.settings.streams[streamname];
                              loadSettings(function()
                              {
                                 showTab('streams');
                              });
                           }
                        }
                     }).text( streamname == 'new' ? 'cancel' : 'delete' )
                  );

                  $('#editserver').append(
                     $('<button>').attr('class', 'floatright').click(function()
                     {
                        var n = $('#stream-edit-name');
                        var s = $('#stream-edit-source');
                        var p = $('#stream-edit-preset');

                        if(n.val() == ''){ n.focus(); return; }
                        if(s.val() == ''){ s.focus(); return; }

								var newname = n.val().replace(/([^a-zA-Z0-9_])/g, '').toLowerCase();

                        sdata.name = newname;
                        sdata.channel.URL = s.val();
                        sdata.preset.cmd = p.val();

                        if(streamname == 'new')
                        {
                           streamname = newname;
                        }

                        if(!settings.settings.streams)
                        {
                           settings.settings.streams = {};
                        }

								delete settings.settings.streams[streamname];

                        settings.settings.streams[newname] = sdata;

                        loadSettings(function()
                        {
                           showTab('streams');
                        });
                           
                     }).text('save')
                  );

                  break;





               case 'embed':

						if(isThereAHTTPConnector())
						{
							var embedbase = 'http://' + parseURL(settings.server).host + ':' + getHTTPControllerPort() + '/';

							$('#page').append( $('<p>').attr('class', 'nocapitals').text('The info embed URL is "' + embedbase + 'info_' + streamname + '.js".') );
							$('#page').append( $('<p>').attr('class', 'nocapitals').text('The embed embed URL is "' + embedbase + 'embed_' + streamname + '.js".') );

							$('#page').append( $('<button>').text('preview').click(function()
							{
								showTab('preview', streamname);
							} ) );

						}else{
							$('#page').append( $('<p>').attr('class', 'nocapitals').text('Could\'t find a HTTP connector. Please add a HTTP connector on the "protocol" page.') );
						}

                  break;



               case 'preview':

						var embed = 'http://' + parseURL(settings.server).host + ':' + getHTTPControllerPort() + '/embed_' + streamname + '.js';

						$('#page').append( $('<div>').attr('id', 'previewcontainer') );

						// jQuery doesn't work -> use DOM magic
						var script = document.createElement('script');
						script.src = embed;
						document.getElementById('previewcontainer').appendChild( script );

                  break;





               case 'limits':
                  $table = $('<table>');
                  $table.html("<thead><th>Type</th><th>Hard/soft</th><th>Value</th><th>applies to</th><th>Action</th></thead>");
                  $tbody = $('<tbody>');

                  var i, tr, limit, stream, clims,
                      alllimits = settings.settings.config.limits;

                  for(stream in settings.settings.streams)
                  {
                     clims = settings.settings.streams[stream].limits;

                     $.each(clims, function(k, v)
                     {
                        this.appliesto = stream;
                        this.appliesi = k;
                     });

                     alllimits = alllimits.concat(clims);
                  }

                  len = alllimits.length;

                  // remove old items
                  $tbody.html('');

                  for(i = 0; i < len; i++)
                  {
                     tr = $('<tr>').attr('id', 'limits-' + i);
                     limit = alllimits[i];

                     tr.append( $('<td>').text( shortToLongLimit(limit.name) ) );
                     tr.append( $('<td>').text( limit.type ) );
                     tr.append( $('<td>').text( limit.val ) );


                     if(limit.appliesto)
                     {
                        tr.append( $('<td>').text( settings.settings.streams[limit.appliesto].name ).attr('id', 'limit-at-' + limit.appliesto + '-' + limit.appliesi) );
                     }else{
                        tr.append( $('<td>').text( 'server' ) );
                     }

                     delete limit.appliesto;
                     delete limit.appliesi;

                     tr.append( $('<td>').attr('class', 'center').append( $('<button>').click(function()
                                {
                                   if(confirmDelete('Are you sure you want to delete this limit?') == true)
                                   {
                                      var id = $(this).parent().parent().attr('id').replace('limits-', '');
                                      var at = $($(this).parent().parent().children()[3]).attr('id');

                                      if(at == undefined)
                                      {
                                         settings.settings.config.limits.splice(id, 1);
                                      }else{
                                         var data = at.replace('limit-at-', '').split('-');
                                         var loc = data.pop();
                                         data = data.join('-');

                                         settings.settings.streams[data].limits.splice(loc, 1);
                                      }

                                      $(this).parent().parent().remove();

                                      loadSettings();
                                   }
                                }).text('delete') ) );

                     $tbody.append(tr);
                  }

                  // add new limit
                  $nltr = $('<tr>').attr('class', 'outsidetable');

                  // type selector
                  $ltype = $('<select>').attr('id', 'new-limit-type');
                  for(i = 0; i < ltypes.length; i++)
                  {
                     $ltype.append( $('<option>').attr('value', ltypes[i][0]).text(ltypes[i][1]) );
                  }
                  $nltr.append( $('<td>').append( $ltype ) );
                  // hard/soft limit
                  $nltr.append( $('<td>').append( $('<select>').attr('id', 'new-limit-hs').append( $('<option>').attr('value', 'hard').text('Hard limit') ).append( $('<option>').attr('value', 'soft').text('Soft limit') ) ) );
                  // value
                  $nltr.append( $('<td>').append( $('<input>').attr('type', 'text').attr('id', 'new-limit-val') ) );

                  // applies to (stream)
                  var $appliesto = $('<select>').attr('id', 'new-limit-appliesto').append( $('<option>').attr('value', 'server').text('Server') );

                  for(var strm in settings.settings.streams)
                  {
                     $appliesto.append(
                        $('<option>').attr('value', strm).text(settings.settings.streams[strm].name)
                     );
                  }
                  $nltr.append( $('<td>').append( $appliesto ) );

                  $nltr.append(
                     $('<td>').attr('class', 'center').append(
                        $('<button>').click(function()
                        {
                           var obj =
                           {
                              name: $('#new-limit-type :selected').val(),
                              type: $('#new-limit-hs :selected').val(),
                              val:  $('#new-limit-val').val()
                           };

                           if( $('#new-limit-appliesto').val() == 'server')
                           {
                              settings.settings.config.limits.push(obj);
                           }else{
                              settings.settings.streams[ $('#new-limit-appliesto').val() ].limits.push(obj);
                           }

                           loadSettings(function()
                           {
                              showTab('limits');
                           });

                        }).text('add new')
                     )
                  );
                  $tbody.append($nltr);

                  $table.append($tbody);
                  $('#page').append($table);

                  break;



               case 'logs':
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
                  $('#page').append($table);

                  $('#page').append(
                     $('<button>').attr('class', 'floatright').click(function()
                     {
                        settings.settings.clearstatlogs = 1;
                        loadSettings(function()
                        {
                           showTab('logs');
                        });
                     }).text('Purge logs')
                  );

                  break;



               case 'disconnect':
                  showTab('login');
                  setHeaderState('disconnected');
                  $('#nav').css('visibility', 'hidden');

                  settings =
                  {
                     server: '',
                     credentials:
                     {
                        username: "",
                        password: "",
                        authstring: ""
                     },
                     settings: {}
                  };
                  break;

            }  // end switch


            //placeholder for older browsers
            $('input[placeholder]').placeholder();

         }

