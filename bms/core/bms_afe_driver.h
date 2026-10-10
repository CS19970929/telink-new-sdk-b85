/*
 * 文件功能：选定 AFE 驱动的统一声明入口；仅转接编译期后端，不引入运行时分发。
 * bms/core/bms_afe_driver.h；实际编译归属见各产品 sources.txt。
 */
#ifndef BMS_AFE_DRIVER_H_
#define BMS_AFE_DRIVER_H_

/* 驱动私有声明供门禁、所选后端和只读诊断使用；应用使用 bms_afe.h，不能绕过通信门禁。 */
#include "bms_afe.h"

/* 后端 FET 写入也使用公共门禁的三帧新样本资格。 */
uint8_t bms_afe_samples_qualified(void);

#if (BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124)
/* 初始化选定 AFE 后端并应用产品配置。 */
void dvc1124_backend_init(void);
/* 采集 AFE 测量和状态，并更新样本有效性。 */
void dvc1124_backend_sample(void);
/* 成功读取但等待新的 ADC 完成，不计入采样资格。 */
uint8_t dvc1124_backend_sample_pending(void);
/* 执行后端 shutdown 流程并返回通信结果。 */
uint8_t dvc1124_backend_enter_shutdown(void);
/* 应用独立 AFE 硬件保护配置并返回结果。 */
uint8_t dvc1124_backend_apply_protection_config(void);
/* 设置后端充放电 MOS 请求并进行保护仲裁。 */
uint8_t dvc1124_backend_set_fets(uint8_t,uint8_t);
/* 设置后端输出授权，禁止绕过公共安全门禁。 */
void dvc1124_backend_set_output_enabled(uint8_t);
/* 取得后端辅助测量与有效性。 */
uint8_t dvc1124_backend_get_aux_measurements(bms_afe_aux_measurements_t *);
/* 取得均衡、温度和断线策略需要的后端快照。 */
uint8_t dvc1124_backend_get_feature_snapshot(bms_afe_feature_snapshot_t *);
/* 返回后端可确认的充电源存在状态。 */
uint8_t dvc1124_backend_get_charge_source_present(uint8_t *);
/* 设置有效通道范围内的后端均衡掩码。 */
uint8_t dvc1124_backend_set_balance_mask(uint32_t);
/* 取得后端缓存或回读的均衡状态掩码。 */
uint8_t dvc1124_backend_get_balance_mask(uint32_t *);
/* 开始后端非阻塞电芯断线检测。 */
uint8_t dvc1124_backend_openwire_start(void);
uint8_t dvc1124_backend_openwire_stop(void);
/* 推进后端断线检测并返回阶段或结果。 */
bms_afe_diag_state_t dvc1124_backend_openwire_poll(bms_afe_openwire_result_t *);
#elif (BMS_AFE_BACKEND == BMS_AFE_BACKEND_SH3673510)
/* 初始化选定 AFE 后端并应用产品配置。 */
void sh3673510_bms_afe_init(void);
/* 采集 AFE 测量和状态，并更新样本有效性。 */
void sh3673510_bms_afe_sample(void);
/* 按器件与板级时序进入 AFE 休眠。 */
uint8_t sh3673510_bms_afe_sleep(void);
/* 应用独立 AFE 硬件保护配置并返回结果。 */
uint8_t sh3673510_bms_afe_apply_protection_config(void);
/* 设置后端充放电 MOS 请求并进行保护仲裁。 */
uint8_t sh3673510_bms_afe_set_fets(uint8_t,uint8_t);
/* 设置后端输出授权，禁止绕过公共安全门禁。 */
void sh3673510_bms_afe_set_output_enabled(uint8_t);
/* 取得后端辅助测量与有效性。 */
uint8_t sh3673510_bms_afe_get_aux_measurements(bms_afe_aux_measurements_t *);
/* 读取成功但等待转换完成，不计入新样本资格。 */
uint8_t sh3673510_bms_afe_sample_pending(void);
/* 取得 FET 请求、控制和保护阻断诊断。 */
uint8_t sh3673510_bms_afe_get_fet_diagnostics(uint8_t *, uint8_t *, uint8_t *, uint8_t *);
typedef struct {
    uint8_t flag1, flag2, bstatus2;
    uint16_t backend_state, sensor_state, mos_ntc_raw, mos_temp_x10;
    uint32_t mos_ntc_ohm;
    uint32_t charge_block_reasons, discharge_block_reasons;
} sh3673510_fet_diag_detail_t;
/* 取得 FET 仲裁的详细诊断字段。 */
uint8_t sh3673510_bms_afe_get_fet_diag_detail(sh3673510_fet_diag_detail_t *detail);
/* 取得均衡、温度和断线策略需要的后端快照。 */
uint8_t sh3673510_backend_get_feature_snapshot(bms_afe_feature_snapshot_t *);
/* 返回后端可确认的充电源存在状态。 */
uint8_t sh3673510_backend_get_charge_source_present(uint8_t *);
/* 设置有效通道范围内的后端均衡掩码。 */
uint8_t sh3673510_backend_set_balance_mask(uint32_t);
/* 取得后端缓存或回读的均衡状态掩码。 */
uint8_t sh3673510_backend_get_balance_mask(uint32_t *);
/* 开始后端非阻塞电芯断线检测。 */
uint8_t sh3673510_backend_openwire_start(void);
uint8_t sh3673510_backend_openwire_stop(void);
/* 推进后端断线检测并返回阶段或结果。 */
bms_afe_diag_state_t sh3673510_backend_openwire_poll(bms_afe_openwire_result_t *);
#else
#error "Unsupported BMS_AFE_BACKEND"
#endif

#endif /* 头文件保护：BMS_AFE_DRIVER_H_。 */
