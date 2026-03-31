# BMS 项目 Flash 模块梳理与重构方案

## 1. 目标

本文档用于梳理当前 BMS 工程内所有与 Flash 相关的模块、实际地址、读写擦除方式，并给出一版适合后续扩展的 Flash 重构方案。

本次约束明确为：

- 目前不方便新增物理 Flash 区域
- 后续要扩展更多运行存储数据
  - 例如 `SOH`
  - 例如更多 runtime 状态
  - 例如事件日志 / 故障日志
- 需要降低当前“多模块各自定义格式、各自擦写”的碎片化问题

---

## 2. 当前 Flash 相关模块总览

### 2.1 SDK 系统保留区

这些区域由 SDK 或 BLE 栈管理，不应直接拿来做应用层数据池。

#### 512K 目标地址（B85/B87）

- `SMP pairing`：`0x74000`
- `MAC`：`0x76000`
- `Calibration`：`0x77000`
- `Master pairing`：`0x78000`

来源：

- `vendor/common/ble_flash.h`
- `vendor/common/ble_flash.c`

说明：

- `flash_sector_mac_address`
- `flash_sector_calibration`
- `flash_sector_smp_storage`
- `flash_sector_master_pairing`

会在启动时根据 Flash 容量自动配置。

### 2.2 应用层自定义区

来自 `vendor/ble_sample/flash_store_cfg.h`。

- `FLASH_ADDR_USER_DATA_START1 = 0x40000`
- `FLASH_ADDR_USER_DATA_END1   = 0x74000`
- `FLASH_ADDR_USER_DATA_START2 = 0x78000`
- `FLASH_ADDR_USER_DATA_END2   = 0x80000`
- `FLASH_ADDR_USER_DATA_BASE1  = 0x40000`
- `FLASH_ADDR_SOFT_PROTECT_BASE = 0x78000`
- `FLASH_ADDR_RUN_KV_BASE = 0x70000`
- `FLASH_ADDR_LOG_BASE = 0x72000`
- `FLASH_ADDR_BLE_NAME_BASE = 0x50000`
- `FLASH_ADR_RUNTIME = 0x51000`

---

## 3. 当前实际在用的 Flash 模块

### 3.1 参数配置 `param.c`

文件：

- `vendor/ble_sample/param.c`
- `vendor/ble_sample/param.h`

地址：

- `PARAM_ADDR = FLASH_ADDR_SOFT_PROTECT_BASE = 0x78000`

读写方式：

- 启动读取整块结构体
- 参数变化时整扇区擦除后整块重写

特点：

- 典型“整块配置 blob”
- 无双备份
- 无版本迁移结构
- 无掉电保护

风险：

- `0x78000` 与 SDK `FLASH_ADR_MASTER_PAIRING_512K` 默认地址重叠
- 当前项目如果未来启用 central custom pairing，会直接冲突

### 3.2 运行分钟数 `runtime.c`

文件：

- `vendor/ble_sample/runtime.c`

地址：

- `FLASH_ADR_RUNTIME = 0x51000`

数据：

- `runtime_min`
- `crc`
- `flag`

读写方式：

- 每次保存都 `erase sector + write struct`
- 每分钟调用一次

特点：

- 高频写
- 单扇区
- 没有 wear leveling

问题：

- 每分钟擦写一次，寿命非常差
- 断电窗口数据一致性弱

### 3.3 蓝牙名 `btname_modbus.c`

文件：

- `vendor/ble_sample/btname_modbus.c`
- `vendor/ble_sample/btname_modbus.h`

地址：

- `BTNAME_SECTOR_ADDR = FLASH_ADDR_BLE_NAME_BASE = 0x50000`

数据：

- `BTNAME_MAGIC`
- `len`
- `checksum`
- `suffix[]`

读写方式：

- 修改名称时整扇区擦除后重写一条记录

特点：

- 低频配置
- 单扇区 blob
- 带简单校验

### 3.4 SOC 运行存储 `soc_kv_store.c`

文件：

- `vendor/ble_sample/soc_kv_store.c`
- `vendor/ble_sample/soc_kv_store.h`

地址：

- `SOC_A = 0x40000`
- `SOC_B = 0x41000`

数据：

- `soc`
- `u8DSG_SOC_Int`
- `cycle`

当前特点：

- 双扇区 append-only
- 已加入 `generation + commit`
- 适合小量运行态 checkpoint

说明：

- 这是当前应用层里相对最接近可复用存储基类的一套实现

### 3.5 BLE 安全配对区

文件：

- `vendor/common/ble_flash.h`
- `vendor/common/ble_flash.c`
- `app.c`

