#include <arpa/inet.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "messages.h"
#include "timer.h"
#include "amf.h"

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

    msg.message_type = htonl(MSG_TYPE_PAGING);
    msg.ue_id = htonl(ue_id);
    msg.tac = htonl(TAC_DEFAULT);
    msg.cn_domain =htonl(CN_DOMAIN_DATA);

    ssize_t sent = send(tcp_skt, &msg, sizeof(msg), 0);
    if(sent < 0){ 
        perror("[AMF] Senf NGAP Paging");
        return -1;
    }
    if (sent != (ssize_t)sizeof(msg)) {
        printf("[AMF] Warning: sent %zd/%zu bytes\n",
               sent,
               sizeof(msg));
        return -1;      
    }

    return 0;
}
/*
 * ============================================================
 * Single mode
 * ============================================================
 *
 * Chạy 1 lần cho 1 UE.
 *
 * Cách chạy:
 *      ./amf
 *      ./amf 1005
 */

static void run_single_mode(int tcp_skt, uint32_t ue_id)
{
    printf("[AMF] Single NGAP Paging mode\n");
    send_ngap_paging(tcp_skt, ue_id);
    printf("[AMF] Send NGAP Paging | UE_ID=%u\n", ue_id);
}


/*
 * ============================================================
 * Load mode
 * ============================================================
 *
 * Cách chạy:
 *      ./amf <rate> <duration_sec> <start_ue_id> <num_ue>
 *
 * Ví dụ:
 *      ./amf 10 60 1001 10
 *
 * Nghĩa là:
 *      rate        = 10 msg/s
 *      duration    = 60 giây
 *      start_ue_id = 1001
 *      num_ue      = 10
 *
 * UE_ID sẽ quay vòng:
 *      1001, 1002, ..., 1010,
 *      1001, 1002, ..., 1010,
 *      ...
 */

static void run_load_mode(
    int tcp_skt,
    uint32_t rate,
    uint32_t duration_sec,
    uint32_t start_ue_id,
    uint32_t num_ue
)
{
    if (rate == 0) {
        printf("[AMF] Invalid rate=0\n");
        return;
    }

    if (num_ue == 0) {
        printf("[AMF] Invalid num_ue=0\n");
        return;
    }
    uint64_t total = (uint64_t)rate * duration_sec;
    long interval_ns = 1000000000L / rate;

    printf("[AMF] Load mode\n");
    printf("[AMF] rate=%d msg/s | duration=%d s | total=%ld\n",
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