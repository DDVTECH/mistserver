#!/bin/bash
servers=( server1 server2 server3 server4 server5 server6 server7 server8 server9 server10 )
serverinfo_server1=( HOST=127.0.0.1 PORT=22 )
serverinfo_server2=( HOST=127.0.0.1 PORT=23 )
serverinfo_server3=( HOST=127.0.0.1 PORT=24 )
serverinfo_server4=( HOST=127.0.0.1 PORT=25 )
serverinfo_server5=( HOST=127.0.0.1 PORT=26 )
serverinfo_server6=( HOST=127.0.0.1 PORT=27 )
serverinfo_server7=( HOST=127.0.0.1 PORT=28 )
serverinfo_server8=( HOST=127.0.0.1 PORT=29 )
serverinfo_server9=( HOST=127.0.0.1 PORT=30 )
serverinfo_server10=( HOST=127.0.0.1 PORT=31 )
groups=( a b c d c b a a b a )
unique_groups=( a b c d )
streams_a="alpha beta gamma delta"
streams_b="epsilon omega"
streams_c="theta pi meh"
streams_d="bla koekjes"
config_a_alpha=( NAME=naama1 INPUT=inputa1 PRESET=preseta1 )
config_a_beta=( NAME=naama2 INPUT=inputa2 PRESET=preseta2 )
config_a_gamma=( NAME=naama3 INPUT=inputa3 PRESET=preseta3 )
config_a_delta=( NAME=naama4 INPUT=inputa4 PRESET=preseta4 )
config_b_epsilon=( NAME=naamb1 INPUT=inputb1 PRESET=presetb1 )
config_b_omega=( NAME=naamb2 INPUT=inputb2 PRESET=presetb2 )
config_c_theta=( NAME=naamc1 INPUT=inputc1 PRESET=presetc1 )
config_c_pi=( NAME=naamc2 INPUT=inputc2 PRESET=presetc2 )
config_c_meh=( NAME=naamc3 INPUT=inputc3 PRESET=presetc3 )
config_d_bla=( NAME=naamd1 INPUT=inputd1 PRESET=presetd1 )
config_d_koekjes=( NAME=naamd2 INPUT=inputd2 PRESET=presetd2 )
server_server1=""
server_server2=""
server_server3=""
server_server4=""
server_server5=""
server_server6=""
server_server7=""
server_server8=""
server_server9=""
server_server10=""
limits_server1=( MAX_USERS=21 MAX_BW=2001 )
limits_server2=( MAX_USERS=22 MAX_BW=2002 )
limits_server3=( MAX_USERS=23 MAX_BW=2003 )
limits_server4=( MAX_USERS=24 MAX_BW=2004 )
limits_server5=( MAX_USERS=25 MAX_BW=2005 )
limits_server6=( MAX_USERS=26 MAX_BW=2006 )
limits_server7=( MAX_USERS=27 MAX_BW=2007 )
limits_server8=( MAX_USERS=28 MAX_BW=2008 )
limits_server9=( MAX_USERS=29 MAX_BW=2009 )
limits_server10=( MAX_USERS=30 MAX_BW=2010 )
server1_isup=1
server2_isup=1
server3_isup=1
server4_isup=1
server5_isup=1
server6_isup=1
server7_isup=1
server8_isup=1
server9_isup=1
server10_isup=1
