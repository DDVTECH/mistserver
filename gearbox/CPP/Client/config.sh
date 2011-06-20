#!/bin/bash
myname=aserv1
server_aserv1="strm3 strm1"
limits_aserv1="MAX_USERS=25 MAX_BW=5000"
limits_aserv1_strm3=""
config_aserv1_strm3=(NAME=strm3 INPUT=file:///root/fifa3.flv PRESET=copy)
config_aserv1_strm1=(NAME=strm1 INPUT=http:///root/fifa3.flv PRESET=raw)
