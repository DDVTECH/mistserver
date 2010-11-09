#!/bin/bash

function UploadConfig (){
  local FILE="./server_info/config_$1.sh"
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
  scp -o PasswordAuthentication=no -o ConnectTimeout=3 -P $PORT $FILE root@$HOST:config.sh &> /dev/null

}

echo_green "Uploading server configurations..."
count=${#servers[@]}
TIMECODE=`date +%s`

for ((j=0; j < count; j++)); do
  UploadConfig ${servers[$j]}
done

for ((j=0; j < count; j++)); do
  eval "isup=\$${servers[$j]}_isup"
  if [ "$isup" -eq "1" ]; then
    wget -qO /dev/null "http://ddvtech.com/gearbox_report.php?serveron=${servers[$j]}\&time=$TIMECODE"
  else
    wget -qO /dev/null "http://ddvtech.com/gearbox_report.php?serveroff=${servers[$j]}\&time=$TIMECODE"
  fi
done
