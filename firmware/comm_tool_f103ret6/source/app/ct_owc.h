#ifndef CT_OWC_H
#define CT_OWC_H

#include <stdint.h>

#define CT_OWC_MAX_BYTES 259u
#define CT_OWC_HEADER_BYTES 32u
#define CT_OWC_SNAPSHOT_VERSION 2u

/* ISR 只记录边沿；以下接口只由主循环调用。 */
void CtOwc_Init(void);
void CtOwc_ResetDecoder(void);
void CtOwc_FeedEdge(uint32_t duration_us, uint8_t level, uint32_t now_ms);
void CtOwc_RecordOverflow(void);
void CtOwc_Poll(uint32_t now_ms);
uint16_t CtOwc_EncodeSnapshot(uint8_t id, uint8_t enabled, uint32_t now_ms,
                             uint8_t *out, uint16_t capacity);

#endif
