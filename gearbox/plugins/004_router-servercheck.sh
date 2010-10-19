#!/bin/bash
config_direct_alpha=""
config_direct_beta=""
config_direct_gamma=""
config_direct_delta=""
config_direct_epsilon=""
config_direct_omega=""
config_direct_theta=""
config_direct_pi=""
config_direct_meh=""
config_direct_bla=""
config_direct_koekjes=""

config_server1_alpha=""
config_server1_beta=""
config_server1_gamma=""
config_server1_delta=""
config_server2_alpha=""
config_server2_beta=""
config_server2_gamma=""
config_server2_delta=""
config_server3_gamma=""
config_server3_alpha=""
config_server3_beta=""
config_server3_delta=""
config_server4_alpha=""
config_server4_beta=""
config_server4_gamma=""
config_server4_delta=""
config_server5_epsilon=""
config_server5_omega=""
config_server6_epsilon=""
config_server6_omega=""
config_server7_epsilon=""
config_server7_omega=""
config_server8_theta=""
config_server8_pi=""
config_server8_meh=""
config_server9_theta=""
config_server9_pi=""
config_server9_meh=""
config_server10_bla=""
config_server10_koekjes=""

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
  local conf="config_$1_$2"
  local ${!conf}
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
        local sourceval=`get_source ${servers[i]} ${streams[j]}`
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
        local sourceval=`get_source ${servers[i]} ${streams[j]}`
        if [ "$sourceval" != "" ]; then
          local sourceup="${sourceval}_isup"
          local sourceup=${!sourceup}
          if [ "$sourceup" == "0" ]; then
            local target="config_${servers[i]}_${streams[j]}"
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

print_conf
check_servers
print_conf
