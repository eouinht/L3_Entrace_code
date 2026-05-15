#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <time.h>
#include "messages.h"
#define GNB_IP "127.0.0.1"
#define GNB_TCP_PORT 6000
#define DEFAULT_RATE 500

#define DEFAULT_TIMES 10


static void sleep_ns(long ns) {
    struct timespec ts;
    ts.tv_sec = ns / 1000000000L;
    ts.tv_nsec = ns % 1000000000L;
    nanosleep(&ts, NULL);
}

int main(int argc, char **argv) {
    int rate = (argc >= 2) ? atoi(argv[1]) : DEFAULT_RATE;
    int seconds = (argc >= 3) ? atoi(argv[2]) : DEFAULT_TIMES;
    if (rate <= 0){
        rate = DEFAULT_RATE;
    }
    if (seconds <= 0){
        seconds = DEFAULT_TIMES;
    }

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) { 
        perror("[AMF] socket"); 
        return 1; 
    }

    struct sockaddr_in gnb_addr;
    memset(&gnb_addr, 0, sizeof(gnb_addr));
    gnb_addr.sin_family = AF_INET;
    gnb_addr.sin_port = htons(GNB_TCP_PORT);
    inet_pton(AF_INET, GNB_IP, &gnb_addr.sin_addr);

    if (connect(sockfd, (struct sockaddr *)&gnb_addr, sizeof(gnb_addr)) < 0){
        perror("[AMF] connect");
        close(sockfd);
        return 1;
    }

    printf("[AMF] Connected. Sending %d NGAP Paging msg/s for %d seconds\n", rate, seconds);

    long interval_ns = 1000000000L / rate;
    uint32_t base_ue = 100000;
    uint64_t total = 0;

    for (int s = 0; s < seconds; s++) {
        for (int i = 0; i < rate; i++) {
            NGAP_Paging_msg paging;
            paging.message_type = htonl(MSG_TYPE_PAGING);
            paging.ue_id = htonl(base_ue + (uint32_t)total);
            paging.tac = htonl(TAC_DEFAULT);
            paging.cn_domain = htonl((total % 2) ? CN_DOMAIN_DATA : CN_DOMAIN_PHONE);

            ssize_t n = send(sockfd, &paging, sizeof(paging), 0);
            if (n != sizeof(paging)) {
                perror("[AMF] send");
                close(sockfd);
                return 1;
            }
            total++;
            sleep_ns(interval_ns);
        }
        printf("[AMF][PERF] sent_total=%lu\n", total);
    }

    printf("[AMF] Finished. total_sent=%lu\n", total);
    close(sockfd);
    return 0;
}