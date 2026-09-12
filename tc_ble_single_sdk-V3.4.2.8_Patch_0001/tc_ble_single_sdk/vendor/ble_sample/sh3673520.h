#ifndef SH3673520_H
#define SH3673520_H

#include <stddef.h>
#include <stdint.h>

#include "sh3673520_reg.h"

typedef enum {
    SH3673520_OK = 0,
    SH3673520_ERR_INVALID_PARAM = -1,
    SH3673520_ERR_TIMEOUT = -2,
    SH3673520_ERR_SPI = -3,
    SH3673520_ERR_CRC = -4,
    SH3673520_ERR_PROTOCOL = -5,
    SH3673520_ERR_NOT_READY = -6,
    SH3673520_ERR_VERIFY = -7,
    SH3673520_ERR_RANGE = -8
} sh3673520_status_t;

typedef struct {
    uint32_t spi_error_count;
    uint32_t crc_error_count;
    uint32_t timeout_count;
    uint32_t protocol_error_count;
    uint32_t retry_count;
    uint32_t successful_transactions;
    uint32_t consecutive_failures;
    sh3673520_status_t last_error;
} sh3673520_comm_stats_t;

typedef struct {
    uint8_t bstatus1;
    uint8_t bstatus2;
} sh3673520_device_status_t;

typedef struct {
    int32_t vadc_raw;
    int32_t cadc_raw;
} sh3673520_current_raw_t;

typedef struct {
    int32_t external_raw[SH3673520_EXTERNAL_TEMP_COUNT];
    int32_t internal_raw;
} sh3673520_temperature_raw_t;

/* Deterministic bring-up: port init -> software reset -> probe -> CADC verify. */
sh3673520_status_t SH3673520_Init(void);
sh3673520_status_t SH3673520_Reset(void);
sh3673520_status_t SH3673520_Probe(void);
uint8_t SH3673520_IsReady(void);

/* Raw register transport. Multi-register writes use bounded single-register writes. */
sh3673520_status_t SH3673520_ReadReg(uint8_t reg, uint8_t *value);
sh3673520_status_t SH3673520_WriteReg(uint8_t reg, uint8_t value);
sh3673520_status_t SH3673520_ReadRegs(uint8_t start_reg, uint8_t *buffer, size_t length);
sh3673520_status_t SH3673520_WriteRegs(uint8_t start_reg,
                                      const uint8_t *buffer,
                                      size_t length);

/* Semantic measurement/status interfaces. */
sh3673520_status_t SH3673520_ReadCellVoltages(int32_t *cell_mv, uint8_t cell_count);
sh3673520_status_t SH3673520_ReadPackVoltage(int32_t *pack_mv);
sh3673520_status_t SH3673520_ReadCurrent(sh3673520_current_raw_t *current);
sh3673520_status_t SH3673520_ReadTemperatures(sh3673520_temperature_raw_t *temperatures);
sh3673520_status_t SH3673520_ReadStatus(sh3673520_device_status_t *status);

/* Pure helpers used by host contract tests and board-level scaling. */
uint8_t SH3673520_Crc8(const uint8_t *data, size_t length);
int32_t SH3673520_DecodeSigned16(uint8_t high, uint8_t low);
int32_t SH3673520_CellRawToMilliVolt(int32_t raw);
int32_t SH3673520_PackRawToMilliVolt(int32_t raw);
sh3673520_status_t SH3673520_CurrentRawToMilliAmp(int32_t raw,
                                                  uint32_t rsense_uohm,
                                                  int32_t *current_ma);
sh3673520_status_t SH3673520_NtcRawToOhm(int32_t raw, uint32_t *resistance_ohm);

void SH3673520_GetCommStats(sh3673520_comm_stats_t *stats);
void SH3673520_ClearCommStats(void);

#endif /* SH3673520_H */
