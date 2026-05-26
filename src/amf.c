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

static const uint32_t watched_ue_ids[] =  {
    // UE_ID        
    0x11111040,   
    0x22222081,  
    0x333330C2,  
    0x44444103,   
    0x55555144    
};
#define WATCHED_PROB_PERCENT 1
#define WATCHED_UE_COUNT ((uint32_t)(sizeof(watched_ue_ids) / sizeof(watched_ue_ids[0])))
#define WATCHED_INSERT_INTERVAL 500

typedef struct {
    uint32_t ue_id;
    uint64_t count;
    uint64_t last_send_ms;
    uint64_t sum_delta_ms;
    uint64_t min_delta_ms;
    uint64_t max_delta_ms;
} watched_ue_stat_t;

static uint32_t random_ue_id(){
    uint32_t value = 0;
    value |= ((uint32_t)(rand() & 0xFF)) << 24;
    value |= ((uint32_t)(rand() & 0xFF)) << 16;
    value |= ((uint32_t)(rand() & 0xFF)) << 8;
    value |= ((uint32_t)(rand() & 0xFF));
    return value;
}

static uint32_t choose_ue_id(uint64_t seq)
{
    static uint32_t watched_rr_index = 0;

    if (seq % WATCHED_INSERT_INTERVAL == 0) {
        uint32_t idx = watched_rr_index % WATCHED_UE_COUNT;
        watched_rr_index++;

        return watched_ue_ids[idx];
    }

    return random_ue_id();
}

static int find_watched_index(uint32_t ue_id)
{
    for (uint32_t i = 0; i < WATCHED_UE_COUNT; i++) {
        if (watched_ue_ids[i] == ue_id) {
            return (int)i;
        }
    }

    return -1;
}
static void build_sfn_list_string(
    uint32_t UE_ID,
    char *buf,
    size_t buf_size
)
{
    if (buf == NULL || buf_size == 0) {
        return;
    }

    buf[0] = '\0';

    /*
     * Công thức:
     * ue_id = UE_ID % 1024
     * target_offset = (T / N) * (ue_id % N)
     *
     * Với T = 64, N = 64:
     * target_offset = UE_ID % 64
     */

    uint32_t ue_id = UE_ID % SFN_MOD;
    uint32_t target_offset = (T / N) * (ue_id % N);

    for (uint16_t sfn = 0; sfn < SFN_MOD; sfn++) {
        if (((sfn + PF_OFFSET) % T) == target_offset){
            char tmp[32];

            snprintf(tmp, sizeof(tmp), "%u ", sfn);

            if (strlen(buf) + strlen(tmp) + 1 < buf_size){
                strcat(buf, tmp);
            }
        }
    }
}

static int export_ue_sfn_list_csv(
    const char *path,
    const uint32_t *ue_list,
    uint32_t ue_count
)
{
    FILE *fp = fopen(path, "w");
    if (fp == NULL) {
        perror("[AMF] fopen ue_sfn_list csv");
        return -1;
    }

    /*
     * Excel columns:
     * 1. ue_id_hex  : UE ID in hexadecimal
     * 2. ue_id_dec  : UE ID in decimal
     * 3. sfn_list   : all valid SFNs for this UE
     */
    fprintf(fp, "ue_id_hex,ue_id_dec,sfn_list\n");

    for (uint32_t i = 0; i < ue_count; i++) {
        char sfn_list[512];

        build_sfn_list_string(
            ue_list[i],
            sfn_list,
            sizeof(sfn_list)
        );

        fprintf(fp, "\"\"\"0x%08X\"\"\",%u,%s\n",
            ue_list[i],
            ue_list[i],
            sfn_list);
    }

    fclose(fp);

    printf("[AMF] Exported UE SFN list to %s\n", path);
    fflush(stdout);

    return 0;
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
    uint32_t duration_sec
){
    if (rate == 0) {
        printf("[AMF] Invalid rate=0\n");
        return;
    }

    uint64_t total = (uint64_t)rate * duration_sec;
    long interval_ns = 1000000000L / rate;

    uint64_t total_sent = 0;
    uint64_t watched_sent = 0;
    uint64_t random_sent = 0;
   
    uint64_t watched_count[WATCHED_UE_COUNT];
    memset(watched_count, 0, sizeof(watched_count));

    printf("[AMF] Load mode\n");
    printf("[AMF] rate=%d msg/s | duration=%d s | total=%ld\n",
           rate, duration_sec, total);
    
    printf("[AMF] watched_probability=%d%%\n", WATCHED_PROB_PERCENT);
    for (uint32_t i = 0; i < WATCHED_UE_COUNT; i++) {
        printf("[AMF][WATCHED] index=%u | UE_ID=0x%08X \n",
               i,
               watched_ue_ids[i]
              );
    }
    fflush(stdout);

    
    for (uint64_t i = 0; i < total; i++) {
        uint32_t ue_id = choose_ue_id(i);  
      

        if (send_ngap_paging(tcp_skt, ue_id) < 0) {
            printf("[AMF] Stop load mode because send failed | seq=%lu | UE_ID=0x%08X\n",
                   i + 1,
                   ue_id);
            fflush(stdout);
            break;
        }
        total_sent++;
        int watched_idx = find_watched_index(ue_id);

        if (watched_idx >= 0) {
            watched_count[watched_idx]++;
            watched_sent++;
        } else {
            random_sent++;
        }

        printf("[AMF][SEND] seq=%lu | UE_ID=0x%08X \n",
               i,
               ue_id
               );
        fflush(stdout);
        // printf("[AMF] Send NGAP Paging | UE_ID=%u\n", ue_id);
        sleep_ns(interval_ns);
    }
    
    printf("\n[AMF][SUMMARY]\n");
    printf("[AMF][SUMMARY] TOTAL_SENT=%lu | EXPECTED_TOTAL=%lu\n",
           total_sent,
           total);
    printf("[AMF][SUMMARY] WATCHED_SENT=%lu | RANDOM_SENT=%lu\n",
           watched_sent,
           random_sent);
    printf("[AMF][SUMMARY] --------------------------------\n");

    for (uint32_t i = 0; i < WATCHED_UE_COUNT; i++) {
        double period_sec = 0.0;

        if (watched_count[i] > 0) {
            period_sec = (double)duration_sec / (double)watched_count[i];
        }

        printf("[AMF][SUMMARY] UE_ID=0x%08X | NGAP_SENT=%lu | avg_period=%.2f s/msg u\n",
               watched_ue_ids[i],
               watched_count[i],
               period_sec);
    }

    printf("[AMF][SUMMARY] --------------------------------\n\n");
    fflush(stdout);
  

}

