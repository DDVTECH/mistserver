var tickformats = {
  "default": function(options){
    var numberformat = this.number(options);
    return (val,n) => {
      return numberformat(val,n)+("unit" in options ? options.unit : "");
    };
  },
  number: function(options){
    return function(val,n){
      
      var suffix = "";
      var sig = false;
      if (typeof n != "undefined") {
        if (val >= 1e9) {
          val /= 1e9;
          suffix = "b";
        }
        else if (val >= 1e6) {
          val /= 1e6;
          suffix = "m";
        }
        else if (val >= 1e3) {
          val /= 1e3;
          suffix = "k";
        }
        sig = 3;
      }
      var opts = {
        style: "decimal",
        useGrouping: true
      };
      
      if (sig !== false) {
        opts.maximumSignificantDigits = sig;
        val = Number(val).toPrecision(sig);
      }
      else {
        opts.maximumSignificantDigits = 3;
      }
      
      var r = new Intl.NumberFormat('nl-NL',opts).format(val);
      return r.replace(/\./g," ").replace(",",".")+suffix;
    };
  },
  currency: function(options){
    var numberformat = this.number(options);
    return (val,n) => {
      return ("unit" in options ? options.unit : "€ ")+" "+numberformat(Math.round(Number(val)*100)/100,n);
    };
  },
  byte: function(options) {
    return function(val){
      var suffix = ['B','KiB','MiB','GiB','TiB','PiB'];
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
      return tickformats.number()(val)+" "+unit;
    };
  },
  bps: function(options){
    return function(val) {
      return tickformats.byte()(val).replace("B","b")+"ps";
    }
  },
  duration: function(options) {
    if (!options || !("scale" in options)) {
      return function(val){
        var sign = (val < 0 ? "-" : "");
        val = Math.abs(val);
        var days = Math.floor(val/24/3600);
        val -= days * 24*3600e3;
        var hours = Math.floor(val/3600e3);
        val -= hours * 3600e3;
        var minutes = Math.floor(val/60e3);
        val -= minutes * 60e3;
        return [
          sign+tickformats.default({unit:"d"})(days),
          [
            ("0"+hours).slice(-2),
            ("0"+minutes).slice(-2),
            ("0"+Math.floor(val/1e3)).slice(-2)
          ].join(":")
        ].join(", ")
      };
    }
    var d = Math.abs(options.scale);
    var days = Math.floor(d/24/3600e3);
    d -= days*24*3600e3;
    if (days < 1) {
      var hours = Math.floor(d/3600e3);
      d -= hours*3600e3;
      if (hours < 1) {
        var minutes = Math.floor(d/60e3);
        if (minutes < 1 ) {
          return function(val) {
            return tickformats.default({unit:"s"})(val*1e-3)
          };
        }
        else {
          return function(val){
            var minutes = Math.floor(val/60e3);
            val -= minutes * 60e3;
            return [
            minutes,
            ("0"+Math.floor(val/1e3)).slice(-2)
            ].join(":")
          };
        }
      }
      else {
        return function(val){
          var hours = Math.floor(val/3600e3);
          val -= hours * 3600e3;
          var minutes = Math.floor(val/60e3);
          val -= minutes * 60e3;
          return [
          hours,
          ("0"+minutes).slice(-2),
          ("0"+Math.floor(val/1e3)).slice(-2)
          ].join(":")
        };
      }
    }
    else {
      return function(val){
        var days = Math.floor(val/24/3600);
        val -= days * 24*3600e3;
        var hours = Math.floor(val/3600e3);
        val -= hours * 3600e3;
        var minutes = Math.floor(val/60e3);
        val -= minutes * 60e3;
        return [
          tickformats.default({unit:"d"})(days),
          [
            ("0"+hours).slice(-2),
            ("0"+minutes).slice(-2),
            ("0"+Math.round(val/1e3)).slice(-2)
          ].join(":")
        ].join(", ")
      };
    }
  
    
    
    return function(val){
      var days = Math.floor(val/24/3600);
      val -= days * 24*3600e3;
      var hours = Math.floor(val/3600e3);
      val -= hours * 3600e3;
      var minutes = Math.floor(val/60e3);
      val -= minutes * 60e3;
      return [
      tickformats.default({unit:"d"})(days),
      [
      ("0"+hours).slice(-2),
      ("0"+minutes).slice(-2),
      ("0"+Math.round(val/1e3)).slice(-2)
      ].join(":")
      ].join(", ")
    };
  },
  text: function(options){
    return function(val) {
      return val;
    }
  }
};
var dgutil = {
  localminmax: function(data,type){
    var points = [];
    points.push({
      index: 0,
      pos: data[0],
      type: (data[1].y > data[0].y ? "min" : "max")
    });
    var dy = 0;
    //find all local min/max
    for (var i = 1; i < data.length; i++) {
      var ndy = data[i].y - data[i-1].y;
      if ((dy < 0) && (ndy > 0)) {
        //local min
        points.push({
          index: i-1,
          pos: data[i-1],
          type: "min"
        });
      }
      else if ((dy > 0) && (ndy < 0)) {
        //local max
        points.push({
          index: i-1,
          pos: data[i-1],
          type: "max"
        });
      }
      if (ndy != 0) {
        dy = ndy;
      }
    }
    points.push({
      index: data.length-1,
      pos: data[data.length-1],
      type: (dy > 0 ? "max" : "min")
    });
    
    
    return points;
  },
  filter_by_dy: function(data,points,scalex,scaley) {
    //filter the results, remove minmax pairs that don't have enough dy between them
    var range = scaley.domain()[1] - scaley.domain()[0];
    var minimal_dy = range / 5;
    
    function total_dy_before(point,i) {
      var last_index = i > 0 ? points[i - 1].index : 0;
      return data[point.index].y - data[last_index].y;
    }

    while (points.length > 2) { //keep at least one pair
      var dys_before = points.map(function(a){return Math.abs(total_dy_before(a,points.indexOf(a)))});
      var lowest = Math.min.apply(this,dys_before);
      if (lowest > minimal_dy) {
        //we're done here
        break;
      }
      var lowest_index = dys_before.indexOf(lowest);
      if (lowest_index > 0) {
        points.splice(lowest_index-1,2); //remove the min and max point with the lowest dy between them
      }
      else {
        points.splice(lowest_index,1)
      }
    }
    
    return points;
  },
  filter_NaN: function(points){
    return points.filter(function(a){
      return !isNaN(a.pos.y) && isFinite(a.pos.y);
    });
    console.log(points);
  },
  minmax2labels: function(data,scalex,scaley) {
    return dgutil.filter_by_dy(data,dgutil.filter_NaN(dgutil.localminmax(data)),scalex,scaley).map(function(a){return a.index;});
  },
  max2labels: function(data,scalex,scaley) {
    return dgutil.filter_by_dy(data,dgutil.filter_NaN(dgutil.localminmax(data)),scalex,scaley).filter(function(a){return a.type == "max"}).map(function(a){return a.index;});
  },
  min2labels: function(data,scalex,scaley) {
    return dgutil.filter_by_dy(data,dgutil.filter_NaN(dgutil.localminmax(data)),scalex,scaley).filter(function(a){return a.type == "min"}).map(function(a){return a.index;});
  },
  differentiate: function(getx,gety,d,j,a) {
    if (j == 0) { return null; }
    var dy = gety(d,j,a) - gety(a[j-1],j-1,a);
    var dx = getx(d,j,a) - getx(a[j-1],j-1,a);
    return dy/dx;
  }
}

