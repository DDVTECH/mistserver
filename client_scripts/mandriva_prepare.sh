#!/bin/bash

DODOWN=0
HAS_GCC=`rpm -qa | grep c++`
HAS_FFM=`rpm -qa | grep ffmpeg`
HAS_SSL=`rpm -qa | grep openssl | grep devel`
HAS_XIN=`rpm -qa | grep xinetd`
HAS_GIT=`rpm -qa | grep git`

if [ -z "$HAS_GCC" ]; then
	DODOWN=1
fi
if [ -z "$HAS_FFM" ]; then
	DODOWN=1
fi
if [ -z "$HAS_SSL" ]; then
	DODOWN=1
fi
if [ -z "$HAS_XIN" ]; then
	DODOWN=1
fi
if [ -z "$HAS_GIT" ]; then
	DODOWN=1
fi


if [ "$DODOWN" -eq "0" ]; then
  urpmi.removemedia -a
  urpmi.addmedia --all-media --distrib --mirrorlist '$MIRRORLIST'
  urpmi.addmedia --all-media --distrib --mirrorlist 'http://plf.zarb.org/mirrors/$RELEASE.$ARCH.list'
  urpmi.update -a
  urpmi --auto --force git openssl-devel xinetd ffmpeg make gcc-c++
fi