static void run_fixed_ue_load_mode(
    int tcp_skt,
    uint32_t rate,
    uint32_t duration_sec,
    uint32_t ue_id
)
{
    if (rate == 0) {
        printf("[AMF] Invalid rate=0\n");
        return;
    }

    uint64_t total = (uint64_t)rate * duration_sec;
    long interval_ns = 1000000000L / rate;

    uint64_t sent_count = 0;

    printf("[AMF] Fixed UE load mode\n");
    printf("[AMF] rate=%u msg/s | duration=%u s | total=%lu\n",
           rate,
           duration_sec,
           total);
    printf("[AMF] target UE_ID=0x%08X | UE_ID_DEC=%u\n",
           ue_id,
           ue_id);
    fflush(stdout);

    for (uint64_t seq = 0; seq < total; seq++) {
        if (send_ngap_paging(tcp_skt, ue_id) < 0) {
            printf("[AMF] Stop fixed UE mode because send failed | seq=%lu | UE_ID=0x%08X\n",
                   seq,
                   ue_id);
            fflush(stdout);
            break;
        }

        sent_count++;

        printf("[AMF][SEND] seq=%lu | mode=FIXED | UE_ID=0x%08X | UE_ID_DEC=%u\n",
               seq,
               ue_id,
               ue_id);
        fflush(stdout);

        sleep_ns(interval_ns);
    }

    double avg_period_sec = 0.0;
    if (sent_count > 0) {
        avg_period_sec = (double)duration_sec / (double)sent_count;
    }

    printf("\n[AMF][SUMMARY] mode=FIXED\n");
    printf("[AMF][SUMMARY] UE_ID=0x%08X | UE_ID_DEC=%u | NGAP_SENT=%lu | avg_period=%.2f s/msg\n",
           ue_id,
           ue_id,
           sent_count,
           avg_period_sec);
    printf("[AMF][SUMMARY] EXPECTED_TOTAL=%lu | ACTUAL_SENT=%lu\n\n",
           total,
           sent_count);
    fflush(stdout);
}

int main(int argc, char **argv)
{
    srand((unsigned int)time(NULL));
    export_ue_sfn_list_csv(
        "logs/ue_sfn_list.csv",
        watched_ue_ids,
        WATCHED_UE_COUNT
    );

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
    } else if (argc == 3) {
        uint32_t rate = (uint32_t)strtoul(argv[1], NULL, 10);
        uint32_t duration_sec = (uint32_t)strtoul(argv[2], NULL, 10);

        run_load_mode(
            tcp_skt,
            rate,
            duration_sec );

    }  else if (argc == 4) {
    uint32_t rate = (uint32_t)strtoul(argv[1], NULL, 10);
    uint32_t duration_sec = (uint32_t)strtoul(argv[2], NULL, 10);
    uint32_t ue_id = (uint32_t)strtoul(argv[3], NULL, 0);

    /*
     * Functional fixed UE mode:
     * ./amf <rate> <duration_sec> <ue_id>
     */
    run_fixed_ue_load_mode(tcp_skt, rate, duration_sec, ue_id);
    }else {
         printf("Usage:\n");
        printf("  %s\n", argv[0]);
        printf("  %s <ue_id>\n", argv[0]);
        printf("  %s <rate> <duration_sec>\n", argv[0]);
        printf("  %s <rate> <duration_sec> <ue_id>\n", argv[0]);
        printf("\nExamples:\n");
        printf("  %s 0x11111040\n", argv[0]);
        printf("  %s 500 3600\n", argv[0]);
        printf("  %s 1 60 0x11111040\n", argv[0]);
    }

    close(tcp_skt);
    return 0;
}