#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "ngap.h"

#define GNB_IP "127.0.0.1"
#define GNB_PORT 6000

#define MSG_TYPE 100 //paging
#define UE_ID 1001   /* UE ID giả lập */
#define TAC 100 // Tracking Area
#define CN_DOMAIN_100 100 
#define CN_DOMAIN_101 101

int main(void){
    int sockfd;
    int ret;
    struct sockaddr_in gnb_addr;
    NGAP_Paging_msg paging;

    // TCP socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(sockfd < 0){
        perror("[AMF] Socket");
        return -1;
    }

    memset(&gnb_addr, 0, sizeof(gnb_addr));
    gnb_addr.sin_family = AF_INET;
    gnb_addr.sin_port = htons(GNB_PORT);
    inet_pton(AF_INET, GNB_IP, &gnb_addr.sin_addr);

    ret = connect(sockfd, (struct sockaddr*)&gnb_addr, sizeof(gnb_addr));
    if(ret < 0){
        perror("[AMF] Connect");
        close(sockfd);
        return -1;
    }
    printf ("[AMF] Connected to gNodeB\n");

    paging.message_type = htonl(MSG_TYPE);
    paging.ue_id = htonl(UE_ID); // Giả sử
    paging.tac = htonl(TAC);
    paging.cn_domain = htonl(CN_DOMAIN_100); // or 101    

    send(sockfd, &paging, sizeof(paging), 0);
    printf("[AMF] Sent NGAP Panging for UE_ID=%u", ntohl(paging.ue_id));
    close(sockfd);
    return 0;
}
