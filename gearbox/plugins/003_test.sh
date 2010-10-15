#!/bin/bash
status_server1="BYTES=212673455"
status_server2="BYTES=212673455"
status_server3="BYTES=212673455"
status_server4="BYTES=212673455"
status_server5="BYTES=212673455"
status_server6="BYTES=212673455"
status_server7="BYTES=212673455"
status_server8="BYTES=212673455"
status_server9="BYTES=212673455"
status_server10="BYTES=212673455"

status_a_alpha="USERS=0 BITRATE=0"
status_a_beta="USERS=3 BITRATE=1000"
status_a_gamma="USERS=15 BITRATE=2000"
status_a_delta="USERS=30 BITRATE=2000"
status_b_epsilon="USERS=13 BITRATE=2000"
status_b_omega="USERS=11 BITRATE=5"
status_c_theta="USERS=1 BITRATE=200"
status_c_pi="USERS=10 BITRATE=1500""
status_c_meh="USERS=14 BITRATE=15"
status_d_bla="USERS=5 BITRATE=1750"
status_d_koekjes="USERS=0 BITRATE=0"

get_stream_users() {
  local temp="status_$1_$2"
  local ${!temp}
  echo $USERS
}

get_server_users() {
  local temp="server_$2"
  local temp=( ${!temp} )
  local users=0
  for((i=0; i<${#temp[@]}; i++)); do
  local meh=`get_stream_users $1 ${temp[i]}`
  local users=$(($users + $meh))
  done
  echo $users
}

calc_totals() {
  total_users=0
  for ((i=0; i<${#unique_groups[@]}; i++)); do
    local temp=balancer_total_${unique_groups[i]}
    eval $temp=0
  done
  for ((i=0; i<${#servers[@]}; i++)); do
    local servernaam=${servers[i]}
    local groupnaam=${groups[i]}
    local streams=server_$servernaam
    local streams=( ${!streams} )
    for((j=0; j<${#streams[@]}; j++)); do
      local streamusers=`get_stream_users ${groupnaam} ${streams[j]}`
      balancer_total_users=$(($balancer_total_users + $streamusers))
      local grouptemp=balancer_total_${servernaam}
      eval $grouptemp=$((${!grouptemp} + $streamusers))
      local grouptemp=balancer_total_${groupnaam}
      eval $grouptemp=$((${!grouptemp} + $streamusers))
    done
  done
}

print_perc() {
  for ((i=0; i<${#servers[@]}; i++)); do
    local servernaam=${servers[i]}
    local groupnaam=${groups[i]}
    capacity=limits_${servernaam}
    local ${!capacity}
    echo "Group '${groupnaam}' -- Server '${servernaam}'"
    currentusercount=`get_server_users $groupnaam $servernaam`
    local temp="$currentusercount/$MAX_USERS*100"
    local temp=`echo "scale=2; $temp" | bc | sed -e 's/\(.*\)\..*/\1/'`
    echo "Currently serving ${temp}% of it's maximum users (${MAX_USERS})"
    local streams=server_$servernaam
    local streams=( ${!streams} )
    for((j=0; j<${#streams[@]}; j++)); do
      local streamusers=`get_stream_users ${groupnaam} ${streams[j]}`
      echo "Sends ${streams[j]}"
      local total=balancer_total_$groupnaam
      local total=${!total}
      local this_users="$streamusers/$total*100"
      local meh=`echo "scale=2; $this_users" | bc | sed -e 's/\(.*\)\..*/\1/'`
      echo "    $meh percent of the $total users in the group"
      local total=balancer_total_$servernaam
      local total=${!total}
      if [[ $total -eq 0 ]]; then
        local this_users=100
      else
        local this_users="$streamusers/$total*100"
      fi
      local meh=`echo "scale=2; $this_users" | bc | sed -e 's/\(.*\)\..*/\1/'`
      echo "    $meh percent of the $total users on the server"
    done
  done
}

calc_totals
for ((i=0; i<${#unique_groups[@]}; i++)); do
  temp=balancer_total_${unique_groups[i]}
  echo "${unique_groups[i]} total users: ${!temp}"
  temp=server_group_${unique_groups[i]}
  temp=( ${!temp} )
  for((j=0; j<${#temp[@]}; j++)); do
    echo -n "  ${temp[j]}: "
    echo `get_server_users ${unique_groups[i]} ${temp[j]} `
  done
done
echo "Total viewers: $balancer_total_users"
echo ""
print_perc
