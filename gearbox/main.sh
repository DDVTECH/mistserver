#!/bin/bash

#functions used in this script and/or plugins
function echo_green (){ echo -e "\E[1;32m$1\E[0m"; }
function echo_red (){ echo -e "\E[1;31m$1\E[0m"; }
function echo_brown (){ echo -e "\E[0;33m$1\E[0m"; }
function add_alert (){ alerts=( "${alerts[@]}" "TYPE=${1} MESSAGE=\"${2}\"" ); }
function alert_get_prefs () { local $plugsett_alerts; echo $PREFERENCES; }

#erase previous alerts
alerts=( )

#read in config, for now from a test file...
echo_green "Reading config..."
. ./test_config.sh

#load in all server configs
. ./load_configs.sh

#load in all plugins in numerical order
for plug in ./plugins/*_*.sh; do
  echo_green "Running $plug..."
  . $plug
done

#write all server configs
. ./write_configs.sh
