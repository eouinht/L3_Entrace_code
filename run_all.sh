#!/bin/bash

mkdir -p logs
rm -f logs/*.log
rm -f logs/ue_ids.txt

pkill -f "./gNodeB"
pkill -f "./ue"
pkill -f "./amf"

sleep 1

AMF_RATE=500
AMF_DURATION=3600


gcc src/gNodeB.c src/timer.c src/queue.c -o gNodeB -Iinclude -lpthread || exit 1
gcc src/ue.c src/timer.c -o ue -Iinclude -lpthread || exit 1
gcc src/amf.c src/timer.c -o amf -Iinclude -lpthread || exit 1



# 2. Start gNB
stdbuf -o0 -e0 ./gNodeB > logs/gnb.log 2>&1 &
sleep 1

# 3. Start UE processes from generated UE ID file
stdbuf -o0 -e0 ./ue 0x11111040 > logs/ue_0x11111040.log 2>&1 &
stdbuf -o0 -e0 ./ue 0x22222081 > logs/ue_0x22222081.log 2>&1 &
stdbuf -o0 -e0 ./ue 0x333330C2 > logs/ue_0x333330C2.log 2>&1 &
stdbuf -o0 -e0 ./ue 0x44444103 > logs/ue_0x44444103.log 2>&1 &
stdbuf -o0 -e0 ./ue 0x55555144 > logs/ue_0x55555144.log 2>&1 &
sleep 2

# 4. Start AMF using the same UE ID file


# Test1: AMF (1 NGAP) -> GNB-> 1UE
# stdbuf -o0 -e0 ./amf 0x11111040  > logs/amf.log 2>&1 & 

# Test2: AMF (1 NGAP/s 60s) -> GNB -> 1UE 
stdbuf -o0 -e0 ./amf 1 60 0x11111040  > logs/amf.log 2>&1 & 

# Test3: AMF (500 NGAP/s 3600s) -> GNB -> UE
# stdbuf -o0 -e0 ./amf "$AMF_RATE" "$AMF_DURATION"  > logs/amf.log 2>&1 &
echo "All processes started."

