
var screenlog = {
  master: function(){
    function gettab() {
      return location.hash.substring(1).split('@')[1];
    }
    
    //which field has focus
    $(document).on('focus','.field',function(){
      screenlog.log.mine.push({
        type: 'focus',
        time: (new Date()).getTime(),
        tab: gettab(),
        element: $(this).attr('name')
      });
    });
    
    //what is the value of this field on blur
    $(document).on('blur','.field',function(){
      screenlog.log.mine.push({
        type: 'blur',
        time: (new Date()).getTime(),
        tab: gettab(),
        element: $(this).attr('name'),
        val: $(this).getval()
      });
    });
    
    //where is the cursor
    var mouse = {
      x: 0,
      y: 0
    };
    var lastmouse = mouse;
    $(document).on('mousemove',function(e){
      mouse = {
        x: e.pageX,
        y: e.pageY
      };
    });
    setInterval(function(){
      if ((mouse.x != lastmouse.x) || (mouse.y != lastmouse.y)) {
        lastmouse = mouse;
        screenlog.log.mine.push({
          type: 'mousepos',
          time: (new Date()).getTime(),
          tab: gettab(),
          pos: mouse
        });
      }
    },1e3);
    
    //which tab are we on
    $(window).on('hashchange', function(e) {
      screenlog.log.mine.push({
        type: 'tab',
        time: (new Date()).getTime(),
        tab: gettab()
      });
    });
    
    var lastsend;
    function sendlog(){
      var l = [];
      for (var i = screenlog.log.mine.length-1; i >= 0; i--) {
        var entry = screenlog.mine.log[i];
        if (entry.time <= lastsend) {
          break;
        }
        l.unshift(entry);
      }
      if (l.length == 0) {
        return;
      }
      var obj = {
        url: 'http://shop.mistserver.org/demo?store',
        data: {
          serverurl: mist.user.host,
          data: JSON.stringify(l)
        },
        method: 'POST',
        error: function(){
          
        },
        success: function(d){
          lastsend = d;
        }
      };
      log('[screenlog]','Send:',l);
      var http = $.ajax(obj);
    }
    setInterval(sendlog,2e3)
    
  },
  slave: function(sid,token){
    
    var lastgotten = 0;
    function getlog() {
      var obj = {
        url: 'http://shop.mistserver.org/demo',
        data: {
          read: sid,
          token: token,
          from: lastgotten
        },
        method: 'POST',
        error: function(){
          
        },
        success: function(d){
          var newlog = JSON.parse(d);
          
          if ('error' in newlog) {
            log('[screenlog]','error',newlog);
            return;
          }
          
          if (newlog.length == 0) { return; }
          
          log('[screenlog]','Receive:',newlog);
          
          if (lastgotten == 0) {
            var d = {blur:[]};
            var done = false;
            for (var i = newlog.length-1; i>=0; i--) {
              var entry = newlog[i];
              switch (entry.type) {
                case 'mousepos':
                case 'focus':
                  if (entry.type in d) {
                    break;
                  }
                  d[entry.type] = entry;
                  break;
                case 'blur':
                  d.blur.unshift(entry);
                  break;
                case 'tab':
                  d[entry.type] = entry;
                  done = true;
                  break;
              }
              if (done) { break; }
            }
            for (var i in d) {
              if (i == 'blur') {
                for (var j in d[i]) {
                  playentry(d[i][j]);
                }
              }
              else {
                playentry(d[i]);
              }
            }
          }
          else {
            playlog(newlog);
          }
          screenlog.log.theirs = screenlog.log.theirs.concat(newlog);
          
          var amount = 50; //how many entries should be shown in the onscreen log
          var show = newlog.slice(-1*amount);
          for (var i in show) {
            var entry = show[i];
            var d = $.extend({},entry);
            delete d.time; delete d.type; delete d.tab;
            UI.elements.screenlog.append(
              $('<div>').addClass('entry').text(
                '['+UI.format.time(entry.time/1e3)+'] '+entry.type+' '+JSON.stringify(d)+' @ '+entry.tab
              )
            );
          }
          $entries = UI.elements.screenlog.children('.entry');
          if ($entries.length > amount) {
            $entries.slice(0,$entries.length-amount).remove();
          }
          UI.elements.screenlog.scrollTop(UI.elements.screenlog[0].scrollHeight);
          
          lastgotten = newlog[newlog.length-1].time;
        }
      };
      var http = $.ajax(obj);
    }
    
    this.elements.cursor = $('<div>').attr('id','cursor');
    $('body').append(this.elements.cursor);
    UI.elements.screenlog = $('<div>').attr('id','screenlog');
    UI.elements.menu.before(UI.elements.screenlog);
    
    
    getlog();
    setInterval(getlog,2e3);
    
    
    function playlog(l) {
      var delay = 0;
      for (var i in l) {
        if (i != 0) {
          delay = l[i].time - l[i-1].time;
        }
        setTimeout(playentry(l[i]),delay);
      }
    }
    function playentry(entry){
      switch (entry.type) {
        case 'mousepos':
          screenlog.elements.cursor.animate({
            'left': entry.pos.x+'px',
            'top': entry.pos.y+'px'
          },0.5e3);
        break;
        case 'focus':
          $('.field.hasFocus').removeClass('hasFocus');
          $('.field[name="'+entry.element+'"]').addClass('hasFocus');
        break;
        case 'blur':
          $('.field.hasChanged').removeClass('hasChanged');
          $('.field[name="'+entry.element+'"]').setval(entry.val).addClass('hasChanged').removeClass('hasFocus');
        break;
        case 'tab':
          location.hash = location.hash.split('@')[0] + '@' + entry.tab;
        break;
      }
    }
    
  },
  log: {
    mine: [],
    theirs: []
  },
  elements: {}
};