地址：

- 512K 默认 `0x74000`

当前项目状态：

- `BLE_APP_SECURITY_ENABLE = 0`

结论：

- 当前工程运行上大概率未实际使用
- 但该区域仍然是 SDK 预留地址，布局设计不能假设它永远可挪用

### 3.6 MAC / Calibration

文件：

- `vendor/common/ble_flash.c`

地址：

- `MAC = 0x76000`
- `Calibration = 0x77000`

用途：

- MAC 地址生成与持久化
- VDD_F / ADC / RF 校准参数

结论：

- 严禁挪作应用数据存储

### 3.7 OTA 相关

文件：

- `vendor/ble_sample/app.c`
- `stack/ble/service/ota/ota_server.h`

当前状态：

- `BLE_OTA_SERVER_ENABLE = 1`

关键信息：

- OTA 新固件 boot address 默认 `0x20000`
- 当固件大于 `128K` 时，SDK 文档建议改用 `0x40000`

这意味着：

- 当前自定义区从 `0x40000` 开始，只在“OTA 备用固件区不占用 0x40000”时才安全
- 一旦未来固件体积增长，需要切到 `0x40000` OTA bank，自定义运行存储区会直接冲突

这是当前布局里最重要的结构风险。

### 3.8 `flash_blob_store.c`

文件：

- `vendor/ble_sample/flash_blob_store.c`
- `vendor/ble_sample/flash_blob_store.h`

状态：

- 已实现
- 当前未接入业务

能力：

- 双槽位 A/B 存储
- `magic/version/payload_size/seq/crc`
- 自动选最新槽

适合：

- 低频配置类数据
- 单体配置 blob

不适合：

- 高频计数器
- 事件日志

---

## 4. 当前地址规划中的问题

### 4.1 `FLASH_ADDR_LOG_BASE = 0x72000` 这套规划不可直接使用

定义写的是：

- `FLASH_ADDR_LOG_BASE = 0x72000`
- `FLASH_ADDR_LOG_SECTORS = 8`

这会覆盖：

- `0x72000 ~ 0x79FFF`

但 512K/B85 目标下：

- `0x74000` 已被 `SMP pairing` 占用
- `0x76000` 已被 `MAC` 占用
- `0x77000` 已被 `Calibration` 占用
- `0x78000` 已被 `Master pairing` / `param` 占用

结论：

- `FLASH_ADDR_LOG_BASE` 只是一个未校核的宏，不能当可用日志区

### 4.2 `PARAM_ADDR = 0x78000` 存在和 SDK 默认区冲突的风险

当前之所以没有马上炸，核心原因大概率是：

- 项目没启用 custom central pairing
- 该扇区暂时只被应用配置使用

但从布局规范角度看，这个地址不安全。

### 4.3 `0x40000` 自定义区与 OTA 扩展空间存在潜在冲突

当前 `soc_kv_store` 已经使用：

- `0x40000`
- `0x41000`

如果 OTA 未来需要把新固件 bank 放到 `0x40000`，这里必须整体搬迁。

结论：

- 这不是当前运行必现 bug
- 但它会直接限制后续固件扩展与 OTA 升级策略

### 4.4 多种存储风格并存

当前工程里并存至少 4 种风格：

- 单扇区整块擦写：`param.c`
- 单扇区小 blob：`btname_modbus.c`
- 高频整扇区擦写：`runtime.c`
- 双扇区日志：`soc_kv_store.c`

结果是：

- 难统一维护
- 难做容量规划
- 难做掉电一致性策略统一

---

## 5. 建议的重构方向

核心原则：

1. 先统一布局，再统一存储框架
2. 配置类、运行态类、日志类必须分层
3. 高频数据不能继续用“整扇区擦写”
4. 不再允许业务模块自己直接定义裸地址

### 5.1 新的分层

建议分为三层。

#### 第一层：布局层 `flash_layout.h`

唯一职责：

- 定义全项目唯一可信的 Flash 地址图
- 按芯片容量、OTA策略、功能开关输出最终地址

要求：

- 所有业务模块禁止再直接写裸地址
- 所有地址都从这里取

建议导出：

- `FLASH_REGION_CFG_A`
- `FLASH_REGION_CFG_B`
- `FLASH_REGION_STATE_A`
- `FLASH_REGION_STATE_B`
- `FLASH_REGION_LOG_BASE`
- `FLASH_REGION_LOG_SECTORS`

#### 第二层：存储基类层

建议保留三种基类。

1. `flash_blob_store`
   适合低频配置类 blob

