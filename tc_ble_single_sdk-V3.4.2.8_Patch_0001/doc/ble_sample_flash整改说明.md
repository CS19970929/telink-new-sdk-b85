# ble_sample Flash 存储整改说明

## 1. 背景

`vendor/ble_sample` 原有 Flash 持久化实现存在以下高风险问题：

- `bms_event_log` 与 `bms_cold_kv_store` 地址重叠。
- `B85 + 512K flash` 场景下，`event_log` 会覆盖 SDK 保留区。
- `runtime` 采用“单 sector + 每分钟整区擦写”方案，存在寿命和掉电一致性问题。
- 多条 Flash 写路径未做写后校验，且在 `APP_FLASH_PROTECTION_ENABLE=1` 时没有统一处理 unlock/relock。
- Modbus 修改保护参数后没有统一落盘，重启后会回退。
- 低压 Flash 保护开关被关闭。

本次整改目标是先解决确定性 bug，再把持久化路径整理到可维护状态。

## 2. 本次修改内容

### 2.1 动态 Flash 布局

新增基于 `blc_flash_capacity` 的运行时布局选择，接口位于：

- `tc_ble_single_sdk/vendor/ble_sample/flash_store_cfg.h`

设计原则：

- 保留旧地址宏，仅用于兼容和迁移。
- 新代码通过 getter 获取实际地址。
- `512K`、`1M`、`2M` 使用不同布局。
- `512K + OTA boot=0x40000` 直接判定为不支持，fail-closed，不继续硬写。

### 2.2 统一安全写封装

新增：

- `tc_ble_single_sdk/vendor/ble_sample/flash_store_safe.h`

提供统一能力：

- Flash 写前自动 `unlock`
- 写后自动 `restore lock`
- `erase_sector` 后整 sector 校验
- `write_page` 后读回校验

这层用于替换多个模块中“无条件返回成功”的写口实现。

### 2.3 runtime 重构

修改文件：

- `tc_ble_single_sdk/vendor/ble_sample/runtime.c`

改动点：

- 改为双 sector append log
- 每条记录包含 `seq/runtime_min/flag/crc/commit`
- 写入频率从“每分钟一次”降低为“每 10 分钟一次 + 状态切换时立即保存”
- 保留旧 `runtime_store_t` 兼容读取
- `512K` 下即使新旧地址相同，也支持旧格式迁移
- 写失败时上报 `ERROR_EEPROM_STORE`

预期效果：

- 7 天窗口擦除次数从约 `10080` 次降到个位数级 sector erase
- 掉电时最多丢失一个保存周期，不再把整个计时窗口清零

### 2.4 hot/cold KV 写路径修复

修改文件：

- `tc_ble_single_sdk/vendor/ble_sample/soc_kv_store.c`
- `tc_ble_single_sdk/vendor/ble_sample/bms_cold_kv_store.c`

改动点：

- `prog/erase` 全部接入 `flash_store_safe.h`
- 地址来源改为动态布局 getter
- `soc_kv_store` 增加旧地址迁移逻辑
- `soc_kv_store_update_and_log_if_changed()` 增加 5 秒节流
- `cycle` 变化仍立即落盘

### 2.5 event log 地址与写路径修复

修改文件：

- `tc_ble_single_sdk/vendor/ble_sample/bms_event_log.c`

改动点：

- `flash_base` / `flash_sectors` 改为运行时配置
- 擦写走统一安全封装
- 保持日志格式和协议读取逻辑不变

### 2.6 btname 持久化迁移

修改文件：

- `tc_ble_single_sdk/vendor/ble_sample/btname_modbus.c`

改动点：

- 地址来源改为动态布局
- 先读新地址，失败再尝试旧地址
- 写入改为安全擦写 + 校验

### 2.7 Modbus 参数持久化修复

修改文件：

- `tc_ble_single_sdk/vendor/ble_sample/modbus_rtu.c`
- `tc_ble_single_sdk/vendor/ble_sample/param.c`

改动点：

- `0x06` 单寄存器写保护参数后立即 `SaveParam()`
- `0x10` 多寄存器事务写完后统一 `SaveParam()`
- `SaveParam()` 失败时上报 `ERROR_EEPROM_STORE`

