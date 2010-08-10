#!/bin/bash
ffmpeg -re -i "$1" -b 1024000 -ar 11025 -f flv - | ./Server_PLS 5000 5

