#!/bin/bash
function PrintConfig (){
  local var=config_$1_$2[*]
  local ${!var}
  echo_brown "      $2 = {NAME: $NAME, INPUT: $INPUT, PRESET: $PRESET}"
}

function PrintServer (){
  echo_red "  Server $1 in group $2"
  local var="$1_isup"
  local isup=${!var}
  if [ "$isup" -eq "1" ]; then
    echo_green "    is currently online"
  else
    echo_red "    is currently OFFLINE!"
  fi
  echo_red "    Handles the following streams:"
  temp=streams_$2
  temp=( ${!temp} )
  for ((i=0; i<${#temp[@]}; i++)); do
    local var="temp[$i]"
    PrintConfig $2 ${!var}
  done
  echo_red "    Of which currently enabled:"
  local var="server_$1"
  echo_brown "      ${!var}"
}

count=${#groups[@]}
echo_red "$count servers defined:"
for ((j=0; j < count; j++)); do
  PrintServer ${servers[$j]} ${groups[$j]}
done
