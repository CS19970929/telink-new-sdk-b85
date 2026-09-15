#ifndef FETCONTROL_H__
#define FETCONTROL_H__
#include "DVC11XX.h"


enum {                 //DSG Output MODE
DSGM_CP=0,             //电荷泵
DSGM_FC                //随源
};
enum {                 //CHG DSG MODE
FET_Close=1,           //关闭FET驱动
FET_Close_BFCO,        //关闭FET驱动，但允许在电流大于续流阈值开启FET驱动
FET_OPEN
};                     //开启FET驱动

enum{                  //ChargePump mode
CP_Close=0,            //关闭电荷泵
CP_6V,                 //电荷泵6V
CP_7V,
CP_8V,
CP_9V,
CP_10V,
CP_11V,
CP_12V,                //电荷泵12V
};

bool PCHG_FETControl(bool abel);
bool PDSG_FETControl(bool abel);
bool CHG_FETControl(u8 mode);
bool DSG_FETControl(u8 mode);
bool DSGM_Control(bool mode);
bool DPC_Config(u8 DPC);
bool BDPT_Config(u16 value);
bool ChargePump_Control(u8 mode);
bool HSFM_Control(bool mode);
#endif
