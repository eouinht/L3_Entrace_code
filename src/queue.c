#include "queue.h"
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

void queue_init(paging_queue_t *q) {
    if(q == NULL) return;
    memset(q, 0, sizeof(*q));
}

bool queue_is_empty_at_sfn(const paging_queue_t *q, uint16_t sfn){
    if (q == NULL || sfn >= SFN_MOD) {
        return true;
    }

    return q->buckets[sfn].count == 0;
}

bool queue_is_full_at_sfn(const paging_queue_t *q, uint16_t sfn){
    if (q == NULL || sfn >= SFN_MOD) {
        return true;
    }
    return q->buckets[sfn].count >= MAX_PAGING_RECORDS;
}

static bool bucket_contains_ue(const paging_bucket_t *bucket,uint32_t ue_id){
    for(uint32_t i = 0; i< bucket->count; i++){
        if (bucket->items[i].ue_id == ue_id){
            return true;
        }
        
    }
    return false;
}
int enqueue_paging(paging_queue_t *q, const paging_req_t *req) {
    if (q == NULL || req == NULL){
        return ENQUEUE_ERR_INVALID;
    }

    uint16_t sfn = req->sfn_to_send;

    if (sfn >= SFN_MOD){
        return ENQUEUE_ERR_INVALID;
    }

    paging_bucket_t *bucket = &q->buckets[sfn];

    if (queue_is_full_at_sfn(q, sfn)){
        return ENQUEUE_ERR_FULL;
    }

    if (bucket_contains_ue(bucket, req->ue_id)){
        printf("[QUEUE] Duplicate paging ignored | UE_ID=0x%08X | SFN=%u\n",
               req->ue_id,
               sfn);
        fflush(stdout);

        return ENQUEUE_ERR_DUPLICATE;
    }

    bucket->items[bucket->count] = *req;
    bucket->count++;

    return ENQUEUE_OK;
}

int dequeue_paging_at_sfn(
    paging_queue_t *q,
    paging_req_t *out, 
    uint16_t sfn,
    uint32_t max_out
){

    if (q == NULL || out == NULL || sfn >= SFN_MOD || max_out == 0) {
        return 0;
    }

    paging_bucket_t *bucket = &q->buckets[sfn];

    uint32_t count = bucket->count;
    if (count > max_out) {
        count = max_out;
    }

    for (uint32_t i = 0; i < count; i++) {
        out[i] = bucket->items[i];
    }

    /*
     * Nếu còn record chưa lấy ra, dồn phần còn lại lên đầu bucket.
     */
    uint32_t remaining = bucket->count - count;

    for (uint32_t i = 0; i < remaining; i++) {
        bucket->items[i] = bucket->items[count + i];
    }

    bucket->count = remaining;

    return (int)count;
}

uint32_t queue_size_at_sfn(const paging_queue_t *q, uint16_t sfn)
{
    return q->buckets[sfn].count;
}

uint32_t queue_total_size(const paging_queue_t *q)
{   
    if (q == NULL) {
        return 0;
    }

    uint32_t total = 0;
    for (uint32_t sfn = 0; sfn < SFN_MOD; sfn++){
        total += q->buckets[sfn].count;
    }
    return total;
}

void queue_dump(const paging_queue_t *q)
{
    printf("\n========== PAGING QUEUE DUMP ==========\n");

    uint32_t total = 0;

    for (uint16_t sfn = 0; sfn < SFN_MOD; sfn++) {

        uint32_t count = q->buckets[sfn].count;

        if (count == 0) {
            continue;
        }

        printf("[SFN %u] bucket_count=%u\n", sfn, count);

        for (uint32_t i = 0; i < count; i++) {

            const paging_req_t *req =
                &q->buckets[sfn].items[i];

            // printf("   [%u] UE_ID=%u | TAC=%u | CN_DOMAIN=%u | TARGET_SFN=%u\n",
            //        i,
            //        req->ue_id,
            //        req->tac,
            //        req->cn_domain,
            //        req->sfn_to_send);

            total++;
        }
    }

    // printf("TOTAL PAGING REQ = %u\n", total);
    // printf("[QUEUE][DEBUG]TOTAL PAGING IN QUEUE = %u\n", queue_total_size(q));
    // printf("=======================================\n\n");

    fflush(stdout);
}
