#include "sh3673520.h"
#include "sh3673520_port.h"

#include <limits.h>

#define SH3673520_TRANSACTION_ATTEMPTS      5u
#define SH3673520_RETRY_DELAY_MS            1u
#define SH3673520_RESET_SETTLE_MS           10u

static sh3673520_comm_stats_t s_comm_stats;
static uint8_t s_ready;

static uint8_t sh3673520_crc8_update(uint8_t crc, uint8_t data)
{
    uint8_t bit;

    crc ^= data;
    for (bit = 0u; bit < 8u; ++bit) {
        if ((crc & 0x80u) != 0u) {
            crc = (uint8_t)((uint8_t)(crc << 1u) ^ SH3673520_CRC8_POLY);
        } else {
            crc = (uint8_t)(crc << 1u);
        }
    }

    return crc;
}

uint8_t SH3673520_Crc8(const uint8_t *data, size_t length)
{
    size_t index;
    uint8_t crc = SH3673520_CRC8_INIT;

    if ((data == NULL) && (length != 0u)) {
        return SH3673520_CRC8_INIT;
    }

    for (index = 0u; index < length; ++index) {
        crc = sh3673520_crc8_update(crc, data[index]);
    }

    return crc;
}

static sh3673520_status_t sh3673520_map_port_status(sh3673520_port_status_t status)
{
    switch (status) {
    case SH3673520_PORT_OK:
        return SH3673520_OK;
    case SH3673520_PORT_ERR_INVALID_PARAM:
        return SH3673520_ERR_INVALID_PARAM;
    case SH3673520_PORT_ERR_NOT_CONFIGURED:
        return SH3673520_ERR_NOT_READY;
    case SH3673520_PORT_ERR_TIMEOUT:
        return SH3673520_ERR_TIMEOUT;
    case SH3673520_PORT_ERR_SPI:
    default:
        return SH3673520_ERR_SPI;
    }
}

static void sh3673520_record_attempt_failure(sh3673520_status_t status)
{
    switch (status) {
    case SH3673520_ERR_TIMEOUT:
        ++s_comm_stats.timeout_count;
        break;
    case SH3673520_ERR_CRC:
        ++s_comm_stats.crc_error_count;
        break;
    case SH3673520_ERR_PROTOCOL:
        ++s_comm_stats.protocol_error_count;
        break;
    case SH3673520_ERR_SPI:
        ++s_comm_stats.spi_error_count;
        break;
    default:
        break;
    }

    s_comm_stats.last_error = status;
}

static void sh3673520_record_success(void)
{
    ++s_comm_stats.successful_transactions;
    s_comm_stats.consecutive_failures = 0u;
    s_comm_stats.last_error = SH3673520_OK;
}

static sh3673520_status_t sh3673520_record_final_failure(sh3673520_status_t status)
{
    ++s_comm_stats.consecutive_failures;
    s_comm_stats.last_error = status;
    return status;
}

static sh3673520_status_t sh3673520_xfer(uint8_t tx, uint8_t *rx)
{
    return sh3673520_map_port_status(sh3673520_port_xfer(tx, rx));
}

