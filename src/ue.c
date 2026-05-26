#include <arpa/inet.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "messages.h"
#include "timer.h"
#include "ue.h"

static uint32_t ue_id = DEFAULT_UE_ID;
static uint16_t ue_sfn = 0;
static uint32_t tick_sync = 0;
static int synced = 0;
static uint64_t total_mib = 0;  
static uint64_t total_rrc = 0;

static uint64_t total_rrc_batch = 0;
static uint64_t total_rrc_not_for_ue = 0;
static uint64_t total_rrc_invalid = 0;
static uint64_t total_rrc_not_synced = 0;
static uint64_t total_rrc_wrong_pf = 0;
static uint64_t ue_tick_count = 0;

static void print_ue_stats(void)
{

    printf("[UE %u][STAT] MIB_RX=%lu | RRC_BATCH_RX=%lu | OWN_RRC_RX=%lu | NOT_FOR_UE=%lu | INVALID_RRC=%lu | NOT_SYNCED=%lu | WRONG_PF=%lu |\n",
           ue_id,
           total_mib,
           total_rrc_batch,
           total_rrc,
           total_rrc_not_for_ue,
           total_rrc_invalid,
           total_rrc_not_synced,
           total_rrc_wrong_pf
           );
    fflush(stdout);
}

/*
 * ============================================================
 * Timer tick
 * ============================================================
 *
 * UE tự tăng SFN mỗi 10ms.
 * Khi nhận MIB từ gNB, UE sẽ sync lại SFN.
 */
static void ue_tick(void *arg){  
    (void)arg; 
    ue_sfn = (ue_sfn + 1) % SFN_MOD;
    tick_sync++;
    ue_tick_count++;
    /*
     * 1 tick = 10ms
     * 6000 ticks = 60s
     */
    if (ue_tick_count % 6000 == 0) {
        print_ue_stats();
    }

}

/*
 * Tính độ lệch SFN có xét vòng modulo 1024.
 *
 * Ví dụ:
 *      a = 2, b = 1020
 *      Không nên hiểu lệch là -1018.
 *      Vì SFN quay vòng, lệch đúng gần hơn là +6.
 */
static int sfn_delta(uint16_t a, uint16_t b) {
    int d = (int)a - (int)b;
    if (d > 512){
        d -= 1024;
    } 
    if (d < -512){
        d += 1024;
    } 
    return d;
}

static uint16_t calc_expected_paging_sfn(uint16_t current_sfn, uint32_t UE_ID)
{
    uint32_t ue_id = UE_ID % 1024;
    uint32_t target_offset = (T / N) * (ue_id % N);
    uint16_t sfn = current_sfn;

    while (((sfn + PF_OFFSET) % T) != target_offset) {
        sfn = (sfn + 1) % SFN_MOD;
    }

    return sfn;
}

/*
 * ============================================================
 * UDP broadcast socket
 * ============================================================
 *
 * Nhiều UE process cùng bind vào port 5000.
 * Vì vậy cần SO_REUSEADDR.
 *
 * Trên Linux, nên bật thêm SO_REUSEPORT nếu có.
 */

static int create_broadcast_rx_socket(uint16_t port)
{
    int skt = socket(AF_INET, SOCK_DGRAM, 0);
    if (skt < 0) {
        perror("[UE] socket");
        return -1;
    }

    int opt = 1;

    if (setsockopt(
            skt,
            SOL_SOCKET,
            SO_REUSEADDR,
            &opt,
            sizeof(opt)
        ) < 0) {
        perror("[UE] setsockopt SO_REUSEADDR");
        close(skt);
        return -1;
    }

// #ifdef SO_REUSEPORT
//     if (setsockopt(
//             skt,
//             SOL_SOCKET,
//             SO_REUSEPORT,
//             &opt,
//             sizeof(opt)
//         ) < 0) {
//         perror("[UE] setsockopt SO_REUSEPORT");
//         close(skt);
//         return -1;
//     }
// #endif

    struct sockaddr_in ue_addr;
    memset(&ue_addr, 0, sizeof(ue_addr));

    ue_addr.sin_family = AF_INET;
    ue_addr.sin_port = htons(port);
    ue_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(skt, (struct sockaddr *)&ue_addr, sizeof(ue_addr)) < 0) {
        perror("[UE] bind broadcast port");
        close(skt);
        return -1;
    }

    return skt;
}

static void handle_mib_message(const uint8_t *buf, ssize_t len)
{
    if (len != (ssize_t)sizeof(MIB_msg)) {
        printf("[UE 0x%08X] Invalid MIB size=%zd, expected=%zu\n",
               ue_id,
               len,
               sizeof(MIB_msg));
        fflush(stdout);
        return;
    }

    MIB_msg mib;
    memcpy(&mib, buf, sizeof(mib));

    uint16_t gnb_sfn = ntohs(mib.sfn_value);
    total_mib++;

    if (!synced) {
        ue_sfn = gnb_sfn;
        synced = 1;
        tick_sync = 0;

        printf("[UE 0x%08X] Initial sync | UE_SFN=%u | gNB_SFN=%u | mib_total=%lu\n",
               ue_id,
               ue_sfn,
               gnb_sfn,
               total_mib);
        fflush(stdout);
        return;
    }

    if (tick_sync >= 80) {
        int before = sfn_delta(ue_sfn, gnb_sfn);

        ue_sfn = gnb_sfn;
        tick_sync = 0;

        printf("[UE 0x%08X] Re-sync | UE_SFN=%u | gNB_SFN=%u | delta_before=%d | mib_total=%lu\n",
               ue_id,
               ue_sfn,
               gnb_sfn,
               before,
               total_mib);
        fflush(stdout);
    }
}

