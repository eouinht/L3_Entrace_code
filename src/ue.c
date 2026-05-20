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

// #define SFN_MOD 1024u
// #define DEFAULT_UE_PORT 5001u
// #define DEFAULT_UE_ID 1001u


// #define T 64u
// #define N 1u
// #define PF_OFFSET 0u
// #define GNB_IP "127.0.0.1"
// #define GNB_UDP_PORT 5000u

static uint32_t ue_id = DEFAULT_UE_ID;

static uint16_t ue_sfn = 0;
static uint32_t tick_sync = 0;
static int synced = 0;

static uint64_t total_mib = 0;  
static uint64_t total_rrc = 0;

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



/*
 * ============================================================
 * UDP broadcast socket
 * ============================================================
 *
 * Nhiều UE process cùng bind vào port 6000.
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

#ifdef SO_REUSEPORT
    if (setsockopt(
            skt,
            SOL_SOCKET,
            SO_REUSEPORT,
            &opt,
            sizeof(opt)
        ) < 0) {
        perror("[UE] setsockopt SO_REUSEPORT");
        close(skt);
        return -1;
    }
#endif

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
        printf("[UE %u] Invalid MIB size=%zd, expected=%zu\n",
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

        printf("[UE %u] Initial sync | UE_SFN=%u | gNB_SFN=%u | mib_total=%lu\n",
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

        printf("[UE %u] Re-sync | UE_SFN=%u | gNB_SFN=%u | delta_before=%d | mib_total=%lu\n",
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
 * RRC Paging bây giờ được gNB broadcast.
 *
 * Tất cả UE nghe được gói này.
 * Nhưng chỉ UE có ue_id trùng target_ue_id mới xử lý.
 */

static void handle_rrc_paging_message(const uint8_t *buf, ssize_t len)
{
    if (len != (ssize_t)sizeof(RRC_Paging_msg)) {
        printf("[UE %u] Invalid RRC Paging size=%zd, expected=%zu\n",
               ue_id,
               len,
               sizeof(RRC_Paging_msg));
        fflush(stdout);
        return;
    }

    RRC_Paging_msg rrc;
    memcpy(&rrc, buf, sizeof(rrc));

    uint32_t target_ue_id = ntohl(rrc.ue_id);
    if (target_ue_id != ue_id) {
        return;
    }
    if (!synced) {
        printf("[UE %u] Ignore own RRC Paging because SFN is not synced yet\n",
               ue_id);
        fflush(stdout);
        return;
    }

    total_rrc++;

    int check_sfn = (ue_sfn + PF_OFFSET) % T;

    if (check_sfn == 0) {
        printf("[UE %u] RX RRC Paging | Target UE_ID=%u | UE_SFN=%u | total_rrc=%lu\n",
               ue_id,
               target_ue_id,
               ue_sfn,
               total_rrc);
        fflush(stdout);
    } else {
        printf("[UE %u] RX own RRC Paging but not paging frame | Target UE_ID=%u | UE_SFN=%u | check_sfn=%d\n",
               ue_id,
               target_ue_id,
               ue_sfn,
               check_sfn);
        fflush(stdout);
    }
}

/*
 * ============================================================
 * Broadcast message dispatcher
 * ============================================================
 *
 * MIB và RRC Paging cùng đi qua port 6000.
 * Vì vậy UE phải đọc byte đầu để biết message type.
 */

static void handle_broadcast_message(const uint8_t *buf, ssize_t len)
{
    if (len <= 0) {
        return;
    }

    uint8_t msg_type = buf[0];

    if (msg_type == MIB_IE1) {
        handle_mib_message(buf, len);
        return;
    }

    if (msg_type == MSG_TYPE_PAGING) {
        handle_rrc_paging_message(buf, len);
        return;
    }

    printf("[UE %u] Unknown broadcast message type=0x%02x | len=%zd\n",
           ue_id,
           msg_type,
           len);
    fflush(stdout);
}

/*
 * ============================================================
 * main
 * ============================================================
 *
 * Cách chạy mới:
 *
 *      ./ue 1001
 *      ./ue 1002
 *      ./ue 1003
 *
 * hoặc nếu muốn truyền port:
 *
 *      ./ue 1001 6000
 *      ./ue 1002 6000
 *      ./ue 1003 6000
 *
 * Không dùng:
 *
 *      ./ue 1001 5001
 *      ./ue 1002 5002
 *
 * nữa, vì gNB không broadcast xuống 5001/5002.
 */

int main(int argc, char **argv)
{
    uint16_t udp_port = UE_BROADCAST_PORT;

    if (argc >= 2) {
        ue_id = (uint32_t)strtoul(argv[1], NULL, 10);
    }

    if (argc >= 3) {
        udp_port = (uint16_t)strtoul(argv[2], NULL, 10);
    }

    int udp_skt = create_broadcast_rx_socket(udp_port);
    if (udp_skt < 0) {
        return 1;
    }

    timer_init(10L * 1000L * 1000L, ue_tick, NULL);
    timer_start();

    printf("[UE %u] Listening broadcast UDP port %u\n",
           ue_id,
           udp_port);
    fflush(stdout);

    while (1) {
        uint8_t buf[128];

        ssize_t len = recvfrom(
            udp_skt,
            buf,
            sizeof(buf),
            0,
            NULL,
            NULL
        );

        if (len < 0) {
            perror("[UE] recvfrom");
            continue;
        }

        handle_broadcast_message(buf, len);
    }

    close(udp_skt);

    return 0;
}
