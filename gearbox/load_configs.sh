#!/bin/bash

function DownloadConfig (){
  var=serverinfo_$1[*]
  local ${!var}
  scp -o PasswordAuthentication=no -o ConnectTimeout=6 -P $PORT root@$HOST:config.sh ./server_info/config_$1.sh &> /dev/null
  if [ $? -ne 0 ]; then
    add_alert "Critical" "Could not access server configuration file at $HOST:$PORT. Assuming $1 is down."
    eval $1_isup=0
  else
    local tmpvals=`ssh -o PasswordAuthentication=no -o ConnectTimeout=6 -p $PORT root@$HOST /root/pls/client_scripts/data_collect.sh`
    . ./server_info/config_$1.sh
    eval $1_isup=1
    eval $1_status="$tmpvals"
  fi
}

echo_green "Downloading server configurations..."
count=${#servers[@]}
for ((j=0; j < count; j++)); do
  DownloadConfig ${servers[$j]}
done
