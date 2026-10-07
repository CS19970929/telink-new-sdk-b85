/*
 * 文件功能：软件保护参数布局与纯参数校验声明；与 AFE 硬件 profile 分开维护。
 * bms/core/bms_protection_params.h；实际编译归属见各产品 sources.txt。
 */
#ifndef BMS_PROTECTION_PARAMS_H_
#define BMS_PROTECTION_PARAMS_H_
#include <stdint.h>
/* 共 65 words，字段顺序与单位保持外部寄存器契约。
 * x10 为 (degC+40)*10 温度编码，a10 为 A*10，filter_10ms 为 10 ms 单位。 */
typedef struct {
// 参数存储顺序与地址分配。
	uint16_t	cell_ovp_first_mv;
	uint16_t	cell_ovp_second_mv;
	uint16_t	cell_ovp_third_mv;
	uint16_t	cell_ovp_recover_mv;
	uint16_t	cell_ovp_filter_10ms;

	uint16_t	cell_uvp_first_mv;
	uint16_t	cell_uvp_second_mv;
	uint16_t	cell_uvp_third_mv;
	uint16_t	cell_uvp_recover_mv;
	uint16_t	cell_uvp_filter_10ms;

	uint16_t	pack_ovp_first_10mv;
	uint16_t	pack_ovp_second_10mv;
	uint16_t	pack_ovp_third_10mv;
	uint16_t	pack_ovp_recover_10mv;
	uint16_t	pack_ovp_filter_10ms;

	uint16_t	pack_uvp_first_10mv;
	uint16_t	pack_uvp_second_10mv;
	uint16_t	pack_uvp_third_10mv;
	uint16_t	pack_uvp_recover_10mv;
	uint16_t	pack_uvp_filter_10ms;

	uint16_t	charge_ocp_first_a10;
	uint16_t	charge_ocp_second_a10;
	uint16_t	charge_ocp_third_a10;
	uint16_t	charge_ocp_recover_a10;
	uint16_t	charge_ocp_filter_10ms;

	uint16_t	discharge_ocp_first_a10;
	uint16_t	discharge_ocp_second_a10;
	uint16_t	discharge_ocp_third_a10;
	uint16_t	discharge_ocp_recover_a10;
	uint16_t	discharge_ocp_filter_10ms;

	uint16_t	charge_otp_first_x10;
	uint16_t	charge_otp_second_x10;
	uint16_t	charge_otp_third_x10;
	uint16_t	charge_otp_recover_x10;
	uint16_t	charge_otp_filter_10ms;

	uint16_t	charge_utp_first_x10;
	uint16_t	charge_utp_second_x10;
	uint16_t	charge_utp_third_x10;
	uint16_t	charge_utp_recover_x10;
	uint16_t	charge_utp_filter_10ms;

	uint16_t	discharge_otp_first_x10;
	uint16_t	discharge_otp_second_x10;
	uint16_t	discharge_otp_third_x10;
	uint16_t	discharge_otp_recover_x10;
	uint16_t	discharge_otp_filter_10ms;

	uint16_t	discharge_utp_first_x10;
	uint16_t	discharge_utp_second_x10;
	uint16_t	discharge_utp_third_x10;
	uint16_t	discharge_utp_recover_x10;
	uint16_t  discharge_utp_filter_10ms;

	uint16_t	mos_otp_first_x10;
	uint16_t	mos_otp_second_x10;
	uint16_t	mos_otp_third_x10;
	uint16_t	mos_otp_recover_x10;
	uint16_t	mos_otp_filter_10ms;

	uint16_t	cell_delta_first_mv;
	uint16_t	cell_delta_second_mv;
	uint16_t	cell_delta_third_mv;
	uint16_t	cell_delta_recover_mv;
	uint16_t	cell_delta_filter_10ms;

	uint16_t	soc_low_first_percent;
	uint16_t	soc_low_second_percent;
	uint16_t	soc_low_third_percent;
	uint16_t	soc_low_recover_percent;
	uint16_t	soc_low_filter_10ms;
} bms_protection_params_t;
extern bms_protection_params_t g_bms_protection_params;
/* 检查软件保护参数的阈值及恢复关系。 */
uint8_t bms_protection_params_valid(void);
#endif
