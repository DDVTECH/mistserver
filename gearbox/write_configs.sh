#!/bin/bash

function UploadConfig (){
  local FILE="./server_info/config_$1_new.sh"
  local tmp=""
  local tmpB=""
  local j=0
  echo "#!/bin/bash" > $FILE
  echo "myname=$1" >> $FILE
  eval "tmp=\${server_$1[@]}"
  echo "server_$1=\"$tmp\"" >> $FILE
  eval "tmp=\${limits_$1[@]}"
  echo "limits_$1=\"$tmp\"" >> $FILE
  eval "tmpB=(\${server_$1[@]})"
  local count=${#tmpB[@]}
  for ((j=0; j < count; j++)); do
    eval "tmp=\${limits_$1_${tmpB[$j]}[@]}"
    echo "limits_$1_${tmpB[$j]}=($tmp)" >> $FILE
  done
  var=serverinfo_$1[*]
  local ${!var}
  scp -o PasswordAuthentication=no -o ConnectTimeout=3 -P $PORT $FILE root@$HOST:config.sh &> /dev/null &
  scp -o PasswordAuthentication=no -o ConnectTimeout=3 -P $PORT ../client_scripts/data_collect.sh root@$HOST:data_collect.sh &> /dev/null &
}

function UploadStats (){
  local isup=0
  eval "isup=\$$1_isup"
  eval "local \$$1_status"
  if [ "$isup" -eq "1" ]; then
    wget --post-data="serveron=$1\&time=$TIMECODE\&users=$USERS\&bytes=$BYTES\&bytes_d=$BYTES_D\&streams=$STREAMS" -qO /dev/null "http://ddvtech.com/gearbox_report.php" &
  else
    wget --post-data="serveroff=$1\&time=$TIMECODE\&users=$USERS\&bytes=$BYTES\&bytes_d=$BYTES_D\&streams=$STREAMS" -qO /dev/null "http://ddvtech.com/gearbox_report.php" &
  fi
}

echo_green "Uploading server configurations and stats..."
count=${#servers[@]}

for ((j=0; j < count; j++)); do
  UploadConfig ${servers[$j]}
  UploadStats ${servers[$j]}
done

wait
echo_green "All operations completed: shutting down."
