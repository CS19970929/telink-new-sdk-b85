/******************************************************************************
;  *    @AFE model                        DVC1124
;  *    @Create Date         2023.4.10
;  *    @Official website         http://www.devechip.com/
;  *----------------------Abstract Description---------------------------------
;  *        Header file for generic DVC1124 series.
;  *        Nanjing Devechip Electronic Technology Co., Ltd.
;  *        All rights reserved.
******************************************************************************/
#ifndef DVC1124_H__
#define DVC1124_H__
//------------------------------------------------------------------------------
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include "stm32f10x.h"
//---------------------------------------------------------------------------
typedef char                            bool;
//------------------------------------------------------------------------------

#define  AFE_MAX_CELL_CNT               24
#define  AFE_MAX_GP_CNT                 6
#define  AFE_REG_VALUE(Rn)              *((u8 *)&g_AfeRegs.Rn)
#define  AFE_ADDR_R(n)                  n
#define _lsbVCELL                       0.0001
#define _lsbVCELL_signed                0.0002
#define _lsbCC1                        (5e-6)
#define _lsbCC2                        (0.3125e-6)
#define ADDRESS                         0x40
#define INVALID_TEMPERATURE            -128
#define GENERAL_DEBOUNCE_DLY_S          3
#define AFE_CMD_RETRY_MAX               3
#pragma pack (push,1)
enum{GP1=0,GP2,GP3,GP4,GP5,GP6};

typedef struct{
u8 cleanR0;
}CleanFlag;

