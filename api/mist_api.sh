#!/bin/bash

# This file contains functions that may be used to make MistServer API calls using your shell.
# Dependencies: curl, jq, md5sum, cut, cat
# If any of the dependencies are missing, the functions will warn you and exit.
#
# Run a function without arguments for usage information and a short explanation.
#
# Function list:
# - mistCall; Make raw JSON API calls, returning raw JSON results.
#

function mistCheckDeps {
  command -v curl >/dev/null 2>&1 && command -v jq >/dev/null 2>&1 && command -v md5sum >/dev/null 2>&1 && command -v cut >/dev/null 2>&1 && command -v cat >/dev/null 2>&1 && return 0
  echo "ERROR: Not all dependencies are installed (curl, jq, md5sum, cut, cat)" >&2
  return 1
}

urlencode() {
    # urlencode <string>
    local LANG=C
    local length="${#1}"
    for (( i = 0; i < length; i++ )); do
        local c="${1:i:1}"
        case $c in
            [a-zA-Z0-9.~_-]) printf "$c" ;;
            *) printf '%%%02X' "'$c" ;; 
        esac
    done
}

function mistCall {
  mistCheckDeps || return 3

  if [ "$#" -eq 0 ] ; then
    echo "mistCall allows you to pass a partial JSON object and make an API call with it." >&2
    echo "The reply is send to standard output, any errors are send to standard error." >&2
    echo "Returns a non-zero exit code on error." >&2
    echo "" >&2
    echo "The COMMAND passed is inserted into a JSON object as follows:" >&2
    echo "  {\"auth\":{...}, COMMAND, \"minimal\":1}" >&2
    echo "" >&2
  fi
  if [ "$#" -gt 4 ] ; then
    echo "ERROR: Usage: mistCall [SERVER] [USERNAME PASSWORD] COMMAND" >&2
    return 3
  fi
  ARG_CMD=""
  ARG_SRV="localhost:4242"
  ARG_USR=""
  ARG_PWD=""
  if [ "$#" -eq 1 ] ; then
    ARG_CMD=$1
  fi
  if [ "$#" -eq 2 ] ; then
    ARG_SRV=$1
    ARG_CMD=$2
  fi
  if [ "$#" -eq 3 ] ; then
    ARG_USR=$1
    ARG_PWD=$2
    ARG_CMD=$3
  fi
  if [ "$#" -eq 4 ] ; then
    ARG_SRV=$1
    ARG_USR=$2
    ARG_PWD=$3
    ARG_CMD=$4
  fi

  #Load auth, if any
  MIST_AUTH=`cat /tmp/MistBashAuth 2> /dev/null`

  #Send request
  CMD=`urlencode "{${MIST_AUTH} ${ARG_CMD}}"`
  RESP=`curl -s -X POST -d "command=$CMD" http://${ARG_SRV}/api2`

  #Catch problems with reaching the server, display clear error message
  if [ "$?" != "0" ] ; then
    echo "ERROR: Could not contact server (${ARG_SRV}), is it correct and online?" >&2
    return 1
  fi

  #Check if this is a challenge
  if [ `echo $RESP | jq -r .authorize.status` == "CHALL" ] ; then
    echo "Challenge detected, (re)calculating login string..." >&2
    PASSW=`echo -n ${ARG_PWD} | md5sum | cut -f 1 -d " "`""
    CHALL=`echo $RESP | jq -r .authorize.challenge`
    SUMMD=`echo -n $PASSW$CHALL | md5sum | cut -f 1 -d " "`
    MIST_AUTH="\"authorize\":{\"username\":\"${ARG_USR}\",\"password\":\"${SUMMD}\"},"
    #Repeat request
    CMD=`urlencode "{${MIST_AUTH} ${ARG_CMD}}"`
    RESP=`curl -s -X POST -d "command=$CMD" http://${ARG_SRV}/api2`
  fi

  #Still a challenge? We must have invalid credentials
  if [ `echo $RESP | jq -r .authorize.status` == "CHALL" ] ; then
    echo "ERROR: User/pass incorrect?" >&2
    return 2
  fi

  #Cache auth credentials for next call
  echo -n $MIST_AUTH > /tmp/MistBashAuth

  #Echo the API call result
  echo $RESP
}

