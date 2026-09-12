# `vendor/ble_sample` 文件/函数/变量审计

分支：`refactor/bms-template-symbol-cleanup`

目标不是单纯追求文件数量最少，而是：

1. 删除当前产品没有运行路径的 sample / test / debug 代码；
2. 缩小公共头文件和全局变量暴露面；
3. 合并没有独立职责的小文件；
4. 保留协议 ABI、Flash 布局、保护/MOS/低功耗行为；
5. 逐步解除 DVC1124 BMS 对历史 SH367309 ABI 的依赖。

> **ABI 约束**：`stCell_Info`、`SYSTEM_ERROR`、`PRT_E2ROM_PARAS` 当前仍被 Modbus 通过字段/偏移映射。即使某个字段看起来没有 C 代码读取，也不能按普通 unused variable 删除或重排。

## 1. 文件级结论

| 文件 | 结论 | 当前处理 |
|---|---|---|
| `SocEnhance.c` | KEEP / NEXT CLEAN | SOC 核心算法，不能按代码量删除；下一阶段仅清死分支/乱码/硬编码依赖，不改算法 |
| `SocEnhance.h` | CLEANED | 清理乱码和 API，结构布局暂时保留 |
| `app.c` | CLEANED / LEGACY-DEBT | 已删除 Telink sample UI、两套 `#if 0` PM、SIF test timer、调试探针；仍待解除 SH367309 compatibility |
| `app.h` | CLEANED | 只保留应用生命周期、BLE OTA 协作、Flash protection API；不再导出 `conf.h` |
| `app_att.c` | CLEANED | 删除整套未注册 HID UUID/report map/buffer/attribute 草稿；保留 GAP/GATT/DeviceInfo/Battery/SPP/OTA handle 顺序 |
| `app_att.h` | CLEANED | 删除几百行已注释 HID handle，只保留真实 handle 表 |
| `app_config.h` | CLEANED | 删除 keyboard map/deep-save sample 配置；保留 Telink SDK 必要 feature 配置 |
| `app_ui.c` | REMOVE | 已删除 |
| `app_ui.h` | REMOVE | 兼容 shim 已删除 |
| `ble_ota.c/.h` | KEEP | OTA 独立职责明确；删除无消费者的 `latest_user_event_tick` 依赖 |
| `bms_cold_kv_store.c/.h` | KEEP / CLEANED | 删除无实现入口的 `bms_cold_system_param_id_t`；KV key/布局不变 |
| `bms_error.h` | KEEP | 承接系统错误布局；新增通用 store-error helper；后续迁移 `System_ERROR_UserCallback` |
| `bms_event_log.c/.h` | KEEP / CLEANED | 删除未使用 debug public type；日志布局不动 |
| `bms_state.h` | KEEP / ABI-LOCKED | BMS 统一运行状态入口；保持 legacy 字段布局，禁止无迁移重排 |
| `btname_modbus.c/.h` | KEEP / CLEANED | 删除 3 个本地 libc wrapper；不再依赖 SH367309 头 |
| `bus_mux.c/.h` | CLEANED | 删除 3 个空函数、1 个单行 wrapper、整块死实现/故障注入草稿 |
| `conf.c` | REMOVE | 已删除；只有一个 `sys_time` 定义，已归 `runtime.c` |
| `conf.h` | CLEANED / TARGET-REMOVE | 历史 14 产品矩阵已收成当前 D008 真值；运行状态只剩正式字段；DVC compatibility alias 仍是迁移债务 |
| `dvc1124.c/.h` | KEEP | 器件级驱动，寄存器/I2C/换算的核心 |
| `dvc1124_bms.c` | KEEP / NEXT CLEAN | DVC→BMS 适配；仍依赖 legacy fault/SystemStatus，应逐步换成 BMS core API |
| `dvc1124_commands.c` | REMOVE | 已并入 `dvc1124_special.c` |
| `dvc1124_core_ot.c` | REMOVE | 已并入 `dvc1124_special.c` |
| `dvc1124_commands.h` | MERGE-PENDING | 仅 2 个 special command API，最终应并入 `dvc1124.h` 后删除 |
| `dvc1124_special.c` | KEEP | 集中 W0C / RC / self-clearing 特殊寄存器操作 |
| `dvc1124_config_service.c/.h` | KEEP | 通信无关的语义配置服务；不能与 transport 混合 |
| `dvc1124_config_store.c/.h` | KEEP / CLEANED | AFE 配置持久化；`CaptureAndSave` 不再作为 public API |
| `dvc1124_project_config.h` | KEEP | 板型/AFE 默认配置单一真源 |
| `dvc1124_reg.h` | KEEP | 寄存器事实层，必须与业务分离 |
| `flash_kv32.c/.h` | KEEP | 通用掉电安全 KV，不与业务 store 合并 |
| `flash_store_cfg.h` | KEEP | Flash 分区真源；历史地址常量还是“已出货区域不得覆盖”的兼容性哨兵 |
| `flash_store_safe.h` | KEEP | Flash protection session 安全封装 |
| `main.c` | CLEANED | 只保留启动/IRQ dispatch/main loop；已去掉 SH367309 直接依赖 |
| `modbus_rtu.c/.h` | KEEP / ABI-LOCKED / NEXT CLEAN | 当前 UART+BLE 共用协议入口是正确方向；寄存器映射仍过大，后续拆 register-map，不改地址 |
| `modbus_uart.c/.h` | CLEANED | DMA struct 收回实现；删未用宏、2 个 debug IRQ counter、实验代码 |
| `param.c/.h` | CLEANED / ABI-LOCKED | 删除 EEPROM/裸 Flash 双路径、`PARAM_ADDR`、未生效 `filter1/filter2`；字段顺序/默认值/epoch 不变 |
| `runtime.c/.h` | KEEP / CLEANED | `sys_time` 已归属这里；移除无用 `g_stCellInfoReport` 依赖；Flash record 格式不动 |
| `sci_upper.h` | REMOVE-PENDING | 现在只是 `bms_state.h` 兼容 shim；待旧 include 全迁移后删除 |
| `sh367309_datadeal.c/.h` | LEGACY-DEBT / NEXT MAJOR | 当前仍提供 SystemStatus、fault/event、温度查表等公共能力；先提取公共逻辑，再删除 DVC 构建中的 309 设备逻辑 |
| `sif_send.c/.h` | CLEANED | 删除双层 `#if 1`、数百行 `#if 0` 草稿；组帧函数私有；500us Timer0 归属 SIF |
| `soc_kv_store.c/.h` | KEEP / CLEANED | 删除未使用 debug type、旧 default alias；key/Flash 格式不变 |

