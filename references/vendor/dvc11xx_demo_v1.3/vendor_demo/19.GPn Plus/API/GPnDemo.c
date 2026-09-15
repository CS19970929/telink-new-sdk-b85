/******************************************************************************
;  * @MCU STM32F103C8T6
;  * @Create Date 2023.01.20
;  * @Official website http://www.devechip.com/
;  * GPn reuse demo
**************************************************************************************/
#include "DVC11XX.h"
#include "GPn.h"
#include "GPnDemo.h"

void FET_Open(void){
#ifdef MODEA
    g_AfeRegs.R81.CHGC = 0x3;
    g_AfeRegs.R81.DSGC = 0x3;
#else
    g_AfeRegs.R81.PCHGC = 1;
    g_AfeRegs.R81.PDSGC = 1;
#endif
    DVC11XX_WriteRegs(AFE_ADDR_R(81),1);
}

void INT_Ctrl(u8 Ctrl){
    memset((u8 *)&g_AfeRegs+121,Ctrl,1);
    DVC11XX_WriteRegs(AFE_ADDR_R(121),1);
}

void GPnDemo_Cal(void){
    if(!MODE){
        float GPn_volage;
        GPn_volage = GPn_Analog_Input_voltage(GP3);
        printf("GPn_volage = %.1f mV\r\n",GPn_volage);

        if(g_AfeRegs.R4_6.CHGF)
            printf("CHG on\r\n");
        if(g_AfeRegs.R4_6.DSGF)
            printf("DSG on\r\n");

        /* Separate physical low-side output observation via MCU GPIO. */
        if(CHG_status){
            printf("LOW SIDE CHG on\r\n");
            CHG_reset;
        }
        if(DSG_status){
            printf("LOW SIDE DSG on\r\n");
            DSG_reset;
        }

        if(EVENT_alert_req){
            printf("AFE INT HAPPENED! Check The Register\r\n");
            EVENT_alert_req=0;
        }
    } else {
        if(g_AfeRegs.R4_6.PCHGF)
            printf("PCHG on\r\n");
        if(g_AfeRegs.R4_6.PDSGF)
            printf("PDSG on\r\n");
        if(PCHG_status){
            printf("LOW SIDE PCHG on\r\n");
            PCHG_reset;
        }
        if(PDSG_status){
            printf("LOW SIDE PDSG on\r\n");
            PCHG_reset;
        }
    }
}
