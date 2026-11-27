#pragma once
#include <stdint.h>
enum { BLC_ADV_DISABLE, BLC_ADV_ENABLE, BLS_LINK_STATE_CONN, SUSPEND_DISABLE,
       HCI_ERR_REMOTE_USER_TERM_CONN };
void bls_ll_setAdvEnable(unsigned enable);
void bls_pm_setSuspendMask(unsigned mask);
void bls_pm_setAppWakeupLowPower(uint32_t tick,unsigned enable);
void bls_ll_terminateConnection(unsigned reason);
unsigned blc_ll_getCurrentState(void);
void blt_sdk_main_loop(void);
