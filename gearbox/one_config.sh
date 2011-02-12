#!/bin/bash
eval $2

scp -o PasswordAuthentication=no -o ConnectTimeout=6 -P $PORT root@$HOST:config.sh ./server_info/config_$1.sh &> /dev/null
if [ $? -ne 0 ]; then
  echo $1_isup=0 > ./server_info/config_$1.sh
  echo isup=0 >> ./server_info/config_$1.sh
else
  eval tmpvals='`ssh -o PasswordAuthentication=no -o ConnectTimeout=6 -p $PORT root@$HOST "./data_collect.sh 2> /dev/null"`'
  echo $1_isup=1 > ./server_info/config_$1.sh
  echo isup=1 >> ./server_info/config_$1.sh
  echo "$1_status=""$tmpvals""" >> ./server_info/config_$1.sh
fi
