/*
 * 文件功能：DVC1124 backend 启动、采样、配置和休眠入口声明；
 * 供公共 guard 按编译期选择调用。
 */
#ifndef DVC1124_BOOT_H_
#define DVC1124_BOOT_H_

#include "dvc1124.h"

/* DVC1124 板级启动配置；运行参数由公共 Config/State/Event 存储负责。 */
#define DVC1124_FIXED_CONFIG_COMPILE_TIME 1u

/*
 * 仅后端使用的原语。
 * 产品/测试代码必须通过受保护的bms_afe_test_enter_shutdown()/bms_afe_test_wake() 生
 * 命周期。
 */
uint8_t dvc1124_backend_enter_shutdown(void);

#endif /* 头文件保护：DVC1124_BOOT_H_。 */