## 2. 已删除/收口的函数与变量

### Telink sample / BLE

- 删除 `app_ui.c` 整套 keyboard/button/HID sample。
- 删除 HID report map、HID UUID、protocol mode、keyboard/consumer report buffer 等实际未注册的静态数据。
- 删除 `latest_user_event_tick`、`advertise_begin_tick`、`sendTerminate_before_enterDeep` 的死路径。
- 删除 host callback 中没有配置 event-mask 的 TK/encryption/confirm 空分支。
- 删除 `app_timer_test_init()`、`app_timer_test_irq_proc()` 和无意义 `timer0_irq_cnt`；替换为 `sif_timer_init()` / `sif_timer_irq_proc()`。

### Bus / UART

- 删除 `owc_stop_tx()`、`modbus_uart_slave_enable()`、`modbus_uart_slave_disable()` 三个空函数。
- 删除 `owc_start_tx_only()` 单行 wrapper。
- 删除 `uart_dmairq_tx_cnt2`、`uart_dmairq_rx_cnt2` 两个只自增、不参与任何行为的 debug counter。
- `mb_dma_pkt_raw_t` 和 DMA buffer geometry 不再暴露在公共头。

### 参数 / 存储

- 删除 `PARAM_SAVE_TO_EEPROM`、`PARAM_SAVE_TO_FLASH`、`PARAM_ADDR` 假双存储接口；当前真实路径只有 cold KV。
- 删除保护默认值中从未进入 `PRT_E2ROM_PARAS` 的 `*_filter1` / `*_filter2`。
- 删除未使用 `soc_kv_dbg_t`。
- 删除仅兼容旧名字的 `SOC_KV_DEFAULT_*`。
- 删除没有对应 getter/setter、实现从未使用的 `bms_cold_system_param_id_t`。
- 删除未使用 `bms_event_log_dbg_t`。

