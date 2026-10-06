/* 文件功能：各持久参数类别的更新编号，决定同格式 OTA 后保留或恢复默认。 */
#pragma once

/*
 * OTA/烧录后：编号不变保留设备值；改变编号仅重置对应类别。编号范围 1..65535。
 * 修改默认值时自行决定是否同时更改编号。回刷旧固件也按编号是否相等判断，
 * 详见 docs/OTA_PARAMETERS.md。
 */
#define BMS_UPDATE_SW_REVISION 1u /* 软件保护 */
#define BMS_UPDATE_AFE_REVISION 1u /* AFE 硬件保护 */
#define BMS_UPDATE_BUSINESS_REVISION 1u /* 容量、加热和均衡 */
#define BMS_UPDATE_SOC_REVISION 1u /* SOC 算法配置与化学体系 */
#define BMS_UPDATE_CALIBRATION_REVISION 1u /* 电流校准 */
#define BMS_UPDATE_IDENTITY_REVISION 1u /* 序列号与蓝牙名称 */
#define BMS_UPDATE_SOC_STATE_REVISION 1u /* SOC、循环和容量学习状态 */
#define BMS_UPDATE_EVENTS_REVISION 1u /* 事件记录 */
