# bms_cold_kv_store 使用说明

## 1. 目的

`bms_cold_kv_store` 是当前项目统一的低频参数持久化层，底层基于 `flash_kv32`。

当前它负责保存四类数据：

- 保护参数 `struct PRT_E2ROM_PARAS`
- 系统参数 `bms_cold_system_params_t`
- 升级重置 epoch 控制值
- 蓝牙名 suffix

`btname` 已经并入 `cold_kv`，不再单独占用独立 sector。

## 2. 当前设计边界

- 适用项目：`8251 / 512K` 当前主分支
- 正式分区：`0x5B000 ~ 0x5EFFF`
- sector 数：`4`
- 底层格式：`flash_kv32` 的 CRC + commit + append-log
- 升级策略：`不迁移`

这意味着旧固件直接刷到当前固件后：

- 旧 `PARAM_ADDR` 不再导入
- 旧 `btname` 单 sector 不再导入
- 旧 `soc_kv/cold_kv/runtime` 历史布局不再导入
- 当前固件会按默认值重建参数

## 3. 当前接口

头文件：

- `tc_ble_single_sdk/vendor/ble_sample/bms_cold_kv_store.h`

主要接口：

```c
int bms_cold_kv_store_init(void);
int bms_cold_kv_store_get_protect(struct PRT_E2ROM_PARAS *protect);
int bms_cold_kv_store_set_protect(const struct PRT_E2ROM_PARAS *protect);
int bms_cold_kv_store_get_system(bms_cold_system_params_t *system);
int bms_cold_kv_store_set_system(const bms_cold_system_params_t *system);
int bms_cold_kv_store_get_system_value(bms_cold_system_param_id_t item, u32 *value);
int bms_cold_kv_store_set_system_value(bms_cold_system_param_id_t item, u32 value);
int bms_cold_kv_store_get_control_value(bms_cold_control_param_id_t item, u32 *value);
int bms_cold_kv_store_set_control_value(bms_cold_control_param_id_t item, u32 value);
int bms_cold_kv_store_get_bt_name_suffix(char *suffix, u16 suffix_size);
int bms_cold_kv_store_set_bt_name_suffix(const char *suffix);
void bms_cold_kv_store_get_default_protect(struct PRT_E2ROM_PARAS *protect);
void bms_cold_kv_store_get_default_system(bms_cold_system_params_t *system);
void bms_cold_kv_store_factory_reset(void);
flash_kv32_dbg_t bms_cold_kv_store_get_dbg(void);
```

## 4. 使用方式

### 4.1 保护参数

项目现有入口仍然是：

```c
LoadParam();
SaveParam();
```

它们现在的底层已经不再直接擦写旧 `PARAM_ADDR`，而是走 `cold_kv`。

### 4.2 系统参数

整包读写：

```c
bms_cold_system_params_t system_cfg;

bms_cold_kv_store_init();
bms_cold_kv_store_get_system(&system_cfg);
system_cfg.flags |= 0x01u;
bms_cold_kv_store_set_system(&system_cfg);
```

单项读写：

```c
u32 series_num = 0u;

bms_cold_kv_store_get_system_value(BMS_SYS_PARAM_SERIES_NUM, &series_num);
bms_cold_kv_store_set_system_value(BMS_SYS_PARAM_SERIES_NUM, 13u);
```

### 4.3 升级重置 epoch

控制项由 `param.c` 统一管理，当前包括：

- `BMS_COLD_CTRL_PROTECT_RESET_EPOCH`
- `BMS_COLD_CTRL_SYSTEM_RESET_EPOCH`
- `BMS_COLD_CTRL_SOC_RESET_EPOCH`
- `BMS_COLD_CTRL_EVENT_LOG_RESET_EPOCH`
- `BMS_COLD_CTRL_RUNTIME_RESET_EPOCH`

只有当默认参数重建成功后，对应 epoch 才会落盘。

### 4.4 蓝牙名

`btname_modbus.c` 现在只负责：

- Modbus 输入解析
- suffix 清洗
- BLE 广播名应用

真正持久化走：

- `bms_cold_kv_store_get_bt_name_suffix()`
- `bms_cold_kv_store_set_bt_name_suffix()`

## 5. 当前行为特点

- 相同值重复写入会直接跳过，不产生无意义落盘
- `flash_kv32` 采用追加写 + commit 末写，异常断电通常只会丢最后一次未完成提交
- `bms_cold_kv_store_factory_reset()` 会一起清空保护参数、系统参数、epoch、蓝牙名 suffix

## 6. 不建议的事情

- 不要再新增旧布局迁移逻辑
- 不要再让 `btname` 回到独立单 sector 擦写
- 不要把 `runtime`、`soc_kv`、`event_log` 直接物理并入 `cold_kv`

这些模块写入频率不同，强行合并会把热写入寿命问题扩散到冷数据。
