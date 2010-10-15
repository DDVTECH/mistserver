for((i=0; i<${#servers[@]}; i++)); do
  temp=${servers[i]}_isup
  eval $temp="1"
done
