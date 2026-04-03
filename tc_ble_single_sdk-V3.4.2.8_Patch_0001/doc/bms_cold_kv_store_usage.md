# bms_cold_kv_store 使用说明

## 1. 目的

`bms_cold_kv_store` 是基于 `flash_kv32` 的冷区业务封装，用来存储低频变化、但需要掉电保存的参数。

当前这层主要面向两类数据：

- 保护参数
  - 过压、欠压、过流、温度、压差、低 SOC 等阈值与恢复值
- BMS 系统参数
  - `bms_type`
  - `series_num`
  - `capacity_factory`
  - `afe_odc2`
  - `fac_init_soc`
  - `init_soc`
  - 以及后续继续扩展的系统级 `u32` 参数

另外，冷区现在还承担“升级后一次性重置 epoch”控制键的持久化，用来决定：

- 是否在这次固件升级后重置保护参数
- 是否在这次固件升级后重置系统参数
- 是否在这次固件升级后重置 SOC 参数

具体策略见 [upgrade_param_reset_policy.md](D:/telink/tc_ble_single_sdk-V3.4.2.8_Patch_0001%20(1)/tc_ble_single_sdk-V3.4.2.8_Patch_0001%20(1)/tc_ble_single_sdk-V3.4.2.8_Patch_0001/doc/upgrade_param_reset_policy.md:1)。

## 2. 为什么要单独做冷区 wrapper

这些参数和 `soc_kv_store` 里的热数据不同：

- 写入频率低
- 生命周期长
- 一般在配置修改、产线校准、恢复默认值时才变化

所以它们不应该和 `SOC / cycle / runtime` 混在一个热区里。

## 3. 当前接口范围

本次会提供：

- 冷区初始化
- 保护参数整体读取/整体写入
- 系统参数整体读取/整体写入
- 单个系统参数读写
- 恢复默认值
- 调试状态读取

并且会把现有 `param.c` 的 `LoadParam/SaveParam` 迁到冷区 wrapper 上，保持原有调用点不变。

## 3.1 当前接口

头文件：

- `tc_ble_single_sdk/vendor/ble_sample/bms_cold_kv_store.h`

当前可用接口：

```c
int bms_cold_kv_store_init(void);
int bms_cold_kv_store_get_protect(struct PRT_E2ROM_PARAS *protect);
int bms_cold_kv_store_set_protect(const struct PRT_E2ROM_PARAS *protect);
int bms_cold_kv_store_get_system(bms_cold_system_params_t *system);
int bms_cold_kv_store_set_system(const bms_cold_system_params_t *system);
int bms_cold_kv_store_get_system_value(bms_cold_system_param_id_t item, u32 *value);
int bms_cold_kv_store_set_system_value(bms_cold_system_param_id_t item, u32 value);
void bms_cold_kv_store_get_default_protect(struct PRT_E2ROM_PARAS *protect);
void bms_cold_kv_store_get_default_system(bms_cold_system_params_t *system);
void bms_cold_kv_store_factory_reset(void);
flash_kv32_dbg_t bms_cold_kv_store_get_dbg(void);
```

说明：

- `get_protect/set_protect`
  - 面向整组保护参数
- `get_system/set_system`
  - 面向整组 BMS 系统参数
- `get_system_value/set_system_value`
  - 面向单个系统级 `u32` 参数
- `factory_reset`
  - 清空冷区并恢复默认值
- `get_dbg`
  - 查看冷区当前活动扇区、写偏移、代次等信息

## 3.2 工程集成注意点

当前 Telink 工程的 `vendor/ble_sample/subdir.mk` 是自动生成文件，默认不进版本控制。  
如果你在新的工作区、另一台机器或重新生成工程后发现 `bms_cold_kv_store.c` 没有参与编译，优先检查这一层源文件列表是否已经重新生成。

当前冷区封装依赖的源码文件至少包括：

- `tc_ble_single_sdk/vendor/ble_sample/flash_kv32.c`
- `tc_ble_single_sdk/vendor/ble_sample/bms_cold_kv_store.c`
- `tc_ble_single_sdk/vendor/ble_sample/param.c`

判断方法很简单：

- `vendor/ble_sample/subdir.mk` 里应当能看到 `bms_cold_kv_store.c`
- 编译输出里应当出现 `vendor/ble_sample/bms_cold_kv_store.o`

## 4. 数据来源

### 4.1 保护参数默认值

继续沿用当前 `param.h` 中已有的：

- `struct PRT_E2ROM_PARAS`
- `E2P_PROTECT_DEFAULT_PRT`

这样可以保证已有保护阈值定义不需要重写。

### 4.2 系统参数默认值

系统参数默认值取自现有配置宏，例如：

- `FD_BMS_TYPE`
- `SeriesNum`
- `CapacityFactory`
- `AFE_ODC2`
- `FAC_INIT_soc`
- `__INIT_SOC__`

## 4.1 当前系统参数结构

当前系统参数示例结构是：

```c
typedef struct {
    u32 bms_type;
    u32 series_num;
    u32 capacity_factory;
    u32 afe_odc2;
    u32 fac_init_soc;
    u32 init_soc;
    u32 flags;
    u32 reserved0;
} bms_cold_system_params_t;
```

这部分不是最终边界，只是当前第一版冷区系统参数示例。  
后续新增系统参数时，优先扩展这里。

## 5. 兼容策略

当前仓库里原本的保护参数存储方式是：

- `param.c/.h`
- 整结构体直接读写一个扇区

这次迁移会保留原有 `LoadParam/SaveParam` 对外接口，但内部改成冷区 KV 存储。  
同时会保留一次“旧参数到新冷区”的迁移逻辑，避免已有设备第一次升级后直接丢掉旧保护参数。

迁移策略是：

1. 启动时先从旧 `PARAM_ADDR` 读出旧版整结构体参数到 RAM。
2. 初始化 `bms_cold_kv_store`。
3. 如果冷区里已经有有效参数，则优先使用冷区。
4. 如果冷区还是默认保护参数、而旧整结构体里存在有效参数，则自动把旧参数迁到冷区。

这样可以避免首次升级直接丢掉老设备上的保护阈值。

## 6. 当前项目如何使用

### 6.1 保护参数

当前项目不用改原有调用点：

```c
LoadParam();
... 使用 g_tParam.protect ...
SaveParam();
```

只是 `LoadParam/SaveParam` 现在的底层已经不再是“整扇区直接擦写结构体”，而是走冷区 KV。

### 6.2 直接读写系统参数

如果以后要保存系统参数，可以直接这样用：

```c
bms_cold_system_params_t system_cfg;

bms_cold_kv_store_init();
bms_cold_kv_store_get_system(&system_cfg);

system_cfg.flags |= 0x01u;
bms_cold_kv_store_set_system(&system_cfg);
```

如果只改一个系统参数：

```c
bms_cold_kv_store_set_system_value(BMS_SYS_PARAM_SERIES_NUM, 13);
```

读取一个系统参数：

```c
u32 series_num = 0;
bms_cold_kv_store_get_system_value(BMS_SYS_PARAM_SERIES_NUM, &series_num);
```

## 7. 扩展方式

后续要扩展新的冷区参数时，优先走下面两条路径：

1. 如果属于保护参数结构的一部分
   - 扩展 `struct PRT_E2ROM_PARAS`
   - 在冷区 wrapper 的字段映射表里增加对应 key

2. 如果属于系统级 `u32` 参数
   - 扩展 `bms_cold_system_params_t`
   - 在系统参数字段映射表里增加对应 key

这样后续扩展只改映射表和默认值，不再碰底层 Flash 逻辑。
