/******************************************************************************
;  *   @MCU                 STM32F103C8T6
;  *   @Create Date         2023.01.20
;  *   @Official website    http://www.devechip.com/
;  *----------------------Abstract Description---------------------------------
;  *                        GPn管脚复用驱动
**************************************************************************************/
#include "DVC11XX.h"
#include "GPn.h"

void GP1_ModeConfig(u8 mode){
    if(mode<=4){
        g_AfeRegs.R116.GP1M=mode;
        DVC11XX_WriteRegs(AFE_ADDR_R(116),1);
    }
}

void GP2_ModeConfig(u8 mode){
    if((mode<=7)&&!((mode>2)&&(mode<6))){
        g_AfeRegs.R116.GP2M=mode;
        DVC11XX_WriteRegs(AFE_ADDR_R(116),1);
    }
}

void GP3_ModeConfig(u8 mode){
    if((mode<=7)&&!((mode>2)&&(mode<6))){
        g_AfeRegs.R116.GP3M=mode;
        DVC11XX_WriteRegs(AFE_ADDR_R(116),1);
    }
}

void GP4_ModeConfig(u8 mode){
    if(mode<=4){
        g_AfeRegs.R117.GP4M=mode;
        DVC11XX_WriteRegs(AFE_ADDR_R(117),1);
    }
}

/* GP5 mode 0x07 = 充电低边输出 CHG_LS */
void GP5_ModeConfig(u8 mode){
    if((mode<=7)&&!((mode>2)&&(mode<6))){
        g_AfeRegs.R117.GP5M=mode;
        DVC11XX_WriteRegs(AFE_ADDR_R(117),1);
    }
}

/* GP6 mode 0x07 = 放电低边输出 DSG_LS */
void GP6_ModeConfig(u8 mode){
    if((mode<=7)&&!((mode>2)&&(mode<6))){
        g_AfeRegs.R117.GP6M=mode;
        DVC11XX_WriteRegs(AFE_ADDR_R(117),1);
    }
}

float GPn_Analog_Input_voltage(u8 GP){
    float temp_voltage;
    u16 GP_value;
    DVC11XX_ReadRegs(AFE_ADDR_R(17+GP),2);
    GP_value=(g_AfeRegs.R17_28.VGP[GP].VGP_H<<8)|g_AfeRegs.R17_28.VGP[GP].VGP_L;
    temp_voltage=GP_value * 0.1;
    return temp_voltage;
}
