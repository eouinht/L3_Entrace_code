#ifndef RRC_H
#define RRC_H

#include<stdint.h>

typedef struct{
    uint32_t message_type;
    uint32_t ue_id;
    uint32_t tac;
    uint32_t cn_domain;
} __attribute__((packed)) RRC_Paging_msg;

#endif