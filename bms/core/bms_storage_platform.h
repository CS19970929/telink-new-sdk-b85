/*
 * 文件功能：存储端口、分区和诊断契约；隔离记录格式与 Telink Flash 实现。
 * bms/core/bms_storage_platform.h；实际编译归属见各产品 sources.txt。
 */
#ifndef BMS_STORAGE_PLATFORM_H_
#define BMS_STORAGE_PLATFORM_H_

#include "storage_port.h"

/* 业务 checkpoint 与平台写失败共用的有限重试间隔，单位 32K tick。 */
#define BMS_STORAGE_RETRY_INTERVAL_32K (5u * 32000u)

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BMS_STORAGE_DOMAIN_CONFIG = 0,
    BMS_STORAGE_DOMAIN_STATE,
    BMS_STORAGE_DOMAIN_RESERVED_2, /* 诊断 schema 1 的原位置保持 NOT_RUN。 */
    BMS_STORAGE_DOMAIN_EVENT,
    BMS_STORAGE_DOMAIN_COUNT
} bms_storage_domain_t;

/* 本次启动以来的诊断，不持久化磨损计数；时间使用 SDK 32K。 */
typedef struct {
    uint32_t program_calls;
    uint32_t erase_calls;
    uint32_t verify_failures;
    uint32_t deferred_writes;
    uint32_t max_program_ticks_32k;
    uint32_t max_erase_ticks_32k;
} bms_storage_diagnostics_t;
/* 发布启动阶段的存储分区与操作诊断。 */
void bms_storage_platform_diag_boot(void);
/* 刷新运行阶段的存储诊断字段。 */
void bms_storage_platform_diag_poll(void);
/* 取得 Flash 操作结果及耗时诊断快照。 */
void bms_storage_platform_get_diagnostics(bms_storage_diagnostics_t *out);
/* 取得 Telink 存储端口操作集合。 */
const storage_port_t *bms_storage_platform_port(void);
/* 取得指定持久域的 Flash 区域配置。 */
int bms_storage_platform_region(bms_storage_domain_t domain, storage_region_t *region);

#ifdef __cplusplus
}
#endif

#endif /* 头文件保护：BMS_STORAGE_PLATFORM_H_。 */
