/*
 * 文件功能：软件保护参数布局与纯参数校验声明；与 AFE 硬件 profile 分开维护。
 * bms/core/bms_protection_params.h；实际编译归属见各产品 sources.txt。
 */
#ifndef BMS_PROTECTION_PARAMS_H_
#define BMS_PROTECTION_PARAMS_H_
#include <stdint.h>
/* 共 65 words，字段名保留外部寄存器契约。 */
struct PRT_E2ROM_PARAS {
// 参数存储顺序与地址分配。
	uint16_t	u16VcellOvp_First;
	uint16_t	u16VcellOvp_Second;
	uint16_t	u16VcellOvp_Third;
	uint16_t	u16VcellOvp_Rcv;
	uint16_t	u16VcellOvp_Filter;

	uint16_t	u16VcellUvp_First;
	uint16_t	u16VcellUvp_Second;
	uint16_t	u16VcellUvp_Third;
	uint16_t	u16VcellUvp_Rcv;
	uint16_t	u16VcellUvp_Filter;

	uint16_t	u16VbusOvp_First;
	uint16_t	u16VbusOvp_Second;
	uint16_t	u16VbusOvp_Third;
	uint16_t	u16VbusOvp_Rcv;
	uint16_t	u16VbusOvp_Filter;

	uint16_t	u16VbusUvp_First;
	uint16_t	u16VbusUvp_Second;
	uint16_t	u16VbusUvp_Third;
	uint16_t	u16VbusUvp_Rcv;
	uint16_t	u16VbusUvp_Filter;

	uint16_t	u16IchgOcp_First;
	uint16_t	u16IchgOcp_Second;
	uint16_t	u16IchgOcp_Third;
	uint16_t	u16IchgOcp_Rcv;
	uint16_t	u16IchgOcp_Filter;

	uint16_t	u16IdsgOcp_First;
	uint16_t	u16IdsgOcp_Second;
	uint16_t	u16IdsgOcp_Third;
	uint16_t	u16IdsgOcp_Rcv;
	uint16_t	u16IdsgOcp_Filter;

	uint16_t	u16TChgOTp_First;
	uint16_t	u16TChgOTp_Second;
	uint16_t	u16TChgOTp_Third;
	uint16_t	u16TChgOTp_Rcv;
	uint16_t	u16TChgOTp_Filter;

	uint16_t	u16TchgUTp_First;
	uint16_t	u16TchgUTp_Second;
	uint16_t	u16TchgUTp_Third;
	uint16_t	u16TchgUTp_Rcv;
	uint16_t	u16TchgUTp_Filter;

	uint16_t	u16TdischgOTp_First;
	uint16_t	u16TdischgOTp_Second;
	uint16_t	u16TdischgOTp_Third;
	uint16_t	u16TdischgOTp_Rcv;
	uint16_t	u16TdischgOTp_Filter;

	uint16_t	u16TdischgUTp_First;
	uint16_t	u16TdischgUTp_Second;
	uint16_t	u16TdischgUTp_Third;
	uint16_t	u16TdischgUTp_Rcv;
	uint16_t  u16TdischgUTp_Filter;

	uint16_t	u16TmosOTp_First;
	uint16_t	u16TmosOTp_Second;
	uint16_t	u16TmosOTp_Third;
	uint16_t	u16TmosOTp_Rcv;
	uint16_t	u16TmosOTp_Filter;

	uint16_t	u16VdeltaOvp_First;
	uint16_t	u16VdeltaOvp_Second;
	uint16_t	u16VdeltaOvp_Third;
	uint16_t	u16VdeltaOvp_Rcv;
	uint16_t	u16VdeltaOvp_Filter;

	uint16_t	u16SocLow_First;
	uint16_t	u16SocLow_Second;
	uint16_t	u16SocLow_Third;
	uint16_t	u16SocLow_Rcv;
	uint16_t	u16SocLow_Filter;
};
typedef struct {
    struct PRT_E2ROM_PARAS protect;
} PARAM_T;
extern PARAM_T g_tParam;
/* 检查软件保护参数的阈值及恢复关系。 */
uint8_t bms_protection_params_valid(void);
#endif
