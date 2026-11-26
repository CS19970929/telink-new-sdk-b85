#pragma once
#include "device.h"
typedef struct {
    uint16_t token, generation, sn_generation, sn_mask, sequence;
    uint32_t opened_ms, fragment_ms, sn_ms;
    uint8_t received, frame[79], sn[32], result;
    bool staging_sn;
} RtuSession;
extern uint8_t device_address[6];
uint16_t rtu_crc(const uint8_t *p,unsigned n);
unsigned rtu_channel(RtuSession *session,const uint8_t *p,unsigned n,uint8_t *out,uint32_t ms);
bool register_read(const RtuSession *session,uint16_t reg,uint16_t *value);
unsigned register_write(RtuSession *session,uint16_t reg,unsigned count,const uint8_t *data,uint32_t ms);
void hw_words(const Config *c,bool effective,uint16_t words[35]);
unsigned hw_write(const uint8_t *data);
bool device_set_allowed(uint8_t allowed,uint32_t ms);
