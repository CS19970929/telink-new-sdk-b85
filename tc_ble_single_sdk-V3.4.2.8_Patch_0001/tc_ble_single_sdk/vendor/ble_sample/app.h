/********************************************************************************************************
 * @file    app.h
 *
 * @brief   BMS application/BLE lifecycle interface for the Telink B85 target.
 *******************************************************************************************************/
#ifndef APP_H_
#define APP_H_

#include "conf.h"
#include "bms_error.h"

extern u8 ota_is_working;

void app_ble_request_normal_conn_param(void);
void app_ble_request_ota_conn_param(void);
void app_ble_restore_normal_power(void);

/** User initialization on power-on or wake from deep sleep. */
void user_init_normal(void);

/** User initialization on deep-retention wake. */
void user_init_deepRetn(void);

/** Main cooperative application loop. */
void main_loop(void);

/**
 * Flash protection operation callback used by application and OTA stack paths.
 * Address range follows [op_addr_begin, op_addr_end).
 */
void app_flash_protection_operation(u8 flash_op_evt, u32 op_addr_begin, u32 op_addr_end);
int app_flash_lock_restore_enabled(void);

#endif /* APP_H_ */
