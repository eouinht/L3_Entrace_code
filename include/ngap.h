#ifndef NGAP_H
#define NGAP_H
#include <stdint.h>

typedef struct{
    uint32_t message_type;
    uint32_t ue_id;
    uint32_t tac;
    uint32_t cn_domain;
} __attribute__((packed)) NGAP_Paging_msg;

#endif