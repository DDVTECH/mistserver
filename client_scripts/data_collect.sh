#!/bin/bash

conn_users=`netstat | grep shared_socket | wc -l`
streamcount=`ps aux | grep Buffer | grep -v grep | wc -l`
total_rcv=`cat /proc/net/dev | grep ":" | grep -v "lo" | cut -d ":" -f 2 | tr -s " " " " | sed 's/^[ \t]*//' | cut -d " " -f 1 | awk '{s+=$0} END {printf("%d", s)}'`
total_snd=`cat /proc/net/dev | grep ":" | grep -v "lo" | cut -d ":" -f 2 | tr -s " " " " | sed 's/^[ \t]*//' | cut -d " " -f 9 | awk '{s+=$0} END {printf("%d", s)}'`
sleep 5
total_rcv_b=`cat /proc/net/dev | grep ":" | grep -v "lo" | cut -d ":" -f 2 | tr -s " " " " | sed 's/^[ \t]*//' | cut -d " " -f 1 | awk '{s+=$0} END {printf("%d", s)}'`
total_snd_b=`cat /proc/net/dev | grep ":" | grep -v "lo" | cut -d ":" -f 2 | tr -s " " " " | sed 's/^[ \t]*//' | cut -d " " -f 9 | awk '{s+=$0} END {printf("%d", s)}'`
((total=$total_rcv + $total_snd))
((total_b=$total_rcv_b + $total_snd_b))
((total_d=($total_b - $total) / 5))


#((mbytes=($total)/1048576))
#((mbytes_d=($total_d)/1048576))
#((mbits=($total)/131072))
#((mbits_d=($total_d)/131072))
#((kbytes_d=($total_d)/1024))
#((kbits_d=($total_d)/128))
#echo "We have $conn_users connected users."
#echo "There was $mbytes MB ($mbits mbit) transfer, currently at $mbytes_d MB/s ($mbits_d mbps, $kbytes_d KB/s, $kbits_d kbps, $total_d bytes/s)."

echo "USERS=$conn_users BYTES=$total_b BYTES_D=$total_d STREAMS=$streamcount"
