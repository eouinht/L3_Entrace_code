#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "messages.h"
#include "queue.h"
#include "timer.h"

#define MIB_IE1 0x01


#define UE_PORT 5000
#define AMF_PORT 6000
#define UE_IP "127.0.0.1"

#define SFN_MOD 1024
#define T 64
#define PF_OFFSET 0

static int tcp_skt = -1;
static int udp_skt = -1;
static struct sockaddr_in ue_addr;

static uint16_t gnb_sfn = 0;
static uint32_t tick_10ms = 0;

static paging_queue_t paging_q;
static pthread_mutex_t q_mtx = PTHREAD_MUTEX_INITIALIZER;

static uint64_t total_rx = 0, total_sent = 0, total_drop = 0;
static uint64_t sec_rx = 0, sec_sent = 0, sec_drop = 0;
static double sec_latency_ms_sum = 0.0;

static volatile sig_atomic_t running = 1;

static double diff_ms(const struct timespec *a, const struct timespec *b){
    return (b->tv_sec - a->tv_sec) * 1000.0 + (b->tv_nsec - a->tv_nsec) / 1000000.0;
}

static int recv_all(int fd, void *buf, size_t len) {
    size_t got = 0;
    char *p = (char *)buf;
    while (got < len) {
        ssize_t n = recv(fd, p + got, len - got, 0);
        if (n == 0) return 0;
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        got += (size_t)n;
    }
    return 1;
}

static void send_rrc_paging(const paging_req_t *req) {
    RRC_Paging_msg rrc;
    rrc.message_type = htonl(MSG_TYPE_PAGING);
    rrc.ue_id = htonl(req->ue_id);
    rrc.tac = htonl(req->tac);
    rrc.cn_domain = htonl(req->cn_domain);

    sendto(udp_skt, &rrc, sizeof(rrc), 0,
           (struct sockaddr *)&ue_addr, sizeof(ue_addr));
}

static void flush_paging_queue_at_pf(void) {
    paging_req_t req;
    struct timespec now;

    while (1) {
        pthread_mutex_lock(&q_mtx);
        int ok = dequeue_paging(&paging_q, &req);
        pthread_mutex_unlock(&q_mtx);

        if (ok != 0) break;

        send_rrc_paging(&req);
        clock_gettime(CLOCK_MONOTONIC, &now);
        sec_latency_ms_sum += diff_ms(&req.rx_time, &now);
        sec_sent++;
        total_sent++;
    }
}

static void gnb_tick(void *arg) {
    (void)arg;
    gnb_sfn = (gnb_sfn + 1) % SFN_MOD;
    tick_10ms++;

    if (tick_10ms % 8 == 0) {
        MIB_msg mib;
        mib.message_id = MIB_IE1;
        mib.sfn_value = htons(gnb_sfn);
        sendto(udp_skt, &mib, sizeof(mib), 0,
               (struct sockaddr *)&ue_addr, sizeof(ue_addr));
    }

    if (((gnb_sfn + PF_OFFSET) % T) == 0) {
        flush_paging_queue_at_pf();
    }
}

static void *ngap_rx_thread(void *arg) {
    (void)arg;
    printf("[gNB] TCP server listening AMF on port %d\n", AMF_PORT);

    while (running) {
        struct sockaddr_in amf_addr;
        socklen_t len = sizeof(amf_addr);
        int conn_fd = accept(tcp_skt, (struct sockaddr *)&amf_addr, &len);
        if (conn_fd < 0) {
            if (errno == EINTR) continue;
            perror("[gNB] accept");
            continue;
        }

        printf("[gNB] AMF connected\n");
        while (running) {
            NGAP_Paging_msg msg;
            int r = recv_all(conn_fd, &msg, sizeof(msg));
            if (r <= 0) break;

            uint32_t type = ntohl(msg.message_type);
            if (type != MSG_TYPE_PAGING) continue;

            paging_req_t req;
            req.ue_id = ntohl(msg.ue_id);
            req.tac = ntohl(msg.tac);
            req.cn_domain = ntohl(msg.cn_domain);
            clock_gettime(CLOCK_MONOTONIC, &req.rx_time);

            pthread_mutex_lock(&q_mtx);
            int ok = enqueue_paging(&paging_q, &req);
            pthread_mutex_unlock(&q_mtx);

            if (ok == 0) {
                sec_rx++;
                total_rx++;
            } else {
                sec_drop++;
                total_drop++;
            }
        }
        close(conn_fd);
        printf("[gNB] AMF disconnected\n");
    }
    return NULL;
}

static void *perf_thread(void *arg) {
    (void)arg;
    while (running) {
        sleep(1);
        pthread_mutex_lock(&q_mtx);
        int qs = queue_size(&paging_q);
        pthread_mutex_unlock(&q_mtx);

        double avg_latency = sec_sent ? sec_latency_ms_sum / (double)sec_sent : 0.0;
        printf("[PERF] rx=%lu msg/s | sent=%lu msg/s | drop=%lu msg/s | queue=%d | avg_latency=%.2f ms | total_rx=%lu total_sent=%lu total_drop=%lu\n",
               sec_rx, sec_sent, sec_drop, qs, avg_latency, total_rx, total_sent, total_drop);

        sec_rx = sec_sent = sec_drop = 0;
        sec_latency_ms_sum = 0.0;
    }
    return NULL;
}

static void handle_sigint(int sig) {
    (void)sig;
    running = 0;
    timer_stop();
    close(tcp_skt);
    close(udp_skt);
}

int main(void) {
    signal(SIGINT, handle_sigint);
    queue_init(&paging_q);

    udp_skt = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_skt < 0) { perror("udp socket"); return 1; }

    memset(&ue_addr, 0, sizeof(ue_addr));
    ue_addr.sin_family = AF_INET;
    ue_addr.sin_port = htons(UE_PORT);
    inet_pton(AF_INET, UE_IP, &ue_addr.sin_addr);

    tcp_skt = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_skt < 0) { perror("tcp socket"); return 1; }

    int opt = 1;
    setsockopt(tcp_skt, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in amf_addr;
    memset(&amf_addr, 0, sizeof(amf_addr));
    amf_addr.sin_family = AF_INET;
    amf_addr.sin_port = htons(AMF_PORT);
    amf_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(tcp_skt, (struct sockaddr *)&amf_addr, sizeof(amf_addr)) < 0) {
        perror("bind"); return 1;
    }
    if (listen(tcp_skt, 16) < 0) { perror("listen"); return 1; }

    pthread_t rx_tid, perf_tid;
    pthread_create(&rx_tid, NULL, ngap_rx_thread, NULL);
    pthread_create(&perf_tid, NULL, perf_thread, NULL);

    timer_init(10L * 1000L * 1000L, gnb_tick, NULL);
    timer_start();

    while (running) pause();
    return 0;
}