typedef struct{
 union {struct{
   u8 SCD:1;
   u8 OCC2:1;
   u8 OCD2:1;
   u8 OCC1:1;
   u8 OCD1:1;
   u8 CUV:1;
   u8 COV:1;
   u8 IWF:1;
 }bitmap;u8 cleanflag;}R0;
 struct{
   u8 CST:4;
   u8 CC2F:1;
   u8 CC1F:1;
   u8 VADF:1;
   u8 PD:1;
 }R1;
 struct{u8 CC1_H;u8 CC1_L;}R2_3;
 struct{
   u8 CC2_H;
   u8 CC2_M;
   u8 CHGF:1;  // R6[0] CHG驱动输出标识位; 0关闭, 1开启
   u8 DSGF:1;  // R6[1] DSG驱动输出标识位; 0关闭, 1开启
   u8 PCHGF:1; // R6[2] PCHG驱动输出标识位
   u8 PDSGF:1; // R6[3] PDSG驱动输出标识位
   u8 CC2_L:4;
 }R4_6;
 struct{u8 VBAT_H;u8 VBAT_L;}R7_8;
 struct{u8 VPK_H;u8 VPK_L;}R9_10;
 struct{u8 VLD_H;u8 VLD_L;}R11_12;
 struct{u8 VCT_H;u8 VCT_L;}R13_14;
 struct{u8 V1P8_H;u8 V1P8_L;}R15_16;
 struct{struct{u8 VGP_H;u8 VGP_L;}VGP[6];}R17_28;
 struct{struct{u8 VCELL_H;u8 VCELL_L;}VCELLS[24];}R29_76;
 struct{u8 VVOS_H;u8 VVOS_L;}R77_78;
 struct{u8 CVOS_H;u8 CVOS_L;}R79_80;
 struct{
   u8 CHGC:2;  // R81[1:0]: 00/01关闭;10体二极管条件自动;11开启
   u8 DSGC:2;  // R81[3:2]: 00/01关闭;10体二极管条件自动;11开启
   u8 DSGM:1;
   u8 PCHGC:1;
   u8 PDSGC:1;
   u8 LDPU:1;
 }R81;
 struct{u8 DPC:5;u8 R82_RVD:2;u8 PDWM:1;}R82;
 struct{
   u8 DBDM:1;u8 DPDM:1;u8 DDM:1;u8 DWM:1;
   u8 PCDM:1;u8 PCCM:1;u8 PCWM:1;u8 PDDM:1;
 }R83;
 struct{
   u8 CBDM:1;u8 CPCM:1;u8 CCM:1;u8 CDM:1;
   u8 CSM:1;u8 CO2M:1;u8 CO1M:1;u8 CWM:1;
 }R84;
 struct{
   u8 CAMZ:1;u8 R85_RVD1:1;u8 CAES:1;u8 CAEW:1;u8 R85_RVD4_6:3;
   u8 HSFM:1; // 高边NFET驱动输出屏蔽;0允许,1屏蔽
 }R85;
 struct{u8 C1OS:2;u8 C1OW:2;u8 R86_RVD:4;}R86;
 u8 R87;u8 R88;
 struct{u8 OCD1T;}R89;
 struct{u8 OCC1T;}R90;
 struct{u8 OCD1D;}R91;
 struct{u8 OCC1D;}R92;
 u8 R93;
 struct{u8 OCD2T:6;u8 OCD2E:1;u8 R94_RVD:1;}R94;
 struct{u8 OCC2T:6;u8 OCC2E:1;u8 R95_RVD:1;}R95;
 struct{u8 OCD2D;}R96;
 struct{u8 OCC2D;}R97;
 struct{u8 SCDT:6;u8 SCDE:1;u8 R98_RVD:1;}R98;
 struct{u8 SCDD;}R99;
 u8 R100;
 struct{u8 CWT;}R101;
 struct{u8 BDPT;}R102;
 struct{u8 CB[3];}R103_R105;
 struct{u8 CM_H;}R106;
 struct{u8 CM_M;}R107;
 struct{u8 V1P8M:1;u8 CTM:1;u8 LDM:1;u8 PKM:1;u8 CM_L:4;}R108;
 struct{
   u8 CVS:1;u8 CMM:1;u8 COW:1;u8 CPVS:3;u8 CUWM:1;u8 R109_RVD:1;
 }R109;
 struct{u8 VAO:2;u8 R110_RVD:2;u8 VAMP:2;u8 VASM:1;u8 VAE:1;}R110;
 u8 R111;
 struct{u8 COVT_H;}R112;
 struct{u8 COVD:4;u8 COVT_L:4;}R113;
 struct{u8 CUVT_H;}R114;
 struct{u8 CUVD:4;u8 CUVT_L:4;}R115;
 struct{
   u8 GP3M:3; // 111 PCHG_LS
   u8 GP2M:3; // 111 PDSG_LS
   u8 GP1M:2;
 }R116;
 struct{
   u8 GP6M:3; // 111 DSG_LS
   u8 GP5M:3; // 111 CHG_LS
   u8 GP4M:2;
 }R117;
 struct{u8 COTT:7;u8 COTF:1;}R118;
 struct{u8 IWT:3;u8 IWTS:1;u8 R119_RVD4:1;u8 V3P3M:1;u8 V3P3EW:1;u8 V3P3ES:1;}R119;
 struct{u8 TWSE:4;u8 R120_RVD:3;u8 TIWK:1;}R120;
 struct{u8 ISCM:1;u8 IOC2M:1;u8 IOC1M:1;u8 ICUM:1;u8 ICOM:1;u8 ICCM:1;u8 IVOM:1;u8 IWM:1;}R121;
 u8 R122;u8 R123;u8 R124;u8 R125;
 struct{u8 F1RT;}R126;
 u8 R127;u8 R128;u8 R129;u8 R130;u8 R131;u8 R132;u8 R133;u8 R134;u8 R135;u8 R136;u8 R137;u8 R138;u8 R139;u8 R140;u8 R141;u8 R142;
 struct{u8 CV;}R143;
 u8 R144;
}TAFERegs;
#pragma pack (pop)

/* Vendor preset data retained as demo evidence only. Do not use as D008 product truth. */
static const unsigned char DVC11XX_PresetConfigRegData_R81To121[]={
  0x00,0x9E,0x59,0xF9,0x2C,0xFF,0x28,0x05,0x40,
  0x40,0x18,0x18,0xB0,0x47,0xC7,0x18,0x18,0x50,0x10,
  0x88,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x68,
  0xCD,0x28,0xE7,0x48,0x5D,0xC8,0x00,0x00,0x00,0xc0,
  0x00,0x00
};

void CleanError(void);
void Initial(void);
void DVC11XX_GPIO_Init(void);
void DVC11XX_ForceSleep(void);
bool DVC11XX_WriteRegs(u8 regAddr,u8 regLen);
bool DVC11XX_ReadRegs(u8 regAddr,u8 regLen);
bool IIC_ReadDataWithCRC(u8 regAddr,void *dataPtr,u16 dataLen);
bool IIC_WriteDataWithCRC(u8 regAddr,void *dataPtr,u16 dataLen);
void delay_init(void);
void delay_us(u16 us);
void delay_ms(u16 ms);
extern u8 IIC_SLAVE_ADDRESS;
extern TAFERegs g_AfeRegs;
extern CleanFlag g_cleanflag;
#endif
