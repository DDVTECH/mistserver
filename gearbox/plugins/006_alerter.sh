print_alerts() {
  local preferences=( `alert_get_prefs` )
  local i=0
  local j=0
  for ((i=0; i<${#alerts[@]}; i++)); do
    echo ${alerts[i]}
    local ${alerts[i]}
    echo "Messagetype= $TYPE"
    echo "Msg= $MESSAGE"
    for ((j=0; j<${#preferences[@]}; j++));do
     if [ "${preferences[j]}" == "$TYPE" ]; then
       echo_red $MESSAGE
     fi
    done
  done
}

print_alerts
