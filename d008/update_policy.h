#pragma once

/* 已发布编号绑定固定值。保留需要跨版本直达的旧任务。普通固件留空。 */
// #define D008_CONFIG_UPDATES D008_UPDATE(UPDATE_CAPACITY, 1, capacity_dah, uint16_t, 300),
//                            D008_UPDATE(UPDATE_SW_OV, 1, sw[OV].trip, uint16_t, 3650),
#ifndef D008_CONFIG_UPDATES
#define D008_CONFIG_UPDATES
#endif
