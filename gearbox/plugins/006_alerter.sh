print_alerts() {
  local i=0
  local j=0
  for ((i=0; i<${#alerts[@]}; i++)); do
    local ${alerts[i]}
    temp="$( echo $MESSAGE | tr _ ' ')"
    local receivers="alerter_$TYPE"
    local receivers=( ${!receivers} );
    for ((j=0; j<${#receivers[@]}; j++));do
      local EMAIL=${receivers[j]};
      echo_red "Sending $EMAIL a $TYPE alert."
      `echo "" | /bin/mail -s "$temp" -r "gearbox@ddvtech.com" "$EMAIL"`
     fi
    done
  done
}

print_alerts
