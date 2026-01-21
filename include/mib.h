#ifndef MIB_H
#define MIB_H

#include <stdint.h>

#define MIB_IE1 0x01

typedef struct {
   uint8_t message_id;
   uint16_t sfn_value; 
} __attribute__((packed)) MIB_msg;

#endif