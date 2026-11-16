#ifndef CT_OWC_H
#define CT_OWC_H
#include <stdint.h>
#define CT_OWC_MAX_BYTES 259u
#define CT_OWC_HEADER_BYTES 32u
#define CT_OWC_SNAPSHOT_VERSION 2u
#define CT_OWC_V3_HEADER_BYTES 36u
#define CT_OWC_MODE_AUTO 0u
#define CT_OWC_MODE_A133 1u
#define CT_OWC_MODE_LISHEN 2u
#define CT_OWC_PROTOCOL_UNKNOWN 0u
#define CT_OWC_LISHEN_SELECTOR 0x80u

/* ISR只记录边沿；解码、协议切换和快照都由主循环调用。 */
void CtOwc_Init(void);
uint8_t CtOwc_SetMode(uint8_t mode);
uint8_t CtOwc_GetMode(void);
void CtOwc_ResetDecoder(void);
void CtOwc_FeedEdge(uint32_t duration_us, uint8_t level, uint32_t now_ms);
void CtOwc_RecordOverflow(void);
void CtOwc_Poll(uint32_t now_ms);
uint16_t CtOwc_EncodeSnapshot(uint8_t id, uint8_t enabled, uint32_t now_ms, uint8_t *out, uint16_t capacity);
uint16_t CtOwc_EncodeSnapshotV3(uint8_t selector, uint8_t enabled, uint32_t now_ms, uint8_t *out, uint16_t capacity);
#endif
