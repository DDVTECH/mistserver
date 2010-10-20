#!/bin/bash

#scp $2 ./bin/* ./client_scripts/* $1:/usr/bin/
scp $2 ./gearbox/gear_key $1:
ssh $2 $1 "echo \"IdentityFile ~/gear_key\" > ~/.ssh/config"
ssh $2 $1 "git clone gitosis@projectlivestream.com:pls"
ssh $2 $1 "cd pls; make client-install"
