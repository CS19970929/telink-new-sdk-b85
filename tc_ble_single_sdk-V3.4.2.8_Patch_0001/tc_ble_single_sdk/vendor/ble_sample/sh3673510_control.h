#ifndef SH3673510_CONTROL_H_
#define SH3673510_CONTROL_H_

#include <stdint.h>

typedef struct
{
    uint8_t flag1;
    uint8_t flag2;
    uint8_t bstatus1;
    uint8_t bstatus2;
} sh3673510_control_status_t;

typedef struct
{
    uint16_t ov_mv;
    uint16_t uv_mv;
    uint16_t ocd1_a10;
    uint16_t ocd2_a10;
    uint16_t occ_a10;
    uint16_t ov_delay_ms;
    uint16_t uv_delay_ms;
    uint16_t ocd1_delay_ms;
    uint16_t ocd2_delay_ms;
    uint16_t occ_delay_ms;
    uint8_t valid;
} sh3673510_protection_actual_t;

uint8_t sh3673510_control_init(void);
uint8_t sh3673510_control_apply_protection(void);
uint8_t sh3673510_control_get_protection_actual(sh3673510_protection_actual_t *actual);
uint8_t sh3673510_control_get_protection_actual(sh3673510_protection_actual_t *actual);
uint8_t sh3673510_control_get_protection_actual(sh3673510_protection_actual_t *actual);
uint8_t sh3673510_control_set_fets(uint8_t charge_on, uint8_t discharge_on);
uint8_t sh3673510_control_read_status(sh3673510_control_status_t *status);
uint8_t sh3673510_control_clear_flag1(uint8_t clear_mask);
uint8_t sh3673510_control_clear_flag2(uint8_t clear_mask);
uint8_t sh3673510_control_set_balance(uint16_t cell_mask);
void sh3673510_control_sleep(void);
uint8_t sh3673510_control_wake(void);
uint8_t sh3673510_control_ready(void);
void sh3673510_board_set_heater(uint8_t enabled);
void sh3673510_board_force_heater_fuse_safe(void);
void sh3673510_board_force_heater_fuse_safe(void);
void sh3673510_board_force_heater_fuse_safe(void);
uint8_t sh3673510_board_wake_active(void);

#endif /* SH3673510_CONTROL_H_ */