### 2.8 低压保护开关恢复

修改文件：

- `tc_ble_single_sdk/vendor/ble_sample/app_config.h`

改动点：

- `APP_BATT_CHECK_ENABLE` 从 `0` 改为 `1`

## 3. 新布局

### 3.1 512K

| 区域 | 地址 |
| --- | --- |
| `event_log` | `0x40000 ~ 0x47FFF` |
| `btname` | `0x50000` |
| `runtime` | `0x51000 ~ 0x52FFF` |
| `soc_kv` | `0x70000 ~ 0x71FFF` |
| `cold_kv` | `0x72000 ~ 0x73FFF` |

说明：

- 避开 `0x74000 ~ 0x7FFFF` 的 SDK 保留区。
- 仅支持 OTA boot address 为 `0x20000` 的当前设计。

### 3.2 1M

| 区域 | 地址 |
| --- | --- |
| `btname` | `0xC0000` |
| `runtime` | `0xC1000 ~ 0xC2FFF` |
| `soc_kv` | `0xC3000 ~ 0xC4FFF` |
| `cold_kv` | `0xC5000 ~ 0xC6FFF` |
| `event_log` | `0xC7000 ~ 0xCEFFF` |

### 3.3 2M

| 区域 | 地址 |
| --- | --- |
| `btname` | `0x1C0000` |
| `runtime` | `0x1C1000 ~ 0x1C2FFF` |
| `soc_kv` | `0x1C3000 ~ 0x1C4FFF` |
| `cold_kv` | `0x1C5000 ~ 0x1C6FFF` |
| `event_log` | `0x1C7000 ~ 0x1CEFFF` |

## 4. 兼容与迁移策略

当前已处理：

- `runtime` 旧格式迁移
- `btname` 旧地址回读
- `soc_kv` 旧地址迁移
- `param` 旧 `PARAM_T` 兼容迁移逻辑保留

当前未做：

- `event_log` 历史日志搬迁

原因：

- 日志不是系统关键配置，优先级低于参数和运行态计数。
- 保持本次改动收敛，先修“不会再写坏”和“关键数据不丢”。

## 5. 风险边界

### 5.1 明确不支持场景

- `512K flash + OTA boot=0x40000`

原因：

- 顶部可用空间不足以同时容纳当前这组业务持久化区。
- 继续沿用旧设计会直接引入固件区/保留区覆盖风险。

当前策略：

- 布局 getter 返回不可用
- 相关模块初始化失败后保持 fail-closed

### 5.2 本机未完成的构建验证

当前环境缺少 `tc32-elf-gcc`，无法做真实目标链编译。

已完成：

- 代码级 diff 检查
- 关键文件静态扫查
- 使用宿主 `clang` 做了有限的语法级尝试

未完成：

- 使用 TC32 工具链的完整编译链接
- 板级运行验证

## 6. 建议测试项

### 6.1 地址与兼容

- `512K` 板子启动后确认 `MAC/calibration/master_pairing` 不再变化
- `1M` 板子启动后确认新布局可正常初始化
- 从旧版本升级后确认 `btname/runtime/param/soc_kv` 能迁移

### 6.2 runtime

- 连续运行 7 天窗口，确认 `factory -> normal` 切换正常
- 在保存前、保存中、保存后掉电，确认重启后不会回到 `0`

### 6.3 Modbus

- `0x06` 写单个保护寄存器后重启，确认值保留
- `0x10` 写一批保护寄存器后重启，确认值保留

### 6.4 event log

- 连续写入超过 `150` 次事件，确认不会擦到参数区和保留区

### 6.5 低压

- 在接近低压门限条件下触发一次 Flash 写，确认系统进入低压保护流程，而不是静默写坏

## 7. 后续建议

如果下一轮继续做增强，建议按这个顺序：

1. 给 `event_log` 增加旧日志搬迁或导出工具
2. 给 `SaveParam()` 和 KV 写入增加返回码上传到上层协议
3. 把布局 getter 独立成 `flash_layout.c/h`，从 header inline 升级为正式模块
4. 基于真实 `tc32-elf-gcc` 工具链补一轮编译和板测脚本
