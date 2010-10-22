#!/bin/bash

function DownloadConfig (){
  var=serverinfo_$1[*]
  local ${!var}
  ssh -n -o PasswordAuthentication=no -o ConnectTimeout=3 $HOST -p $PORT "cat ~/config.sh" > ./server_info/config_$1.sh
  if [ $? -ne 0 ]; then
    echo_red "WARNING: Could not download server config for $1 from $HOST:$PORT!"
    add_alert "Critical" "Server '$1' is down"
    eval $1_isup=0
  else
    . ./server_info/config_$1.sh
    eval $1_isup=1
  fi
}

count=${#servers[@]}
for ((j=0; j < count; j++)); do
  DownloadConfig ${servers[$j]}
done
