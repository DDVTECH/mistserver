#!/bin/bash
#ffmpeg -y -i "$1" -acodec libfaac -ar 44100 -vcodec libx264 -b 1000k -cmp +chroma -partitions +parti4x4+partp8x8+partb8x8 -i_qfactor 0.71 -keyint_min 25 -b_strategy 1 -g 150 -r 20 -f flv - 2> /dev/null | ./Server_PLS 500



#ffmpeg -y -i "$1" -ar 44100 -vcodec libx264 -b 1000k -g 150 -r 20 -f flv - | ./Server_PLS 500

ffmpeg -i "$1" -re -acodec aac -ar 44100 -vcodec libx264 -b 700k -vpre ultrafast -refs 1 -bf 0 -g 150 -f flv - 2> /dev/null | ./Server_PLS 500

