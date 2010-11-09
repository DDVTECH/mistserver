#!/bin/bash

TIMECODE=`date +%s`

function DownloadConfig (){
  var=serverinfo_$1[*]
  local ${!var}
  scp -o PasswordAuthentication=no -o ConnectTimeout=3 -P $PORT root@$HOST:config.sh ./server_info/config_$1.sh &> /dev/null
  if [ $? -ne 0 ]; then
    add_alert "Critical" "Could not access server configuration file at $HOST:$PORT. Assuming $1 is down."
    eval $1_isup=0
  else
    . ./server_info/config_$1.sh
    eval $1_isup=1
  fi
}

echo_green "Downloading server configurations..."
count=${#servers[@]}
for ((j=0; j < count; j++)); do
  DownloadConfig ${servers[$j]}
done
