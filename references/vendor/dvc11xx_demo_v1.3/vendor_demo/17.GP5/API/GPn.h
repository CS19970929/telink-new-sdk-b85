#ifndef GPN_H__
#define GPN_H__
#include "DVC11XX.h"

#define CHG_reset  GPIO_ResetBits(GPIOA,GPIO_Pin_0)
#define CHG_status GPIO_ReadInputDataBit(GPIOA,GPIO_Pin_0)//GPIO(PA,0)

extern u8 EVENT_alert_req;
void GP1_ModeConfig(u8 mode);
void GP2_ModeConfig(u8 mode);
void GP3_ModeConfig(u8 mode);
void GP4_ModeConfig(u8 mode);
void GP5_ModeConfig(u8 mode);
void GP6_ModeConfig(u8 mode);
void GPn_ModeConfig(bool mode);
float GPn_Analog_Input_voltage(u8 GP);
#endif
