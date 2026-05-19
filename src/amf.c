#include <arpa/inet.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "messages.h"

#define GNB_IP "127.0.0.1"
#define GNB_TCP_PORT 6000

#define DEFAULT_UE_ID 1001u
#define DEFAULT_RATE 500u
#define DEFAULT_DURATION 10u
#define UE_ID_BASE 1000u
#define DEFAULT_NUM_UE 3u

static void sleep_ns(long ns)
{
    struct timespec ts;

    ts.tv_sec = ns / 1000000000L;
    ts.tv_nsec = ns % 1000000000L;

    nanosleep(&ts, NULL);
}

static int connect_to_gnb(void)
{
    int tcp_skt = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_skt < 0) {
        perror("[AMF] socket");
        return -1;
    }

    struct sockaddr_in gnb_addr;
    memset(&gnb_addr, 0, sizeof(gnb_addr));

    gnb_addr.sin_family = AF_INET;
    gnb_addr.sin_port = htons(GNB_TCP_PORT);

    if (inet_pton(AF_INET, GNB_IP, &gnb_addr.sin_addr) <= 0) {
        perror("[AMF] inet_pton");
        close(tcp_skt);
        return -1;
    }

    if (connect(tcp_skt, (struct sockaddr *)&gnb_addr, sizeof(gnb_addr)) < 0) {
        perror("[AMF] connect");
        close(tcp_skt);
        return -1;
    }

    return tcp_skt;
}

static int send_ngap_paging(int tcp_skt, uint32_t ue_id)
{
    NGAP_Paging_msg msg;

    memset(&msg, 0, sizeof(msg));

    msg.message_type = MSG_TYPE_PAGING;
    msg.ue_id = htonl(ue_id);
    msg.tac = htonl(TAC_DEFAULT);
    msg.cn_domain =htonl(CN_DOMAIN_DATA);

    ssize_t sent = send(tcp_skt, &msg, sizeof(msg), 0);
    if (sent != (ssize_t)sizeof(msg)) {
        perror("[AMF] send");
        return -1;
    }

    return 0;
}

static void run_single_mode(int tcp_skt, uint32_t ue_id)
{
    printf("[AMF] Single NGAP Paging mode\n");
    

    send_ngap_paging(tcp_skt, ue_id);
    printf("[AMF] Send NGAP Paging | UE_ID=%u\n", ue_id);
}

static void run_load_mode(
    int tcp_skt,
    uint32_t rate,
    uint32_t duration_sec,
    uint32_t start_ue_id,
    uint32_t num_ue
)
{
    uint64_t total = (uint64_t)rate * duration_sec;
    long interval_ns = 1000000000L / rate;

    printf("[AMF] Load mode\n");
    printf("[AMF] rate=%u msg/s | duration=%u s | total=%lu\n",
           rate, duration_sec, total);

    for (uint64_t i = 0; i < total; i++) {
        uint32_t ue_id = start_ue_id + (i%num_ue);

        if (send_ngap_paging(tcp_skt, ue_id) < 0) {
            break;
        }

        printf("[AMF] Send NGAP Paging | UE_ID=%u\n", ue_id);

        sleep_ns(interval_ns);
    }
}

int main(int argc, char **argv)
{
    int tcp_skt = connect_to_gnb();
    if (tcp_skt < 0) {
        return 1;
    }

    printf("[AMF] Connected to gNB %s:%d\n", GNB_IP, GNB_TCP_PORT);

    if (argc == 1) {
        run_single_mode(tcp_skt, DEFAULT_UE_ID);
    } else if (argc == 2) {
        uint32_t ue_id = (uint32_t)strtoul(argv[1], NULL, 10);
        run_single_mode(tcp_skt, ue_id);
        
    } else {
        uint32_t rate = (uint32_t)strtoul(argv[1], NULL, 10);
        uint32_t duration_sec = (uint32_t)strtoul(argv[2], NULL, 10);

        uint32_t start_ue_id = DEFAULT_UE_ID;
        uint32_t num_ue = DEFAULT_NUM_UE;

        if (argc >= 4) {
            start_ue_id = (uint32_t)strtoul(argv[3], NULL, 10);
        }

        if (argc >= 5) {
            num_ue = (uint32_t)strtoul(argv[4], NULL, 10);
        }

        run_load_mode(
            tcp_skt,
            rate,
            duration_sec,
            start_ue_id,
            num_ue
        );
    }

    close(tcp_skt);

    return 0;
}