2. `flash_state_log_store`
   适合运行态 checkpoint / 小 KV / 计数器
   本质上可以演化自现在的 `soc_kv_store`

3. `flash_event_log_store`
   适合事件日志、故障日志、告警日志
   应该是环形日志结构

#### 第三层：业务对象层

建议拆成：

1. `config_store`
   包含：
   - 保护参数 `g_tParam`
   - 蓝牙名称
   - 其他低频设置项

2. `runtime_store`
   包含：
   - `soc`
   - `dsg_int`
   - `cycle`
   - `soh`
   - `runtime_min`
   - mode/factory flag
   - 未来的若干运行态标志

3. `event_log_store`
   包含：
   - 故障事件
   - 保护动作事件
   - 恢复事件
   - 上下电事件

---

## 6. 在“不新增物理区域”前提下的推荐方案

这里给出一版可实施方案，但先声明前提。

### 6.1 前提条件

必须先确认一件事：

- 当前 OTA 是否会占用 `0x40000` 作为新固件区

如果答案是“未来可能占用”，那么 `0x40000~0x73FFF` 这整块都不能作为长期稳定数据区。

如果答案是“当前产品固件规模可控，长期只使用 `0x20000` OTA bank”，那么 `0x40000~0x73FFF` 可以继续作为应用数据池。

### 6.2 若 `0x40000~0x73FFF` 可继续使用

建议布局如下：

#### 配置区

- `0x50000`：`config_blob_A`
- `0x52000`：`config_blob_B`

说明：

- 故意跳过 `0x51000`
- 因为现阶段 `runtime.c` 仍占用 `0x51000`
- 这样可以先完成 `param + btname` 迁移，不和旧运行时间存储冲突

把以下内容统一进一个配置 blob：

- `g_tParam`
- 蓝牙名称 suffix
- 其他低频配置项

这样可以直接替代：

- `param.c`
- `btname_modbus.c`
- `runtime.c` 里若有低频模式参数

#### 运行态区

- `0x40000 ~ 0x43FFF`：4 个 sector

用于：

- `soc`
- `dsg_int`
- `cycle`
- `soh`
- `runtime_min`
- 未来运行态 checkpoint

建议采用：

- append-only state record
- 周期 checkpoint
- 扇区满时 rollover

#### 事件日志区

- `0x44000 ~ 0x4FFFF`

用作环形日志区。

日志记录建议字段：

- `timestamp / runtime_min`
- `event_id`
- `level`
- `arg0~argN`
- `crc`

### 6.3 若未来 OTA 可能占用 `0x40000`

那么建议反过来：

1. 先把所有“必须长期保留”的数据压缩到高地址安全区
2. 放弃把 `0x40000` 作为正式长期数据区

但在 512K/B85 下，高地址又被 SDK 系统区大量占用，因此可用空间会明显变小。

这时更合理的路径是：

- 重新确认 OTA 策略
- 重新裁剪日志容量
- 必要时关闭不使用的 SDK reserve 功能

否则“既要 OTA 可扩展、又要大量本地日志、又不增区域”三者很难同时满足。

---

## 7. 推荐实施顺序

### 阶段 1：整理地址事实源

1. 新建 `flash_layout.h`
2. 把当前所有应用层地址收口
3. 明确“哪些区可长期使用，哪些区只是暂时碰巧没冲突”

### 阶段 2：统一低频配置存储

目标：

- 用 `flash_blob_store` 接管 `param + btname`

收益：

- 把两套单扇区擦写逻辑合并
- 配置类掉电一致性统一

当前代码状态：

- 已新增 `config_store`
- 已把 `param.c`
- 已把 `btname_modbus.c`
接入统一双槽配置区

### 阶段 3：统一运行态存储

目标：

- 用统一 `runtime_state_store` 接管
  - `soc_kv_store`
  - `runtime.c`
  - 后续 `soh`

收益：

- 高频状态统一做 append-only / checkpoint
- 避免继续出现“每分钟整扇区擦除”

当前代码状态：

- 已新增 `runtime_state_store`
- `runtime.c` 已改为写 `0x42000/0x43000` 双槽状态区
- 旧 `0x51000` 只保留为一次性迁移来源
- `soc_kv_store.c` 暂时仍保留 append-only 双扇区实现
- `soc_kv_store.c` 会低频同步 `soc/dsg/cycle` checkpoint 到 `runtime_state_store`
- 当 `soc_kv_store` 双扇区都失效时，可从 `runtime_state_store` 快照恢复最近状态

说明：

