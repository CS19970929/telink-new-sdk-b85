# 升级后参数重置策略

## 1. 目标

这套策略用于解决一个很常见的问题：

- 固件升级后，旧板子上已经保存了旧参数
- 新版本程序希望只在“首次升级到这版固件”时，把某些参数重置到新初始值
- 后续重启和后续运行不应重复重置

当前已经支持 3 类参数：

- `SOC` 持久化参数
- 保护参数
- `bms_cold_kv_store` 中的系统参数

## 2. 控制方式

控制宏在 [conf.h](D:/telink/tc_ble_single_sdk-V3.4.2.8_Patch_0001%20(1)/tc_ble_single_sdk-V3.4.2.8_Patch_0001%20(1)/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/conf.h:108)：

```c
#define FW_UPGRADE_RESET_PROTECT_EPOCH   0u
#define FW_UPGRADE_RESET_SYSTEM_EPOCH    0u
#define FW_UPGRADE_RESET_SOC_EPOCH       0u
```

语义如下：

- `0`：本次固件不触发该类参数重置
- 非 `0`：表示本次固件要求的“重置 epoch”

设备启动时会把“已执行过的 epoch”保存在冷区控制键里。  
如果当前固件配置的 epoch 和已执行 epoch 不一致，就执行一次重置；执行成功后再把新的 epoch 落盘。

这样就能保证：

- 同一块板子升级到目标版本时，只重置一次
- 后续再次上电、再次重启，不会重复重置
- 你可以分别控制 `SOC`、保护参数、系统参数是否在这次升级中重置

## 3. 使用方法

### 3.1 不重置任何参数

保持默认值：

```c
#define FW_UPGRADE_RESET_PROTECT_EPOCH   0u
#define FW_UPGRADE_RESET_SYSTEM_EPOCH    0u
#define FW_UPGRADE_RESET_SOC_EPOCH       0u
```

### 3.2 只重置 SOC

例如这次升级后，只想让所有旧板子的 `SOC` 参数回到新的初始值：

```c
#define FW_UPGRADE_RESET_PROTECT_EPOCH   0u
#define FW_UPGRADE_RESET_SYSTEM_EPOCH    0u
#define FW_UPGRADE_RESET_SOC_EPOCH       2026040301u
```

### 3.3 只重置保护参数

```c
#define FW_UPGRADE_RESET_PROTECT_EPOCH   2026040301u
#define FW_UPGRADE_RESET_SYSTEM_EPOCH    0u
#define FW_UPGRADE_RESET_SOC_EPOCH       0u
```

### 3.4 同时重置多个参数域

```c
#define FW_UPGRADE_RESET_PROTECT_EPOCH   2026040302u
#define FW_UPGRADE_RESET_SYSTEM_EPOCH    2026040302u
#define FW_UPGRADE_RESET_SOC_EPOCH       2026040302u
```

是否使用同一个 epoch 数值不重要，关键是：

- 想触发一次新的重置，就把对应宏改成一个新的非 `0` 值
- 不想触发，就保持不变

## 4. 当前实现位置

### 4.1 启动触发入口

升级重置逻辑在 [app.c](D:/telink/tc_ble_single_sdk-V3.4.2.8_Patch_0001%20(1)/tc_ble_single_sdk-V3.4.2.8_Patch_0001%20(1)/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/app.c:1345) 启动早期执行：

```c
LoadParam();
Param_UpgradeReset_Apply();
```

放在这里的原因是：

- 保护参数重置必须早于 AFE 配置
- `SOC` 重置必须早于 `soc_param_lib_init()`

### 4.2 具体逻辑

重置实现位于 [param.c](D:/telink/tc_ble_single_sdk-V3.4.2.8_Patch_0001%20(1)/tc_ble_single_sdk-V3.4.2.8_Patch_0001%20(1)/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/param.c:1)：

- `Param_UpgradeReset_Apply()`
- `param_upgrade_apply_default_protect()`
- `param_upgrade_apply_default_system()`
- `param_upgrade_apply_default_soc()`

控制 epoch 保存在冷区 KV 控制键中，对应接口在：

- [bms_cold_kv_store.h](D:/telink/tc_ble_single_sdk-V3.4.2.8_Patch_0001%20(1)/tc_ble_single_sdk-V3.4.2.8_Patch_0001%20(1)/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/bms_cold_kv_store.h:1)
- [bms_cold_kv_store.c](D:/telink/tc_ble_single_sdk-V3.4.2.8_Patch_0001%20(1)/tc_ble_single_sdk-V3.4.2.8_Patch_0001%20(1)/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/bms_cold_kv_store.c:1)

## 5. 重置后的默认值来源

### 5.1 保护参数

保护参数默认值来自：

- `E2P_PROTECT_DEFAULT_PRT`

### 5.2 系统参数

系统参数默认值来自：

- `FD_BMS_TYPE`
- `SeriesNum`
- `CapacityFactory`
- `AFE_ODC2`
- `FAC_INIT_soc`
- `__INIT_SOC__`

### 5.3 SOC 参数

`SOC` 升级重置当前使用的是“工厂初始参数”语义，而不是单纯使用 `soc_kv_store` 的存储默认宏：

- `soc = FAC_INIT_soc`
- `dsg = 0`
- `cycle = 100`

这样做是为了和 [SocEnhance.c](D:/telink/tc_ble_single_sdk-V3.4.2.8_Patch_0001%20(1)/tc_ble_single_sdk-V3.4.2.8_Patch_0001%20(1)/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/SocEnhance.c:192) 当前的工厂初始化逻辑保持一致。

## 6. 为什么不是每次开机都 reset

如果每次开机都 reset，会带来两个问题：

- 学习结果和运行中累积的 `SOC/cycle` 永远保存不住
- 保护参数和系统参数的人工修改也会被反复覆盖

所以这里采用的是“升级迁移”语义：

- 只在目标 epoch 首次出现时执行
- 后续一直保留新值正常运行

## 7. 适合的使用场景

适合：

- 修改了 SOC 算法，希望旧板子重新从新初值开始
- 修改了保护参数默认表，希望旧板子升级后自动切到新保护阈值
- 修改了 BMS 系统参数默认值，希望旧设备同步到新出厂参数

不适合：

- 想要“每次恢复出厂”
- 想通过运行时命令临时清空参数

这两类需求应当走单独的“手动恢复出厂”接口，而不是升级 epoch 机制。

## 8. 建议的版本管理方式

建议把 epoch 当成“参数迁移版本号”，而不是简单日期。

例如：

- `2026040301u`
- `2026040302u`
- `2026041501u`

推荐规则：

- 这次固件不想重置：保持不变
- 想再重置一次：对应参数域的 epoch 改成新值
- 不同参数域可以各自独立递增

这样最容易回溯，也最方便量产控制。
