#!/bin/bash
ffmpeg -re -i "$1" -b 1024000 -ar 11025 -f flv - 2> /dev/null | ./Server_PLS 500


