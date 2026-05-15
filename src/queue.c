#include "queue.h"
#include <string.h>

void queue_init(paging_queue_t *q) {
    q->front = 0;
    q->rear = 0;
    q->count = 0;
    memset(q->storage, 0, sizeof(q->storage));
}

bool queue_is_empty(const paging_queue_t *q) {
    return q->count == 0;
}

bool queue_is_full(const paging_queue_t *q) {
    return q->count == MAX_PAGING_QUEUE;
}

int enqueue_paging(paging_queue_t *q, const paging_req_t *req) {
    if (queue_is_full(q)) return -1;
    q->storage[q->rear] = *req;
    q->rear = (q->rear + 1) % MAX_PAGING_QUEUE;
    q->count++;
    return 0;
}

int dequeue_paging(paging_queue_t *q, paging_req_t *req) {
    if (queue_is_empty(q)) return -1;
    *req = q->storage[q->front];
    q->front = (q->front + 1) % MAX_PAGING_QUEUE;
    q->count--;
    return 0;
}

int queue_size(const paging_queue_t *q) {
    return q->count;
}
