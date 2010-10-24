#!/bin/bash

#usage: remote_install.sh install_type hostname port [optional_params]

ITYPE=$1
HOST=$2
PORT=$3
PARAMS=$4

echo "Transferring host prepare file..."
scp -P $PORT $PARAMS ./client_scripts/${ITYPE}_prepare.sh $HOST:
echo "Preparing host..."
ssh -p $PORT $PARAMS $HOST "sh ./${ITYPE}_prepare.sh"
echo "Transferring keyfile for DDVSS install"
scp -P $PORT $PARAMS ./gearbox/gear_key $HOST:
echo "Making keyfile default keyfile"
ssh -p $PORT $PARAMS $HOST "echo \"IdentityFile ~/gear_key\" > ~/.ssh/config"
echo "Cloning the git repository"
ssh -p $PORT $PARAMS $HOST "rm -rf pls; git clone gitosis@projectlivestream.com:pls"
echo "Installing DDVSS"
ssh -p $PORT $PARAMS $HOST "cd pls; make client-install"

