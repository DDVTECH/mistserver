#!/bin/bash
servers=(server1 server2)
groups=(a b)
streams_a=(c e)
streams_a_count=${#streams_a[@]}
streams_b=(d f)
streams_b_count=${#streams_b[@]}
config_a_c=(NAME=lol INPUT=blaat PRESET=koekjes)
config_a_e=(NAME=d INPUT=e PRESET=f)
config_b_d=(NAME=saus INPUT=pudding PRESET=zout)
config_b_f=(NAME=a INPUT=b PRESET=c)

function PrintConfig (){
  var=config_$1_$2[*]
  local ${!var}
  echo "Groep $1 heeft stream $2 met als waardes (NAME: $NAME, INPUT: $INPUT, PRESET: $PRESET)!"
}
function PrintGroups (){
  countn="streams_$1_count"
  count=${!countn}
  for ((i=0; i < count; i++))
  do
    var="streams_$1[$i]"
    PrintConfig $1 ${!var}
  done
}

count=${#groups[@]}
for ((j=0; j < count; j++))
do
  PrintGroups ${groups[$j]}
done
