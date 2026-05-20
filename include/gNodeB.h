#ifndef GNODEB_H
#define GNODEB_H

#include <stdint.h>
#include <pthread.h>

#define SFN_MOD 1024

#define GNB_TCP_PORT 6000
#define GNB_UDP_PORT 5000 // gNB nghe UE 
#define LOCAL_IP "127.0.0.1"

#define T 64
#define N 1
#define PF_OFFSET 0

#define UE_ID_BASE 1000
#define UE_PORT_BASE 5000

#define MIB_BROADCAST_IP "255.255.255.255"
#define MIB_BROADCAST_PORT 6000 // UE nghe gNB

#define RRC_BROADCAST_IP "255.255.255.255"
#define RRC_BROADCAST_PORT 6000

extern volatile uint16_t g_sfn;

void *tcp_receiver_thread(void *arg);
void *paging_scheduler_thread(void *arg);
void *perf_thread(void *arg);

int paging_frame_match(uint32_t ue_id, uint16_t sfn);

#endif