static sh3673520_status_t sh3673520_read_regs_once(uint8_t start_reg,
                                                   uint8_t *buffer,
                                                   uint8_t length)
{
    sh3673520_status_t status;
    sh3673520_port_status_t port_status;
    uint8_t rx;
    uint8_t crc;
    uint8_t index;
    uint8_t received_crc;
    uint8_t begun = 0u;

    port_status = sh3673520_port_begin();
    if (port_status != SH3673520_PORT_OK) {
        return sh3673520_map_port_status(port_status);
    }
    begun = 1u;

    crc = SH3673520_CRC8_INIT;

    status = sh3673520_xfer(SH3673520_SPI_CMD_READ, &rx);
    if (status != SH3673520_OK) {
        goto finish;
    }
    if (rx != SH3673520_SPI_RESPONSE_IDLE) {
        status = SH3673520_ERR_PROTOCOL;
        goto finish;
    }
    crc = sh3673520_crc8_update(crc, rx);

    status = sh3673520_xfer(start_reg, &rx);
    if (status != SH3673520_OK) {
        goto finish;
    }
    if (rx != SH3673520_SPI_CMD_READ) {
        status = SH3673520_ERR_PROTOCOL;
        goto finish;
    }
    crc = sh3673520_crc8_update(crc, rx);

    status = sh3673520_xfer(length, &rx);
    if (status != SH3673520_OK) {
        goto finish;
    }
    if (rx != start_reg) {
        status = SH3673520_ERR_PROTOCOL;
        goto finish;
    }
    crc = sh3673520_crc8_update(crc, rx);

    status = sh3673520_xfer(0x00u, &rx);
    if (status != SH3673520_OK) {
        goto finish;
    }
    if (rx != length) {
        status = SH3673520_ERR_PROTOCOL;
        goto finish;
    }
    crc = sh3673520_crc8_update(crc, rx);

    for (index = 0u; index < length; ++index) {
        status = sh3673520_xfer(0x00u, &rx);
        if (status != SH3673520_OK) {
            goto finish;
        }
        buffer[index] = rx;
        crc = sh3673520_crc8_update(crc, rx);
    }

    status = sh3673520_xfer(0x00u, &received_crc);
    if (status != SH3673520_OK) {
        goto finish;
    }

    if (received_crc != crc) {
        status = SH3673520_ERR_CRC;
        goto finish;
    }

    status = SH3673520_OK;

finish:
    if (begun != 0u) {
        sh3673520_port_end();
    }
    return status;
}

static sh3673520_status_t sh3673520_write_reg_once(uint8_t reg, uint8_t value)
{
    sh3673520_status_t status;
    sh3673520_port_status_t port_status;
    uint8_t rx;
    uint8_t crc;
    uint8_t frame[3];
    uint8_t begun = 0u;

    frame[0] = SH3673520_SPI_CMD_WRITE;
    frame[1] = reg;
    frame[2] = value;
    crc = SH3673520_Crc8(frame, sizeof(frame));

    port_status = sh3673520_port_begin();
    if (port_status != SH3673520_PORT_OK) {
        return sh3673520_map_port_status(port_status);
    }
    begun = 1u;

    status = sh3673520_xfer(frame[0], &rx);
    if (status != SH3673520_OK) {
        goto finish;
    }
    if (rx != SH3673520_SPI_RESPONSE_IDLE) {
        status = SH3673520_ERR_PROTOCOL;
        goto finish;
    }

    status = sh3673520_xfer(frame[1], &rx);
    if (status != SH3673520_OK) {
        goto finish;
    }
    if (rx != SH3673520_SPI_CMD_WRITE) {
        status = SH3673520_ERR_PROTOCOL;
        goto finish;
    }

    status = sh3673520_xfer(frame[2], &rx);
    if (status != SH3673520_OK) {
        goto finish;
    }
    if (rx != reg) {
        status = SH3673520_ERR_PROTOCOL;
        goto finish;
    }

    status = sh3673520_xfer(crc, &rx);
    if (status != SH3673520_OK) {
        goto finish;
    }
    if (rx != value) {
        status = SH3673520_ERR_PROTOCOL;
        goto finish;
    }

    status = sh3673520_xfer(0x00u, &rx);
    if (status != SH3673520_OK) {
        goto finish;
    }
    if (rx != SH3673520_SPI_ACK) {
        status = SH3673520_ERR_PROTOCOL;
        goto finish;
    }

    status = SH3673520_OK;

finish:
    if (begun != 0u) {
        sh3673520_port_end();
    }
    return status;
}

