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
#include "gNodeB.h"

static uint16_t gnb_sfn = 0;
static uint32_t tick_count = 0;

static int udp_skt = -1;
static int tcp_skt = -1;
static int amf_conn = -1;

static volatile sig_atomic_t running = 1;

static pthread_mutex_t paging_mutex = PTHREAD_MUTEX_INITIALIZER;
// static int paging_pending = 0;
// static uint32_t pending_ue_id = 0;
static paging_queue_t paging_q;
static void send_rrc_paging(const paging_req_t *req);
static void close_fd_if_open(int *fd)
{
    if (*fd >= 0) {
        close(*fd);
        *fd = -1;
    }
}

static void cleanup_sockets(void)
{
    close_fd_if_open(&amf_conn);
    close_fd_if_open(&tcp_skt);
    close_fd_if_open(&udp_skt);
}

static void handle_sigint(int sig)
{
    (void)sig;
    running = 0;
    cleanup_sockets();
}

static int send_udp_broadcast(
    const char* broadcast_ip, 
    uint16_t broadcast_port, 
    const void*data, 
    size_t len)
    {
    struct sockaddr_in bcast_addr;
    memset(&bcast_addr, 0, sizeof(bcast_addr));

    bcast_addr.sin_family = AF_INET;
    bcast_addr.sin_port = htons(broadcast_port);
    bcast_addr.sin_addr.s_addr = inet_addr(broadcast_ip);
    ssize_t sent = sendto(
        udp_skt,
        data,
        len,
        0,
        (struct sockaddr *)&bcast_addr,
        sizeof(bcast_addr)
    );
    if(sent < 0) {
        perror("[gNB] broadcast MIB sendto");
        return -1;
    }
    if((size_t)sent != len){
        printf("[gNB] Warning: sent %zd/%zu bytes \n", sent, len);
        return -1;
    }
    printf("[gNB] Broadcast MSG | SFN=%u | Broadcast=%d:%s\n",
           gnb_sfn,
           broadcast_port,
           broadcast_ip);

}

static void broadcast_mib(void)
{
    MIB_msg mib;
    memset(&mib, 0, sizeof(mib));
    mib.message_id = MIB_IE1;
    mib.sfn_value = htons(gnb_sfn);
    if(send_udp_broadcast(MIB_BROADCAST_IP, MIB_BROADCAST_PORT, &mib,sizeof(mib)) < 0){
        printf("[gNB][MIB] Failed to broadcast MIB | SFN=%u\n", gnb_sfn);
    }
    printf("[gNB][Send][MIB] Broadcast MIB | SFN=%u | Broadcast=%s:%d\n",
           gnb_sfn,
           MIB_BROADCAST_IP,
           MIB_BROADCAST_PORT);
    
}


// static void *rrc_sender_thread(void *arg){
//     (void)arg;

//     while(running){
//         paging_req_t batch[MAX_REQ_PER_SFN];
//         pthread_mutex_lock(&paging_mutex);
//         uint16_t current_sfn = gnb_sfn;
//         int n = dequeue_paging_at_sfn(&paging_q, batch, current_sfn, MAX_REQ_PER_SFN);
//         pthread_mutex_unlock(&paging_mutex);
//         for(int i = 0; i<n; i++){
//             send_rrc_paging(&batch[i]);
//         }
//         sleep_ns(1000000L);
//     }
//     return NULL;
// }

static void send_rrc_paging(const paging_req_t *req){
    RRC_Paging_msg rrc;
    memset(&rrc, 0, sizeof(rrc));
    rrc.message_type = MSG_TYPE_PAGING;
    rrc.ue_id = htonl(req->ue_id);
    rrc.tac = htonl(req->tac);
    rrc.cn_domain = htonl(req->cn_domain);

    if(send_udp_broadcast(RRC_BROADCAST_IP,RRC_BROADCAST_PORT,&rrc, sizeof(rrc)) <0){
        printf("[gNB][Send][RRC] Failed to broadcast RRC | SFN=%u\n", gnb_sfn);
    }
    printf("[gNB][Send][RRC] Broadcast RRC | SFN =%u | Broadcast=%s:%d\n", 
        gnb_sfn, RRC_BROADCAST_IP, RRC_BROADCAST_PORT);
}

static void broadcast_rrc(uint16_t current_sfn){
    paging_req_t batch[MAX_REQ_PER_SFN];
    pthread_mutex_lock(&paging_mutex);
    int n = dequeue_paging_at_sfn(&paging_q, batch, current_sfn, MAX_REQ_PER_SFN);
    pthread_mutex_unlock(&paging_mutex);
    for (int i = 0; i < n; i++){
        send_rrc_paging(&batch[i]);
    }
}

static uint16_t calc_paging_sfn(uint16_t current_sfn, uint32_t ue_id){
    uint32_t target_offset = (T / N)  * (ue_id % N);
    uint16_t sfn = current_sfn;
    while(((sfn + PF_OFFSET) % T) != target_offset){
        sfn = (sfn + 1) % SFN_MOD;
    }
    return sfn;
}

// Nhận NGAP 
static int parse_ngap_paging(const NGAP_Paging_msg *ngap, paging_req_t *req){
    if(ngap->message_type != MSG_TYPE_PAGING){
        printf("[gNB][Rec] Invalid NGAP type=0x%02x\n",
               ngap->message_type);
        return -1;
    }
    memset(req, 0, sizeof(*req));
    req->message_type = ngap->message_type;
    req->ue_id = ntohl(ngap->ue_id);
    req->tac = ntohl(ngap->tac);
    req->cn_domain = ntohl(ngap->cn_domain);
    req->sfn_to_send = calc_paging_sfn(gnb_sfn, req->ue_id);
    return 0;
        
}

