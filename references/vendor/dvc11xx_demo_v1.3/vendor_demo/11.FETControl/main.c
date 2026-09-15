/******************************************************************************
;  *   @MCU                 STM32F103C8T6
;  *   @Create Date         2023.09.21
;  *   @Official website    http://www.devechip.com/
;  *----------------------Abstract Description---------------------------------
;  *                        FET复用
;  * 1、包含CHG、DSG、PCHG、PDSG控制；DSG输出模式配置、DSG下拉强度配置、续流保护阈值
;  * 配置、电荷泵配置模式配置、FET驱动屏蔽配置；
;  * 2、默认向串口1打印输出
;  * 应用注意事项：本示例仅作驱动函数调用及操作AFE芯片寄存器演示，不作为实际业务逻辑参考
**************************************************************************************/
#include "DVC11XX.h"
#include "FETControl.h"

void FET_Config(void){
    DSGM_Control(DSGM_CP);
    DPC_Config(16);
    BDPT_Config(80);
    ChargePump_Control(CP_10V);
    HSFM_Control(DISABLE);
    CHG_FETControl(FET_OPEN);
    DSG_FETControl(FET_Close_BFCO);
}

int main(void){
    Initial();
    FET_Config();
    while(1){
        if(DVC11XX_ReadRegs(AFE_ADDR_R(4),3)){
            if(g_AfeRegs.R4_6.PDSGF) printf("PDSG OPEN!\r\n");
            if(g_AfeRegs.R4_6.PCHGF) printf("PCHG OPEN!\r\n");
            if(g_AfeRegs.R4_6.DSGF)  printf("DSG OPEN!\r\n");
            if(g_AfeRegs.R4_6.CHGF)  printf("CHG OPEN!\r\n");
        }
        delay_ms(1000);
    }
}
