#!/bin/bash

urpmi.removemedia -a
urpmi.addmedia --all-media --distrib --mirrorlist '$MIRRORLIST'
urpmi.addmedia --all-media --distrib --mirrorlist 'http://plf.zarb.org/mirrors/$RELEASE.$ARCH.list'
urpmi.update -a
urpmi --auto --force git openssl-devel xinetd ffmpeg make gcc-c++

