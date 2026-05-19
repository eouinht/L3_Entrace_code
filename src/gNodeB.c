#include <arpa/inet.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include "messages.h"
#include "queue.h"
#include "timer.h"

#define SFN_MOD 1024

#define GNB_TCP_PORT 6000
#define LOCAL_IP "127.0.0.1"

#define T 64
#define N 1
#define PF_OFFSET 0

#define UE_ID_BASE 1000
#define UE_PORT_BASE 5000
#define TEST_UE_COUNT 3
#define GNB_UDP_PORT 5000

static uint16_t gnb_sfn = 0;
static uint32_t tick_count = 0;


static int udp_skt = -1;
static int tcp_skt = -1;
static int amf_conn = -1;
static pthread_mutex_t paging_mutex = PTHREAD_MUTEX_INITIALIZER;
static int paging_pending = 0;
static uint32_t pending_ue_id = 0;

static volatile sig_atomic_t running = 1;

static paging_queue_t paging_q;

// typedef struct{
//     uint32_t message_type;
//     uint32_t ue_id;
//     uint32_t tac;
//     uint32_t cn_domain;
//     uint16_t sfn_to_send;
// } __attribute__((packed)) paging_req_t;
static paging_req_t pending_req;

static void handle_sigint(int sig)
{
    (void)sig;
    running = 0;

    if (amf_conn >= 0) {
        close(amf_conn);
        amf_conn = -1;
    }

    if (tcp_skt >= 0) {
        close(tcp_skt);
        tcp_skt = -1;
    }

    if (udp_skt >= 0) {
        close(udp_skt);
        udp_skt = -1;
    }
}

static uint16_t ue_port_from_id(uint32_t ue_id)
{
    if (ue_id <= UE_ID_BASE){
        return 0;
    }

    return (uint16_t)(UE_PORT_BASE + (ue_id - UE_ID_BASE));
}

static void send_udp_to_ue(uint16_t port, const void *data, size_t len)
{
    struct sockaddr_in ue_addr;
    memset(&ue_addr, 0, sizeof(ue_addr));

    ue_addr.sin_family = AF_INET;
    ue_addr.sin_port = htons(port);
    inet_pton(AF_INET, LOCAL_IP, &ue_addr.sin_addr);

    sendto(
        udp_skt,
        data,
        len,
        0,
        (struct sockaddr *)&ue_addr,
        sizeof(ue_addr)
    );
}

static void broadcast_mib(void)
{
    MIB_msg mib;

    mib.message_id = MIB_IE1;
    mib.sfn_value = htons(gnb_sfn);

    for (uint32_t i = 1; i <= TEST_UE_COUNT; i++){
        uint16_t port = (uint16_t)(UE_PORT_BASE + i);

        send_udp_to_ue(
            port,
            &mib,
            sizeof(mib)
        );
        printf("[gNB] Broadcast MIB at Port=%u | SFN=%u\n", port,gnb_sfn);
    }

    // printf("[gNB] Broadcast MIB | SFN=%u\n", gnb_sfn);
}

static int is_paging_frame(uint16_t sfn, uint32_t ue_id)
{
    return ((sfn + PF_OFFSET) % T) ==
           ((T / N) * (ue_id % N));
}



static uint16_t calc_paging_sfn(uint16_t current_sfn, uint32_t ue_id){
    uint32_t target_offset = (T / N)  * (ue_id % N);
    uint16_t sfn = current_sfn;
    while(((sfn + PF_OFFSET) % T) != target_offset){
        sfn = (sfn + 1) % SFN_MOD;
    }
    return sfn;
}
static void send_rrc_paging(const paging_req_t *req)
{
    uint16_t port = ue_port_from_id(req->ue_id);

    if (port == 0){
        printf("[gNB Send] Invalid UE_ID=%u, cannot calculate UDP port\n", req->ue_id);
        return;
    }

    RRC_Paging_msg rrc;
    memset(&rrc, 0, sizeof(rrc));
    rrc.message_type = MSG_TYPE_PAGING;
    rrc.ue_id = htonl(req->ue_id);
    rrc.tac = htonl(req->tac);
    rrc.cn_domain = htonl(req->cn_domain);

    send_udp_to_ue(
        port,
        &rrc,
        sizeof(rrc)
    );

    printf("[gNB Send] Send RRC Paging | UE_ID=%u | port=%u | SFN=%u\n",
           req->ue_id, port, gnb_sfn);
}


