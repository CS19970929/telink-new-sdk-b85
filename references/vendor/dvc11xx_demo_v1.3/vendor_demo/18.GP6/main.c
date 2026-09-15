/******************************************************************************
;  * @MCU STM32F103C8T6
;  * @Create Date 2023.03.10
;  * @Official website http://www.devechip.com/
;  * GP6 example: DSG low-side output
;  * Physical demo connection: AFE GP6 -> MCU PA1
;  * Vendor note: demo only, not production business logic.
**************************************************************************************/
#include "DVC11XX.h"
#include "GPn.h"
#include "FETControl.h"

void GPIO_Ini_Config(void){
    GPIO_InitTypeDef GPIO_InitStructure;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA,ENABLE);
    /* Original demo configures PA0 here while DSG_status reads PA1. Kept as vendor-code evidence. */
    GPIO_InitStructure.GPIO_Pin=GPIO_Pin_0;
    GPIO_InitStructure.GPIO_Mode=GPIO_Mode_IPU;
    GPIO_InitStructure.GPIO_Speed=GPIO_Speed_50MHz;
    GPIO_Init(GPIOA,&GPIO_InitStructure);
}

int main(void){
    Initial();
    GP6_ModeConfig(0x07);//GP6 DSG_LS (original comment says CHG_LS, another demo inconsistency)
    DSG_FETControl(ENABLE);//vendor demo comment says open; see README caveat about mode inconsistency
    while(1){
        if(DVC11XX_ReadRegs(AFE_ADDR_R(6), 1)){
            if(g_AfeRegs.R4_6.DSGF)
                printf("DSG on\r\n");
            if(DSG_status){
                printf("LOW SIDE DSG on\r\n");
                DSG_reset;
            }
        }
        delay_ms(1000);
    }
}
