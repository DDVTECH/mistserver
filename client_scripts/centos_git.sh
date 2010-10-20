#!/bin/bash

yum -y install gettext-devel expat-devel curl-devel zlib-devel openssl-devel
yum -y install gcc
wget http://kernel.org/pub/software/scm/git/git-1.6.1.tar.gz
tar xvfz git-1.6.1.tar.gz
cd git-1.6.1
make prefix=/usr/local all
make prefix=/usr/local install
cd ..
rm -rf ./git-1.6*