var tooltip = document.createElement("div");
tooltip.style.display = "none";
tooltip.classList.add("tooltip");
document.body.appendChild(tooltip);
tooltip.set = function(html){
  tooltip.innerHTML = "";
  if (typeof html == "string") {
    tooltip.innerHTML = html;
  }
  else {
    tooltip.appendChild(html);
  }
};

function drawGraph(options) {
  var svg = document.createElementNS("http://www.w3.org/2000/svg", "svg");
  options.container.appendChild(svg);
  
  var chart_container = d3.select(svg)
    .attr("class","chart chart"+uid);
  
  try{
    
    var margin = { //proper values calculated later
      right: options.margin,
      top: options.margin,
      bottom: options.margin,
      left: options.margin
    };
    var width, height;
    
    var uid = Math.random().toString().slice(2);
    
    var chart_container = d3.select(svg)
      .attr("class","chart chart"+uid);
    
    var defs = chart_container.append("defs");
    var filter = defs.append("filter").attr("id","dropshadow")
      .attr("width","200%")
      .attr("height","200%")
      .attr("x","-50%")
      .attr("y","-50%");
    filter.append("feFlood")
      .attr("flood-color","#000")
      .attr("flood-opacity",0.2)
      .attr("result","ds1");
    filter.append("feComposite")
      .attr("in","ds1")
      .attr("in2","SourceGraphic")
      .attr("operator","in")
      .attr("result","ds2");
    filter.append("feGaussianBlur")
      .attr("in","ds2")
      .attr("result","blur")
      .attr("stdDeviation",1.5);
    filter.append("feOffset")
      .attr("dx",1)
      .attr("dy",3)
      .attr("result","ds3");
    filter.append("feComposite")
      .attr("in","SourceGraphic")
      .attr("in2","ds3")
      .attr("result","ds4");
    var g = defs.append("linearGradient").attr("id","mask_gradient")
      .attr("x1",0).attr("y1",0)
      .attr("x2",0).attr("y2","75%");
    g.append("stop")
      .attr("offset","0%")
      .attr("stop-color","white");
    g.append("stop")
      .attr("offset","100%")
      .attr("stop-color","black");
    var gm = defs.append("mask").attr("id","chart_transparency_gradient").append("rect")
      .attr("x",0)
      .attr("y",0)
      .attr("width","100%")
      .attr("height","100%")
      .attr("fill","url(#mask_gradient)");
    var gm = defs.append("mask").attr("id","barchart_transparency_gradient").append("rect")
      .attr("x",0)
      .attr("y",0)
      .attr("width","100%")
      .attr("height","140%")
      .attr("fill","url(#mask_gradient)");
    
    var title = chart_container.append("text")
      .attr("class","title")
      .attr("text-anchor", "middle")
      .attr("dominant-baseline", "hanging")
      .attr("alignment-baseline","hanging")
      .attr("x","50%")
      .attr("y",options.margin)
      .text(options.title);
    
    
    margin.top += title.node().getBBox().height + options.margin;
    
    var chart = chart_container.append("g");
    
    switch (options.type) {
      case "pie":{
        margin.top += options.margin;
        
        var series = options.get(options.raw);
      
        var pie = d3.pie()
          .value(function(d){
            return d.value.value;
          })
          .sort(function(a,b){ return a.index - b.index;}) //don't sort by values, keep original sorting
          (d3.entries(series));
        
        var data_container = chart.append("g")
          .attr("class","pie data_container");
        var slice = data_container.selectAll(".slice")
          .data(pie)
          .enter()
          .append("path")
          .attr("class","slice series");
        if ("color" in options) {
          slice.attr("style",function(d,j,a) {
            return "fill: "+options.color(series[j],j)+";"
          });
        }
        
        var sum = d3.sum(series,function(d){return d.value});
        var text_v = chart.append("text")
          .attr("x","50%")
          .attr("dy","-0.2em")
          .attr("font-size","1.2em")
          .style("text-anchor", "middle");
        var text_l = chart.append("text")
          .attr("x","50%")
          .attr("dy","1em")
          .attr("font-size","0.8em")
          .style("text-anchor", "middle");
        
        var text_default = {
          label: series[0].label,
          value: tickformats.default({unit:"%"})(series[0].value/sum*100)
        };
        text_l.text(text_default.label);
        text_v.text(text_default.value);
        
        slice.on("mouseover",function(d){
          tooltip.style.display = "";
          text_l.text(d.data.value.label);
          text_v.text(tickformats.default({unit:"%"})(d.value/sum*100));
        });
        slice.on("mousemove",function(e){
          tooltip.style.left = d3.event.pageX+"px";
          tooltip.style.top = d3.event.pageY+"px";
          tooltip.style.right = "";
          
          var f = document.createDocumentFragment();
          
          var title = document.createElement("p");
          title.appendChild(document.createTextNode(e.data.value.label+": "+tickformats.number()(e.value)+"/"+tickformats.number()(sum)));
          f.appendChild(title);
          
          tooltip.set(f);
        });
        slice.on("mouseout",function(e){
          tooltip.style.display = "none";
          text_l.text(text_default.label);
          text_v.text(text_default.value);
        });
        
        svg.updateSize = function(){
          var targetwidth = (options.width ? options.width : svg.getBoundingClientRect().width);
          var targetheight = (options.height ? options.height : svg.getBoundingClientRect().height);
          
          width = targetwidth - margin.left - margin.right;
          height = targetheight - margin.top - margin.bottom;
          
          chart_container
            .attr("width", targetwidth)
            .attr("height", targetheight);
          chart
            .attr("transform", "translate(0," + margin.top + ")");
          
          var r = Math.min(width,height)/2;
          
          data_container.attr("transform","translate("+targetwidth/2+","+height/2+")");
          slice.attr("d",d3.arc()
            .innerRadius(2*r/3)
            .outerRadius(r)
            .cornerRadius(5)
            .padAngle(0.01*Math.PI)
          );
          
          text_v.attr("y",height/2);
          text_l.attr("y",height/2);
        };
        
        setTimeout(svg.updateSize,1); //make it asynchronous
        
        if (!options.width || !options.height) {
          var tracksize = false;
          var resizetimer = false;
          var resizeObserver = new ResizeObserver(function(entries,observer){
            for (var i in entries) {
              if (entries[i].target == options.container) {
                if (!tracksize) {
                  tracksize = {
                    w: entries[i].contentRect.width,
                    h: entries[i].contentRect.height
                  };
                }
                else if ((entries[i].contentRect.width != tracksize.w) || (entries[i].contentRect.height != tracksize.h)) {
                  tracksize.w = entries[i].contentRect.width;
                  tracksize.h = entries[i].contentRect.height;
                  if (resizetimer) { clearTimeout(resizetimer); }
                  resizetimer = setTimeout(function(){
                    svg.updateSize();
                    resizetimer = false;
                  },200);
                }
              }
            }
          });
          resizeObserver.observe(options.container,{box:"border-box"});
        }
        
        break;
      }
      default: {
        var mask = chart.append("mask").attr("id","chartarea"+uid);
        mask._r1 = mask.append("rect");
        mask._r2 = mask.append("rect")
          .attr("fill","#fff")
          .attr("x",0)
          .attr("y",-5);
        mask.updateSize = function(){
          this._r1
            .attr("x",-margin.left)
            .attr("y",-margin.top)
            .attr("width",width+margin.left+margin.right)
            .attr("height",height+margin.top+margin.bottom);
          this._r2
            .attr("width",width+5)
            .attr("height",height+5);
        };
        
        var chartArea = chart.append("rect")
          .attr("class","chartarea");
        var cursor = chart.append("path")
          .attr("class","cursor")
          .attr("data-axis",options.x.type || "default");
        cursor.node().style.display = "none";
        
        function isDefined(d,format) {
          if (format == "text") return !!d;
          return !(isNaN(d) || (d === Infinity) || (d === null));
        }
        
        function createScale(options) {
          var scale;
          switch (options.type) {
            case "time":
            case "duration": {
              scale = d3.scaleTime();
              break;
            }
            case "text": {
              scale = d3.scaleBand().padding(0.1);
              break;
            }
            default: {
              scale = d3.scaleLinear();
            }
          }
          return scale;
        }
        
        var scale = {
          x: createScale(options.x),
          y: []
        };
        var axes = {
          x: d3.axisBottom().ticks(5),
          y: []
        };
        var xAxis = axes.x;
        
        options.x.extent = null;
        
        //gather datapoints
        if (!Array.isArray(options.y)) { options.y = [options.y]; }
        for (var i in options.y) {
          var axis = options.y[i];
          axis.extent = null;
          var s = createScale(axis);
          for (var j in axis.series) {
            var series = axis.series[j];
            series.data = [];
            var raw = options.raw;
            if ("raw" in series) { raw = series.raw; }
            for (var k in raw) {
              var d = raw[k];
              var xget = options.x.get;
              if ("x" in series) { xget = series.x; }
              var x = xget(d,k,raw,j);
              if (options.x.type != "text") {
                if ((options.x.min) && (x < options.x.min)) { continue; }
                if ((options.x.max) && (x > options.x.max)) { continue; }
              }
              try {
                var y = series.get(d,k,raw,j);
              }
              catch(e){
                throw "Error thrown while retrieving y series "+i+": "+e;
              }
              //if ((series.min) && (y < series.min)) { continue; }
              //if ((series.max) && (y > series.max)) { continue; }
              if (options.x.type != "text") {
                if (isDefined(x)) {
                  if (options.x.extent === null) { options.x.extent = {min: x, max: x}; }
                  else {
                    options.x.extent.min = Math.min(options.x.extent.min,x);
                    options.x.extent.max = Math.max(options.x.extent.max,x);
                  }
                }
              }
              if (isDefined(y)) {
                if (axis.extent === null) { axis.extent = {min:y, max: y}; }
                else {
                  axis.extent.min = Math.min(axis.extent.min,y);
                  axis.extent.max = Math.max(axis.extent.max,y);
                }
              }
              
              series.data.push({
                x: x,
                y: y
              });
            }
          }
          if (axis.format == "text") {
            axis.extent = {min:0,max:1};
          }
          if (axis.extent === null) { throw "Y series "+i+" does not contain data."; }
          if (axis.include0) {
            if (axis.extent.max < 0) { axis.extent.max = 0; }
            if (axis.extent.min > 0) { axis.extent.min = 0; }
          }
          var range = axis.extent.max - axis.extent.min;
          if ("min" in axis) { axis.extent.min = axis.min; }
          else if ((axis.extent.min < 0) || (axis.extent.min - range/10 > 0)) { //dont pass 0 if not needed
            axis.extent.min -= range/10;
          }
          if ("max" in axis) { axis.extent.max = axis.max; }
          else { axis.extent.max += range/10; }
          if (axis.extent.min == axis.extent.max) { axis.extent.max ++; }
          s.domain([axis.extent.min,axis.extent.max]);
          if (series.label == "First viewer") {
            console.warn();
          }
          
          scale.y.push(s);
        }
        if (options.x.type != "text") {
          if (options.x.include0) {
            if (options.x.extent.max < 0) { options.x.extent.max = 0; }
            if (options.x.extent.min > 0) { options.x.extent.min = 0; }
          }
          if (options.x.min) { options.x.extent.min = options.x.min; }
          if (options.x.max) { options.x.extent.max = options.x.max; }
          scale.x.domain([options.x.extent.min,options.x.extent.max]);
          
          switch (options.x.type) {
            case "duration": {
              if (!options.x.format) {
                var d = options.x.extent.max;
                options.x.format = tickformats.duration({scale:options.x.extent.max});
              }
              break;
            }
          }
          if ("tickValues" in options.x) xAxis.tickValues(options.x.tickValues(scale.x.domain()));
        }
        else {
          var allx = [];
          for (var i in options.y) {
            for (var j in options.y[i].series) {
              allx = allx.concat(options.y[i].series[j].data.map(a=>{return a.x;}));
            }
          }
          scale.x.domain(allx);
        }
        
        if ("format" in options.x) { xAxis.tickFormat(options.x.format); }
        else if (options.x.type == "text") { xAxis.tickFormat((v) => { return v; }); }
        
        cursor.node()._set = function(val){
          var x = scale.x(val);
          if (options.x.type == "text") {
            x += scale.x.bandwidth() / 2;
          }
          cursor.attr("transform","translate("+x+",0)").attr("data-x",val);
        };
        
        var data_container = chart
          .append("g")
          .attr("class","data_container")
          .attr("mask","url('#chartarea"+uid+"')");
        var data_container_rect = data_container
          .append("rect")
          .attr("class","chartarea");
        var icon_container = data_container.append("g").attr("class","icons").attr("id","icons"+uid);
        data_container = data_container.append("g").attr("class","data").attr("id","data"+uid);
        
        
        var xAxis_container = chart.append("g").attr("class", "x axis");
        
        xAxis.scale(scale.x);
        xAxis_container.call(xAxis);
        
        margin.bottom = xAxis_container.node().getBBox().height + options.margin;
        
        var g = xAxis_container.append("g").attr("class","label");
        var xAxis_container_text = g
          .append("text")
          .attr("y","-1.8em")
          .attr("dy", "1em")
          .style("text-anchor", "end")
          .attr("fill","currentColor")
          .text(options.x.label);
        var size = xAxis_container_text.node().getBBox();
        g.append("rect")
          .attr("width",size.width+4)
          .attr("height",size.height+4);
        g.node().appendChild(xAxis_container_text.node()); //put text last
          
        
        var yAxes = [];
        var lastwidth = 0;
        for (var i in scale.y) {
          var yAxis_container = chart.append("g").attr("class", "y axis").attr("transform","translate("+(-lastwidth)+",0)");
          
          var yAxis = d3.axisLeft().ticks(3);
          axes.y.push(yAxis);
          yAxis.tickFormat(tickformats.default(options.y[i]));
          switch (options.y[i].type) {
            case "byte":
            case "bps": {
              //set proper tick values
              var domain = scale.y[i].domain();
              var range = domain[1]-domain[0];
              
              
              var n = 3; //estimated amount of ticks
              var delta = Math.pow(2,Math.floor(Math.log2(range/(n)))); //space between ticks
              var ticks = [Math.floor(domain[0]/delta)*delta];
              while (ticks[0] + delta < domain[1]) {
                ticks.unshift(ticks[0] + delta);
              }
              yAxis.tickValues(ticks.reverse());
              
              //set proper formatting
              yAxis.tickFormat(tickformats[options.y[i].type]());
              break;
            }
            case "duration": {
              yAxis.tickFormat(tickformats.duration({scale:scale.y[i].domain()[1]}));
              break;
            }
          }
          if (options.y[i].format) {
            if (typeof options.y[i].format == "function") {
              yAxis.tickFormat(options.y[i].format);
            }
            else if (options.y[i].format in tickformats) { yAxis.tickFormat(tickformats[options.y[i].format](options.y[i])); }
          }
          yAxis.scale(scale.y[i]);
          yAxis_container.call(yAxis);
          
          yAxis.container = yAxis_container;
          yAxis.updateSize = function(){
            this.container.call(this);
          };
          
          yAxes.push(yAxis);
          
          if (("show" in options.y[i]) && (!options.y[i].show)) { yAxis_container.node().style.display = "none"; }
          else {
            lastwidth = yAxis_container.node().getBBox().width + options.margin;
            margin.left += lastwidth; 
          }
          
          var g = yAxis_container.append("g")
            .attr("class","label")
            .attr("transform", "rotate(-90)");
          var t = g.append("text")
            .attr("x",0)
            .attr("y","0.5em")
            .attr("dy", "1em")
            .style("text-anchor", "end")
            .attr("fill","currentColor")
            .text(options.y[i].label);
          var size = t.node().getBBox();
          g.append("rect")
            .attr("x",size.x-2)
            .attr("y",size.y-2)
            .attr("width",size.width+4)
            .attr("height",size.height+4);
          g.node().appendChild(t.node()); //put text last
          
        }
        
        var map = [];
        var map_series = [];
        var options_y_copy = [];
        
        for (var i in options.y) {
          options_y_copy.push(Object.assign({},options.y[i]));
          options_y_copy[i].series = [];
          function createAxisGradient(options) {
            if (!options.id) { options.id = Math.random().toString().slice(2); }
            if (!options.mode) { options.mode = "vertical"; }
            
            var start = options.stops[0][0];
            var end = options.stops[options.stops.length-1][0];
            var p1 = options.scale(start);
            var p2 = options.scale(end);
            
            var g = defs.append("linearGradient").attr("id","series_gradient_"+options.id)
              .attr("gradientUnits","userSpaceOnUse");
            if (options.mode == "vertical") {
              g.attr("x1",0).attr("y1",p1)
                .attr("x2",0).attr("y2",p2);
            }
            else {
              g.attr("y1",0).attr("x1",p1)
                .attr("y2",0).attr("x2",p2);
            }
            
            var range = end-start;
            function toperc(v) {
              return (v-start)/range;
            }
            for (var k in options.stops) {
              g.append("stop")
                .attr("offset",toperc(options.stops[k][0]))
                .attr("stop-color",options.stops[k][1]);
            }
            options.target.node().style.setProperty("--color","url(\"#series_gradient_"+options.id+"\")");
            
            return g;
          }
          function processSeries(s,i,j) {
            var scaley = scale.y[i];
            var container = data_container.append("g").attr("class","series").attr("data-axis",i).attr("data-index",j).attr("data-type",s.type).attr("data-label",s.label);
            
            if (s.animate) {
              container.node().classList.add("animate");
            }
            
            switch (s.type) {
              case "bar":{
                //everything is done in the update function
                break;
              }
              case "line":
              default: {
                if (("dots" in s) && (s.dots == false)) { container.node().classList.add("dots-hidden"); }
                if (("fill" in s) && (s.fill)) {
                  container.node().classList.add("show-fill");
                  if (s.fill == "fade") {
                    container.node().classList.add("fill-fade");
                  }
                }
                
                var components = {};
                
                components.line = container
                  .append("path")
                  .attr("class","line")
                  .datum(s.data)
                  .attr("d", d3.line()
                    .defined(function(d) { return isDefined(d.y); })
                    .x(function(d) { return scale.x(d.x); })
                    .y(function(d) { return scaley(d.y); })
                    .curve(s.step ? (s.step == "before" ? d3.curveStepBefore : (s.step == "after" ? d3.curveStepAfter : d3.curveStep )) : d3.curveMonotoneX)
                  );
                if (("line" in s) && (s.line == false)) { container.node().classList.add("line-hidden"); }
                
                components.fill = container
                  .append("path")
                  .attr("class","fill")
                  .attr("d",components.line.node().getAttribute("d")+"V"+scaley(scaley.domain()[0])+"H"+components.line.node().getBBox().x+"Z")
                
                components.dots = container.append("g").attr("class","dots");
                var dot = components.dots.selectAll(".dot")
                  .data(s.data)
                  .enter().append("circle")
                  .attr("class", "dot")
                  .attr("data-index",function(d,j,a){return j;});
                dot
                  .attr("cx",function(d,j,a) { return scale.x(d.x); })
                  .attr("cy",function(d,j,a) { return isDefined(d.y) ? scaley(d.y) : 0; });
                
                
                if ("color" in s) {
                  if (typeof s.color == "function") {
                    dot.attr("style",function(d,j,a) {
                      return "--color: "+s.color(d,j,options.raw,d.x,d.y,s)+";"
                    });
                  }
                  else {
                    container.node().style.setProperty("--color",s.color);
                  }
                }
                dot.filter(function(d,j,a){
                  return !isDefined(d.y);
                }).remove();
                
                if ("color" in s) {
                  if (s.color == "gradient") {
                    container.node().classList.add("color-gradient");
                    
                    components.axisGradient = createAxisGradient({
                      id: [i,j,Math.random().toString().slice(2)].join("_"),
                      stops: s.gradient,
                      scale: scaley,
                      target: container
                    });
                  }
                }
              }
            }
            
            options_y_copy[i].series[j].update = function(){
              container.selectAll("*").remove();
              
              switch (s.type) {
                case "bar":{
                  var amount_of_series = 0;
                  var index_of_this = 0;
                  for (var k in options.y) {
                    amount_of_series += options.y[k].series.length;
                    if (k < i) {
                      index_of_this += options.y[k].series.length;
                    }
                    else if (k == i) {
                      index_of_this += Number(j);
                    }
                  }
                  var w = scale.x.bandwidth()/amount_of_series;
                  var gs = container.selectAll(".bar")
                    .data(s.data)
                    .enter().append("g")
                    .attr("class","bar")
                    .attr("data-index",function(d,j,a){return j;});
                  gs.append("rect")
                    .attr("class","stroke")
                    .attr("x",function(d) { return (w*index_of_this)+scale.x(d.x); })
                    .attr("y",function(d) { return scaley(d.y); })
                    .attr("width",w)
                    .attr("height",function(d) { return height - scaley(d.y)+1; }); //the +1 hides the bottom stroke behind the bottom axis
                  gs.append("rect")
                    .attr("class","fill")
                    .attr("x",function(d) { return (w*index_of_this)+scale.x(d.x); })
                    .attr("y",function(d) { return scaley(d.y); })
                    .attr("width",w)
                    .attr("height",function(d) { return height - scaley(d.y); });
                  if (s.animate) {
                    //these CSS vars are used by the CSS animation
                    gs.style("--bar-y",function(d) { return scaley(d.y)+"px"; });
                    if (s.animate == "sequential") {
                      gs.style("--data-delay",function(d,j) { return 0.5+(j*0.5)+"s"; });
                    }
                    else {
                      gs.style("--data-delay","0.5s");
                    }
                  }
                  break;
                }
                case "line":
                default: {
                  
                  if (s.bands) {
                    for (var k in s.bands) {
                      /*
                       * to create a polygon between the upper and lower band, we're going to add two lines
                       * the lower data line will be in reverse order, so it starts n the right side and go left
                       * then we merge the two paths so that we get a polygon
                      */
                      var upper_data = [];
                      var lower_data = [];
                      var raw = options.raw;
                      if ("raw" in options_y_copy[i].series[j]) { raw = options_y_copy[i].series[j].raw; }
                      for (var n in raw) {
                        var xget = options.x.get;
                        if ("x" in options_y_copy[i].series[j]) { xget = options_y_copy[i].series[j].x; }
                        var x = xget(raw[n],n,raw,j);
                        var upper = s.bands[k].upper(raw[n],n,raw,j);
                        var lower = s.bands[k].lower(raw[n],n,raw,j);
                        if (isDefined(upper)) { //data should not be added if it is not continuous
                          upper_data.push({
                            x: x,
                            y: upper
                          });
                        }
                        if (isDefined(lower)) {
                          lower_data.push({
                            x: x,
                            y: lower
                          });
                        }
                      }
                      lower_data.reverse();
                      var p_upper = container.append("path")
                        .attr("class","band")
                        .attr("data-index",k)
                        .datum(upper_data)
                        .attr("d",d3.line()
                          //.defined(function(d) { return isDefined(d.y); })
                          .x(function(d) { return scale.x(d.x); })
                          .y(function(d) { return scaley(d.y); })
                          .curve(d3.curveMonotoneX)
                        );
                      var p_lower = container.append("path")
                        .attr("class","band")
                        .attr("data-index",k)
                        .datum(lower_data)
                        .attr("d",d3.line()
                        //.defined(function(d) { return isDefined(d.y); })
                          .x(function(d) { return scale.x(d.x); })
                          .y(function(d) { return scaley(d.y); })
                          .curve(d3.curveMonotoneX)
                        ).remove();
                      var d = p_lower.attr("d");
                      //add a straigt line from the end of the upper line to the start of the lower one, and add a z to close the path
                      d = "L"+d.slice(1)+"z";
                      p_upper.node().setAttribute("d",p_upper.attr("d")+d);
                      
                    }
                  }
                  
                  components.line = container
                    .append("path")
                    .attr("class","line")
                    .datum(s.data)
                    .attr("d", d3.line()
                      .defined(function(d) { return isDefined(d.y); })
                      .x(function(d) { return scale.x(d.x); })
                      .y(function(d) { return scaley(d.y); })
                      .curve(s.step ? (s.step == "before" ? d3.curveStepBefore : (s.step == "after" ? d3.curveStepAfter : d3.curveStep )) : d3.curveMonotoneX)
                    );
                  if (("line" in s) && (s.line == false)) { container.node().classList.add("line-hidden"); }
                  
                  components.fill = container
                    .append("path")
                    .attr("class","fill")
                    .attr("d",components.line.node().getAttribute("d")+"V"+scaley(scaley.domain()[0])+"H"+components.line.node().getBBox().x+"Z")
                  
                  components.dots = container.append("g").attr("class","dots");
                  var dot = components.dots.selectAll(".dot")
                    .data(s.data)
                    .enter().append("circle")
                    .attr("class", "dot")
                    .attr("data-index",function(d,j,a){return j;});
                  dot
                    .attr("cx",function(d,j,a) { return scale.x(d.x); })
                    .attr("cy",function(d,j,a) { return isDefined(d.y) ? scaley(d.y) : 0; });
                  
                  
                  if (("color" in s) && (typeof s.color == "function")) {
                    dot.attr("style",function(d,j,a) {
                      return "--color: "+s.color(d,j,options.raw,d.x,d.y,s)+";"
                    });
                  }
                  dot.filter(function(d,j,a){
                    return !isDefined(d.y);
                  }).remove();
                  
                  if ("color" in s) {
                    if (s.color == "gradient") {
                      container.node().classList.add("color-gradient");
                      
                      components.axisGradient = createAxisGradient({
                        id: [i,j,Math.random().toString().slice(2)].join("_"),
                        stops: s.gradient,
                        scale: scaley,
                        target: container
                      });
                    }
                  }
                  
                  if ("labels" in s) {
                    //print a label at every returned position
                    var is = s.labels(s.data,scale.x,scaley);
                    var g = container.append("g").attr("class","labels");
                    components.labels = g;
                    var xmax = scale.x(scale.x.domain()[1]);
                    for (var n in is) {
                      var sg = g.append("g").attr("class","label");
                      
                      sg.append("circle").attr("cx",scale.x(s.data[is[n]].x)).attr("cy",scaley(s.data[is[n]].y));
                      
                      var dir = "up";
                      if (scaley(s.data[is[n]].y) < options.margin) {
                        //label to close to upper chart edge
                        dir = "down";
                      }
                      else if (scaley(s.data[is[n]].y) > scaley(scaley.domain()[0]) - options.margin) {
                        //label to close to lower chart edge
                        dir = "up";
                      }
                      else {
                        var prev = is[n] > 0 ? is[n]-1 : is[n];
                        var next = is[n]+1 < s.data.length ? is[n]+1 : is[n];
                        var avg = (s.data[prev].y + s.data[is[n]].y + s.data[next].y)/3;
                        if (avg > s.data[is[n]].y) { dir = "down"; }
                      }
                      
                      var tx = scale.x(s.data[is[n]].x);
                      
                      var t = sg.append("text").text(yAxes[i].tickFormat()(s.data[is[n]].y))
                        .attr("x",tx)
                        .attr("y",scaley(s.data[is[n]].y)+(dir == "up" ? -10 : 10))
                        .attr("text-anchor","middle").attr("alignment-baseline",dir == "up" ? "baseline" : "hanging");
                      
                      if (tx - t.node().getBBox().width/2 < 0) {
                        t.attr("text-anchor","start");
                      }
                      else if (tx + t.node().getBBox().width/2 > xmax) {
                        t.attr("text-anchor","end");
                      }
                      
                      //label background
                      var size = t.node().getBBox();
                      sg.append("rect")
                        .attr("x",size.x-2)
                        .attr("y",size.y-2)
                        .attr("width",size.width+4)
                        .attr("height",size.height+4);
                      
                      sg.node().appendChild(t.node()); //put text last (thus above the rect)
                    }
                    
                  }
                  if (s.animate) {
                    components.line.node().style.setProperty("--path-length",components.line.node().getTotalLength());
                  }
                }
              }
              if (!("displayInTooltip" in s) || (s.displayInTooltip)) {
                var series_map = [];
                for (var k = 0; k < s.data.length; k++) {
                  var info = {
                    s: s,
                    i: k,
                    x: s.data[k].x,
                    y: s.data[k].y,
                    axis: i,
                    series: map_series.length
                  }
                  if (!isDefined(info.y,options_y_copy[info.axis].format)) { continue; }
                  info.xPos = scale.x(info.x)+(options.x.type == "text" ? scale.x.bandwidth()/2 : 0 );
                  info.yPos = scaley(info.y);
                  series_map.push(info);
                }
                map_series.push(series_map);
                map.push.apply(map,series_map);
              }
            };
          }
          if (options.y[i].icons) {
            var containers = [];
            if (!Array.isArray(options.y[i].icons)) { options.y[i].icons = [options.y[i].icons]; }
            function processIcons(i,j) {
              var container = icon_container.append("g").attr("class","icons").attr("data-axis",i).attr("data-index",j);
              containers.push(container);
              container.update = function(){
                this.selectAll("*").remove();
                options.y[i].icons[j].apply(this,[scale.x,scale.y[i]]);
              };
            }
            for (var j in options.y[i].icons) {
              if (typeof options.y[i].icons[j] == "function") {
                processIcons(i,j);
              }
            }
            options_y_copy[i].updateIcons = function(){
              for (var j in containers) {
                containers[j].update();
              }
            }
          }
          options_y_copy[i].update = function(){
            for (var j in this.series) {
              this.series[j].update();
            }
            if ("updateIcons" in this) {
              this.updateIcons();
            }
          }
          for (var j in options.y[i].series) {
            options_y_copy[i].series.push(Object.assign({},options.y[i].series[j]));
            processSeries(options.y[i].series[j],i,j);
          }
        }
        
        svg.updateSize = function(){
          var targetwidth = (options.width ? options.width : svg.getBoundingClientRect().width);
          var targetheight = (options.height ? options.height : svg.getBoundingClientRect().height);
          
          width = targetwidth - margin.left - margin.right;
          height = targetheight - margin.top - margin.bottom;
          
          chart_container
            .attr("width", targetwidth)
            .attr("height", targetheight);
          chart
            .attr("transform", "translate(" + margin.left + "," + margin.top + ")");
          
          mask.updateSize();
          
          chartArea
            .attr("width",width)
            .attr("height",height);
          
          if (options.flip) {
            cursor.attr("d","M0 0,h"+width);
          }
          else {
            cursor.attr("d","M0 0,v"+height);
          }
          
          data_container_rect
            .attr("width",width)
            .attr("height",height);
          
          scale.x.range([ 0, width]);
          for (var i in scale.y) {
            scale.y[i].range([ height, 0]);
          }
          
          xAxis_container_text
            .attr("x",width);
          xAxis_container  
            .attr("transform","translate(0,"+height+")");
          xAxis_container.call(xAxis);
          
          var size = xAxis_container_text.node().getBBox();
          xAxis_container.selectAll("g.label > rect")
            .attr("x",size.x-2)
            .attr("y",size.y-2);
          
          for (var i in yAxes) {
            yAxes[i].updateSize();
          }
          map.length = 0; //clear the array without breaking references
          map_series.length = 0;
          //console.log("cleared");
          for (var i in options_y_copy) {
            options_y_copy[i].update();
          }
        };
        svg.updateSize();
        if (!options.width || !options.height) {
          var tracksize = false;
          var resizetimer = false;
          var resizeObserver = new ResizeObserver(function(entries,observer){
            for (var i in entries) {
              if (entries[i].target == options.container) {
                if (!tracksize) {
                  tracksize = {
                    w: entries[i].contentRect.width,
                    h: entries[i].contentRect.height
                  };
                }
                else if ((entries[i].contentRect.width != tracksize.w) || (entries[i].contentRect.height != tracksize.h)) {
                  tracksize.w = entries[i].contentRect.width;
                  tracksize.h = entries[i].contentRect.height;
                  if (resizetimer) { clearTimeout(resizetimer); }
                  resizetimer = setTimeout(function(){
                    svg.updateSize();
                    resizetimer = false;
                  },200);
                }
              }
            }
          });
          resizeObserver.observe(options.container,{box:"border-box"});
        }
        
        
        //sort map by xpos
        var getpos = options.flip ? "yPos" : "xPos";
        map.sort(function(a,b){
          return a[getpos] - b[getpos];
        });
        var style = document.createElement("style");
        defs.node().appendChild(style);
        
        var lastx = false;
        var lastt = 0;
        var cursors = [];
        chartArea.node().addEventListener("mouseover",function(e){
          tooltip.style.display = "";
          if (cursor) cursor.node().style.display = "";
          cursors = Array.from(document.querySelectorAll(".chart .cursor[data-axis=\""+cursor.node().getAttribute("data-axis")+"\"]"));
          for (var i = 0; i < cursors.length; i++) {
            cursors[i].style.display = "";
          }
        });
        chartArea.node().addEventListener("mousemove",function(e){
          var w = tooltip.parentNode.getBoundingClientRect();
          if (e.pageX > w.width/2) {
            tooltip.style.right = w.width - e.pageX+"px";
            tooltip.style.left = "";
          }
          else {
            tooltip.style.left = e.pageX+"px";
            tooltip.style.right = "";
          }
          tooltip.style.top = e.pageY+"px";
          
          if (new Date().getTime() - lastt < 100) { return; }
          
          var parent = this.getBoundingClientRect();
          var pos = {
            x: e.offsetX-margin.left,
            y: e.offsetY-margin.top
          };
          
          var xPos = options.flip ? pos.y : pos.x;
          
          if ((lastx !== false) && (Math.abs(lastx - xPos) < 2)) { return; }
          lastx = xPos;
          lastt = new Date().getTime();
          
          function findClosest(array,match,lookupfunc) {
            if (array.length == 0) { return false; }
            var low = 0;
            if (lookupfunc(array[low]) > match) { return low; }
            var high = array.length-1;
            if (lookupfunc(array[high]) < match) { return high; }
            
            while (high - low > 1) {
              var middle = Math.floor((low+high)/2);
              var value = lookupfunc(array[middle]);
              if (value < match) {
                low = middle;
              }
              else {
                high = middle;
              }
            }
            
            return match - lookupfunc(array[low]) < lookupfunc(array[high]) - match ? low : high;
          }
          var getpos = "xPos"; //options.flip ? "yPos" : "xPos";
          var xi = findClosest(map,xPos,function(a){ return a[getpos]; });
          if (xi === false) { tooltip.set(""); return false; }
          xPos = map[xi][getpos];
          
          if (cursor) {
            for (var i = 0; i < cursors.length; i++) {
              cursors[i]._set(map[xi].x);
            }
          }
          
          var tolerance = 5; //in pixels
          var matches = [];
          if (map_series.length == 1) {
            //find fuzzy matches
            var n = 10; //max how many items do we want
            for (var i = Math.max(0,xi - n); i < map.length; i++) {
              if (map[i][getpos] > xPos + tolerance*0.5) { break; }
              if (map[i][getpos] > xPos - tolerance*0.5) {
                matches.push(map[i]);
              }
            }
            if (matches.length > n) {
              matches.sort(function(a,b){
                return Math.abs(a[getpos] - xPos) - Math.abs(b[getpos] - xPos);
              });
              matches = matches.slice(0,n);
              matches.sort(function(a,b){ return a[getpos] - b[getpos] }) //resort by xpos
            }
          }
          else {
            //allow 1 match per series
            matches.push(map[xi]);
            for (var s = 0; s < map_series.length; s++) {
              if (s == map[xi].series) { continue; } //we found it already
              var i = findClosest(map_series[s],xPos,function(a){ return a[getpos]; });
              if (Math.abs(map_series[s][i][getpos] - xPos) > tolerance*0.5) { continue; } //its outside of the tolerance
              matches.push(map_series[s][i]);
            }
            matches.sort(function(a,b){ return a.series - b.series; });
          }
          
          var e = new Event("plotHover",{
            bubbles: true,
            cancelable: true
          });
          e.info = {x: map[xi].x, matches: matches};
          svg.dispatchEvent(e);
          
          if ((options.tooltip) && (typeof options.tooltip == "function")) {
            tooltip.set(options.tooltip.call(tooltip,map[xi].x,matches));
          }
          else {
            var f = document.createDocumentFragment();
            
            var table = document.createElement("table");
            f.appendChild(table);
            var tr = document.createElement("tr");
            table.appendChild(tr);
            var td = document.createElement("th");
            tr.appendChild(td);
            td.appendChild(document.createTextNode(options.x.label+":"));
            var td = document.createElement("th");
            td.style.textAlign = "right";
            tr.appendChild(td);
            var xf = axes.x.tickFormat() ? axes.x.tickFormat() : (scale.x.tickFormat ? scale.x.tickFormat() : (v)=>{return v;});
            td.appendChild(document.createTextNode(xf(map[xi].x)));
            
            var css = [];
            for (var i in matches) {
              var m = matches[i];
              
              var tr = document.createElement("tr");
              table.appendChild(tr);
              var td = document.createElement("td");
              tr.appendChild(td);
              td.appendChild(document.createTextNode(m.s.label+":"));
              var td = document.createElement("td");
              td.style.textAlign = "right";
              tr.appendChild(td);
              td.appendChild(document.createTextNode(yAxes[m.axis].tickFormat()(m.y)));
              
              css.push("#data"+uid+" .series[data-index=\""+m.series+"\"] .dots .dot[data-index=\""+m.i+"\"] { opacity: 1; r: var(--larger-radius); display: block; }"); //highlight dots
              css.push("#data"+uid+" .series[data-type=\"bar\"] .bar[data-index=\""+m.i+"\"] .fill { fill-opacity: 0.8; }"); //highlight bars
            }
            style.innerText = css.join("\n");
            
            tooltip.set(f);
          }
        });
        chartArea.node().addEventListener("mouseout",function(e){
          tooltip.style.display = "none";
          if (cursor) cursor.node().style.display = "none";
          style.innerText = "";
          lastx = false;
          for (var i = 0; i < cursors.length; i++) {
            cursors[i].style.display = "none";
          }
          cursors = [];
        });
        
        var children = Array.from(data_container.node().children).filter(function(a){return a.classList.contains("series")});
        var allseries = [];
        for (var i in options.y) {
          for (var j in options.y[i].series) {
            if (("displayInLegend" in options.y[i].series[j]) && !options.y[i].series[j].displayInLegend) {
              allseries.push(null);
            }
            else {
              allseries.push(options.y[i].series[j]);
            }
          }
        }
        if (allseries.length > 1) {
          var legend = chart.append("g")
            .attr("class","legend");
          var backgr = legend.append("rect")
            .attr("width","1em");
          var g = legend.append("g");
          var em = backgr.node().getBBox().width;
          var m = 0;
          for (var n = 0; n < allseries.length; n++) {
            if (allseries[n] === null) { continue; }
            var s = g.append("g")
              .attr("class",children[n].getAttribute("class"))
              .attr("data-type",children[n].getAttribute("data-type"))
              .attr("data-label",children[n].getAttribute("data-label"));
            if (allseries[n].color) {
              if (allseries[n].color == "gradient") {
                s.node().style.setProperty("--color","url(#legend_gradient_"+n+")");
                createAxisGradient({
                  id: [n,Math.random().toString().slice(2)].join("_"),
                  scale: function(v){
                    //at 0.1em, show the first stop
                    //at 0.8em, show the last stop
                    var start = allseries[n].gradient[0][0];
                    var end = allseries[n].gradient[allseries[n].gradient.length-1][0];
                    
                    var alpha = 0.8 / (end - start);
                    var beta = 0.1 - start * alpha;
                    return (alpha * v + beta)*em;
                  },
                  stops: allseries[n].gradient,
                  target: s,
                  mode: "horizontal"
                });
              }
              else {
                s.node().style.setProperty("--color",children[n].style.getPropertyValue("--color"));
              }
            }
            
            var r = s.append("rect")
              .attr("class","fill")
              .attr("width","1em")
              .attr("height","0.5em");
            var p = s.append("path")
              .attr("class","line")
              .attr("d","M0 0h"+em);
            var d = s.append("circle")
              .attr("class", "dot");
            var t = s.append("text")
              .text(options.y[children[n].getAttribute("data-axis")].series[children[n].getAttribute("data-index")].label);
            if (!options.legend) {
              options.legend = {};
            }
            if ((!options.legend.layout) || (options.legend.layout == "vertical")) {
              r.attr("transform","translate("+0.5*em+","+(m*1.2+0.9)*em+")");
              p.attr("transform",r.attr("transform"));
              d.attr("cx","1em")
                .attr("cy",(m*1.2+0.9)+"em");
              t.attr("y",(m*1.2+1.2)+"em")
                .attr("x","1.8em");
              m++;
            }
            else {
              r.attr("transform","translate("+(m+0.5*em)+","+0.5*em+")");
              p.attr("transform",r.attr("transform"));
              d.attr("cy","0.5em")
                .attr("cx",m+1*em);
              t.attr("x",m+1.8*em)
                .attr("y","1em");
              m += t.node().getBBox().width + 2.5*em;
            }
          }
          if ((!options.legend.layout) || (options.legend.layout == "vertical")) {
            backgr.attr("height",(m*1.2+0.4)+"em")
              .attr("width","calc("+legend.node().getBBox().width+"px + 1.2em)");
          }
          else {
            backgr.attr("height",legend.node().getBBox().height+0.5*em)
              .attr("width",m);
          }
          
          switch (options.legend.position) {
            case "top":
            case "bottom":
            case "left":
            case "right": {
              svg.appendChild(legend.node());
              switch (options.legend.position) {
                case "top": {
                  margin.top += backgr.node().getBBox().height + options.margin;
                  legend.node().style.setProperty("transform","translate(calc(50% - "+legend.node().getBBox().width/2+"px),"+(options.margin)+"px)");
                  break;
                }
                case "bottom": {
                  margin.bottom += backgr.node().getBBox().height + options.margin;
                  legend.node().style.setProperty("transform","translate(calc(50% - "+legend.node().getBBox().width/2+"px),calc(100% - "+(legend.node().getBBox().height+options.margin)+"px))");
                  break;
                }
                case "left": {
                  margin.left += backgr.node().getBBox().width + options.margin;
                  legend.node().style.setProperty("transform","translate("+options.margin+"px,calc(50% - "+legend.node().getBBox().height/2+"px)");
                  break;
                }
                case "right":
                default: {
                  margin.right += backgr.node().getBBox().width + options.margin;
                  legend.node().style.setProperty("transform","translate(calc(100% - "+(legend.node().getBBox().width+options.margin)+"px),calc(50% - "+legend.node().getBBox().height/2+"px)");
                }
              }
              svg.updateSize();
              break;
            }
            case "bottomleft": {
              legend.attr("transform","translate("+1*em+","+(height - backgr.node().getBBox().height - em)+")");
              break;
            }
            case "topright": {
              legend.attr("transform","translate("+(width - backgr.node().getBBox().width)+",0)");
              break;
            }
            case "bottomright": {
              legend.attr("transform","translate("+(width - backgr.node().getBBox().width)+","+(height - backgr.node().getBBox().height - 2*em)+")");
              break;
            }
            default:
            case "topleft": {
              legend.attr("transform","translate("+2*em+",0)");
              break;
            }
          }
          
          var t = ["mouseover","mousemove","mouseout"];
          for (var i in t) {
            legend.node().addEventListener(t[i],function(e){
              chartArea.node().dispatchEvent(new MouseEvent(e.type,e)); //pass event
              e.stopPropagation();
            });
          }
        }
      }
    }
  }
  catch (e){
    console.error(e);
    chart_container.selectAll("*").remove();
    chart_container.append("text")
      .attr("x","50%")
      .attr("y","50%")
      .attr("dy","-0.5em")
      .style("text-anchor", "middle")
      .text(e);
  }
  return svg;
}
