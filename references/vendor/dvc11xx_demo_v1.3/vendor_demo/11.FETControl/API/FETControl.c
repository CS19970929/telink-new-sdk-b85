/******************************************************************************
;  *   @MCU                 STM32F103C8T6
;  *   @Create Date         2023.09.20
;  *   @Official website    http://www.devechip.com/
;  *----------------------Abstract Description---------------------------------
;  *                        FET控制驱动
**************************************************************************************/
#include "FETControl.h"

/** 预充驱动开关 */
bool PCHG_FETControl(bool abel){
    g_AfeRegs.R81.PCHGC=abel;
    return DVC11XX_WriteRegs(AFE_ADDR_R(81),1);
}

/** 预放驱动开关 */
bool PDSG_FETControl(bool abel){
    g_AfeRegs.R81.PDSGC=abel;
    return DVC11XX_WriteRegs(AFE_ADDR_R(81),1);
}

/** 充电驱动开关 */
bool CHG_FETControl(u8 mode){
    if(mode==2)
        g_AfeRegs.R84.CBDM=0;
    g_AfeRegs.R81.CHGC=mode;
    return DVC11XX_WriteRegs(AFE_ADDR_R(81),4);
}

/** 放电驱动开关 */
bool DSG_FETControl(u8 mode){
    if(mode==2)
        g_AfeRegs.R83.DBDM=0;
    g_AfeRegs.R81.DSGC=mode;
    return DVC11XX_WriteRegs(AFE_ADDR_R(81),3);
}

/** 高边DSG输出模式设置 */
bool DSGM_Control(bool mode){
    g_AfeRegs.R81.DSGM=mode;
    return DVC11XX_WriteRegs(AFE_ADDR_R(81),1);
}

/** 高边DSG下拉强度设置 */
bool DPC_Config(u8 DPC){
    if(DPC<31){
        g_AfeRegs.R82.DPC=DPC;
        return DVC11XX_WriteRegs(AFE_ADDR_R(82),1);
    }
    return ERROR;
}

/** 续流保护阈值设置，保护阈值=BDPT*40uV */
bool BDPT_Config(u16 value){
    if(value<=10200){
        g_AfeRegs.R102.BDPT=value/40;
        return DVC11XX_WriteRegs(AFE_ADDR_R(102),1);
    }
    return ERROR;
}

/** 电荷泵输出设置 */
bool ChargePump_Control(u8 mode){
    g_AfeRegs.R109.CPVS=mode;
    return DVC11XX_WriteRegs(AFE_ADDR_R(109),1);
}

/** 高边驱动屏蔽设置 */
bool HSFM_Control(bool mode){
    g_AfeRegs.R85.HSFM=mode;
    return DVC11XX_WriteRegs(AFE_ADDR_R(85),1);
}
