#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <time.h>
#include <pthread.h>

#include "timer.h"
#include "mib.h"
#include "ngap.h"
#include "rrc.h"


#define MIB_IE1 0x01


#define UE_PORT 5000
#define AMF_PORT 6000
#define UE_IP "127.0.0.1"

#define SFN_MOD 1024
#define T 64
#define PF_offset 0

#define MSG_TYPE 100 //paging
#define TAC 100 // Tracking Area
#define CN_DOMAIN_100 100 
#define CN_DOMAIN_101 101

#define MAX_PAGING_QUEUE 2048

static int tcp_skt;
static int udp_skt;
struct sockaddr_in ue_addr;

static uint16_t gnb_sfn = 0;
static int tick = 0;

static int paging_pending = 0;
static uint32_t paging_ue_id;

static uint32_t paging_tac;
static uint32_t paging_domain;

static pthread_mutex_t paging_mtx = PTHREAD_MUTEX_INITIALIZER;


void gnb_t(void *arg)
{
    gnb_sfn = (gnb_sfn + 1)% SFN_MOD;
    tick++;

    if (tick % 8 == 0){
        MIB_msg mib;
        mib.message_id = MIB_IE1;
        mib.sfn_value = htons(gnb_sfn);
        sendto(
            udp_skt, 
            &mib, 
            sizeof(mib), 
            0, 
            (struct sockaddr *)&ue_addr, 
            sizeof(ue_addr));
        // printf("[gNodeB] Send MIB: SFN %u\n", gnb_sfn);
    }

    pthread_mutex_lock(&paging_mtx);
    if (paging_pending)
    {
        if((gnb_sfn + PF_offset) % T == 0)
        {
            RRC_Paging_msg paging;
            paging.message_type = htonl(100);
            paging.ue_id = htonl(paging_ue_id);
            paging.tac = htonl(TAC);
            paging.cn_domain = htonl(CN_DOMAIN_100);

            sendto(
                udp_skt, 
                &paging, 
                sizeof(paging), 
                0, 
                (struct sockaddr*)&ue_addr, 
                sizeof(ue_addr));

            printf("[gNodeB] Send RRC paging: UE_ID=%u at SFN=%u\n", paging_ue_id, gnb_sfn);
            paging_pending = 0; 
        }
    }
    pthread_mutex_unlock(&paging_mtx);
}

void *ngap_rx(void *arg)
{
    
    struct sockaddr_in amf_addr;
    socklen_t addrlen = sizeof(amf_addr);
    printf("[gNodeB] Waiting NGAP Paging from AMF...\n");

    while(1){
        int conn_fd = accept(tcp_skt,(struct sockaddr*)&amf_addr, &addrlen );
        if (conn_fd < 0){
        perror("[gNodeB] Accept");
        continue;
        }
        NGAP_Paging_msg paging;
        recv(conn_fd, &paging, sizeof(paging), 0);
        pthread_mutex_lock(&paging_mtx);
        paging_ue_id  = ntohl(paging.ue_id);
        paging_tac    = ntohl(paging.tac);
        paging_domain = ntohl(paging.cn_domain);
        paging_pending = 1;
        pthread_mutex_unlock(&paging_mtx);
        printf("[gNodeB] Rx NGAP Paging for UE_ID=%u\n", paging_ue_id);
        close(conn_fd);
        
    }

    return NULL;
}

int main(void){

    pthread_t tid;
    struct sockaddr_in amf_addr;

    udp_skt = socket(AF_INET, SOCK_DGRAM, 0);
    memset(&ue_addr, 0, sizeof(ue_addr));
    ue_addr.sin_family = AF_INET;
    ue_addr.sin_port = htons(UE_PORT);
    inet_pton(AF_INET, UE_IP, &ue_addr.sin_addr);
    

    tcp_skt = socket(AF_INET, SOCK_STREAM, 0);
    memset(&amf_addr, 0, sizeof(amf_addr));
    amf_addr.sin_family = AF_INET;
    amf_addr.sin_port = htons(AMF_PORT);
    amf_addr.sin_addr.s_addr = INADDR_ANY;

    bind(tcp_skt, (struct sockaddr*)&amf_addr, sizeof(amf_addr));
    listen(tcp_skt, 1);
    printf("[gNodeB] Listening AMF on TCP Port=%d\n", AMF_PORT);

    pthread_create(&tid, NULL, ngap_rx, NULL);

    timer_init(10*1000*1000, gnb_t, NULL);
    timer_start();

    while(1) pause();

    return 0;

}
