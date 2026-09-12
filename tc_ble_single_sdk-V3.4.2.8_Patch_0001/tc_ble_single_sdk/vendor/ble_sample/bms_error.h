#ifndef BMS_ERROR_H_
#define BMS_ERROR_H_

#include <stdint.h>

/* Order is part of the existing Modbus 0xD109..0xD114 byte layout. */
typedef enum
{
    BMS_ERROR_AFE1 = 0,
    BMS_ERROR_AFE2,
    BMS_ERROR_CAN,
    BMS_ERROR_EEPROM_COM,
    BMS_ERROR_SPI,
    BMS_ERROR_UPPER,
    BMS_ERROR_CLIENT,
    BMS_ERROR_SCREEN,
    BMS_ERROR_WIFI,
    BMS_ERROR_BLUETOOTH,
    BMS_ERROR_APP,
    BMS_ERROR_CBC_CHG,
    BMS_ERROR_EEPROM_STORE,
    BMS_ERROR_HSE,
    BMS_ERROR_LSE,
    BMS_ERROR_VDELTA,
    BMS_ERROR_BALANCE,
    BMS_ERROR_ADC,
    BMS_ERROR_HEAT,
    BMS_ERROR_COOL,
    BMS_ERROR_CBC_DSG,
    BMS_ERROR_SOC_CAL,
    BMS_ERROR_TEMP_BREAK,
    BMS_ERROR_DSG_SHORT,
    BMS_ERROR_COUNT
} bms_error_id_t;

typedef char bms_error_order_must_match_protocol[
    (BMS_ERROR_AFE1 == 0 &&
     BMS_ERROR_CBC_CHG == 11 &&
     BMS_ERROR_COUNT == 24) ? 1 : -1];

/* Counters saturate instead of wrapping through zero during a persistent fault. */
void bms_error_raise(bms_error_id_t error);
void bms_error_clear(bms_error_id_t error);
uint8_t bms_error_get(bms_error_id_t error);

#endif /* BMS_ERROR_H_ */
