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
NUM_UE=500
UE_ID_FILE="logs/ue_ids.txt"

gcc src/gNodeB.c src/timer.c src/queue.c -o gNodeB -Iinclude -lpthread || exit 1
gcc src/ue.c src/timer.c -o ue -Iinclude -lpthread || exit 1
gcc src/amf.c src/timer.c -o amf -Iinclude -lpthread || exit 1

# 1. Generate UE IDs first
./amf --gen-ids "$NUM_UE" "$UE_ID_FILE"
if [ $? -ne 0 ]; then
    echo "Generate UE IDs failed."
    exit 1
fi

# 2. Start gNB
stdbuf -o0 -e0 ./gNodeB > logs/gnb.log 2>&1 &
sleep 1

# 3. Start UE processes from generated UE ID file
idx=0
while read -r UE_ID; do
    stdbuf -o0 -e0 ./ue "$UE_ID" > "logs/ue_${idx}_${UE_ID}.log" 2>&1 &
    idx=$((idx + 1))
done < "$UE_ID_FILE"

sleep 2

# 4. Start AMF using the same UE ID file
stdbuf -o0 -e0 ./amf "$AMF_RATE" "$AMF_DURATION" "$UE_ID_FILE" > logs/amf.log 2>&1 &

echo "All processes started."
echo "UE_ID_FILE=$UE_ID_FILE"
echo "AMF: rate=$AMF_RATE msg/s | duration=$AMF_DURATION s | num_ue=$NUM_UE"