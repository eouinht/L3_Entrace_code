#include <stdio.h>
#include <stdint.h>

#define T 64
#define N 1
#define PF_OFFSET 0
#define SFN_MOD 1024

static uint16_t calc_next_paging_sfn(uint16_t current_sfn, uint32_t ue_id)
{
    uint32_t target_offset = (T / N) * (ue_id % N);
    uint16_t sfn = current_sfn;

    while (((sfn + PF_OFFSET) % T) != target_offset) {
        sfn = (sfn + 1) % SFN_MOD;
    }

    return sfn;
}



int main()
{
    uint32_t ue_id = 1004;
    for(int i = 0; i <= 1024; i++){
        uint16_t target_sfn = calc_next_paging_sfn(i, ue_id);
        if (i == target_sfn){
            printf("[gNB] UE_ID=%u | current_sfn=%u | target_offset=%u | target_sfn=%u\n",
            ue_id,
            i,
            (T / N) * (ue_id % N),
            target_sfn);
        }

        
    }
    return 0;
}