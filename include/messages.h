#ifndef MESSAGES_H
#define MESSAGES_H

#include <stdint.h>

#define MSG_TYPE_PAGING 100
#define TAC_DEFAULT 100
#define CN_DOMAIN_PHONE 100
#define CN_DOMAIN_DATA 101
#define MIB_IE1 0x01

#pragma pack(push, 1)
typedef struct {
   uint8_t message_id;
   uint16_t sfn_value; 
} __attribute__((packed)) MIB_msg;

typedef struct{
    uint32_t message_type;
    uint32_t ue_id;
    uint32_t tac;
    uint32_t cn_domain;
} __attribute__((packed)) RRC_Paging_msg;

typedef struct{
    uint32_t message_type;
    uint32_t ue_id;
    uint32_t tac;
    uint32_t cn_domain;
} __attribute__((packed)) NGAP_Paging_msg;

#pragma pack(pop)

#endif