#ifndef BMS_AFE_H_
#define BMS_AFE_H_

#include <stdint.h>

/*
 * Compile-time AFE boundary used by the BMS application.
 *
 * A product build contains one AFE adapter, so a runtime ops table would only
 * add indirection and code size.  A new AFE implements these four operations;
 * app.c and the BMS core do not include device-register APIs.
 */
void bms_afe_init(void);
void bms_afe_sample(void);
void bms_afe_sleep(void);
uint8_t bms_afe_set_fets(uint8_t charge_on, uint8_t discharge_on);

#endif /* BMS_AFE_H_ */
