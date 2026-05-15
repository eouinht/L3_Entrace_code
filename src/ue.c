#include <arpa/inet.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "messages.h"
#include "timer.h"

#define SFN_MOD 1024
#define UDP_PORT 5000
#define T 64
#define PF_OFFSET 0

static uint16_t ue_sfn = 0;
static uint32_t tick_sync = 0;
static int synced = 0;
static uint64_t sec_rrc = 0, total_rrc = 0;

static void ue_tick(void *arg) {
    (void)arg;
    ue_sfn = (ue_sfn + 1) % SFN_MOD;
    tick_sync++;
}

int main(void) {
    int udp_skt = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_skt < 0){ 
        perror("[UE] socket"); 
        return 1; 
    }

    struct sockaddr_in ue_addr;
    memset(&ue_addr, 0, sizeof(ue_addr));
    ue_addr.sin_family = AF_INET;
    ue_addr.sin_port = htons(UDP_PORT);
    ue_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(udp_skt, (struct sockaddr *)&ue_addr, sizeof(ue_addr)) < 0){
        perror("[UE] bind"); return 1;
    }

    timer_init(10L * 1000L * 1000L, ue_tick, NULL);
    timer_start();
    printf("[UE] Listening UDP port %d\n", UDP_PORT);

    uint32_t last_tick = 0;
    while (1) {
        uint8_t buf[128];
        ssize_t len = recvfrom(udp_skt, buf, sizeof(buf), 0, NULL, NULL);
        if (len <= 0) continue;

        if (len >= (ssize_t)sizeof(MIB_msg) && buf[0] == MIB_IE1) {
            MIB_msg mib;
            memcpy(&mib, buf, sizeof(mib));
            uint16_t gnb_sfn = ntohs(mib.sfn_value);
            if (!synced) {
                ue_sfn = gnb_sfn;
                synced = 1;
                tick_sync = 0;
                printf("[UE] Initial sync SFN=%u\n", ue_sfn);
            } else if (tick_sync >= 80) {
                ue_sfn = gnb_sfn;
                tick_sync = 0;
            }
        } else if (len >= (ssize_t)sizeof(RRC_Paging_msg)) {
            RRC_Paging_msg rrc;
            memcpy(&rrc, buf, sizeof(rrc));
            uint32_t ue_id = ntohl(rrc.ue_id);
            (void)ue_id;

            if (!synced) continue;
            if (((ue_sfn + PF_OFFSET) % T) == 0) {
                sec_rrc++;
                total_rrc++;
            }
        }

        if (tick_sync / 100 != last_tick / 100) {
            printf("[UE][PERF] rrc_paging=%lu msg/s | total=%lu | current_sfn=%u\n",
                   sec_rrc, total_rrc, ue_sfn);
            sec_rrc = 0;
        }
        last_tick = tick_sync;
    }
}
