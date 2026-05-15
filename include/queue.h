#ifndef QUEUE_H
#define QUEUE_H

#include <stdint.h>
#define MAX_QUEUE_SIZE 2048

typedef struct{
    uint32_t ue_id;
    uint32_t tac;
    uint32_t cn_domain;
    uint16_t target_sfn;
} __attribute__((packed)) paging_req_t;

typedef struct{
    paging_req_t storage[MAX_QUEUE_SIZE];
    int front;
    int rear;
    int count;
} paging_queue_t;

void queue_init(paging_queue_t *q);
bool queue_is_full(paging_queue_t *q);
bool queue_is_empty(paging_queue_t *q);
int enqueue_paging(paging_queue_t *q,paging_req_t *req);
int dequeue_paging(paging_queue_t *q,paging_req_t *req);

#endif