static sh3673520_status_t sh3673520_reset_once(void)
{
    sh3673520_status_t status;
    sh3673520_port_status_t port_status;
    uint8_t rx;
    uint8_t crc;
    uint8_t frame[3];
    uint8_t begun = 0u;

    frame[0] = SH3673520_SPI_CMD_RESET;
    frame[1] = SH3673520_SPI_RESET_KEY1;
    frame[2] = SH3673520_SPI_RESET_KEY2;
    crc = SH3673520_Crc8(frame, sizeof(frame));

    port_status = sh3673520_port_begin();
    if (port_status != SH3673520_PORT_OK) {
        return sh3673520_map_port_status(port_status);
    }
    begun = 1u;

    status = sh3673520_xfer(frame[0], &rx);
    if (status != SH3673520_OK) {
        goto finish;
    }
    if (rx != SH3673520_SPI_RESPONSE_IDLE) {
        status = SH3673520_ERR_PROTOCOL;
        goto finish;
    }

    status = sh3673520_xfer(frame[1], &rx);
    if (status != SH3673520_OK) {
        goto finish;
    }
    if (rx != SH3673520_SPI_CMD_RESET) {
        status = SH3673520_ERR_PROTOCOL;
        goto finish;
    }

    status = sh3673520_xfer(frame[2], &rx);
    if (status != SH3673520_OK) {
        goto finish;
    }
    if (rx != SH3673520_SPI_RESET_KEY1) {
        status = SH3673520_ERR_PROTOCOL;
        goto finish;
    }

    status = sh3673520_xfer(crc, &rx);
    if (status != SH3673520_OK) {
        goto finish;
    }
    if (rx != SH3673520_SPI_RESET_KEY2) {
        status = SH3673520_ERR_PROTOCOL;
        goto finish;
    }

    status = sh3673520_xfer(0x00u, &rx);
    if (status != SH3673520_OK) {
        goto finish;
    }
    if (rx != SH3673520_SPI_ACK) {
        status = SH3673520_ERR_PROTOCOL;
        goto finish;
    }

    status = SH3673520_OK;

finish:
    if (begun != 0u) {
        sh3673520_port_end();
    }
    return status;
}

static uint8_t sh3673520_should_recover_port(sh3673520_status_t status)
{
    return (uint8_t)(((status == SH3673520_ERR_TIMEOUT) ||
                      (status == SH3673520_ERR_SPI)) ? 1u : 0u);
}

static sh3673520_status_t sh3673520_read_with_retry(uint8_t start_reg,
                                                    uint8_t *buffer,
                                                    uint8_t length)
{
    uint8_t attempt;
    sh3673520_status_t status = SH3673520_ERR_SPI;

    for (attempt = 0u; attempt < SH3673520_TRANSACTION_ATTEMPTS; ++attempt) {
        status = sh3673520_read_regs_once(start_reg, buffer, length);
        if (status == SH3673520_OK) {
            sh3673520_record_success();
            return SH3673520_OK;
        }

        sh3673520_record_attempt_failure(status);
        if ((status == SH3673520_ERR_NOT_READY) ||
            (status == SH3673520_ERR_INVALID_PARAM)) {
            break;
        }

        if ((uint8_t)(attempt + 1u) < SH3673520_TRANSACTION_ATTEMPTS) {
            ++s_comm_stats.retry_count;
            if (sh3673520_should_recover_port(status) != 0u) {
                (void)sh3673520_port_recover();
            }
            sh3673520_port_delay_ms(SH3673520_RETRY_DELAY_MS);
        }
    }

    return sh3673520_record_final_failure(status);
}

static sh3673520_status_t sh3673520_write_with_retry(uint8_t reg, uint8_t value)
{
    uint8_t attempt;
    sh3673520_status_t status = SH3673520_ERR_SPI;

    for (attempt = 0u; attempt < SH3673520_TRANSACTION_ATTEMPTS; ++attempt) {
        status = sh3673520_write_reg_once(reg, value);
        if (status == SH3673520_OK) {
            sh3673520_record_success();
            return SH3673520_OK;
        }

        sh3673520_record_attempt_failure(status);
        if ((status == SH3673520_ERR_NOT_READY) ||
            (status == SH3673520_ERR_INVALID_PARAM)) {
            break;
        }

        if ((uint8_t)(attempt + 1u) < SH3673520_TRANSACTION_ATTEMPTS) {
            ++s_comm_stats.retry_count;
            if (sh3673520_should_recover_port(status) != 0u) {
                (void)sh3673520_port_recover();
            }
            sh3673520_port_delay_ms(SH3673520_RETRY_DELAY_MS);
        }
    }

    return sh3673520_record_final_failure(status);
}

sh3673520_status_t SH3673520_ReadRegs(uint8_t start_reg, uint8_t *buffer, size_t length)
{
    uint32_t last_reg;

    if ((buffer == NULL) || (length == 0u) || (length > 255u)) {
        return SH3673520_ERR_INVALID_PARAM;
    }

    last_reg = (uint32_t)start_reg + (uint32_t)length - 1u;
    if ((start_reg < SH3673520_REG_READ_MIN) ||
        (last_reg > SH3673520_REG_READ_MAX)) {
        return SH3673520_ERR_INVALID_PARAM;
    }

    return sh3673520_read_with_retry(start_reg, buffer, (uint8_t)length);
}

