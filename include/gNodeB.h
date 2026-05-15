#ifndef GNODEB_H
#define GNODEB_H

#include <stdint.h>
#include <pthread.h>

#define GNB_TCP_PORT 6000
#define GNB_UDP_PORT 5000

#define MAX_QUEUE_SIZE 8192

extern volatile uint16_t g_sfn;

void *tcp_receiver_thread(void *arg);
void *paging_scheduler_thread(void *arg);
void *perf_thread(void *arg);

int paging_frame_match(uint32_t ue_id, uint16_t sfn);

#endif