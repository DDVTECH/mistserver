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

groups=( a a a a b b b c c d )
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

server_server1="alpha"
server_server2="alpha"
server_server3="alpha"
server_server4="alpha"
server_server5=""
server_server6=""
server_server7=""
server_server8=""
server_server9=""
server_server10=""

limits_server1="MAX_USERS=21 MAX_BW=2001"
limits_server2="MAX_USERS=22 MAX_BW=2002"
limits_server3="MAX_USERS=23 MAX_BW=2003"
limits_server4="MAX_USERS=24 MAX_BW=2004"
limits_server5="MAX_USERS=25 MAX_BW=2005"
limits_server6="MAX_USERS=26 MAX_BW=2006"
limits_server7="MAX_USERS=27 MAX_BW=2007"
limits_server8="MAX_USERS=28 MAX_BW=2008"
limits_server9="MAX_USERS=29 MAX_BW=2009"
limits_server10="MAX_USERS=30 MAX_BW=2010"

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

status_server1="BYTES=212673455"
status_server2="BYTES=212673455"
status_server3="BYTES=212673455"
status_server4="BYTES=212673455"
status_server5="BYTES=212673455"
status_server6="BYTES=212673455"
status_server7="BYTES=212673455"
status_server8="BYTES=212673455"
status_server9="BYTES=212673455"
status_server10="BYTES=212673455"

status_a_alpha_server1="USERS=0 BITRATE=0"
status_a_alpha_server2="USERS=0 BITRATE=0"
status_a_alpha_server3="USERS=0 BITRATE=0"
status_a_alpha_server4="USERS=0 BITRATE=0"
status_a_beta_server1="USERS=3 BITRATE=1000"
status_a_beta_server2="USERS=3 BITRATE=1000"
status_a_beta_server3="USERS=3 BITRATE=1000"
status_a_beta_server4="USERS=3 BITRATE=1000"
status_a_gamma_server1="USERS=15 BITRATE=2000"
status_a_gamma_server2="USERS=15 BITRATE=2000"
status_a_gamma_server3="USERS=15 BITRATE=2000"
status_a_gamma_server4="USERS=15 BITRATE=2000"
status_a_delta_server1="USERS=30 BITRATE=2004"
status_a_delta_server2="USERS=30 BITRATE=2004"
status_a_delta_server3="USERS=30 BITRATE=2004"
status_a_delta_server4="USERS=30 BITRATE=2004"
status_b_epsilon_server5="USERS=13 BITRATE=2000"
status_b_epsilon_server6="USERS=13 BITRATE=2000"
status_b_epsilon_server7="USERS=13 BITRATE=2000"
status_b_omega_server5="USERS=11 BITRATE=5"
status_b_omega_server6="USERS=11 BITRATE=5"
status_b_omega_server7="USERS=11 BITRATE=5"
status_c_theta_server8="USERS=1 BITRATE=200"
status_c_theta_server9="USERS=1 BITRATE=200"
status_c_pi_server8="USERS=10 BITRATE=1500"
status_c_pi_server9="USERS=10 BITRATE=3000"
status_c_meh_server8="USERS=14 BITRATE=15"
status_c_meh_server9="USERS=14 BITRATE=15"
status_d_bla_server10="USERS=5 BITRATE=1750"
status_d_koekjes_server10="USERS=0 BITRATE=0"

config_direct_alpha=""
config_direct_beta=""
config_direct_gamma=""
config_direct_delta=""
config_direct_epsilon=""
config_direct_omega=""
config_direct_theta=""
config_direct_pi=""
config_direct_meh=""
config_direct_bla=""
config_direct_koekjes=""

config_server1_alpha=""
config_server1_beta=""
config_server1_gamma=""
config_server1_delta=""
config_server2_alpha=""
config_server2_beta=""
config_server2_gamma=""
config_server2_delta=""
config_server3_gamma=""
config_server3_alpha=""
config_server3_beta=""
config_server3_delta=""
config_server4_alpha=""
config_server4_beta=""
config_server4_gamma=""
config_server4_delta=""
config_server5_epsilon=""
config_server5_omega=""
config_server6_epsilon=""
config_server6_omega=""
config_server7_epsilon=""
config_server7_omega=""
config_server8_theta=""
config_server8_pi=""
config_server8_meh=""
config_server9_theta=""
config_server9_pi=""
config_server9_meh=""
config_server10_bla=""
config_server10_koekjes=""