- 这一阶段只先切走 `runtime.c`
- 没有把 `soc_kv_store.c` 强行改成高频写双槽 blob
- 否则会把 `soc` 频繁变化退化成“每次变化都擦 sector”，寿命更差
- 当前 `runtime_state_store` 对 SOC 的角色是“低频备份层”，不是主日志层

### 阶段 4：新增事件日志

目标：

- 引入 ring log

要求：

- 支持有限容量覆盖
- 支持掉电恢复
- 支持后续 Modbus/BLE 导出

---

## 8. 结论

当前项目的 Flash 现状不是“空间不够”，而是“地址规划不统一、存储风格不统一、未来 OTA 扩展边界不清晰”。

结论分三点：

1. 目前实际在用的应用层 Flash 模块主要有：
   - `param.c`
   - `runtime.c`
   - `btname_modbus.c`
   - `soc_kv_store.c`

2. 当前地址规划里至少有两个高风险点：
   - `0x78000` 与 SDK `master pairing` 默认地址冲突
   - `0x40000` 与 OTA 扩展 bank 存在潜在冲突

3. 最合适的重构方向不是继续新增一个个模块，而是建立：
   - 一个统一布局层
   - 一个统一配置 blob 存储层
   - 一个统一运行态日志层
   - 一个事件环形日志层

如果你后续要真正落地，我建议下一步直接做两件事：

1. 我先帮你产出 `flash_layout.h + flash_storage_types.h` 的统一设计
2. 然后优先把 `param.c + btname_modbus.c + runtime.c` 合并到一个新框架里

这样重构收益最大，且对现有业务影响最小。

---

## 9. 当前固件体量确认

从当前工程产物 `825x_ble_sample.lst` 可见：

- `_bin_size_ = 0x121EC`

折算约为：

- `74220 bytes`
- 约 `72.5 KB`

这说明在当前版本下：

- 固件明显小于 `0x20000 = 128KB`
- 当前 OTA 新固件 bank 仍可继续使用 `0x20000`
- 短期内 `0x40000` 还没有被 OTA 强制占用

结论：

- **短期可实施方案** 可以继续使用 `0x40000 ~ 0x73FFF`
- 但要明确标注为“阶段 A 布局”
- 后续若固件逼近 `110KB~120KB`，应启动“阶段 B 迁移”

---

## 10. 建议的具体地址方案

这里给出两版方案：

- 阶段 A：适用于当前固件体量，允许继续用 `0x40000`
- 阶段 B：适用于未来 OTA 需要切到 `0x40000` 的情况

### 10.1 阶段 A：当前版本推荐布局

前提：

- 继续使用 OTA 新固件区 `0x20000`
- 不把 `0x40000` 让给 OTA

#### 512K Flash 现阶段推荐布局表

| 地址范围 | Sector 数 | 用途 | 建议实现 |
|---|---:|---|---|
| `0x40000 ~ 0x40FFF` | 1 | runtime state A | append-only / checkpoint |
| `0x41000 ~ 0x41FFF` | 1 | runtime state B | append-only / checkpoint |
| `0x42000 ~ 0x42FFF` | 1 | runtime state ext A | 未来扩展状态 |
| `0x43000 ~ 0x43FFF` | 1 | runtime state ext B | 未来扩展状态 |
| `0x44000 ~ 0x44FFF` | 1 | event log 0 | ring log |
| `0x45000 ~ 0x45FFF` | 1 | event log 1 | ring log |
| `0x46000 ~ 0x46FFF` | 1 | event log 2 | ring log |
| `0x47000 ~ 0x47FFF` | 1 | event log 3 | ring log |
| `0x48000 ~ 0x48FFF` | 1 | event log 4 | ring log |
| `0x49000 ~ 0x49FFF` | 1 | event log 5 | ring log |
| `0x4A000 ~ 0x4AFFF` | 1 | event log 6 | ring log |
| `0x4B000 ~ 0x4BFFF` | 1 | event log 7 | ring log |
| `0x4C000 ~ 0x4CFFF` | 1 | 保留 | 后续扩展 |
| `0x4D000 ~ 0x4DFFF` | 1 | 保留 | 后续扩展 |
| `0x4E000 ~ 0x4EFFF` | 1 | 保留 | 后续扩展 |
| `0x4F000 ~ 0x4FFFF` | 1 | 保留 | 后续扩展 |
| `0x50000 ~ 0x50FFF` | 1 | config blob A | 双槽 blob |
| `0x52000 ~ 0x52FFF` | 1 | config blob B | 双槽 blob |
| `0x53000 ~ 0x73FFF` | 33 | 预留 | 后续再分配 |
| `0x74000` | 1 | SMP pairing | SDK 保留 |
| `0x75000` | 1 | 当前空闲 | 暂不使用，保守保留 |
| `0x76000` | 1 | MAC | SDK 保留 |
| `0x77000` | 1 | Calibration | SDK 保留 |
| `0x78000` | 1 | Master pairing/旧 param 冲突区 | 不再给业务新用 |
| `0x79000 ~ 0x7FFFF` | 7 | 高地址尾部 | 先保留 |

