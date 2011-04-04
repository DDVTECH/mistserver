#!/bin/bash
get_empty() {
  allstreams=streams_${unique_groups[$1]}
  allstreams=( ${!allstreams} )
  for ((j=(${#allstreams[@]}-1); j>=0; j--)); do
    temp=server_group_${unique_groups[$1]}
    temp=( ${!temp} )
    for ((k=0; k<${#temp[@]}; k++)); do
      currentstreams=server_${temp[k]}
      currentstreams=( ${!currentstreams} )
      for ((l=0; l<${#currentstreams[@]}; l++)); do
        if [ "${allstreams[j]}" == "${currentstreams[l]}" ]; then
          allstreams=( "${allstreams[@]:0:j}" "${allstreams[@]:j+1}" )
          break
        fi
      done
    done
  done
  allservers=server_group_${unique_groups[$1]}
  allservers=( ${!allservers} )
  for ((j=${#allservers[@]}-1; j>=0; j--)); do
    currentserver=server_${allservers[j]}
    currentserver=( ${!currentserver} )
    if [[ ${#currentserver[@]} -ne 0 ]]; then
      allservers=( "${allservers[@]:0:j}" "${allservers[@]:j+1}" )
    fi
  done
}

for ((i=0; i<${#servers[@]}; i++)); do
  temp=server_group_${groups[i]}
  array=""
  serverisup=${servers[i]}_isup
  serverisup=${!serverisup}
  if [[ $serverisup -eq 1 ]]; then
    if [ "${!temp}" != "" ]; then
      array=${!temp}
    fi
    if [ "$array" == "" ]; then
      array="${servers[i]}"
    else
      array="${array} ${servers[i]}"
    fi
    eval $temp=\"${array}\"
  fi
done

for ((i=0; i<${#unique_groups[@]}; i++)); do
  get_empty $i
  if [[ ${#allservers[@]} -ne 0 ]]; then 
    for ((j=0; j<${#allservers[@]}; j++)); do
      result=""
      if [[ "${#allstreams[@]}" -ne "0" ]]; then
        tempindex=j%${#allstreams[@]}
      else
        tempindex=0
      fi
      result="${allstreams[$tempindex]}"
      target=server_${allservers[j]}
      eval $target=\"$result\"
    done
  fi
  get_empty $i
  allservers=server_group_${unique_groups[i]}
  allservers=${!allservers}
  allservers=( ${allservers} )
  if [[ ${#allservers[@]} -ne 0 ]]; then 
    for ((j=0; j<${#allstreams[@]}; j++)); do
      result=""
      tempindex=$((${j} % ${#allservers[@]}))
      target=server_${allservers[${tempindex}]}
      result="${!target} ${allstreams[j]}"
      eval $target=\"$result\"
    done
  fi
done

for ((i=0; i<${#unique_groups[@]}; i++)); do
  get_empty $i
  if [ "${#allstreams[@]}" != "0" ]; then
    add_alert "Warning" "Not all streams of group '${unique_groups[i]}' could be assigned to a server."
  fi
  if [ "${#allservers[@]}" != "0" ]; then
    add_alert "Warning" "Not all servers of group '${unique_groups[i]}' could be assigned a stream."
  fi
done
