#pragma once
#include "soc.h"
typedef enum { CONFIG_DATA, SOC_DATA, EVENT_DATA, DOMAIN_COUNT } Domain;
enum { CONFIG_FORMAT_VERSION=2, SOC_FORMAT_VERSION=2, LOG_FORMAT_VERSION=2 };
typedef enum { STORE_BAD, STORE_BLANK, STORE_VALID, STORE_CONFLICT, STORE_IO_ERROR } StoreResult;
StoreResult store_open(Domain domain, void *latest, unsigned size);
StoreResult store_open_config(Config *latest, const Config *assembly);
bool store_available(Domain domain);
/* 同步调用的暂缓原因由 NOR 端报告，主循环下次再试。 */
extern bool store_deferred;
bool store_append(Domain domain, const void *data, unsigned size);
bool store_prepare_soc_space(void);
unsigned store_events(void);
bool store_event_at(unsigned newest_index, uint16_t *event);

/* 平台检查布局、真实读取、页边界和 BLE 时序。主机可替换为 NOR 故障模型。 */
bool nor_read(uint32_t addr, void *out, unsigned size);
bool nor_write(uint32_t addr, const void *data, unsigned size);
bool nor_erase(uint32_t sector);
