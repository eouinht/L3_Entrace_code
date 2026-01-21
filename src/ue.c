#include <stdio.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include "timer.h"
#include "rrc.h"
#include "mib.h"

#define SFN_MOD 1024
#define UDP_PORT 5000

#define PF_offset 0
#define T 64

#define MIB_IE1 0x01
#define UE_ID 1001   /* UE ID giả lập */

static uint16_t ue_sfn = 0;
static int synced = 0;
static uint32_t tick_sync = 0;

void ue_t(void *arg)
{
    ue_sfn = (ue_sfn + 1)%SFN_MOD;
    tick_sync++;
}

int main()
{
    int udp_skt ;
    struct sockaddr_in ue_addr;

    udp_skt  = socket(AF_INET, SOCK_DGRAM, 0);
    
    memset(&ue_addr, 0, sizeof(ue_addr));
    ue_addr.sin_family = AF_INET;
    ue_addr.sin_port = htons(UDP_PORT);
    ue_addr.sin_addr.s_addr = INADDR_ANY;

    bind(udp_skt , (struct sockaddr*)&ue_addr, sizeof(ue_addr));
    
    timer_init(10*1000*1000, ue_t, NULL);
    timer_start();

    while(1){
        uint8_t buf[64];
        ssize_t len = recvfrom(
            udp_skt,
            &buf, 
            sizeof(buf), 
            0, 
            NULL, 
            NULL);
        
        if (len <= 0) 
            continue;
        
        if(buf[0] == MIB_IE1 && len >= sizeof(MIB_msg)){
            MIB_msg mib;
            memcpy(&mib, buf, sizeof(mib));
            uint16_t gnb_sfn = ntohs(mib.sfn_value);
            // printf("[UE] RX MIB from gNodeB: SFN=%u\n", gnb_sfn);
                    
            if(!synced){
                ue_sfn = gnb_sfn;
                synced = 1;
                tick_sync = 0;
                printf("[UE] Initial sync SFN=%u\n", ue_sfn);
            }
            else if(tick_sync % 80 == 0){
                ue_sfn = gnb_sfn;
                tick_sync = 0;
                printf("[UE] Re-sync SFN=%u\n", ue_sfn);
            }

        }
        else if(len >= sizeof(RRC_Paging_msg)){
            RRC_Paging_msg paging;
            memcpy(&paging, buf, sizeof(paging));
            

            if (!synced) {
                printf("[UE] Ignore RRC Paging (SFN not synced)\n");
                continue;
            }

            uint32_t ue_id = ntohl(paging.ue_id);
            printf ("[UE][DRX] wake up| UE_ID=%u | SFN=%u\n",ue_id, ue_sfn);

        }
        
        
        
        
        
        
        

        
    }
}