// Nhận NGAP 
static void *ngap_receiver_thread(void *arg)
{
    (void)arg;
    printf("[gNB Rec] NGAP receiver thread started\n");
    printf("[gNB Rex] Waiting for AMF connection...\n");
    fflush(stdout);

    amf_conn = accept(tcp_skt, NULL, NULL);
    if (amf_conn < 0) {
        perror("[gNB Rec] accept");
        running = 0;
        return NULL;
    }

    printf("[gNB Rec] AMF connected\n");
    fflush(stdout);

    while (running) {
        NGAP_Paging_msg ngap;
        paging_req_t req;

        ssize_t len = recv(amf_conn, &ngap, sizeof(ngap), 0);

        // printf("[gNB recv] NGAP len=%ld expected=%lu\n",
        //        len, sizeof(NGAP_Paging_msg));
        // fflush(stdout);

        if (len == 0) {
            printf("[gNB Rec] AMF disconnected\n");
            break;
        }

        if (len < 0) {
            perror("[gNB Rec] recv NGAP");
            break;
        }

        if (len != (ssize_t)sizeof(NGAP_Paging_msg)) {
            printf("[gNB Rec] Invalid NGAP size\n");
            continue;
        }

        if (ngap.message_type != MSG_TYPE_PAGING) {
            printf("[gNB Rec] Invalid NGAP type=0x%02x\n", ngap.message_type);
            continue;
        }
        req.ue_id = ntohl(ngap.ue_id);
        req.tac = ntohl(ngap.tac);
        req.cn_domain = ntohl(ngap.cn_domain);
        req.message_type = ntohl(ngap.message_type);
        req.sfn_to_send = calc_paging_sfn(gnb_sfn, req.ue_id);
        
        pthread_mutex_lock(&paging_mutex);
        enqueue_paging(&paging_q, &req);
        // queue_dump(&paging_q);
        pthread_mutex_unlock(&paging_mutex);

        // uint32_t ue_id = ntohl(ngap.ue_id);

        printf("[gNB Rec] RX NGAP Paging from AMF | UE_ID=%u | current_sfn=%u\n",
               req.ue_id, gnb_sfn);
        fflush(stdout);

        pthread_mutex_lock(&paging_mutex);
        paging_pending = 1;
        // pending_ue_id = ue_id;
        pending_req = req;
        pthread_mutex_unlock(&paging_mutex);
    }

    running = 0;
    return NULL;
}

// Tăng time mỗi 10ms, gửi MIB(nếu đúng time), gửi RRC(nếu đúng time)
static void gnb_tick(void *arg)
{
    (void)arg;

    gnb_sfn = (gnb_sfn + 1) % SFN_MOD;
    tick_count++;

    if (tick_count % 8 == 0){
        broadcast_mib();
    }

    // pthread_mutex_lock(&paging_mutex);

    // if (paging_pending ){

    
    //     uint32_t ue_id = pending_ue_id;
    //     paging_req_t req = pending_req;

    //     if(is_paging_frame(gnb_sfn, req.ue_id)) {
        
    //         paging_pending = 0;
    //         pthread_mutex_unlock(&paging_mutex);
    //         send_rrc_paging(&req);
    //         return;
    //     }
    //     // printf("[gNB] Paging Pending | UE_ID=%u | current_sfn=%u | wait SFN = %u\n", ue_id, gnb_sfn, calc_paging_sfn(gnb_sfn, ue_id));
          
    // }

    // pthread_mutex_unlock(&paging_mutex);
}

