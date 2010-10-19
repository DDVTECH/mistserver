#!/bin/bash

###TODO: If no direct slots free: check server per server for free slots, if there are, set
###      ROUTER_SOURCE of current stream to server with free slot

plugsett_router="BRANCHING=2"

get_branching( ) {
  local $plugsett_router
  echo $BRANCHING
}

check_direct() {
  local i=0
  local allstreams=""
  for (( i=0; i < ${#unique_groups[@]}; i++ )); do
    local add="streams_${unique_groups[i]}"
    local allstreams="${allstreams} ${!add}"
  done
  local allstreams=( $allstreams )
  for ((i=0; i < ${#allstreams[@]}; i++ )); do
    local target="config_direct_${allstreams[i]}"
    local usedby=`get_usedby $target`
    if [ "$usedby" == "" ]; then
      update $target "ROUTER_USEDBY" "0"
    fi
  done
  local j=0
  for ((i=0; i<${#servers[@]}; i++ )); do
    local streams="server_${servers[i]}"
    local streams=( ${!streams} )
    for ((j=0; j<${#streams[@]}; j++)); do
      local target="config_${servers[i]}_${streams[j]}"
      local usedby=`get_usedby $target`
      if [ "$usedby" == "" ]; then
        update $target "ROUTER_USEDBY" "0"
      fi
    done
  done
}


reconfig() {
  local i=0
  local j=0
  for ((i=0; i<${#servers[@]}; i++ )); do
    local isup="${servers[i]}_isup"
    local isup=${!isup}
    if [ "$isup" == "1" ]; then
      local streams="server_${servers[i]}"
      local streams=( ${!streams} )
      for ((j=0; j<${#streams[@]}; j++)); do
        local target="config_${servers[i]}_${streams[j]}"
        local sourceval=`get_source $target`
        if [ "$sourceval" == "" ]; then
          if [ "`get_usedby "config_direct_${streams[j]}"`" -lt "`get_branching`" ]; then
            echo "${servers[i]} - ${streams[j]}: No source specified, but direct slot free"
            update $target "ROUTER_SOURCE" "direct"
            update $target "INPUT" "direct"
            local oldval=`get_usedby "config_direct_${streams[j]}"`
            update "config_direct_${streams[j]}" "ROUTER_USEDBY" "$(($oldval + 1))"
          fi
        fi
      done
    fi
  done
}

print_conf
check_direct
reconfig
print_conf
