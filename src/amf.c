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

static uint32_t random_ue_id(){
    uint32_t value = 0;
    value |= ((uint32_t)(rand() & 0xFF)) << 24;
    value |= ((uint32_t)(rand() & 0xFF)) << 16;
    value |= ((uint32_t)(rand() & 0xFF)) << 8;
    value |= ((uint32_t)(rand() & 0xFF));
    return value;
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

static uint64_t now_ms(void){
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((uint64_t)ts.tv_sec*1000ULL) + ts.tv_nsec/1000000ULL;
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
    const uint32_t *ue_id_list,
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
    double expected_period_ms = ((double)num_ue*1000.0)/(double)rate;

    
    uint64_t *paging_count_per_ue = calloc(num_ue, sizeof(uint64_t));
    uint64_t *last_send_ms = calloc(num_ue, sizeof(uint64_t));

    if (paging_count_per_ue == NULL || last_send_ms == NULL ) 
    {
        perror("[AMF] calloc");
        free(paging_count_per_ue);
        free(last_send_ms);
        return;
    }


    printf("[AMF] Load mode\n");
    printf("[AMF] rate=%d msg/s | duration=%d s | total=%ld\n",
           rate, duration_sec, total);
    
    for (uint64_t i = 0; i < total; i++) {
        
        uint32_t ue_index = (uint32_t)(i % num_ue);
        uint32_t ue_id = ue_id_list[ue_index];     
        if (send_ngap_paging(tcp_skt, ue_id) < 0) {
            printf("[AMF] Stop load mode because send failed | seq=%lu | UE_ID=0x%08X\n",
                   i + 1,
                   ue_id);
            fflush(stdout);
            break;
        }
        uint64_t t_now = now_ms();
        
        if(last_send_ms[ue_index] == 0){
            printf("[AMF][Send] Seq=%lu | UE_ID=%u | first_send | expected_period=%2.7f ms\n", i+1, ue_id, expected_period_ms);

        }else{
            uint64_t delta_ms = t_now - last_send_ms[ue_index];
            printf("[AMF][Send] Seq=%lu | UE_ID=%u | delta_since_last=%lu ms | expected_period=%2.7f ms\n", i+1, ue_id, delta_ms, expected_period_ms);
        }
        fflush(stdout);
        last_send_ms[ue_index] = t_now;
        paging_count_per_ue[ue_index]++;
        // printf("[AMF] Send NGAP Paging | UE_ID=%u\n", ue_id);
        sleep_ns(interval_ns);
    }
    

    printf("\n[AMF][SUMMARY] NGAP Paging count per UE\n");
    printf("[AMF][SUMMARY] --------------------------------\n");
    uint64_t total_sent = 0;
    for (uint32_t i = 0; i < num_ue; i++) {
        uint32_t ue_id = ue_id_list[i];
        uint64_t count = paging_count_per_ue[i];
        total_sent += count;

        printf("[AMF][SUMMARY] UE_ID=%u | NGAP_PAGING_SENT=%lu\n",
               ue_id,
               count);
    }

    printf("[AMF][SUMMARY] TOTAL_SENT=%lu | EXPECTED_TOTAL=%lu\n",
           total_sent,
           total);
    printf("[AMF][SUMMARY] --------------------------------\n\n");
    fflush(stdout);

    free(paging_count_per_ue);
    free(last_send_ms);
  

}


static int generate_ue_id_file(uint32_t num_ue, const char *path)
{
    FILE *fp = fopen(path, "w");
    if (fp == NULL) {
        perror("[AMF] fopen ue id file");
        return -1;
    }

    for (uint32_t i = 0; i < num_ue; i++) {
        uint32_t ue_id = random_ue_id();

        fprintf(fp, "0x%08X\n", ue_id);
    }

    fclose(fp);

    printf("[AMF] Generated %u UE IDs into %s\n", num_ue, path);
    fflush(stdout);

    return 0;
}

static uint32_t *load_ue_id_file(const char *path, uint32_t *out_num_ue)
{
    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        perror("[AMF] fopen ue_id_file");
        return NULL;
    }

    uint32_t capacity = 1024;
    uint32_t count = 0;

    uint32_t *list = calloc(capacity, sizeof(uint32_t));
    if (list == NULL) {
        perror("[AMF] calloc ue_id_list");
        fclose(fp);
        return NULL;
    }

    char line[128];

    while (fgets(line, sizeof(line), fp) != NULL) {
        if (count >= capacity) {
            capacity *= 2;

            uint32_t *new_list = realloc(list, capacity * sizeof(uint32_t));
            if (new_list == NULL) {
                perror("[AMF] realloc ue_id_list");
                free(list);
                fclose(fp);
                return NULL;
            }

            list = new_list;
        }

        /*
         * base = 0 để đọc được cả:
         *   1001
         *   0xA13F92C0
         */
        list[count] = (uint32_t)strtoul(line, NULL, 0);
        count++;
    }

    fclose(fp);

    *out_num_ue = count;
    return list;
}
int main(int argc, char **argv)
{
    srand((unsigned int)time(NULL));

    /*
     * Generate UE ID file:
     *   ./amf --gen-ids <num_ue> <ue_id_file>
     */
    if (argc == 4 && strcmp(argv[1], "--gen-ids") == 0) {
        uint32_t num_ue = (uint32_t)strtoul(argv[2], NULL, 10);
        const char *ue_id_file = argv[3];

        return generate_ue_id_file(num_ue, ue_id_file);
    }

    int tcp_skt = connect_to_gnb();
    if (tcp_skt < 0) {
        return 1;
    }

    /*
     * Single mode:
     *   ./amf
     */
    if (argc == 1) {
        uint32_t ue_id = random_ue_id();

        printf("[AMF] Single mode | random UE_ID=0x%08X\n", ue_id);
        fflush(stdout);

        run_single_mode(tcp_skt, ue_id);

    /*
     * Single mode with specified UE ID:
     *   ./amf <ue_id>
     * Example:
     *   ./amf 0xA13F92C0
     */
    } else if (argc == 2) {
        uint32_t ue_id = (uint32_t)strtoul(argv[1], NULL, 0);

        printf("[AMF] Single mode | UE_ID=0x%08X\n", ue_id);
        fflush(stdout);

        run_single_mode(tcp_skt, ue_id);

    /*
     * Load mode:
     *   ./amf <rate> <duration_sec> <ue_id_file>
     * Example:
     *   ./amf 500 3600 logs/ue_ids.txt
     */
    } else if (argc == 4) {
        uint32_t rate = (uint32_t)strtoul(argv[1], NULL, 10);
        uint32_t duration_sec = (uint32_t)strtoul(argv[2], NULL, 10);
        const char *ue_id_file = argv[3];

        uint32_t num_ue = 0;
        uint32_t *ue_id_list = load_ue_id_file(ue_id_file, &num_ue);

        if (ue_id_list == NULL || num_ue == 0) {
            printf("[AMF] Failed to load UE IDs from %s\n", ue_id_file);
            free(ue_id_list);
            close(tcp_skt);
            return 1;
        }

        printf("[AMF] Loaded %u UE IDs from %s\n", num_ue, ue_id_file);
        fflush(stdout);

        run_load_mode(
            tcp_skt,
            rate,
            duration_sec,
            ue_id_list,
            num_ue
        );

        free(ue_id_list);

    } else {
        printf("Usage:\n");
        printf("  %s\n", argv[0]);
        printf("  %s <ue_id>\n", argv[0]);
        printf("  %s --gen-ids <num_ue> <ue_id_file>\n", argv[0]);
        printf("  %s <rate> <duration_sec> <ue_id_file>\n", argv[0]);
    }

    close(tcp_skt);
    return 0;
}