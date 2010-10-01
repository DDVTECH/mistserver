#!/bin/bash
servers=(server1 server2 server3)
serverinfo_server1=(HOST=127.0.0.1 PORT=22)
serverinfo_server2=(HOST=rassat01.thulinma.com PORT=22)
serverinfo_server3=(HOST=rassat01.thulinma.com PORT=222)
groups=(a b a)
streams_a=(c e)
streams_a_count=${#streams_a[@]}
streams_b=(d f)
streams_b_count=${#streams_b[@]}
config_a_c=(NAME=lol INPUT=blaat PRESET=koekjes)
config_a_e=(NAME=d INPUT=e PRESET=f)
config_b_d=(NAME=saus INPUT=pudding PRESET=zout)
config_b_f=(NAME=a INPUT=b PRESET=c)
