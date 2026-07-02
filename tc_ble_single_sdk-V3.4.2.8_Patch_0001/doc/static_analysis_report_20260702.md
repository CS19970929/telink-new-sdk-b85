# BMS cppcheck 静态分析报告

- 分析时间：2026-07-02 15:02:23
- Git 分支：renzheng
- Git 提交：e3913d5
- 工程：tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/project/tlsr_tc32/B85/825x_ble_sample
- 分析范围：tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/*.c
- cppcheck 版本：Cppcheck 2.21.0
- cppcheck 返回码：0
- 原始 XML：tc_ble_single_sdk-V3.4.2.8_Patch_0001/build/static_analysis/cppcheck_bms_ble_sample.xml

## 结果摘要

| 级别 | 数量 |
| --- | ---: |
| error | 1 |
| warning | 9 |
| style | 83 |
| performance | 0 |
| portability | 0 |
| information | 10 |
| total | 103 |

## SSR-MCU 契约检查

| 检查项 | 结论 | 说明 |
| --- | --- | --- |
| SSR-MCU-001 启动期自检位于 C 初始化前 | PASS | 检查启动汇编中 CPU/PC/RAM/Flash/Clock 自检顺序，并确认其早于 .bss/.data 初始化。 |
| SSR-MCU-001 启动期自检宏在 B85 工程启用 | PASS | 检查 825x_ble_sample boot/B85/subdir.mk 的汇编宏配置。 |
| SSR-MCU-002 主循环每轮调用运行期自检 | PASS | 检查 main 死循环中存在 bms_mcu_selftest_runtime_check 调用。 |
| SSR-MCU-002 运行期自检顺序符合需求 | PASS | 检查 CPU/PC/Clock/Flash/RAM/ADC/IRQ 检查顺序。 |
| 失败路径进入 fail-safe 循环 | PASS | 检查失败处理不可返回、关断输出并进入报警循环。 |
| bms_mcu_selftest.c 纳入 B85 ble_sample 构建 | PASS | 检查 825x_ble_sample/vendor/ble_sample/subdir.mk 的源文件和对象文件列表。 |

## 问题明细

| 级别 | 规则 | 文件 | 行号 | 描述 |
| --- | --- | --- | ---: | --- |
| information | normalCheckLevelMaxBranches | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/app.c | 0 | Limiting analysis of branches. Use --check-level=exhaustive to analyze all branches. |
| style | unreadVariable | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/app.c | 637 | Variable 'power_on_delay' is assigned a value that is never used. |
| style | unreadVariable | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/app.c | 638 | Variable 'weichi_delay' is assigned a value that is never used. |
| style | constVariablePointer | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/app_att.c | 427 | Variable 'data' can be declared as pointer to const |
| style | unusedStructMember | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/app_att.c | 37 | struct member 'gap_periConnectParams_t::intervalMin' is never used. |
| style | unusedStructMember | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/app_att.c | 39 | struct member 'gap_periConnectParams_t::intervalMax' is never used. |
| style | unusedStructMember | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/app_att.c | 41 | struct member 'gap_periConnectParams_t::latency' is never used. |
| style | unusedStructMember | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/app_att.c | 43 | struct member 'gap_periConnectParams_t::timeout' is never used. |
| style | unreadVariable | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/app_att.c | 434 | Variable 'r' is assigned a value that is never used. |
| information | normalCheckLevelMaxBranches | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/bms_cold_kv_store.c | 0 | Limiting analysis of branches. Use --check-level=exhaustive to analyze all branches. |
| style | variableScope | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/flash_store_safe.h | 42 | The scope of the variable 'chunk' can be reduced. |
| style | variableScope | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/flash_store_safe.h | 64 | The scope of the variable 'chunk' can be reduced. |
| style | variableScope | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/flash_store_safe.h | 84 | The scope of the variable 'page_off' can be reduced. |
| style | variableScope | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/flash_store_safe.h | 85 | The scope of the variable 'chunk' can be reduced. |
| information | normalCheckLevelMaxBranches | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/bms_event_log.c | 0 | Limiting analysis of branches. Use --check-level=exhaustive to analyze all branches. |
| style | knownConditionTrueFalse | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/bms_event_log.c | 405 | Condition 'flash_sectors<2u' is always false |
| style | knownConditionTrueFalse | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/bms_event_log.c | 406 | Condition 'slots_per_sector==0u' is always false |
| style | knownConditionTrueFalse | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/bms_event_log.c | 407 | Condition '(unsigned short)(slots_per_sector*flash_sectors)==0u' is always false |
| style | variableScope | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/bms_event_log.c | 521 | The scope of the variable 'reg' can be reduced. |
| style | variableScope | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/bms_event_log.c | 522 | The scope of the variable 'value' can be reduced. |
| information | normalCheckLevelMaxBranches | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/btname_modbus.c | 0 | Limiting analysis of branches. Use --check-level=exhaustive to analyze all branches. |
| information | normalCheckLevelMaxBranches | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/flash_kv32.c | 0 | Limiting analysis of branches. Use --check-level=exhaustive to analyze all branches. |
| style | knownConditionTrueFalse | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/flash_kv32.c | 700 | Condition '!kv_scan_active_sector(kv,active_idx,&last_seq)' is always false |
| style | knownConditionTrueFalse | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/flash_kv32.c | 705 | Condition 'kv->dbg.next_seq==0u' is always false |
| style | variableScope | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/flash_kv32.c | 351 | The scope of the variable 'i' can be reduced. |
| style | variableScope | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/flash_kv32.c | 487 | The scope of the variable 'crc' can be reduced. |
| style | variableScope | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/flash_kv32.c | 488 | The scope of the variable 'calc_crc' can be reduced. |
| style | variableScope | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/flash_kv32.c | 489 | The scope of the variable 'stored_crc' can be reduced. |
| style | variableScope | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/flash_kv32.c | 490 | The scope of the variable 'seq' can be reduced. |
| style | variableScope | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/flash_kv32.c | 491 | The scope of the variable 'payload_len' can be reduced. |
| style | variableScope | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/flash_kv32.c | 492 | The scope of the variable 'total_len' can be reduced. |
| style | variableScope | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/flash_kv32.c | 493 | The scope of the variable 'item_count' can be reduced. |
| style | constParameterPointer | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/flash_kv32.c | 577 | Parameter 'kv' can be declared as pointer to const |
| information | normalCheckLevelMaxBranches | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/modbus_rtu.c | 0 | Limiting analysis of branches. Use --check-level=exhaustive to analyze all branches. |
| warning | identicalConditionAfterEarlyExit | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/modbus_rtu.c | 323 | Identical condition 'req_len<4', second condition is always false |
| style | variableScope | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/modbus_rtu.c | 50 | The scope of the variable 'u16SciTemp' can be reduced. |
| style | variableScope | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/modbus_rtu.c | 51 | The scope of the variable 'j' can be reduced. |
| style | variableScope | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/modbus_rtu.c | 52 | The scope of the variable 'k' can be reduced. |
| style | variableScope | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/modbus_rtu.c | 53 | The scope of the variable 'a' can be reduced. |
| style | variableScope | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/modbus_rtu.c | 469 | The scope of the variable 'v' can be reduced. |
| style | unsignedPositive | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/modbus_rtu.c | 56 | Unsigned expression 'reg' can't be negative so it is unnecessary to test it. |
| information | normalCheckLevelMaxBranches | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/runtime.c | 0 | Limiting analysis of branches. Use --check-level=exhaustive to analyze all branches. |
| style | knownConditionTrueFalse | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/runtime.c | 130 | Condition 'flash_store_cfg_get_runtime_sectors()<2u' is always false |
| style | knownConditionTrueFalse | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/runtime.c | 154 | Condition 'g_runtime_next_seq==0u' is always false |
| style | knownConditionTrueFalse | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/runtime.c | 385 | Condition 'flash_store_cfg_get_runtime_sectors()<2u' is always false |
| information | normalCheckLevelMaxBranches | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/sh367309_datadeal.c | 0 | Limiting analysis of branches. Use --check-level=exhaustive to analyze all branches. |
| style | redundantAssignment | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/sh367309_datadeal.c | 942 | Variable 'p' is assigned an expression that holds the same value. |
| style | variableScope | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/sh367309_datadeal.c | 600 | The scope of the variable 'i' can be reduced. |
| style | variableScope | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/sh367309_datadeal.c | 695 | The scope of the variable 'isdiff' can be reduced. |
| style | variableScope | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/sh367309_datadeal.c | 696 | The scope of the variable 'update_ok' can be reduced. |
| style | variableScope | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/sh367309_datadeal.c | 844 | The scope of the variable 'isdiff' can be reduced. |
| style | variableScope | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/sh367309_datadeal.c | 845 | The scope of the variable 'update_ok' can be reduced. |
| style | variableScope | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/sh367309_datadeal.c | 919 | The scope of the variable 't_tmp32a' can be reduced. |
| style | variableScope | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/sh367309_datadeal.c | 919 | The scope of the variable 't_tmp32b' can be reduced. |
| style | variableScope | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/sh367309_datadeal.c | 920 | The scope of the variable 'k' can be reduced. |
| style | variableScope | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/sh367309_datadeal.c | 920 | The scope of the variable 'b' can be reduced. |
| style | variableScope | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/sh367309_datadeal.c | 921 | The scope of the variable 'ret' can be reduced. |
| style | variableScope | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/sh367309_datadeal.c | 1023 | The scope of the variable 'u32temp' can be reduced. |
| style | variableScope | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/sh367309_datadeal.c | 1048 | The scope of the variable 't_i32temp' can be reduced. |
| style | variableScope | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/sh367309_datadeal.c | 1074 | The scope of the variable 't_u16VcellTemp' can be reduced. |
| style | unsignedLessThanZero | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/sh367309_datadeal.c | 1031 | Checking if unsigned expression 'u32temp' is less than zero. |
| style | unsignedLessThanZero | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/sh367309_datadeal.c | 1034 | Checking if unsigned expression 'u32temp' is less than zero. |
| style | unsignedLessThanZero | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/sh367309_datadeal.c | 1037 | Checking if unsigned expression 'u32temp' is less than zero. |
| warning | constStatement | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/sh367309_datadeal.c | 706 | Redundant code: Found a statement that begins with string constant. |
| warning | constStatement | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/sh367309_datadeal.c | 707 | Found suspicious operator ',', result is not used. |
| warning | constStatement | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/sh367309_datadeal.c | 723 | Redundant code: Found a statement that begins with string constant. |
| warning | constStatement | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/sh367309_datadeal.c | 759 | Redundant code: Found a statement that begins with string constant. |
| warning | constStatement | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/sh367309_datadeal.c | 854 | Redundant code: Found a statement that begins with string constant. |
| warning | constStatement | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/sh367309_datadeal.c | 855 | Found suspicious operator ',', result is not used. |
| warning | constStatement | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/sh367309_datadeal.c | 871 | Redundant code: Found a statement that begins with string constant. |
| warning | constStatement | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/sh367309_datadeal.c | 906 | Redundant code: Found a statement that begins with string constant. |
| style | constParameterPointer | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/sh367309_datadeal.c | 382 | Parameter 'WrBuf' can be declared as pointer to const |
| style | constParameterPointer | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/sh367309_datadeal.c | 405 | Parameter 'WrBuf' can be declared as pointer to const |
| style | constVariablePointer | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/sh367309_datadeal.c | 602 | Variable 'P' can be declared as pointer to const |
| style | constVariablePointer | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/sh367309_datadeal.c | 704 | Variable 'P' can be declared as pointer to const |
| style | constVariablePointer | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/sh367309_datadeal.c | 852 | Variable 'P' can be declared as pointer to const |
| style | unreadVariable | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/sh367309_datadeal.c | 600 | Variable 'i' is assigned a value that is never used. |
| style | unreadVariable | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/sh367309_datadeal.c | 695 | Variable 'isdiff' is assigned a value that is never used. |
| style | unreadVariable | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/sh367309_datadeal.c | 844 | Variable 'isdiff' is assigned a value that is never used. |
| style | unreadVariable | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/sh367309_datadeal.c | 1022 | Variable 'result' is assigned a value that is never used. |
| style | unreadVariable | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/sh367309_datadeal.c | 1023 | Variable 'u32temp' is assigned a value that is never used. |
| style | unreadVariable | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/sh367309_datadeal.c | 1435 | Variable 'su8_IdischgOcp2_Flag' is assigned a value that is never used. |
| information | normalCheckLevelMaxBranches | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/sif_send.c | 0 | Limiting analysis of branches. Use --check-level=exhaustive to analyze all branches. |
| error | arrayIndexOutOfBounds | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/sif_send.c | 919 | Array 'sif_report.private.vcell.arrVoltage[5]' accessed at index 9, which is out of bounds. |
| style | constParameterPointer | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/sif_send.c | 156 | Parameter 'data' can be declared as pointer to const |
| style | constVariablePointer | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/sif_send.c | 245 | Variable 'p' can be declared as pointer to const |
| style | unreadVariable | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/sif_send.c | 208 | Variable 'i' is assigned a value that is never used. |
| style | unusedVariable | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/sif_send.c | 241 | Unused variable: res |
| style | unreadVariable | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/sif_send.c | 243 | Variable 'nums' is assigned a value that is never used. |
| style | unreadVariable | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/sif_send.c | 244 | Variable 'is60s' is assigned a value that is never used. |
| style | unreadVariable | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/sif_send.c | 558 | Variable 'temp' is assigned a value that is never used. |
| style | unusedVariable | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/sif_send.c | 717 | Unused variable: send_status |
| style | knownConditionTrueFalse | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/soc_kv_store.c | 71 | Condition 'item==SOC_ITEM_SOH' is always false |
| style | knownConditionTrueFalse | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/soc_kv_store.c | 94 | Condition 'flash_store_cfg_get_soc_kv_sectors()<2u' is always false |
| style | knownConditionTrueFalse | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/soc_kv_store.c | 71 | Same expression on both sides of '\|\|' because 'item==SOC_ITEM_DSG' and 'item==SOC_ITEM_SOH' represent the same value. |
| information | normalCheckLevelMaxBranches | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/SocEnhance.c | 0 | Limiting analysis of branches. Use --check-level=exhaustive to analyze all branches. |
| style | knownConditionTrueFalse | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/SocEnhance.c | 399 | Condition 'factory_a10==0u' is always false |
| style | variableScope | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/SocEnhance.c | 708 | The scope of the variable 'lo' can be reduced. |
| style | variableScope | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/SocEnhance.c | 709 | The scope of the variable 'hi' can be reduced. |
| style | variableScope | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/SocEnhance.c | 710 | The scope of the variable 'numerator' can be reduced. |
| style | variableScope | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/SocEnhance.c | 879 | The scope of the variable 'hold_ticks' can be reduced. |
| style | variableScope | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/SocEnhance.c | 880 | The scope of the variable 'rebound_stable' can be reduced. |
| style | constParameterPointer | tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/SocEnhance.c | 1529 | Parameter '_soc' can be declared as pointer to const |

## 命令记录

```powershell
"C:\Program Files\Cppcheck\cppcheck.exe" --xml --xml-version=2 --enable=warning,style,performance,portability,information --std=c99 --language=c --platform=unix32 --max-configs=1 --check-level=normal --quiet --inline-suppr -I "D:\telink\tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)\tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)\tc_ble_single_sdk-V3.4.2.8_Patch_0001\tc_ble_single_sdk\project\tlsr_tc32\B85" -I "D:\telink\tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)\tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)\tc_ble_single_sdk-V3.4.2.8_Patch_0001\tc_ble_single_sdk" -I "D:\telink\tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)\tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)\tc_ble_single_sdk-V3.4.2.8_Patch_0001\tc_ble_single_sdk\vendor\common" -I "D:\telink\tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)\tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)\tc_ble_single_sdk-V3.4.2.8_Patch_0001\tc_ble_single_sdk\common" -I "D:\telink\tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)\tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)\tc_ble_single_sdk-V3.4.2.8_Patch_0001\tc_ble_single_sdk\drivers\B85" -D__PROJECT_8258_BLE_SAMPLE__=1 -DCHIP_TYPE=CHIP_TYPE_825x "D:\telink\tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)\tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)\tc_ble_single_sdk-V3.4.2.8_Patch_0001\tc_ble_single_sdk\vendor\ble_sample\app.c" "D:\telink\tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)\tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)\tc_ble_single_sdk-V3.4.2.8_Patch_0001\tc_ble_single_sdk\vendor\ble_sample\app_att.c" "D:\telink\tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)\tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)\tc_ble_single_sdk-V3.4.2.8_Patch_0001\tc_ble_single_sdk\vendor\ble_sample\app_ui.c" "D:\telink\tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)\tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)\tc_ble_single_sdk-V3.4.2.8_Patch_0001\tc_ble_single_sdk\vendor\ble_sample\bms_cold_kv_store.c" "D:\telink\tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)\tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)\tc_ble_single_sdk-V3.4.2.8_Patch_0001\tc_ble_single_sdk\vendor\ble_sample\bms_event_log.c" "D:\telink\tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)\tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)\tc_ble_single_sdk-V3.4.2.8_Patch_0001\tc_ble_single_sdk\vendor\ble_sample\bms_mcu_selftest.c" "D:\telink\tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)\tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)\tc_ble_single_sdk-V3.4.2.8_Patch_0001\tc_ble_single_sdk\vendor\ble_sample\btname_modbus.c" "D:\telink\tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)\tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)\tc_ble_single_sdk-V3.4.2.8_Patch_0001\tc_ble_single_sdk\vendor\ble_sample\bus_mux.c" "D:\telink\tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)\tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)\tc_ble_single_sdk-V3.4.2.8_Patch_0001\tc_ble_single_sdk\vendor\ble_sample\conf.c" "D:\telink\tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)\tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)\tc_ble_single_sdk-V3.4.2.8_Patch_0001\tc_ble_single_sdk\vendor\ble_sample\flash_kv32.c" "D:\telink\tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)\tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)\tc_ble_single_sdk-V3.4.2.8_Patch_0001\tc_ble_single_sdk\vendor\ble_sample\main.c" "D:\telink\tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)\tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)\tc_ble_single_sdk-V3.4.2.8_Patch_0001\tc_ble_single_sdk\vendor\ble_sample\modbus_rtu.c" "D:\telink\tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)\tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)\tc_ble_single_sdk-V3.4.2.8_Patch_0001\tc_ble_single_sdk\vendor\ble_sample\modbus_uart.c" "D:\telink\tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)\tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)\tc_ble_single_sdk-V3.4.2.8_Patch_0001\tc_ble_single_sdk\vendor\ble_sample\param.c" "D:\telink\tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)\tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)\tc_ble_single_sdk-V3.4.2.8_Patch_0001\tc_ble_single_sdk\vendor\ble_sample\runtime.c" "D:\telink\tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)\tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)\tc_ble_single_sdk-V3.4.2.8_Patch_0001\tc_ble_single_sdk\vendor\ble_sample\sh367309_datadeal.c" "D:\telink\tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)\tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)\tc_ble_single_sdk-V3.4.2.8_Patch_0001\tc_ble_single_sdk\vendor\ble_sample\sif_send.c" "D:\telink\tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)\tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)\tc_ble_single_sdk-V3.4.2.8_Patch_0001\tc_ble_single_sdk\vendor\ble_sample\soc_kv_store.c" "D:\telink\tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)\tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)\tc_ble_single_sdk-V3.4.2.8_Patch_0001\tc_ble_single_sdk\vendor\ble_sample\SocEnhance.c"
```

## 已知限制

- 本报告使用 cppcheck 的通用 C 静态分析能力，不能替代 TC32 交叉编译器告警和目标板运行测试。
- `--platform=unix32` 用于近似 32 位嵌入式目标；若后续获得 TC32 专用 cppcheck 平台配置，应替换为目标专用配置。
- SDK/芯片库中依赖编译器扩展和内存映射寄存器，可能产生保守告警，需要结合代码审查确认。

## 结论

SSR-MCU 契约检查通过；cppcheck 结果见问题明细。
