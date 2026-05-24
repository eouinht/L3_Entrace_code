#!/bin/bash

mkdir -p logs
rm -f logs/*.log

pkill -f "./gNodeB"
pkill -f "./ue"
pkill -f "./amf"

# ============================================================
# AMF test parameters
# Format:
#   ./amf <rate> <duration_sec> <start_ue_id> <num_ue>
#
# Example:
#   ./amf 3 10 1001 3
#
# Meaning:
#   rate         = number of NGAP Paging messages per second
#   duration_sec = how long AMF sends NGAP Paging messages
#   start_ue_id  = first UE ID
#   num_ue       = number of UE IDs used in round-robin order
#
# You can change these values to test different traffic loads.
# ============================================================

gcc src/gNodeB.c src/timer.c src/queue.c -o gNodeB -Iinclude -lpthread
gcc src/ue.c src/timer.c -o ue -Iinclude -lpthread
gcc src/amf.c src/timer.c -o amf -Iinclude -lpthread

stdbuf -o0 -e0 ./gNodeB > logs/gnb.log 2>&1 &
sleep 1


stdbuf -o0 -e0 ./amf 500 10 1001 500 > logs/amf.log 2>&1 &

sleep 1
stdbuf -o0 -e0 ./ue 1001 > logs/ue_1001.log 2>&1 &
stdbuf -o0 -e0 ./ue 1002 > logs/ue_1002.log 2>&1 &
stdbuf -o0 -e0 ./ue 1003 > logs/ue_1003.log 2>&1 &


echo "All processes started."