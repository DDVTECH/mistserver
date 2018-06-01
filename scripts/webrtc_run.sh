#!/bin/bash

cd ${PWD}/../build
set -x
#export MIST_CONTROL=1
make MistOutWebRTC

if [ $? -ne 0 ] ; then
    echo "Failed to compile."
    exit
fi

#-fsanitize=address 
#export MALLOC_CHECK_=2
# valgrind --trace-children=yes
#   ASAN_OPTIONS=symbolize=1 ASAN_SYMBOLIZER_PATH=$(shell which llvm-symbolizer) 
if [ 0 -ne 0 ] ; then
    reset && valgrind --trace-children=yes ./MistOutHTTPS \
                      --port 4433 \
                      --cert ~/.ssh/certs/arch680.rox.lu.crt \
                      --key ~/.ssh/certs/arch680.rox.lu.key \
                      --debug 10
else
    reset && ./MistOutHTTPS \
                 --port 4433 \
                 --cert ~/.ssh/certs/arch680.rox.lu.crt \
                 --key ~/.ssh/certs/arch680.rox.lu.key \
                 --debug 10
fi

