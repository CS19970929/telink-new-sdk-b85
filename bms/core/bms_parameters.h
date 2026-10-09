/* 文件功能：软件保护运行参数所有者、提交及持久域启动安全门禁。 */
#ifndef BMS_PARAMETERS_H_
#define BMS_PARAMETERS_H_

#include "bms_protection_params.h"

/* 一次完成 Config/State/Event 初始化；仅安全配置失败保持输出阻断。 */
void bms_parameters_init(void);
/* 候选校验和持久保存成功后才发布，不清除失败的启动门禁。 */
uint8_t bms_protection_params_commit(const bms_protection_params_t *candidate);
/* 刷新运行参数和启动门禁诊断。 */
void bms_parameters_diag_poll(void);
#endif