static void *rrc_sender_thread(void *arg){
    (void)arg;

    while(running){
        paging_req_t batch[MAX_REQ_PER_SFN];
        pthread_mutex_lock(&paging_mutex);
        uint16_t current_sfn = gnb_sfn;
        int n = dequeue_paging_at_sfn(&paging_q, batch, current_sfn, MAX_REQ_PER_SFN);
        pthread_mutex_unlock(&paging_mutex);
        for(int i = 0; i<n; i++){
            send_rrc_paging(&batch[i]);
        }
        sleep_ns(1000000L);
    }
    return NULL;
}
int main(void)
{

    signal(SIGINT, handle_sigint);
    signal(SIGTERM, handle_sigint);

    queue_init(&paging_q);
    // UDP for UE
    udp_skt = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_skt < 0) {
        perror("[gNB] UDP socket");
        return 1;
    }

    int opt = 1;
    setsockopt(udp_skt, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));


    struct sockaddr_in udp_addr;
    memset(&udp_addr, 0, sizeof(udp_addr));

    udp_addr.sin_family = AF_INET;
    udp_addr.sin_port = htons(GNB_UDP_PORT);
    udp_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(udp_skt, (struct sockaddr *)&udp_addr, sizeof(udp_addr)) < 0) {
        perror("[gNB] UDP bind");
        close(udp_skt);
        return 1;
    }

    printf("[gNB] UDP server listening on port %u for UE\n", GNB_UDP_PORT);

    // TCP for AMF
    tcp_skt = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_skt < 0) {
        perror("[gNB] TCP socket");
        close(udp_skt);
        return 1;
    }

    setsockopt(tcp_skt, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    // #ifdef SO_REUSEPORT
    // setsockopt(
    //     tcp_skt,
    //     SOL_SOCKET,
    //     SO_REUSEPORT,
    //     &opt,
    //     sizeof(opt)
    // );
    // #endif
    struct sockaddr_in tcp_addr;
    memset(&tcp_addr, 0, sizeof(tcp_addr));

    tcp_addr.sin_family = AF_INET;
    tcp_addr.sin_port = htons(GNB_TCP_PORT);
    tcp_addr.sin_addr.s_addr = INADDR_ANY;

    
    if (bind(tcp_skt, (struct sockaddr *)&tcp_addr, sizeof(tcp_addr)) < 0){
        perror("[gNB] TCP bind");
        close(tcp_skt);
        close(udp_skt);
        return 1;
    }

    if (listen(tcp_skt, 5) < 0){
        perror("[gNB] listen");
        close(tcp_skt);
        close(udp_skt);
        return 1;
    }
    printf("[gNB] TCP server listening on port %u for AMF\n", GNB_TCP_PORT);

    timer_init(10L * 1000L * 1000L, gnb_tick, NULL);
    timer_start();

    printf("[gNB] Started | TCP port=%d | UDP port = %d | SFN timer=10ms\n", GNB_TCP_PORT,GNB_UDP_PORT);

    pthread_t ngap_thread;
    pthread_t rrc_thread;

    if (pthread_create(&ngap_thread, NULL, ngap_receiver_thread, NULL) != 0) {
        perror("[gNB] NGAP thread");
        return 1;
    }
    if(pthread_create(&rrc_thread, NULL, rrc_sender_thread, NULL) != 0){
        perror("[gNB] RRC thread");
        return 1;
    }

    while (running) {
        sleep(1);
       
    }

    pthread_join(ngap_thread, NULL);
    pthread_join(rrc_thread, NULL);

    if (amf_conn >= 0) close(amf_conn);
    if (tcp_skt >= 0) close(tcp_skt);
    if (udp_skt >= 0) close(udp_skt);

    printf("[gNB] EXIT\n");
    fflush(stdout);
    return 0;
}