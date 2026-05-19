#include <arpa/inet.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "messages.h"
#include "timer.h"

#define SFN_MOD 1024u
#define DEFAULT_UE_PORT 5001u
#define DEFAULT_UE_ID 1001u


#define T 64u
#define N 1u
#define PF_OFFSET 0u
#define GNB_IP "127.0.0.1"
#define GNB_UDP_PORT 5000u

static uint32_t ue_id = DEFAULT_UE_ID;
static uint16_t ue_sfn = 0;
static uint32_t tick_sync = 0;
static int synced = 0;
static uint64_t sec_rrc = 0;
static uint64_t total_mib = 0;  
static uint64_t total_rrc = 0;

// static uint32_t ue_paging_frame(uint32_t ue_id)
// {       
//     return (T/N)*(ue_id%N);
// }

static void ue_tick(void *arg){  
    (void)arg; 
    ue_sfn = (ue_sfn + 1) % SFN_MOD;
    tick_sync++;
}

static int sfn_delta(uint16_t a, uint16_t b) {
    int d = (int)a - (int)b;
    if (d > 512){
        d -= 1024;
    } 
    if (d < -512){
        d += 1024;
    } 
    return d;
}



int main(int argc, char **argv){
    int udp_port = DEFAULT_UE_PORT;

    if(argc >= 2){
        ue_id = (uint32_t)strtoul(argv[1], NULL, 10);
    }
    if (argc >= 3){
        udp_port = atoi(argv[2]);
    }
    
    int udp_skt = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_skt < 0){ 
        perror("[UE] socket"); 
        return 1; 
    }

    int opt = 1;
    setsockopt(udp_skt, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in ue_addr;
    memset(&ue_addr, 0, sizeof(ue_addr));
    ue_addr.sin_family = AF_INET;
    ue_addr.sin_port = htons(udp_port);
    ue_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(udp_skt, (struct sockaddr *)&ue_addr, sizeof(ue_addr)) < 0){
        perror("[UE] bind"); 
        close(udp_skt);
        return 1;
    }

    timer_init(10L * 1000L * 1000L, ue_tick, NULL);
    timer_start();
    printf("[UE %u] Listening UDP port %d\n", ue_id, udp_port);
    fflush(stdout);

    uint32_t last_tick = 0;

    while (1) {
        uint8_t buf[128];
        ssize_t len = recvfrom(udp_skt, buf, sizeof(buf), 0, NULL, NULL);
        if (len <= 0) continue;

        if (len >= (ssize_t)sizeof(MIB_msg) && buf[0] == MIB_IE1) {
            MIB_msg mib;
            memcpy(&mib, buf, sizeof(mib));
            uint16_t gnb_sfn = ntohs(mib.sfn_value);
            total_mib++;

            if (!synced) {
                ue_sfn = gnb_sfn;
                synced = 1;
                tick_sync = 0;
                printf("[UE %u] Initial sync | UE_SFN=%u | gNB_SFN=%u\n",ue_id, ue_sfn, gnb_sfn);
            } else if (tick_sync >= 80) {
                int before = sfn_delta(ue_sfn, gnb_sfn);
                ue_sfn = gnb_sfn;
                tick_sync = 0;
                printf("[UE %u] Re-sync: UE_SFN=%u, gNB_SFN=%u, delta_before=%d, mib_total=%lu\n",
                       ue_id, ue_sfn, gnb_sfn, before, total_mib);
                fflush(stdout);
            }
        } else if (len >= (ssize_t)sizeof(RRC_Paging_msg)) {
            RRC_Paging_msg rrc;
            memcpy(&rrc, buf, sizeof(rrc));
            uint32_t target_ue_id = ntohl(rrc.ue_id);
            
            if(target_ue_id!=ue_id){
                printf("[UE %u] Ignore Paging for UE_ID=%u\n", ue_id, target_ue_id);
                fflush(stdout);
                continue;
            }
            if(!synced){
                printf("[UE %u] Ignore Paging because SNF noy synced yet\n", ue_id);
                fflush(stdout);
                continue;
            }

            total_rrc++;
            int check_sfn = (ue_sfn + PF_OFFSET) % T;
            if (check_sfn == 0) {
                printf("[UE %u] RX RRC Paging at UE_SFN=%u | total_rrc=%lu \n", ue_id, ue_sfn, total_rrc);
                fflush(stdout);
                   
            }
        }

       
    }
}