### 10.2 阶段 A 下各业务模块迁移建议

#### 配置类统一进 `config blob`

放到：

- `0x50000`
- `0x52000`

建议纳入字段：

- `PARAM_T g_tParam`
- 蓝牙名 suffix
- 配置版本号
- 保留字段

目标：

- 替代 `param.c`
- 替代 `btname_modbus.c`
- 停止继续使用 `0x78000`

#### 运行态统一进 `runtime state store`

放到：

- `0x40000 ~ 0x43FFF`

建议字段：

- `soc`
- `dsg_int`
- `cycle`
- `soh`
- `runtime_min`
- `bms_mode`
- `factory_expired`
- `last_shutdown_reason`
- `fault_summary`
- `reserved`

目标：

- 替代 `runtime.c`
- 继承并扩展 `soc_kv_store`
- 为后续 SOH、运行状态扩展留位置

#### 事件日志独立为 ring log

放到：

- `0x44000 ~ 0x4BFFF`

建议日志记录结构：

- `u32 seq`
- `u32 runtime_min`
- `u16 event_id`
- `u8 level`
- `u8 src`
- `u32 arg0`
- `u32 arg1`
- `u16 crc`

单条建议大小：

- `16B` 或 `24B`

这样：

- 8 个 sector = `32KB`
- 如果单条 `16B`，约可存 `2048` 条
- 对 BMS 故障/恢复/保护事件基本够用

---

## 11. 建议的新模块划分

### 11.1 `flash_layout.h`

职责：

- 全项目唯一地址真相源
- 定义 sector 粒度分配表
- 明确阶段 A / 阶段 B

建议接口：

- `FLASH_REGION_CFG_A`
- `FLASH_REGION_CFG_B`
- `FLASH_REGION_STATE_A`
- `FLASH_REGION_STATE_B`
- `FLASH_REGION_EVENT_LOG_BASE`
- `FLASH_REGION_EVENT_LOG_SECTORS`

### 11.2 `config_store.c`

职责：

- 管理低频配置 blob

内容：

- `PARAM_T`
- 蓝牙名称
- 配置版本迁移

底层复用：

- `flash_blob_store.c`

### 11.3 `runtime_state_store.c`

职责：

- 管理运行态 checkpoint

建议替代：

- `soc_kv_store.c`
- `runtime.c`

特点：

- append-only
- checkpoint
- generation
- 掉电恢复

### 11.4 `event_log_store.c`

职责：

- 管理事件日志

特点：

- 环形写入
- 覆盖最旧记录
- 支持按序导出

---

## 12. 阶段 B：未来固件增长后的迁移策略

触发条件建议：

- 当固件尺寸 > `100KB`
- 或 OTA 策略准备切到 `0x40000`

此时要执行：

1. 停止把 `0x40000 ~ 0x73FFF` 视为长期安全数据区
2. 把运行态和配置收缩到高地址安全区域
3. 缩小事件日志容量
4. 必要时关闭不使用的 SDK 预留功能

结论：

- 阶段 A 适合当前版本快速重构
- 阶段 B 是后续容量压力下的迁移路线

不建议一开始就按阶段 B 做，因为会严重压缩当前可用空间，增加复杂度。

---

## 13. 明确建议

如果你准备正式开始重构，我建议按下面顺序执行。

### 第一步

先做：

- `flash_layout.h`
- `flash_storage_types.h`

把所有地址定义收口，停止业务代码直接引用：

- `FLASH_ADDR_SOFT_PROTECT_BASE`
- `FLASH_ADR_RUNTIME`
- `FLASH_ADDR_BLE_NAME_BASE`
- `FLASH_ADDR_USER_DATA_BASE1`

### 第二步

把：

- `param.c`
- `btname_modbus.c`

合并进 `config_store`

这是收益最大、风险最低的一步。

### 第三步

把：

- `runtime.c`
- `soc_kv_store.c`

合并进 `runtime_state_store`

这是最关键的一步，因为它能消灭当前最糟糕的“每分钟整扇区擦除”问题。

### 第四步

新增：

- `event_log_store.c`

再把故障/保护/恢复日志逐步接入。