sh3673520_status_t SH3673520_ReadReg(uint8_t reg, uint8_t *value)
{
    return SH3673520_ReadRegs(reg, value, 1u);
}

sh3673520_status_t SH3673520_WriteReg(uint8_t reg, uint8_t value)
{
    if ((reg < SH3673520_REG_WRITE_MIN) ||
        (reg > SH3673520_REG_WRITE_MAX)) {
        return SH3673520_ERR_INVALID_PARAM;
    }

    return sh3673520_write_with_retry(reg, value);
}

sh3673520_status_t SH3673520_WriteRegs(uint8_t start_reg,
                                      const uint8_t *buffer,
                                      size_t length)
{
    size_t index;
    uint32_t last_reg;
    sh3673520_status_t status;

    if ((buffer == NULL) || (length == 0u)) {
        return SH3673520_ERR_INVALID_PARAM;
    }

    last_reg = (uint32_t)start_reg + (uint32_t)length - 1u;
    if ((start_reg < SH3673520_REG_WRITE_MIN) ||
        (last_reg > SH3673520_REG_WRITE_MAX)) {
        return SH3673520_ERR_INVALID_PARAM;
    }

    /*
     * The official write frame writes one register and has no burst-length
     * field. Preserve the public multi-register API by issuing bounded,
     * individually acknowledged register writes.
     */
    for (index = 0u; index < length; ++index) {
        status = SH3673520_WriteReg((uint8_t)((uint32_t)start_reg + (uint32_t)index),
                                   buffer[index]);
        if (status != SH3673520_OK) {
            return status;
        }
    }

    return SH3673520_OK;
}

sh3673520_status_t SH3673520_Reset(void)
{
    uint8_t attempt;
    sh3673520_status_t status = SH3673520_ERR_SPI;

    s_ready = 0u;

    for (attempt = 0u; attempt < SH3673520_TRANSACTION_ATTEMPTS; ++attempt) {
        status = sh3673520_reset_once();
        if (status == SH3673520_OK) {
            sh3673520_record_success();
            sh3673520_port_delay_ms(SH3673520_RESET_SETTLE_MS);
            return SH3673520_OK;
        }

        sh3673520_record_attempt_failure(status);
        if (status == SH3673520_ERR_NOT_READY) {
            break;
        }

        if ((uint8_t)(attempt + 1u) < SH3673520_TRANSACTION_ATTEMPTS) {
            ++s_comm_stats.retry_count;
            if (sh3673520_should_recover_port(status) != 0u) {
                (void)sh3673520_port_recover();
            }
            sh3673520_port_delay_ms(SH3673520_RETRY_DELAY_MS);
        }
    }

    return sh3673520_record_final_failure(status);
}

sh3673520_status_t SH3673520_Probe(void)
{
    uint8_t status_bytes[2];

    /*
     * SH3673520 has no documented chip-ID register. A CRC-valid, protocol-valid
     * read of the documented read-only BSTATUS1/BSTATUS2 pair is the probe.
     */
    return SH3673520_ReadRegs(SH3673520_REG_BSTATUS1, status_bytes, sizeof(status_bytes));
}

sh3673520_status_t SH3673520_Init(void)
{
    sh3673520_port_status_t port_status;
    sh3673520_status_t status;
    uint8_t sconf5;
    uint8_t verify;

    s_ready = 0u;

    port_status = sh3673520_port_init();
    if (port_status != SH3673520_PORT_OK) {
        status = sh3673520_map_port_status(port_status);
        sh3673520_record_attempt_failure(status);
        return sh3673520_record_final_failure(status);
    }

    status = SH3673520_Reset();
    if (status != SH3673520_OK) {
        return status;
    }

    status = SH3673520_Probe();
    if (status != SH3673520_OK) {
        return status;
    }

    /*
     * Keep product protection/FET parameters untouched. The only base setting
     * enforced here is CADC enable, preserving every other SCONF5 bit.
     */
    status = SH3673520_ReadReg(SH3673520_REG_SCONF5, &sconf5);
    if (status != SH3673520_OK) {
        return status;
    }

    if ((sconf5 & SH3673520_SCONF5_CADC_EN_MASK) == 0u) {
        status = SH3673520_WriteReg(
            SH3673520_REG_SCONF5,
            (uint8_t)(sconf5 | SH3673520_SCONF5_CADC_EN_MASK));
        if (status != SH3673520_OK) {
            return status;
        }
    }

    status = SH3673520_ReadReg(SH3673520_REG_SCONF5, &verify);
    if (status != SH3673520_OK) {
        return status;
    }
    if ((verify & SH3673520_SCONF5_CADC_EN_MASK) == 0u) {
        s_comm_stats.last_error = SH3673520_ERR_VERIFY;
        ++s_comm_stats.consecutive_failures;
        return SH3673520_ERR_VERIFY;
    }

    s_ready = 1u;
    return SH3673520_OK;
}

