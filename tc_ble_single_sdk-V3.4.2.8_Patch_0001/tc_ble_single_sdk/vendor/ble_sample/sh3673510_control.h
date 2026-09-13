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

uint8_t sh3673510_control_init(void);
uint8_t sh3673510_control_apply_protection(void);
uint8_t sh3673510_control_set_fets(uint8_t charge_on, uint8_t discharge_on);
uint8_t sh3673510_control_read_status(sh3673510_control_status_t *status);
uint8_t sh3673510_control_clear_flag1(uint8_t clear_mask);
uint8_t sh3673510_control_clear_flag2(uint8_t clear_mask);
uint8_t sh3673510_control_set_balance(uint16_t cell_mask);
void sh3673510_control_sleep(void);
uint8_t sh3673510_control_wake(void);
uint8_t sh3673510_control_ready(void);
void sh3673510_board_set_heater(uint8_t enabled);
uint8_t sh3673510_board_wake_active(void);

#endif /* SH3673510_CONTROL_H_ */
