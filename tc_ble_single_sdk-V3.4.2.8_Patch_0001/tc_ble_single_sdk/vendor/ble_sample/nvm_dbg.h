#pragma once
/*
 * nvm_dbg.h
 * 统一 NVM/Flash 调试日志接口（可重定向到你项目的 printf/UART log）
 *
 * 用法：
 *   - 默认使用 printf（要求你工程里可用）
 *   - 若你希望用 Telink 的 tlkapi_printf / log_task 等，定义 NVM_PRINTF 为你的函数
 *   - 通过 NVM_LOG_LEVEL 控制输出等级
 */

#include <stdint.h>
#include <stddef.h>

#ifndef NVM_LOG_LEVEL
// 0=OFF, 1=ERROR, 2=WARN, 3=INFO, 4=DEBUG, 5=TRACE
#define NVM_LOG_LEVEL 4
#endif

#ifndef NVM_PRINTF
#include <stdio.h>
#define NVM_PRINTF printf
#endif

#ifdef __cplusplus
extern "C" {
#endif

void nvm_dbg_hexdump(const char *tag, const void *data, size_t len);

#define NVM_LOGE(fmt, ...) do{ if(NVM_LOG_LEVEL>=1) NVM_PRINTF("[NVM][E] " fmt "\r\n", ##__VA_ARGS__);}while(0)
#define NVM_LOGW(fmt, ...) do{ if(NVM_LOG_LEVEL>=2) NVM_PRINTF("[NVM][W] " fmt "\r\n", ##__VA_ARGS__);}while(0)
#define NVM_LOGI(fmt, ...) do{ if(NVM_LOG_LEVEL>=3) NVM_PRINTF("[NVM][I] " fmt "\r\n", ##__VA_ARGS__);}while(0)
#define NVM_LOGD(fmt, ...) do{ if(NVM_LOG_LEVEL>=4) NVM_PRINTF("[NVM][D] " fmt "\r\n", ##__VA_ARGS__);}while(0)
#define NVM_LOGT(fmt, ...) do{ if(NVM_LOG_LEVEL>=5) NVM_PRINTF("[NVM][T] " fmt "\r\n", ##__VA_ARGS__);}while(0)

#ifdef __cplusplus
}
#endif
