#!/bin/bash
get_stream_bitrate() {

  local temp="status_$1_$2_$3"
  local ${!temp}
  echo $BITRATE
}

get_server_bitrate() {
  local temp="server_$2"
  local temp=( ${!temp} )
  local bitrate=0
  local i=0
  for((i=0; i<${#temp[@]}; i++)); do
    local meh=`get_stream_bitrate $1 ${temp[i]} $2`
    local bitrate=$(($bitrate + $meh))
  done
  echo $bitrate
}

get_proc( ) {
  local capacity=limits_$1
  local ${!capacity}
  local currentbitrate=`get_server_bitrate $2 $1`
  echo $((($currentbitrate*100)/$MAX_BW))
}

calc_totals() {
  balancer_total_bitrate=0
  local i=0
  for ((i=0; i<${#servers[@]}; i++)); do
    local servernaam=${servers[i]}
    local groupnaam=${groups[i]}
    local streams=server_$servernaam
    local streams=( ${!streams} )
    for((j=0; j<${#streams[@]}; j++)); do
      local streambitrate=`get_stream_bitrate ${groupnaam} ${streams[j]} $servernaam`
      balancer_total_bitrate=$(($balancer_total_bitrate + $streambitrate))
      local servertemp=balancer_total_bitrate_${servernaam}
      eval $servertemp=$((${!servertemp} + $streambitrate))
      local grouptemp=balancer_total_bitrate_${groupnaam}
      eval $grouptemp=$((${!grouptemp} + $streambitrate))
    done
    local proc="balancer_bitrate_${servernaam}_proc"
    local val=`get_proc $servernaam $groupnaam`
    eval $proc=$val
  done
}

print_perc() {
  local i=0
  for ((i=0; i<${#servers[@]}; i++)); do
    local servernaam=${servers[i]}
    local groupnaam=${groups[i]}
    local capacity=limits_${servernaam}
    local ${!capacity}
    local currentbitrate=`get_server_bitrate $groupnaam $servernaam`
    local temp=`get_proc $servernaam $groupnaam`
    local streams=server_$servernaam
    local streams=( ${!streams} )
    echo "Group '${groupnaam}' -- Server '${servernaam}'"
    echo "Currently serving ${temp}% of it's maximum bandwith ( $currentbitrate / ${MAX_BW} )"
  done
}

get_free( ) {
  temparray=()
  local i=0
  for(( i=0; i<${#servers[@]}; i++ )); do
    checkserv=$i
    if [ "${groups[i]}" == "$1" ]; then
      tempvar="server_${servers[i]}"
      tempvar=( ${!tempvar} )
      for(( j=0; j<${#tempvar[@]}; j++ )); do
        if [ "${tempvar[j]}" == "$2" ]; then
          checkserv=""
        fi
        break
      done
      temparray=( "${temparray[@]}" "$checkserv" )
    fi
  done
  echo ${temparray[@]}
}

balance( ) {
  local i=0
  for (( i=0; i<${#servers[@]}; i++ )); do
    local capacity=limits_${servers[i]}
    local ${!capacity}
    local temp=`get_proc ${servers[i]} ${groups[i]}`
    if [[ $temp -ge 100 ]]; then
      add_alert "Balancing" "Server '${servers[i]}' reached it's maximum capacity, starting to balance."
      local streams=server_${servers[i]}
      local streams=( ${!streams} )
      local maxbitrate=0
      local maxstream=-1
      local j=0
      for (( j=0; j<${#streams[@]}; j++ )); do
        local val=`get_stream_bitrate ${groups[i]} ${streams[j]} ${servers[i]}`
        if [[ $val -gt $maxbitrate ]]; then
          local maxbitrate=$val
          local maxstream=$j
        fi
      done
      local freeserver=`get_free ${groups[i]} ${streams[maxstream]}`
      local freeserver=( ${freeserver} )
      local leaststream=0
      local leastbitrate=`get_proc ${servers[${freeserver[0]}]} ${groups[${freeserver[0]}]}`
      for (( j=0; j<${#freeserver[@]}; j++ )); do
        local tempbitrate=`get_proc ${servers[${freeserver[j]}]} ${groups[${freeserver[j]}]}`
        if [[ $tempbitrate -le $leastbitrate ]]; then
          local leaststream=$j
          local leastbitrate=$tempbitrate
          local leastserver=${freeserver[j]}
        fi
      done
      target="server_${servers[$leastserver]}"
      currentval=${!target}
      newval="${currentval} ${streams[maxstream]}"
      eval $target=\"${newval}\"
      add_alert "Balancing" "  Stream '${streams[maxstream]}' added on server '${servers[leastserver]}'"
    fi
  done
}

calc_totals
balance