uint8_t SH3673520_IsReady(void)
{
    return s_ready;
}

int32_t SH3673520_DecodeSigned16(uint8_t high, uint8_t low)
{
    uint16_t raw = (uint16_t)(((uint16_t)high << 8u) | (uint16_t)low);

    if ((raw & 0x8000u) != 0u) {
        return (int32_t)raw - 65536L;
    }

    return (int32_t)raw;
}

int32_t SH3673520_CellRawToMilliVolt(int32_t raw)
{
    return (raw * 5L) / 32L;
}

int32_t SH3673520_PackRawToMilliVolt(int32_t raw)
{
    return (raw * 125L) / 32L;
}

sh3673520_status_t SH3673520_CurrentRawToMilliAmp(int32_t raw,
                                                  uint32_t rsense_uohm,
                                                  int32_t *current_ma)
{
    int64_t numerator;
    int64_t denominator;
    int64_t result;

    if ((current_ma == NULL) ||
        (rsense_uohm == 0u) ||
        (raw < -32768L) ||
        (raw > 32767L)) {
        return SH3673520_ERR_INVALID_PARAM;
    }

    numerator = (int64_t)raw * 100000000LL;
    denominator = 29127LL * (int64_t)rsense_uohm;
    result = numerator / denominator;

    if ((result > (int64_t)INT32_MAX) || (result < (int64_t)INT32_MIN)) {
        return SH3673520_ERR_RANGE;
    }

    *current_ma = (int32_t)result;
    return SH3673520_OK;
}

sh3673520_status_t SH3673520_NtcRawToOhm(int32_t raw, uint32_t *resistance_ohm)
{
    int32_t denominator;
    uint32_t numerator;

    if ((resistance_ohm == NULL) || (raw < 0L) || (raw >= 32768L)) {
        return SH3673520_ERR_INVALID_PARAM;
    }

    denominator = 32768L - raw;
    numerator = (uint32_t)raw * 10000UL;
    *resistance_ohm = numerator / (uint32_t)denominator;
    return SH3673520_OK;
}

static sh3673520_status_t sh3673520_require_ready(void)
{
    return (s_ready != 0u) ? SH3673520_OK : SH3673520_ERR_NOT_READY;
}

sh3673520_status_t SH3673520_ReadCellVoltages(int32_t *cell_mv, uint8_t cell_count)
{
    uint8_t raw[SH3673520_MAX_CELLS * 2u];
    uint8_t index;
    sh3673520_status_t status;

    if ((cell_mv == NULL) ||
        (cell_count < SH3673520_MIN_CELLS) ||
        (cell_count > SH3673520_MAX_CELLS)) {
        return SH3673520_ERR_INVALID_PARAM;
    }

    status = sh3673520_require_ready();
    if (status != SH3673520_OK) {
        return status;
    }

    status = SH3673520_ReadRegs(SH3673520_REG_CELL1H,
                                raw,
                                (size_t)cell_count * 2u);
    if (status != SH3673520_OK) {
        return status;
    }

    for (index = 0u; index < cell_count; ++index) {
        int32_t sample = SH3673520_DecodeSigned16(raw[(uint8_t)(index * 2u)],
                                                  raw[(uint8_t)(index * 2u + 1u)]);
        cell_mv[index] = SH3673520_CellRawToMilliVolt(sample);
    }

    return SH3673520_OK;
}

