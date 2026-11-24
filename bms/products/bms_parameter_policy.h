/* 文件功能：保留历史编号只读协议，OTA 决策见 bms_update_policy.h。 */
#pragma once

/* 编号依赖产品身份；独立 host 夹具须先声明其模拟产品 ID。 */

/*
 * 新固件忽略客户旧编号，以 BIN 内更新范围和一次性标记决定是否更新。
 * 下列常量只供既有 0x2E80..0x2E88 读取，不能用来证明参数已应用。
 * 详见 docs/OTA_PARAMETERS.md；旧实现的固件回刷仍可能按旧策略重置参数。
 */
/* 历史只读地址保持原值；新固件不再据此覆盖客户参数。 */
#if defined(BMS_BUILD_PARAMETERS_REVISION) || defined(BMS_BUILD_SOC_STATE_REVISION)
#error "OTA update revisions are obsolete; select parameter groups and update identifier"
#endif
#define BMS_UPDATE_SW_REVISION 1u /* 软件保护 */
#define BMS_UPDATE_AFE_REVISION 3u /* D008 历史只读值，不代表当前保护设定。 */
#define BMS_UPDATE_BUSINESS_REVISION 1u /* 容量、加热和均衡 */
#define BMS_UPDATE_SOC_REVISION 1u /* SOC 算法配置与化学体系 */
#define BMS_UPDATE_CALIBRATION_REVISION 1u /* 电流校准 */
#define BMS_UPDATE_IDENTITY_REVISION 1u /* 序列号与蓝牙名称 */
#define BMS_UPDATE_SOC_STATE_REVISION 1u /* SOC 和循环状态 */
#define BMS_UPDATE_EVENTS_REVISION 1u /* 事件记录 */
