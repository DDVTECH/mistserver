#!/bin/bash

. config.sh

function check_config() {
  local tmp
  eval "tmp=server_${myname}"
  configed_streams=( ${!tmp} )
}

function check_running() {
  local i=0
  running_streams=()
  running_pid=( `pidof Buffer` )
  for i in ${running_pid[@]}; do
    running_streams=( ${running_streams[@]} `ps -p $i h -o args | sed -e 's/.* \(.*\)/\1/'` )
  done
}

function calc_stop() {
  local change=1
  local i=0
  local j=0
  stop_streams=( ${running_streams[@]} )
  while [[ "$change" == "1" ]]; do
    change=0
    for (( i=0; i<${#stop_streams[@]}; i++ )); do
      for (( j=0; j<${#configed_streams[@]}; j++)); do
        if [[ "${stop_streams[$i]}" == "${configed_streams[$j]}" ]]; then
          stop_streams=( ${stop_streams[@]:0:$i} ${stop_streams[@]:$(($i+1))} )
          change=1
        fi
      done 
    done
  done
  local j=0
  stop_pid=()
  for i in ${stop_streams[@]}; do
    for (( j=0; j<${#running_streams[@]}; j++ )); do
      if [[ "$i" == "${running_streams[$j]}" ]]; then
        stop_pid=( ${stop_pid[@]} ${running_pid[$j]} )
      fi
    done
  done
}

function calc_start() {
  local change=1
  local i=0
  local j=0
  start_streams=( ${configed_streams[@]} )
  while [[ "$change" == "1" ]]; do
    change=0
    for (( i=0; i<${#start_streams[@]}; i++ )); do
      for (( j=0; j<${#running_streams[@]}; j++)); do
        if [[ "${start_streams[$i]}" == "${running_streams[$j]}" ]]; then
          start_streams=( ${start_streams[@]:0:$i} ${start_streams[@]:$(($i+1))} )
          change=1
        fi
      done 
    done
  done
}

function stop() {
  for i in ${stop_pid[@]}; do
    `kill -9 $i`
  done
}

function start() {
  for i in ${start_streams[@]}; do
    local var="config_${myname}_$i[*]"
    local ${!var}
    local tmpcommand=""
    if [[ "${INPUT:0:7}" == "file://" ]]; then
      INPUT=${INPUT:7}
      if [[ "$PRESET" == "raw" ]]; then
        tmpcommand="${tmpcommand}cat $INPUT |"
      elif [[ "$PRESET" == "copy" ]]; then
        tmpcommand="${tmpcommand} ffmpeg -re -async 2 -i $INPUT -acodec copy -vcodec copy -f flv - 2> /dev/null |"
      elif [[ "$PRESET" == "h264-high" ]]; then
        tmpcommand="${tmpcommand} ffmpeg -i "$INPUT" -re -acodec libfaac -ar 11025 -vcodec libx264 -b 1500k -vpre ultrafast -refs 1 -bf 0 -g 150 -f flv - 2> /dev/null |"
      else
        tmpcommand="${tmpcommand} ffmpeg -i "$INPUT" -re -acodec libfaac -ar 11025 -vcodec libx264 -b 700k -vpre ultrafast -refs 1 -bf 0 -g 150 -f flv - 2> /dev/null |"
      fi
    elif [[ "${INPUT:0:6}" == "raw://" ]]; then
      local rawseparator=`expr index "${INPUT:6}" /`
      local rawserv=${INPUT:6:$rawseparator-1}
      local rawstream=${INPUT:$rawseparator}
      tmpcommand="${tmpcommand}ssh $rawserv \"echo $rawstream\" |"
    else
      if [[ "$PRESET" == "raw" ]]; then
        tmpcommand="${tmpcommand}wget -q -O - $INPUT |"
      elif [[ "$PRESET" == "copy" ]]; then
        tmpcommand="${tmpcommand} ffmpeg -re -async 2 -i $INPUT -acodec copy -vcodec copy -f flv - 2> /dev/null |"
      elif [[ "$PRESET" == "h264-high" ]]; then
        tmpcommand="${tmpcommand} ffmpeg -i "$INPUT" -re -acodec libfaac -ar 11025 -vcodec libx264 -b 1500k -vpre ultrafast -refs 1 -bf 0 -g 150 -f flv - 2> /dev/null |"
      else
        tmpcommand="${tmpcommand} ffmpeg -i "$INPUT" -re -acodec libfaac -ar 11025 -vcodec libx264 -b 700k -vpre ultrafast -refs 1 -bf 0 -g 150 -f flv - 2> /dev/null |"
      fi
    fi
    tmpcommand="${tmpcommand} Buffer 500 $NAME"
    eval "${tmpcommand} 2> /dev/null &"
  done
}

check_config
check_running
calc_start
calc_stop

stop
start
