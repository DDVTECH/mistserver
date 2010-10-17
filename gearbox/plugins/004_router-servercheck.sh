#!/bin/bash
config_server1_alpha="INPUT=bla ROUTER_SOURCE=meh ROUTER_USEDBY=0"
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

update( ) {
  local target=$1
  local var=$2
  local value=$3
  local string=${!target}
  local length=${#var}
  local loopstring=${string:i:length}
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
    eval $target="\"${string/${varstring:0:(limit-1)}/$newstring}\""
  fi
  local string=${!target}
  echo $string
}

check_servers( ) {
  local i=0
  for ((i=0; i<${#servers[@]}; i++ )); do
    echo "Checking server ${servers[i]}"
    local isup="server_${servers[i]}_isup"
    isup=${!isup}
    if [[ $isup -eq 0 ]]; then
      echo "  Server is up"
    else
      echo "  Server is down"
    fi
  done
}

#check_servers
target="config_server1_alpha"
var="ROUTER_SOURCE"
val=""
update $target $var $val
