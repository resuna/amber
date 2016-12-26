#!/bin/sh

TCPREMOTEIP=1.`date +%H`.3.`date +%M`
export TCPREMOTEIP
AMBERCHECK=YES
export AMBERCHECK

mkdir -p test.d

echo $TCPREMOTEIP
./amber -l -d ./test.d -t 10 -i 15 -p RELAYCLIENT -s "421 Aborted" echo OK
echo $?