/*
 * ============================================================
 * RRC Paging handler
 * ============================================================
 *
 * RRC Paging được gNB broadcast.
 *
 * Tất cả UE nghe được gói này.
 * Nếu UE có ue_id trùng target_ue_i sẽ in log là UE đã nhận được RRC.
 */

static void handle_rrc_paging_message(const uint8_t *buf, ssize_t len)
{
    if (len < (ssize_t)sizeof(uint32_t)){
        printf("[UE %u] Invalid RRC Paging size=%zd\n", ue_id, len);
        fflush(stdout);
        return;
    }

    uint32_t number_records_in;
    memcpy(&number_records_in, buf, sizeof(number_records_in));
    uint32_t number_records = ntohl(number_records_in);

    if (number_records == 0 || number_records > MAX_PAGING_RECORDS){
        printf("[UE 0x%08X] Invalid number_records=%u\n",ue_id, number_records);
        fflush(stdout);
        return;
    }

    size_t msg_len = sizeof(uint32_t) + ((size_t)number_records *sizeof(PagingRecord));
    if((size_t)len != msg_len){
        printf("[UE 0x%08X] Invalid RRC Paging Batch size=%zd, expected=%zu, records=%u\n",
               ue_id,
               len,
               msg_len,
               number_records);
        fflush(stdout);
        return;
    }
    int found = 0;
    uint32_t tac = 0;
    uint32_t cn_domain = 0;
    const uint8_t *record_base = buf + sizeof(uint32_t);

    for (uint32_t i = 0; i < number_records; i++) {
        PagingRecord record;
        memcpy(
            &record,
            record_base + i * sizeof(PagingRecord),
            sizeof(record)
        );
        uint32_t message_type = ntohl(record.message_type);
        uint32_t target_ue_id = ntohl(record.ue_id);

        if (message_type != MSG_TYPE_PAGING){
            continue;
            
        }
        
        if (target_ue_id == ue_id) {
            found = 1;
            tac = ntohl(record.tac);
            cn_domain = ntohl(record.cn_domain);
            break;
        }
        
    }

    if (!found){
        total_rrc_not_for_ue++;
        return;
    }

    if (!synced){
        total_rrc_not_synced++;
        printf("[UE 0x%08X] Ignore own RRC Paging because SFN is not synced yet\n",
               ue_id);
        fflush(stdout);
        return;
    }
    total_rrc_batch++;
    

    int check_sfn = (ue_sfn + PF_OFFSET) % T;
    uint16_t actual_sfn = ue_sfn;
    uint16_t expected_sfn = calc_expected_paging_sfn(ue_sfn, ue_id);

    if(actual_sfn == expected_sfn) {
        total_rrc++;
        printf("[UE 0x%08X] RX RRC Paging Batch | UE_SFN=%u | Expected_SFN=%u | records=%u | TAC=%u | CN_DOMAIN=%u | total_rrc=%lu\n",
               ue_id,
               ue_sfn,
               expected_sfn,
               number_records,
               tac,
               cn_domain,
               total_rrc);
        fflush(stdout);
    }else{
        total_rrc_wrong_pf++;
        printf("[UE 0x%08X] RX own RRC Paging Batch but not paging frame | UE_SFN=%u | Expected_SFN=%u | records=%u\n",
               ue_id,
               ue_sfn,
               expected_sfn,
               number_records);
        fflush(stdout);
    }
}

/*
 * ============================================================
 * Broadcast message dispatcher
 * ============================================================
 *
 * MIB và RRC Paging cùng đi qua port 5000.
 * Vì vậy UE phải đọc byte đầu để biết message type.
 */

static void handle_broadcast_message(const uint8_t *buf, ssize_t len)
{
    if (len <= 0) {
        return;
    }

    if (len == (ssize_t)sizeof(MIB_msg) && buf[0] == MIB_IE1) {
        handle_mib_message(buf, len);
        return;
    }

    if (len >= (ssize_t)(sizeof(uint32_t) + sizeof(PagingRecord))) {

        handle_rrc_paging_message(buf, len);
        
        return;
    }

    printf("[UE 0x%08X] Unknown broadcast message | len=%zd\n",
           ue_id,
           len);
    fflush(stdout);
}

/*
 * ============================================================
 * main
 * ============================================================
 * Cách chạy N Ue ứng với UE_ID (1000 + N), UE_ID này tự đặt, không theo chuẩn:
 *
 *      ./ue 1001
 *      ./ue 1002
 *      ./ue 1003
 *
 */


int main(int argc, char **argv)
{
    uint16_t udp_port = GNB_UDP_PORT;

    if (argc >= 2) {
        ue_id = (uint32_t)strtoul(argv[1], NULL, 0);
    }

    int udp_skt = create_broadcast_rx_socket(udp_port);
    if (udp_skt < 0) {
        return 1;
    }

    timer_init(10L * 1000L * 1000L, ue_tick, NULL);
    timer_start();

    printf("[UE 0x%08X] Listening broadcast UDP port %u\n",
           ue_id,
           udp_port);
    fflush(stdout);

    while (1) {

        uint8_t buf[sizeof(RRC_PagingBatch_msg)];
        ssize_t len = recvfrom(
            udp_skt,
            buf,
            sizeof(buf),
            0,
            NULL,
            NULL
        );

        // printf("[UE %u][RAW] RX UDP len=%zd | first_byte=0x%02x\n",
        //     ue_id,
        //     len,
        //     buf[0]);
        // fflush(stdout);

        if (len < 0) {
            perror("[UE 0x%08X] recvfrom");
            continue;
        }
        
        handle_broadcast_message(buf, len);
    }

    close(udp_skt);

    return 0;
}
