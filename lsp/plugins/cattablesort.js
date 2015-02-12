// by Carina van der Meer for DDVtech,
// based off the "Stupid jQuery table plugin" by JoeQuery (http://joequery.github.io/Stupid-Table-Plugin/)

(function($){
  $.fn.stupidtable = function(){
    var $table = $(this);
    $table.on('click', 'thead th', function() {
      $(this).stupidsort();
    });
  }
  $.fn.stupidsort = function(){
    var $th = $(this);
    var $table = $th.closest('table');
    var $tbody = $table.children('tbody');
    var $trs = $tbody.children('tr');
    var datatype = $th.attr('data-sort-type');
    if (!datatype) { return; } //no data type set? => don't sort by this column
    var sortasc = true;
    if ($th.hasClass('sorting-asc')) { sortasc = false; }
    
    //find the index of the column that needs sorting
    var col_index = 0;
    $th.prevAll().each(function(){
      var colspan = $(this).attr('colspan');
      col_index += (colspan ? Number(colspan) : 1);
    });
    
    //a function to return the values that need sorting
    function getsortval(tr) {
      var $tds = $(tr).children('td,th');
      
      //find the correct td
      var i = 0;
      var $td;
      $tds.each(function(){
        if (i == col_index) {
          $td = $(this);
          return false; //break
        }
        var colspan = $(this).attr('colspan');
        i += (colspan ? Number(colspan) : 1);
      });
      
      //get the value
      var val;
      if (typeof $td.data('sort-value') != 'undefined') {
        val = $td.data('sort-value');
      }
      else if (typeof $td.attr('data-sort-value') != 'undefined') {
        val = $td.attr('data-sort-value');
      }
      else {
        val = $td.text();
      }
      //cast to the datatype
      switch (datatype) {
        case 'string':
        case 'string-ins':
          //always sort strings case insensitive
          val = String(val).toLowerCase();
          break;
        case 'int':
          val = parseInt(Number(val));
          break;
        case 'float':
          val = Number(val);
          break;
      }
      return val;
    }
    
    //do the actual sort
    $trs.sort(function(a,b){
      var factor = (sortasc ? 1 : -1);
      a = getsortval(a);
      b = getsortval(b);
      if (a > b) { return factor * 1; }
      if (a < b) { return factor * -1; }
      return 0;
    })
    $tbody.append($trs);
    
    $table.find('thead th').removeClass('sorting-asc').removeClass('sorting-desc')
    $th.addClass((sortasc ? 'sorting-asc' : 'sorting-desc'));
  }
})(jQuery);