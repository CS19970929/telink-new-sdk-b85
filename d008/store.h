#pragma once
#include "soc.h"
typedef enum { CONFIG_DATA, SOC_DATA, EVENT_DATA, DOMAIN_COUNT } Domain;
typedef enum { STORE_BAD, STORE_BLANK, STORE_VALID } StoreResult;
StoreResult store_open(Domain domain, void *latest, unsigned size);
bool store_append(Domain domain, const void *data, unsigned size);
unsigned store_events(void);
bool store_event_at(unsigned newest_index, uint16_t *event);

/* 平台检查布局、真实读取、页边界和 BLE 时序。主机可替换为 NOR 故障模型。 */
bool nor_read(uint32_t addr, void *out, unsigned size);
bool nor_write(uint32_t addr, const void *data, unsigned size);
bool nor_erase(uint32_t sector);
