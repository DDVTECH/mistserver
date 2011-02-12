#!/bin/bash

function DownloadConfig (){
  var=serverinfo_$1[*]
  ./one_config.sh $1 "${!var}" &
}

function GatherConfig (){
  . ./server_info/config_$1.sh
  if [ $isup -ne 1 ]; then
    add_alert "Critical" "Could not access server configuration file for $1. Assuming $1 is down."
  fi
}

echo_green "Downloading server configurations..."
count=${#servers[@]}
for ((j=0; j < count; j++)); do
  DownloadConfig ${servers[$j]}
done

sleep 10

echo_green "Gathering results..."
for ((j=0; j < count; j++)); do
  GatherConfig ${servers[$j]}
done
