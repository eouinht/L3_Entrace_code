#ifndef QUEUE_H
#define QUEUE_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#define MAX_PAGING_QUEUE 8192 // Chọn ngẫu nhiên
#define MAX_PAGING_RECORDS 32 // Theo chuẩn
#define SFN_MOD 1024

#define ENQUEUE_OK 0
#define ENQUEUE_ERR_FULL -1
#define ENQUEUE_ERR_DUPLICATE -2
#define ENQUEUE_ERR_INVALID -3

typedef struct{
    uint32_t message_type;
    uint32_t ue_id;
    uint32_t tac;
    uint32_t cn_domain;
    uint16_t sfn_to_send;
} __attribute__((packed)) paging_req_t;

typedef struct {
    paging_req_t items[MAX_PAGING_RECORDS];
    uint32_t count;
} paging_bucket_t;

typedef struct {
    paging_bucket_t buckets[SFN_MOD];
}paging_queue_t;

void queue_init(paging_queue_t *q);
bool queue_is_empty_at_sfn(const paging_queue_t *q, uint16_t sfn);
bool queue_is_full_at_sfn(const paging_queue_t *q, uint16_t sfn);
int enqueue_paging(paging_queue_t *q, const paging_req_t *req);
int dequeue_paging_at_sfn(paging_queue_t *q, paging_req_t *out, uint16_t sfn, uint32_t max_out);
uint32_t queue_size_at_sfn(const paging_queue_t *q, uint16_t sfn);
uint32_t queue_total_size(const paging_queue_t *q);
void queue_dump(const paging_queue_t *q);
#endif
