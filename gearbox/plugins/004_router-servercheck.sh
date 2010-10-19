#!/bin/bash
server1_isup="0"

update( ) {
  local target=$1
  local var=$2
  local value=$3
  local string=${!target}
  local length=${#var}
  local loopstring=${string:i:length}
  local i=0
  for ((i=0; i<${#string}; i++ )); do
    local loopstring=${string:i:length}
    if [ "$loopstring" == "$var" ]; then
      break;
    fi
  done
  if [ "$loopstring" != "$var" ]; then
    eval $target="\"${!target} ${var}=${value}\""
  else
    local varstring=${string:i}
    local limit=`expr index "$varstring" ' '`
    local newstring="${var}=${value}"
    if [ "$limit" != "0" ]; then
      eval $target="\"${string/${varstring:0:(limit-1)}/$newstring}\""
    else
      eval $target="\"${string/${varstring}/$newstring}\""
    fi
  fi
  local string=${!target}
}

get_source( ) {
  local ${!1}
  echo $ROUTER_SOURCE
}

get_usedby( ) {
  local ${!1}
  echo $ROUTER_USEDBY
}

check_servers( ) {
  local i=0
  for ((i=0; i<${#servers[@]}; i++ )); do
    local isup="${servers[i]}_isup"
    local isup=${!isup}
    if [ "$isup" == "0" ]; then
      local j=0
      local streams="server_${servers[i]}"
      local streams=( ${!streams} )
      for ((j=0; j<${#streams[@]}; j++)); do
        local target="config_${servers[i]}_${streams[j]}"
        local sourceval=`get_source $target`
        if [ "$sourceval" != "" ]; then
          local target="config_${sourceval}_${streams[j]}"
          local usage=`get_usedby $target`
          local usage=$((${usage}-1))
          update $target "ROUTER_USEDBY" $usage
        fi
        local target="config_${servers[i]}_${streams[j]}"
        update $target "INPUT" ""
        update $target "ROUTER_SOURCE" ""
        update $target "ROUTER_USEDBY" "0"
      done
    else
      local j=0
      local streams="server_${servers[i]}"
      local streams=( ${!streams} )
      for ((j=0; j<${#streams[@]}; j++)); do
        local target="config_${servers[i]}_${streams[j]}"
        local sourceval=`get_source $target`
        if [ "$sourceval" != "" ]; then
          local sourceup="${sourceval}_isup"
          local sourceup=${!sourceup}
          if [ "$sourceup" == "0" ]; then
            update $target "ROUTER_SOURCE" ""
            update $target "INPUT" ""
          fi
        fi
      done
    fi
  done
}

print_conf( ) {
  local i=0
  local allstreams=""
  for (( i=0; i < ${#unique_groups[@]}; i++ )); do
    local add="streams_${unique_groups[i]}"
    local allstreams="${allstreams} ${!add}"
  done
  local allstreams=( $allstreams )
  for ((i=0; i < ${#allstreams[@]}; i++ )); do
    local target="config_direct_${allstreams[i]}"
    echo "${target}: ${!target}"
  done
  for ((i=0; i<${#servers[@]}; i++ )); do
    local streams="server_${servers[i]}"
    local streams=( ${!streams} )
    local j=0
    for ((j=0; j<${#streams[@]}; j++ )); do
      local target="config_${servers[i]}_${streams[j]}"
      echo "${target}: ${!target}"
    done
  done
}

check_servers
