print_alerts() {
  EMAILMESSAGE="/tmp/alerter.txt"
  echo "Gearbox has encountered the following problems:" > $EMAILMESSAGE
  local preferences=( $plugsett_alerter )
  local $plugsett_alerter
  local i=0
  local j=0
  for ((i=0; i<${#alerts[@]}; i++)); do
    local ${alerts[i]}
    for ((j=0; j<${#preferences[@]}; j++));do
     if [ "${preferences[j]}" == "$TYPE" ]; then
       temp="$( echo $MESSAGE | tr _ ' ')"
       `echo "" | /bin/mail -s "$temp" -r "gearbox@ddvtech.com" "$EMAIL"`
     fi
    done
  done
}

print_alerts
