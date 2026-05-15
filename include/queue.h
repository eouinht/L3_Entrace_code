#ifndef QUEUE_H
#define QUEUE_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#define MAX_PAGING_QUEUE 8192

typedef struct {
    uint32_t ue_id;
    uint32_t tac;
    uint32_t cn_domain;
    struct timespec rx_time;
} paging_req_t;

typedef struct {
    paging_req_t storage[MAX_PAGING_QUEUE];
    int front;
    int rear;
    int count;
} paging_queue_t;

void queue_init(paging_queue_t *q);
bool queue_is_empty(const paging_queue_t *q);
bool queue_is_full(const paging_queue_t *q);
int enqueue_paging(paging_queue_t *q, const paging_req_t *req);
int dequeue_paging(paging_queue_t *q, paging_req_t *req);
int queue_size(const paging_queue_t *q);

#endif