### 配置 / 运行状态

- 删除 `conf.c`。
- 删除 C21/C31/D11/D31/C700/M1PRO/M23/M32/T3MAX/T3/M25/T1-T2/C11 等当前分支永远不会生效的产品条件矩阵。
- `Time_T` 正式运行字段只保留 `low_power_mode`；CHG/DSG/debug 仅在测试宏打开时存在。
- 删除 `DEV_NAME_STR2`、`_Cur`、`UPDNLMT16` 等无调用定义。

### SIF

- 删除重复 include、重复 public-packet 实现、所有 `#if 0` 协议草稿和注释测试注入。
- public/private/cell packet builder 全部改为文件私有。
- 三类 packet 工作 buffer 合为 union，避免同时占用不需要的 RAM。

## 3. 当前不能按“unused”直接删除的字段

以下数据结构由协议/持久化按 offset 使用，必须先迁移协议，再谈字段删除：

- `struct stCell_Info`
- `struct SYSTEM_ERROR`
- `struct PRT_E2ROM_PARAS`
- `SOC_CALCULATE_ELEMENT` 中被 Modbus/显示数据引用的字段

判断标准不是“IDE Find References 为 0”，而是：

1. 是否被指针偏移/`memcpy`/寄存器窗口映射；
2. 是否属于已出货 Flash layout；
3. 是否改变 BLE handle；
4. 是否改变 OTA/上位机协议地址。

## 4. 下一批必须继续处理

### A. `sh367309_datadeal.*`

优先抽出：

- `SYSTEM_ERROR_COMMAND/System_ERROR_UserCallback`
- `SystemStatus`
- `FaultFlag/FaultWarnRecord2`
- NTC 查表 / `GetEndValue`

完成后，DVC1124 分支应能停止编译 SH367309 的寄存器/I2C/MTP 设备逻辑。

### B. `SocEnhance.c`

只做行为保持型清理：

- 删除 `#if 1/#else` 旧算法分支；
- 清掉乱码注释和失效 `#define`；
- 隐藏非协议需要的全局状态；
- 把 `g_stCellInfoReport` 读取集中到输入 snapshot，避免算法到处访问全局。

SOC 算法本身本轮不重新设计。

### C. `modbus_rtu.c/.h`

保持所有地址不变，拆出 register-map 表和 product-info map；UART/BLE 继续共用 `modbus_on_frame()`。

### D. Build source exclusion

当前 `bms_tools/bms.py` 要求 `SOURCE_GROUPS` 发现的每个 SDK source 都出现在 `source_order.txt`。因此 keyboard/audio/USB sample 虽然应用已不调用，目前不能只从列表删除，否则 `sources --check` 会失败。

后续应给构建器增加显式、版本受控的 `SOURCE_EXCLUDES`，再逐项通过 TC32 link 证明可删除：

- `application/keyboard/*`
- `application/audio/*`
- `application/app/usb*`
- 以及未被 BLE stack/产品调用的 B85 peripheral driver。

SDK 原始文件建议仍保留在仓库，仅从 BMS build set 排除，便于和 Telink 官方基线对照。

## 5. 合并前验证

Windows + 锁定 TC32 工具链必须执行：

```powershell
python tests/dvc1124_config_quick_check.py
python tests/flash_quick_check.py
python bms_tools/bms.py sources --check
python bms_tools/bms.py ci --jobs 4
```

如果有已验证量产/当前基线 bin，再执行：

```powershell
python bms_tools/bms.py baseline <reference.bin>
```

本审计分支在上述验证通过前不得直接合并到量产分支。