sh3673520_status_t SH3673520_ReadPackVoltage(int32_t *pack_mv)
{
    uint8_t raw[2];
    sh3673520_status_t status;

    if (pack_mv == NULL) {
        return SH3673520_ERR_INVALID_PARAM;
    }

    status = sh3673520_require_ready();
    if (status != SH3673520_OK) {
        return status;
    }

    status = SH3673520_ReadRegs(SH3673520_REG_VTOPH, raw, sizeof(raw));
    if (status != SH3673520_OK) {
        return status;
    }

    *pack_mv = SH3673520_PackRawToMilliVolt(
        SH3673520_DecodeSigned16(raw[0], raw[1]));
    return SH3673520_OK;
}

sh3673520_status_t SH3673520_ReadCurrent(sh3673520_current_raw_t *current)
{
    uint8_t raw[2];
    sh3673520_status_t status;

    if (current == NULL) {
        return SH3673520_ERR_INVALID_PARAM;
    }

    status = sh3673520_require_ready();
    if (status != SH3673520_OK) {
        return status;
    }

    status = SH3673520_ReadRegs(SH3673520_REG_CURH, raw, sizeof(raw));
    if (status != SH3673520_OK) {
        return status;
    }
    current->vadc_raw = SH3673520_DecodeSigned16(raw[0], raw[1]);

    status = SH3673520_ReadRegs(SH3673520_REG_CADCDH, raw, sizeof(raw));
    if (status != SH3673520_OK) {
        return status;
    }
    current->cadc_raw = SH3673520_DecodeSigned16(raw[0], raw[1]);

    return SH3673520_OK;
}

sh3673520_status_t SH3673520_ReadTemperatures(sh3673520_temperature_raw_t *temperatures)
{
    uint8_t raw[(SH3673520_EXTERNAL_TEMP_COUNT + 1u) * 2u];
    uint8_t index;
    sh3673520_status_t status;

    if (temperatures == NULL) {
        return SH3673520_ERR_INVALID_PARAM;
    }

    status = sh3673520_require_ready();
    if (status != SH3673520_OK) {
        return status;
    }

    status = SH3673520_ReadRegs(SH3673520_REG_TEMP1H, raw, sizeof(raw));
    if (status != SH3673520_OK) {
        return status;
    }

    for (index = 0u; index < SH3673520_EXTERNAL_TEMP_COUNT; ++index) {
        temperatures->external_raw[index] =
            SH3673520_DecodeSigned16(raw[(uint8_t)(index * 2u)],
                                     raw[(uint8_t)(index * 2u + 1u)]);
    }

    temperatures->internal_raw = SH3673520_DecodeSigned16(
        raw[SH3673520_EXTERNAL_TEMP_COUNT * 2u],
        raw[SH3673520_EXTERNAL_TEMP_COUNT * 2u + 1u]);

    return SH3673520_OK;
}

sh3673520_status_t SH3673520_ReadStatus(sh3673520_device_status_t *status)
{
    uint8_t raw[2];
    sh3673520_status_t result;

    if (status == NULL) {
        return SH3673520_ERR_INVALID_PARAM;
    }

    result = sh3673520_require_ready();
    if (result != SH3673520_OK) {
        return result;
    }

    /*
     * Deliberately read only BSTATUS1/BSTATUS2. FLAG2 is not included because
     * VADC_FLG and CADC_FLG are read-clear.
     */
    result = SH3673520_ReadRegs(SH3673520_REG_BSTATUS1, raw, sizeof(raw));
    if (result != SH3673520_OK) {
        return result;
    }

    status->bstatus1 = raw[0];
    status->bstatus2 = raw[1];
    return SH3673520_OK;
}

void SH3673520_GetCommStats(sh3673520_comm_stats_t *stats)
{
    if (stats != NULL) {
        *stats = s_comm_stats;
    }
}

void SH3673520_ClearCommStats(void)
{
    s_comm_stats.spi_error_count = 0u;
    s_comm_stats.crc_error_count = 0u;
    s_comm_stats.timeout_count = 0u;
    s_comm_stats.protocol_error_count = 0u;
    s_comm_stats.retry_count = 0u;
    s_comm_stats.successful_transactions = 0u;
    s_comm_stats.consecutive_failures = 0u;
    s_comm_stats.last_error = SH3673520_OK;
}
