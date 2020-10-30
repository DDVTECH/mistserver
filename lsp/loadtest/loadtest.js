
var width = document.body.clientWidth / 2 - 50;

var result;
function useResult(r) {
  result = r;
  console.log("raw data",result);

  //bw: [up,down] bandwidth (counter,total,bytes) (gemeten door Mist op een gemeten verbinding)
  //bw: ingesteld voor load balancer (0? 1gbit)
  //cpu: Total CPU usage in tenths of percent.
  //curr: [total viewers,total inputs, outputs,sessies] (huidig)
  //logs: counter, aantal log msgs
  //mem_: in kilobyte
  //obw: other bandwidth (counter, bytes) (bandbreedte Mist die niet meetelt voor bandwidth limit)
  //st: system bandwidth [up,down]
  //tot: [viewers, inputs, outputs] counters
  //stream:curr (zonder sessies)
  //triggers:{triggernaam:{count(times execed),ms (sum exec time),fails(times failed)}}

  //cpu
  //mem used
  //shm used

  /*viewer_output for each viewer [ 
    *  bootsecs van begin deze viewer,
    *  bootsecs van stop deze viewer,
    *  verschil [0] en [1],
    *  aantal ms aan media dat is ontvangen,
    *  streamtijd [ms] van het eerste pakket,
    *  bootsecs van eerste pakket
    * ]
    * fail op mediatijd 5s < streamtijd
    * grafiek: media ms ontvanten - streamtijd eerste pakket - result.testing_time*1e3
    */
  
  


  var data = result.dataPoints;

  var t = document.createElement("h3");
  t.appendChild(document.createTextNode("Test results"+(result.protocol ? ": "+result.protocol : "")));
  document.body.appendChild(t);
  var table = document.createElement("table");
  document.body.appendChild(table);
  table.style.margin = "1em auto";
  if (result.url) {
    var tr = document.createElement("tr");
    table.appendChild(tr);
    var t = document.createElement("td");
    tr.appendChild(t);
    t.appendChild(document.createTextNode("Tested "));
    var a = document.createElement("a");
    a.appendChild(document.createTextNode(result.url));
    a.setAttribute("href",result.url);
    t.appendChild(a);
  }
  if (result.viewers) {
    var tr = document.createElement("tr");
    table.appendChild(tr);
    var t = document.createElement("td");
    tr.appendChild(t);
    t.appendChild(document.createTextNode("Attempted "+result.viewers+" connections"));
  }
  if (result.testing_time) {
    var tr = document.createElement("tr");
    table.appendChild(tr);
    var t = document.createElement("td");
    tr.appendChild(t);
    t.appendChild(document.createTextNode("Test duration "+tickformats.duration({scale:result.testing_time*1e3})(result.testing_time*1e3)));
  }
  if (data && data.length) {
    var tr = document.createElement("tr");
    table.appendChild(tr);
    var t = document.createElement("td");
    tr.appendChild(t);
    var d = new Date(data[0].timestamp)
    t.appendChild(document.createTextNode("Started at "+d.toDateString()+", "+d.toLocaleTimeString()));
  }
  
  var prometheus = false;
  var viewer_output = false;
  function w(text) {
    var t = document.createElement("p");
    t.setAttribute("class","error");
    t.appendChild(document.createTextNode("Warning: "+text))
    document.body.appendChild(t);
  }
  if (!result.url) {
    w("Results do not contain test url.");
  }
  if (!("streamInfo" in result) || (result.streamInfo == null)) {
    w("Results do not contain stream info.");
  }
  else if ("error" in result.streamInfo) {
    w("Stream info contains error \""+result.streamInfo.error+"\"");
  }
  if (!("dataPoints" in result) || (result.dataPoints == null)) {
    w("Results do not contain prometheus output.");
  }
  else if (result.dataPoints.length == 0) {
    w("Prometheus output is empty.");
  }
  else if (!("data" in result.dataPoints[0]) || (typeof result.dataPoints[0].data != "object")) {
    w("Prometheus output is malformed.");
  }
  else { prometheus = true; }
  if (!("viewer_output" in result) || (!result.viewer_output) || (result.viewer_output.length == 0)){
    w("Results do not contain test output.");
  }
  else { viewer_output = true; }

  if (result.streamInfo && result.streamInfo.meta) {
    var t = document.createElement("table");
    var tr = document.createElement("tr");
    t.appendChild(tr);
    var td = document.createElement("th");
    tr.appendChild(td);
    td.appendChild(document.createTextNode("Tracks"));
    var td = document.createElement("th");
    tr.appendChild(td);
    td.appendChild(document.createTextNode("Bitrate"));
    t.style.margin = "0 auto";
    var totalrate = 0;
    var totalpeak = 0;
    for (var i in result.streamInfo.meta.tracks) {
      var track = result.streamInfo.meta.tracks[i];
      var tr = document.createElement("tr");
      t.appendChild(tr);
      var td = document.createElement("td");
      td.style.textTransform = "capitalize";
      td.style.textAlign = "left";
      tr.appendChild(td);
      td.appendChild(document.createTextNode(track.type+" ("+track.codec+"):"));
      var td = document.createElement("td");
      tr.appendChild(td);
      var avg = tickformats.bps()(track.bps*8);
      var peak = tickformats.bps()(track.maxbps*8);
      td.appendChild(document.createTextNode(avg+(avg != peak ? " (peak: "+peak+")" : "")));
      td.style.textAlign = "left";
      totalrate += track.bps;
      totalpeak += track.maxbps;
    }
    var tr = document.createElement("tr");
    t.appendChild(tr);
    var td = document.createElement("td");
    tr.appendChild(td);
    td.appendChild(document.createTextNode("Total bitrate:"));
    td.style.textAlign = "left";
    var td = document.createElement("td");
    td.style.textAlign = "left";
    td.style.borderTop = "1px solid var(--text)";
    tr.appendChild(td);
    var avg = tickformats.bps()(totalrate*8);
    var peak = tickformats.bps()(totalpeak*8);
    td.appendChild(document.createTextNode(avg+(avg != peak ? " (peak: "+peak+")" : "")));
    document.body.appendChild(t);
  }

  var graphs = document.createElement("div");
  graphs.classList.add("graphs");
  document.body.appendChild(graphs);

  var x = {
    label: "Time",
    type: "duration",
    get: function(d,i,a){ return d.timestamp-a[0].timestamp; }
  };
  var margin = 10;

  function viewerDelay(d) {
    return result.testing_time*1e3 - (d.media_stop - d.media_start);
  }
  function hasViewerFailed(delay) { return (delay > 5e3 ? true : false); }

  
  if (viewer_output) {
    drawGraph({
      title: "Test results",
      type: "pie",
      raw: result.viewer_output,
      container: graphs,
      margin: margin,
      get: function(raw){
        var pass = 0;
        var fail = 0;
        for (var i in raw) {
          if (hasViewerFailed(viewerDelay(raw[i]))) {
            fail++;
          }
          else {
            pass++;
          }
        }
        return [
          {
            label: "Passed",
            value: pass,
            color: "var(--color-3)"
          },
          {
            label: "Failed",
            value: fail,
            color: "var(--color-2)"
          }
        ];
      },
      color: function(d,i){
        return d.color;
      }
    });
  }

  if (prometheus) {
    var br = document.createElement("div");
    br.appendChild(document.createTextNode("Server load"));
    br.style.width = "100%";
    graphs.appendChild(br);
    
    drawGraph({
      title: "CPU load",
      raw: data,
      container: graphs,
      margin: margin,
      y: {
        label: "CPU Load",
        unit: "%",
        min: 0,
        max: 100,
        series: [{
          label: "CPU Load",
          get: function(d){ return d.data.cpu/10; },
          dots: false,
          //fill: "fade",
          color: "gradient",
          gradient: [[0,"var(--color-1)"],[75,"var(--color-1)"],[90,"orange"],[100,"red"]],
          labels: dgutil.minmax2labels
        }]
      },
      x: x
    });
    
    drawGraph({
      title: "RAM",
      raw: data,
      container: graphs,
      margin: margin,
      y: {
        label: "RAM",
        type: "byte",
        min: 0,
        max: data[0].data.mem_total*1024,
        series: [{
          label: "RAM",
          get: function(d){ return d.data.mem_used*1024; },
          dots: false,
          color: "gradient",
          gradient: [[0,"var(--color-1)"],[0.75*data[0].data.mem_total*1024,"var(--color-1)"],[0.9*data[0].data.mem_total*1024,"orange"],[data[0].data.mem_total*1024,"red"]],
          labels: dgutil.max2labels
        }]
      },
      x: x
    });

    var br = document.createElement("div");
    br.appendChild(document.createTextNode("Mist stats"));
    br.style.width = "100%";
    graphs.appendChild(br);

    drawGraph({
      title: "Viewers",
      raw: data,
      container: graphs,
      margin: margin,
      y: {
        label: "Viewers",
        min: 0,
        series: [{
          label: "Viewers",
          get: function(d){ return d.data.curr[0]; }, 
          dots: false,
          labels: dgutil.max2labels
        }]
      },
      x: x
    });
    
    drawGraph({
      title: "Logs",
      raw: data,
      container: graphs,
      margin: margin,
      y: {
        label: "Messages/s",
        unit: "/s",
        series: [{
          label: "Messages/s",
          get: function(d,i,a){
            return dgutil.differentiate(x.get,function(d){ return d.data.logs; },d,i,a)*1e3;
          },
          dots: false,
          labels: dgutil.max2labels
        }]
      },
      x: x
    });
    
    drawGraph({
      title: "Bandwidth (system)",
      raw: data,
      container: graphs,
      margin: margin,
      y: {
        label: "Bandwidth",
        type: "bps",
        min: 0,
        series: [{
          label: "Up",
          get: function(d,i,a){
            return dgutil.differentiate(x.get,function(d){ return d.data.st[0]; },d,i,a)*8e3;
          }, //*8 for bits, *1000 because the time axis is in ms instead of s
          dots: false,
          labels: dgutil.max2labels
        },{
          label: "Down",
          get: function(d,i,a){
            return dgutil.differentiate(x.get,function(d){ return d.data.st[1]; },d,i,a)*8e3;
          }, //*8 for bits, *1000 because the time axis is in ms instead of s
          dots: false,
          labels: dgutil.max2labels
        }]
      },
      x: x
    });
    var bandwidth_ext_graph = drawGraph({
      title: "Bandwidth (Mist, external)",
      type: "line",
      raw: data,
      container: graphs,
      margin: margin,
      y: {
        label: "Bandwidth",
        type: "bps",
        min: 0,
        series: [{
          label: "Up",
          get: function(d,i,a){
            return dgutil.differentiate(x.get,function(d,i,a){ return d.data.bw[0]; },d,i,a)*8e3;
          },
          dots: false,
          labels: dgutil.max2labels
        },{
          label: "Down",
          get: function(d,i,a){
            return dgutil.differentiate(x.get,function(d,i,a){ return d.data.bw[1]; },d,i,a)*8e3;
          },
          dots: false,
          labels: dgutil.max2labels
        }]
      },
      x: x
    });

    drawGraph({
      title: "Average bandwidth (Mist, per viewer)",
      type: "line",
      raw: data,
      container: graphs,
      margin: margin,
      y: {
        label: "Bandwidth",
        type: "bps",
        series: [{
          label: "Up",
          get: function(d,i,a){
            return dgutil.differentiate(x.get,function(d,i,a){ return d.data.bw[0]; },d,i,a) * 8e3 / d.data.curr[0];
          },
          dots: false,
          labels: dgutil.max2labels
        },{
          label: "Down",
          get: function(d,i,a){
            return dgutil.differentiate(x.get,function(d,i,a) { return d.data.bw[1]; },d,i,a) * 8e3 / d.data.curr[0];
          },
          dots: false,
          labels: dgutil.max2labels
        }]
      },
      x: x
    });
    
    if ("pkts" in data[0].data) {
      drawGraph({
        title: "Packet loss",
        type: "line",
        raw: data,
        container: graphs,
        margin: margin,
        y: {
          label: "Percentage of packets sent",
          unit: "%",
          min: 0,
          series: [{
            label: "Packets lost",
            get: function(d,i,a){
              var sent = dgutil.differentiate(x.get,function(d,i,a){ return d.data.pkts[0]; },d,i,a)*1e3;
              var lost = dgutil.differentiate(x.get,function(d,i,a){ return d.data.pkts[1]; },d,i,a)*1e3;
              return sent == 0 ? 0 : lost/sent*100;
            },
            dots: false,
            color: "var(--red)",
            labels: dgutil.max2labels
          },{
            label: "Packets retransmitted",
            get: function(d,i,a){
              var sent = dgutil.differentiate(x.get,function(d,i,a){ return d.data.pkts[0]; },d,i,a)*1e3;
              var resent = dgutil.differentiate(x.get,function(d,i,a){ return d.data.pkts[2]; },d,i,a)*1e3;
              return sent == 0 ? 0 : resent/sent*100;
            },
            dots: false,
            color: "var(--blue)",
            labels: dgutil.max2labels
          }/*{
            label: "Packets sent",
            get: function(d,i,a){
              var sent = d.data.pkts[0] - a[0].data.pkts[0];
              return sent;
            }
          },{
            label: "Packets lost",
            get: function(d,i,a){
              var sent = d.data.pkts[1] - a[0].data.pkts[1];
              return sent;
            }
          },{
            label: "Packets retransmitted",
            get: function(d,i,a){
              var sent = d.data.pkts[2] - a[0].data.pkts[2];
              return sent;
            }
          }*/]
        },
        x: x
      });
    }
  }
  if (viewer_output) {
    
    var br = document.createElement("div");
    br.appendChild(document.createTextNode("Test results"));
    br.style.width = "100%";
    graphs.appendChild(br);

    var viewers_start = Array.from(result.viewer_output);
    viewers_start = viewers_start.filter(function(v){
      return (v.media_start != 0) || (v.media_stop != 0);
    });
    viewers_start = viewers_start.sort(function(a,b){ return a.system_start - b.system_start; }); //order by start
    var teststart = viewers_start[0].system_start;
    
    var viewer_timings_graph = drawGraph({
      title: "Viewer timings",
      raw: viewers_start,
      container: graphs,
      margin: margin,
      y: [{
        label: "Time",
        type: "duration",
        min: 0,
        series: [
          {
            label: "Start",
            get: function(d){ return (d.system_start - teststart); },
            line: false
          },{
            label: "First packet",
            get: function(d){ return (d.system_firstmedia - teststart); },
            line: false
          },{
            label: "Media received",
            get: function(d){ return (d.system_firstmedia - teststart + (d.media_stop - d.media_start)); },
            line: false
          },{
            label: "Stop",
            get: function(d){ return (d.system_stop - teststart); },
            line: false
          }
        ]
      },{
        label: "Text",
        format: "text",
        show: false,
        series: [{
          label: "Error",
          displayInLegend: false,
          get: function(d){ return d.error; },
          dots: false,
          line: false
        }]
      }],
      x: {
        label: "Viewer",
        get: function(d,i,a){ return i; }
      }
    });
    
    if ("media" in viewers_start[0]) {
      setTimeout(function(){
        function calcAvgsForEach(data,getFunc) {
          var out = {};
          for (var i in data) {
            if (data[i].media) {
              for (var j = 0; j < data[i].media.length; j++) {
                var entry = data[i].media[j];
                var t = Math.round((entry[0] - teststart)/1e3)*1e3; //group by second
                if (!(t in out)) {
                  out[t] = {
                    data: []
                  };
                }
                var current = getFunc(entry,i,data[i],j,data);
                out[t].data.push({
                  viewer: i,
                  value: current
                });
              }
            }
          }
          for (var t in out) {
            var entry = out[t];
            entry.sum = 0;
            entry.count = entry.data.length;
            if (entry.data.length) {
              entry.max = entry.data[0].value;
              entry.min = entry.data[0].value;
              for (var i in entry.data) {
                var current = entry.data[i].value;
                entry.sum += current;
                if (entry.max < current) { entry.max = current; }
                else if (entry.min > current) { entry.min = current; }
              }
              entry.avg = entry.sum / entry.count;
              //calculate stdev
              entry.stdev = 0;
              entry.stdev_upper = 0;
              entry.stdev_lower = 0;
              entry.stdev_upper_count = 0;
              entry.stdev_lower_count = 0;
              for (var i in entry.data) {
                var current = entry.data[i].value ;
                entry.data[i].deviation = Math.pow(current - entry.avg,2);
                entry.stdev += entry.data[i].deviation;
                entry["stdev_"+(current > entry.avg ? "upper" : "lower")] += entry.data[i].deviation;
                entry["stdev_"+(current > entry.avg ? "upper" : "lower")+"_count"] += 1;
              }
              entry.stdev = Math.pow(entry.stdev/entry.count,0.5);
              entry.stdev_lower = Math.pow(entry.stdev_lower/entry.stdev_lower_count,0.5);
              entry.stdev_upper = Math.pow(entry.stdev_upper/entry.stdev_upper_count,0.5);
            }
          }
          return out;
        }
        var viewer_data_by_systemtime = calcAvgsForEach(viewers_start,function(entry,i,viewer,j,a){
          if (j == 0) { return undefined; }
          var preventry = a[i].media[j-1];
          var t = Math.round((entry[0] - teststart)/1e3)*1e3; //group by second
          var current = (entry[1] - preventry[1]) / (entry[0] - preventry[0]);
          return current;
        });
        
        var series = [
          {
            label: "Target (1×)",
            get: function(){ return 1; },
            dots: false,
            color: "gray",
            displayInTooltip: false
          },{
            label: "Maximum",
            get: function(d,i){ return d.max; },
            dots: false,
            line: false,
            displayInLegend: false,
            color: "var(--blue)"
          },{
            label: "Average",
            get: function(d,i){ return d.avg; },
            dots: false,
            color: "var(--blue)",
            bands: [{ //add bands to display the range the delays occur, and highlight the standard deviation
              upper: function(d,i){ return d.avg + d.stdev_upper; },
              lower: function(d,i){ return d.avg - d.stdev_lower; }
            },{
              upper: function(d,i){ return d.max; },
              lower: function(d,i){ return d.min; }
            }]
          },{
            label: "Minimum",
            get: function(d,i){ return d.min; },
            dots: false,
            line: false,
            displayInLegend: false,
            color: "var(--blue)"
          }
        ];
        
        var media_rate_graph = drawGraph({
          title: "Media rate over time",
          raw: viewer_data_by_systemtime,
          container: graphs,
          margin: margin,
          y: {
            label: "Media rate",
            series: series,
            unit: "×"
          },
          x: {
            include0: true,
            label: "Time",
            type: "duration",
            get: function(d,i,a){
              return i;
            }
          }
        });
        
        var delay_graph;
        if ("delayGraph" in localStorage) {
          function calcDelay(entry,i,viewer,j,a) {
            var media_received = entry[1] - viewer.media_start;
            var time_passed = entry[0] - viewer.system_start;
            return time_passed - media_received;
          }
          var viewer_delay_by_systemtime = calcAvgsForEach(viewers_start,calcDelay);
          
          var delay_series = [{
              label: "Target (0s)",
              get: function(){ return 0; },
              dots: false,
              color: "gray",
              displayInTooltip: false
            },{
              label: "Maximum",
              get: function(d,i){ return d.max; },
              dots: false,
              line: false,
              displayInLegend: false,
              color: "var(--red)"
            },{
              label: "Average",
              get: function(d,i){ return d.avg; },
              dots: false,
              color: "var(--blue)",
              bands: [{ //add bands to display the range the delays occur, and highlight the standard deviation
                upper: function(d,i){ return d.avg + d.stdev_upper; },
                lower: function(d,i){ return d.avg - d.stdev_lower; }
              },{
                upper: function(d,i){ return d.max; },
                lower: function(d,i){ return d.min; }
              }]
            },{
              label: "Minimum",
              get: function(d,i){ return d.min; },
              dots: false,
              line: false,
              displayInLegend: false,
              color: "var(--green)"
            }
          ];
          
          var defsettings = {
            dots: false,
            x: function(d,i){
              return d[0] - teststart;
            }
          };
          
          delay_graph = drawGraph({
            title: "Media delay over time",
            raw: viewer_delay_by_systemtime,
            container: graphs,
            margin: margin,
            y: {
              label: "Delay",
              type: "duration",
              series: delay_series
            },
            x: {
              include0: true,
              label: "Time",
              type: "duration",
              get: function(d,i,a){
                return i;
              }
            }
          });
        }
        

      
        viewer_bandwidth_by_systemtime = calcAvgsForEach(viewers_start,function(entry,i,viewer,j,a){
          if (j == 0) { return undefined; }
          var preventry = a[i].media[j-1];
          var t = Math.round((entry[0] - teststart)/1e3)*1e3; //group by second
          var current = (entry[2] - preventry[2]) / (entry[0] - preventry[0]) * 8000; //*1000 to go from /ms to /s, *8 to go from bytes to bits
          return current;
        });

        graphs.insertBefore(drawGraph({
          title: "Bandwidth (analysers)",
          raw: viewer_bandwidth_by_systemtime,
          container: graphs,
          margin: margin,
          y: {
            label: "Bandwidth",
            type: "bps",
            min: 0,
            series: [{
              label: "Up",
              get: function(d,i,a){
                return d.sum;
              },
              dots: false,
              labels: dgutil.max2labels
            }]
          },
          x: {
            include0: true,
            label: "Time",
            type: "duration",
            get: function(d,i,a){
              return i;
            }
          }
        }),bandwidth_ext_graph.nextSibling);

        var bandwidth_series = [{
          label: "Maximum",
          get: function(d,i){ return d.max; },
          dots: false,
          line: false,
          displayInLegend: false,
          color: "var(--red)"
        },{
          label: "Average",
          get: function(d,i){ return d.avg; },
          dots: false,
          color: "var(--blue)",
          bands: [{ //add bands to display the range the delays occur, and highlight the standard deviation
            upper: function(d,i){ return d.avg + d.stdev_upper; },
            lower: function(d,i){ return d.avg - d.stdev_lower; }
          },{
            upper: function(d,i){ return d.max; },
            lower: function(d,i){ return d.min; }
          }]
        },{
          label: "Minimum",
          get: function(d,i){ return d.min; },
          dots: false,
          line: false,
          displayInLegend: false,
          color: "var(--green)"
        }];
        var bandwidth_analysers_graph = drawGraph({
          title: "Bandwidth (analysers) over time",
          raw: viewer_bandwidth_by_systemtime,
          container: graphs,
          margin: margin,
          y: {
            label: "Bandwidth",
            type: "bps",
            min: 0,
            series: bandwidth_series
          },
          x: {
            include0: true,
            label: "Time",
            type: "duration",
            get: function(d,i,a){
              return i;
            }
          }
        });



        var lastviewer = false;
        viewer_timings_graph.addEventListener("click",function(){
          var viewer = this.querySelector(".cursor").getAttribute("data-x");
          if (viewer != lastviewer) {
            if (lastviewer !== false) { series.pop(); }
            series.push(function(k){ return {
              label: "Viewer "+k,
              raw: viewers_start[k].media,
              color: (hasViewerFailed(viewerDelay(viewers_start[k])) ? "var(--red)" : "var(--green)"),
              dots: false,
              get: function(d,i,raw) {
                if (i == 0) { return undefined; }
                entry = d;
                preventry = raw[i-1];
                return (entry[1] - preventry[1]) / (entry[0] - preventry[0]);
              },
              x: function(d,i){
                return d[0] - teststart;
              }
            };}(viewer));
            setTimeout(function(){
              var new_graph = drawGraph({
                title: "Media rate over time",
                raw: viewer_data_by_systemtime,
                container: graphs,
                margin: margin,
                y: {
                  label: "Media rate",
                  series: series,
                  unit: "×"
                },
                x: {
                  include0: true,
                  label: "Time",
                  type: "duration",
                  get: function(d,i,a){
                    return i;
                  }
                }
              });
              media_rate_graph.parentElement.insertBefore(new_graph,media_rate_graph);
              media_rate_graph.parentElement.removeChild(media_rate_graph);
              media_rate_graph = new_graph;
            },1);//async
            
            if (delay_graph) {
              if (lastviewer !== false) { delay_series.pop(); }
              delay_series.push(function(k){
                return Object.assign({
                  label: "Viewer "+k,
                  raw: viewers_start[k].media,
                  color: (hasViewerFailed(viewerDelay(viewers_start[k])) ? "var(--red)" : "var(--green)"),
                  get: function(d,i,raw){
                    return calcDelay(d,i,viewers_start[k],viewers_start);
                  },
                  dots: false,
                  x: function(d,i){
                    return d[0] - teststart;
                  }
                });
              }(viewer));
              setTimeout(function(){
                var new_graph = drawGraph({
                  title: "Media delay over time",
                  raw: viewer_delay_by_systemtime,
                  container: graphs,
                  margin: margin,
                  y: {
                    label: "Delay",
                    type: "duration",
                    series: delay_series
                  },
                  x: {
                    include0: true,
                    label: "Time",
                    type: "duration",
                    get: function(d,i,a){
                      return i;
                    }
                  }
                });
                delay_graph.parentElement.insertBefore(new_graph,delay_graph);
                delay_graph.parentElement.removeChild(delay_graph);
                delay_graph = new_graph;
              },1);//async
            }
            
            //bandwidth graph
            if (lastviewer !== false) { bandwidth_series.pop(); }
            bandwidth_series.push(function(k){ return {
              label: "Viewer "+k,
              raw: viewers_start[k].media,
              color: (hasViewerFailed(viewerDelay(viewers_start[k])) ? "var(--red)" : "var(--green)"),
              dots: false,
              get: function(d,i,raw) {
                if (i == 0) { return undefined; }
                entry = d;
                preventry = raw[i-1];
                return (entry[2] - preventry[2]) / (entry[0] - preventry[0]) * 8000;
              },
              x: function(d,i){
                return d[0] - teststart;
              }
            };}(viewer));
            setTimeout(function(){
              var new_graph = drawGraph({
                title: "Bandwidth (analysers) over time",
                raw: viewer_bandwidth_by_systemtime,
                container: graphs,
                margin: margin,
                y: {
                  label: "Bandwidth",
                  type: "bps",
                  min: 0,
                  series: bandwidth_series
                },
                x: {
                  include0: true,
                  label: "Time",
                  type: "duration",
                  get: function(d,i,a){
                    return i;
                  }
                }
              });
              bandwidth_analysers_graph.parentElement.insertBefore(new_graph,bandwidth_analysers_graph);
              bandwidth_analysers_graph.parentElement.removeChild(bandwidth_analysers_graph);
              bandwidth_analysers_graph = new_graph;
            },1);//async


            lastviewer = viewer;
          }
        });

      },1);
    }
  }
}  

function getJSON(a) { 
  if (typeof a == "string") {
    var t = new XMLHttpRequest();
    t.addEventListener("load",function(e){
      if (t.status != 200) {
        document.body.appendChild(document.createTextNode("Failed to retrieve stats.json"));
        return;
      }
      useResult(JSON.parse(e.target.responseText));
    });
    t.open("GET", a, true);
    t.send();
  }
  else useResult(a);
}
getJSON("stats.json");
