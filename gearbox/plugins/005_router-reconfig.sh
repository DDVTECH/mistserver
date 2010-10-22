#!/bin/bash

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
            update $target "ROUTER_SOURCE" "direct"
            update $target "INPUT" "direct"
            local oldval=`get_usedby "config_direct_${streams[j]}"`
            update "config_direct_${streams[j]}" "ROUTER_USEDBY" "$(($oldval + 1))"
          else
            local assigned="0"
            local k=0
            for (( k=0; k<${#servers[@]}; k++ )); do
              local tempisup="${servers[k]}_isup"
              local tempisup=${!tempisup}
              if [ "$tempisup" == "1" ]; then
                local l=0
                local thisstreams="server_${servers[k]}"
                local thisstreams=( ${!thisstreams} )
                for (( l=0; l<${#thisstreams[@]}; l++ )); do
                  if [ "$assigned" == "0" ]; then
                    local source="config_${servers[k]}_${thisstreams[l]}"
                    thisused=`get_usedby $source`
                    if [ "`get_usedby $source`" -lt "`get_branching`" ]; then
                      local oldval=`get_usedby $source`
                      update $source "ROUTER_USEDBY" "$(($oldval + 1))"
                      update "config_${servers[i]}_${streams[j]}" "ROUTER_SOURCE" "${servers[k]}"
                      update "config_${servers[i]}_${streams[j]}" "INPUT" "${servers[k]}"
                      local assigned="1"
                    fi
                  fi
                done
              fi
            done
          fi
        fi
      done
    fi
  done
}

check_direct
reconfig
