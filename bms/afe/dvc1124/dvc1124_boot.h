/* 文件功能：DVC1124 backend 启动、采样、配置和休眠入口声明；供公共 guard 按编译期选择调用。 */
#ifndef DVC1124_BOOT_H_
#define DVC1124_BOOT_H_

#include "dvc1124.h"

/* DVC1124 板级启动配置；运行参数由公共 Config/State/Event 存储负责。 */
#define DVC1124_FIXED_CONFIG_COMPILE_TIME 1u

/* Backend-only primitive. Product/test code must use the guarded
 * bms_afe_test_enter_shutdown()/bms_afe_test_wake() lifecycle. */
uint8_t dvc1124_backend_enter_shutdown(void);

#endif /* DVC1124_BOOT_H_ */
