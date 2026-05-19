#!/bin/bash

mkdir -p logs
rm -f logs/*.log

pkill -f "./gNodeB"
pkill -f "./ue"
pkill -f "./amf"

gcc src/gNodeB.c src/timer.c src/queue.c -o gNodeB -Iinclude -lpthread
gcc src/ue.c src/timer.c -o ue -Iinclude -lpthread
gcc src/amf.c -o amf -Iinclude

stdbuf -o0 -e0 ./ue 1001 5001 > logs/ue_1001.log 2>&1 &
stdbuf -o0 -e0 ./ue 1002 5002 > logs/ue_1002.log 2>&1 &
stdbuf -o0 -e0 ./ue 1003 5003 > logs/ue_1003.log 2>&1 &

sleep 1
stdbuf -o0 -e0 ./gNodeB > logs/gnb.log 2>&1 &
sleep 1
stdbuf -o0 -e0 ./amf 10 10 1001 10 > logs/amf.log 2>&1 &

echo "All processes started."