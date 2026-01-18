/*
*********************************************************************************************************
*
*	模块名称 : 应用程序参数模块
*	文件名称 : param.c
*	版    本 : V1.0
*	说    明 : 读取和保存应用程序的参数
*	修改记录 :
*		版本号  日期        作者     说明
*		V1.0    2013-01-01 armfly  正式发布
*
*	Copyright (C), 2012-2013, 安富莱电子 www.armfly.com
*
*********************************************************************************************************
*/

// #include "bsp.h"
#include "drivers.h"
#include "stack/ble/ble.h"
#include "app.h"
#include "param.h"

PARAM_T g_tParam;

/* 将16KB 一个扇区的空间预留出来做为参数区 For MDK */
//const u8 para_flash_area[16*1024] __attribute__((at(ADDR_FLASH_SECTOR_3)));

/*
*********************************************************************************************************
*	函 数 名: LoadParam
*	功能说明: 从Flash读参数到g_tParam
*	形    参：无
*	返 回 值: 无
*********************************************************************************************************
*/
void LoadParam(void)
{
#ifdef PARAM_SAVE_TO_FLASH
	/* 读取CPU Flash中的参数 */
	// flash_read_page(PARAM_ADDR, (u8 *)&g_tParam, sizeof(PARAM_T));
	flash_read_page(PARAM_ADDR, sizeof(PARAM_T), (u8 *)&g_tParam);
	printf("paramVer = %d", g_tParam.ParamVer);
	array_printf((unsigned char *)&g_tParam, sizeof(g_tParam));
#endif

#ifdef PARAM_SAVE_TO_EEPROM
	/* 读取EEPROM中的参数 */
	ee_ReadBytes((u8 *)&g_tParam, PARAM_ADDR, sizeof(PARAM_T));
#endif

	/* 填充缺省参数 */
	if (g_tParam.ParamVer != PARAM_VER)
	{
		struct PRT_E2ROM_PARAS default_param = E2P_PROTECT_DEFAULT_PRT;

		g_tParam.ParamVer = PARAM_VER;
		printf("flash param, paramVer = %d", g_tParam.ParamVer);
		// g_tParam.protect = E2P_PROTECT_DEFAULT_PRT;
		g_tParam.protect = default_param;
		array_printf((unsigned char *)&g_tParam, sizeof(g_tParam));

		SaveParam();							/* 将新参数写入Flash */
	}
}

/*
*********************************************************************************************************
*	函 数 名: SaveParam
*	功能说明: 将全局变量g_tParam 写入到CPU内部Flash
*	形    参: 无
*	返 回 值: 无
*********************************************************************************************************
*/
void SaveParam(void)
{
#ifdef PARAM_SAVE_TO_FLASH
	/* 将全局的参数变量保存到 CPU Flash */
	flash_erase_sector(PARAM_ADDR);
	flash_write_page(PARAM_ADDR, sizeof(PARAM_T), (unsigned char *)&g_tParam);
#endif

#ifdef PARAM_SAVE_TO_EEPROM
	/* 将全局的参数变量保存到EEPROM */
	ee_WriteBytes((u8 *)&g_tParam, PARAM_ADDR, sizeof(PARAM_T));
#endif
}

/***************************** 安富莱电子 www.armfly.com (END OF FILE) *********************************/