static void enqueue_ngap_paging(const paging_req_t *req){
    pthread_mutex_lock(&paging_mutex);
    enqueue_paging(&paging_q, req);
    // queue_dump(&paging_q);
    pthread_mutex_unlock(&paging_mutex);
}

static void *ngap_receiver_thread(void *arg)
{
    (void)arg;
    printf("[gNB][Rec] NGAP receiver thread started\n");
    // printf("[gNB Rex] Waiting for AMF connection...\n");
    fflush(stdout);

    amf_conn = accept(tcp_skt, NULL, NULL);
    if (amf_conn < 0) {
        perror("[gNB][Rec] accept");
        running = 0;
        return NULL;
    }

    printf("[gNB][Rec] AMF connected\n");
    fflush(stdout);

    while (running) {
        NGAP_Paging_msg ngap;
        paging_req_t req;
        ssize_t len = recv(amf_conn, &ngap, sizeof(ngap), 0);
        if (len == 0) {
            printf("[gNB][Rec] AMF disconnected\n");
            break;
        }

        if (len < 0) {
            perror("[gNB][Rec] recv NGAP");
            break;
        }

        if (len != (ssize_t)sizeof(NGAP_Paging_msg)) {
            printf("[gNB][Rec] Invalid NGAP size: %zd, expected %zu\n",
                   len, sizeof(NGAP_Paging_msg));
            continue;
        }

        if (parse_ngap_paging(&ngap, &req) < 0) {
            continue;
        }
        enqueue_ngap_paging(&req);

        printf("[gNB][Rec] RX NGAP Paging | UE_ID=%u | current_sfn=%u | target_sfn=%u\n",
               req.ue_id,
               gnb_sfn,
               req.sfn_to_send);

        fflush(stdout);
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
    broadcast_rrc(gnb_sfn);
    if (tick_count % 8 == 0){
        broadcast_mib();
    }

}



static int init_udp_socket(void){
    udp_skt = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_skt < 0) {
        perror("[gNB] UDP socket");
        return 1;
    }

    int opt = 1;
    if (setsockopt(udp_skt, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0){
        perror("[gNB] UDP setsockopt SO_REUSEADDR");
        close_fd_if_open(&udp_skt);
        return -1;
        }
    
        if (setsockopt(udp_skt, SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt)) < 0){
        perror("[gNB] UDP setsockopt SO_BROADCAST");
        close_fd_if_open(&udp_skt);
        return -1;
    }
    struct sockaddr_in udp_addr;
    memset(&udp_addr, 0, sizeof(udp_addr));

    udp_addr.sin_family = AF_INET;
    udp_addr.sin_port = htons(GNB_UDP_PORT);
    udp_addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(udp_skt, (struct sockaddr *)&udp_addr, sizeof(udp_addr)) < 0) {
        perror("[gNB] UDP bind");
        close_fd_if_open(&udp_skt);
        return 1;
    }
    printf("[gNB] UDP server listening on port %u for UE\n", GNB_UDP_PORT);

    return 0;
}

static int init_tcp_server(void){
    // TCP for AMF
    tcp_skt = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_skt < 0) {
        perror("[gNB] TCP socket");
        return -1;
    }

    int opt = 1;
    if(setsockopt(tcp_skt, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0){
        perror("[gNB] TCP setsocket SO_REUSEADDR");
        close_fd_if_open(&tcp_skt);
        return -1;
    }
    struct sockaddr_in tcp_addr;
    memset(&tcp_addr, 0, sizeof(tcp_addr));

    tcp_addr.sin_family = AF_INET;
    tcp_addr.sin_port = htons(GNB_TCP_PORT);
    tcp_addr.sin_addr.s_addr = INADDR_ANY;

    
    if (bind(tcp_skt, (struct sockaddr *)&tcp_addr, sizeof(tcp_addr)) < 0){
        perror("[gNB] TCP bind");
        close_fd_if_open(&tcp_skt);
        return -1;
    }

    if (listen(tcp_skt, 5) < 0){
        perror("[gNB] listen");
        close_fd_if_open(&tcp_skt);
        return -1;
    }
    printf("[gNB] TCP server listening on port %u for AMF\n",GNB_TCP_PORT);
    return 0;
}
int main(void)
{
    pthread_t ngap_thread;
    // pthread_t rrc_thread;
    signal(SIGINT, handle_sigint);
    signal(SIGTERM, handle_sigint);

    queue_init(&paging_q);
    if(init_udp_socket()< 0){
        cleanup_sockets();
        return 1;
    }
    if (init_tcp_server() < 0){
        cleanup_sockets();
        return 1;
    }
    // printf("[gNB] TCP server listening on port %u for AMF\n", GNB_TCP_PORT);
    printf("[gNB] Start timer\n");
    timer_init(10L * 1000L * 1000L, gnb_tick, NULL);
    timer_start();

    if (pthread_create(&ngap_thread, NULL, ngap_receiver_thread, NULL) != 0) {
        perror("[gNB] NGAP thread");
        return -1;
    }
    // if(pthread_create(&rrc_thread, NULL, rrc_sender_thread, NULL) != 0){
    //     perror("[gNB] RRC thread");
    //     return -1;
    // }

    while (running) {
        sleep(1);     
    }

    pthread_join(ngap_thread, NULL);
    // pthread_join(rrc_thread, NULL);

    cleanup_sockets();

    printf("[gNB] EXIT\n");
    fflush(stdout);
    return 0;
}