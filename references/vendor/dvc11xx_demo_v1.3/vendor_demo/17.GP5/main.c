/******************************************************************************
;  * @MCU STM32F103C8T6
;  * @Create Date 2023.09.20
;  * @Official website http://www.devechip.com/
;  * GP5 example: CHG low-side output
;  * Physical demo connection: AFE GP5 -> MCU PA0
;  * Vendor note: demo only, not production business logic.
**************************************************************************************/
#include "DVC11XX.h"
#include "GPn.h"
#include "FETControl.h"

void GPIO_Ini_Config(void){
    GPIO_InitTypeDef GPIO_InitStructure;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA,ENABLE);
    GPIO_InitStructure.GPIO_Pin=GPIO_Pin_0;
    GPIO_InitStructure.GPIO_Mode=GPIO_Mode_IPU;
    GPIO_InitStructure.GPIO_Speed=GPIO_Speed_50MHz;
    GPIO_Init(GPIOA,&GPIO_InitStructure);
}

int main(void){
    Initial();
    GP5_ModeConfig(0x07);//GP5 CHG_LS
    CHG_FETControl(ENABLE);//vendor demo comment says open; see README caveat about mode inconsistency
    while(1){
        if(DVC11XX_ReadRegs(AFE_ADDR_R(6), 1)){
            if(g_AfeRegs.R4_6.CHGF)
                printf("CHG on\r\n");
            if(CHG_status){
                printf("LOW SIDE CHG on\r\n");
                CHG_reset;
            }
        }
        delay_ms(1000);
    }
}
