#ifndef AMF_H
#define AMF_H

#include <stdint.h>

#define GNB_IP "127.0.0.1"
#define GNB_TCP_PORT 6000

#define DEFAULT_UE_ID 1001u
#define DEFAULT_RATE 500u
#define DEFAULT_DURATION 10u
#define UE_ID_BASE 1000u
#define DEFAULT_NUM_UE 3u

void run_amf(int rate);

#endif