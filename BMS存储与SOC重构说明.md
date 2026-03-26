# BMS 存储与 SOC 重构说明

## 本轮已完成的修改

### 1. 通用 Flash Blob 存储底座

新增文件：

- `/Users/cs/Downloads/work/todo/tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/flash_blob_store.h`
- `/Users/cs/Downloads/work/todo/tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/flash_blob_store.c`

特点：

- 双副本 A/B sector
- `magic/version/payload_size/seq/crc`
- 启动自动选最新有效副本
- 保存时写备用 sector，再切换 active

适用对象：

- `PARAM_T`
- runtime 工厂模式状态
- 蓝牙名配置
- 后续 SOC 快照

### 2. 参数存储统一到底座

修改文件：

- `/Users/cs/Downloads/work/todo/tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/param.c`

结果：

- `LoadParam()` 和 `SaveParam()` 不再直接裸擦写单扇区
- 改为双副本 blob 存储
- 启动若无有效参数，自动回默认值并保存

### 3. runtime 工厂模式计时存储统一到底座

修改文件：

- `/Users/cs/Downloads/work/todo/tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/runtime.c`

结果：

- 不再单 sector 裸写
- 改为双副本存储运行分钟数

### 4. 蓝牙名读写收敛

修改文件：

- `/Users/cs/Downloads/work/todo/tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/btname_modbus.c`

结果：

- 蓝牙名 suffix 存储改为统一 blob store
- 增加地址范围与数量检查
- 避免原来单 sector 自定义记录的重复造轮子

### 5. AFE 通讯与参数写入可靠性修复

修改文件：

- `/Users/cs/Downloads/work/todo/tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/sh367309_datadeal.c`
- `/Users/cs/Downloads/work/todo/tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/modbus_rtu.c`

结果：

- `TwiRead()` 改为真实 CRC 校验，并避免原来 `Length+1` 直接写进用户 buffer 的越界风险
- `MTPRead()` 返回真实状态
- `MTPWrite()` 增加回读校验
- Modbus 写保护参数改为“整帧写完后一次性 SaveParam + 下发 AFE”

### 6. 危险 Modbus 控制增加解锁机制

修改文件：

- `/Users/cs/Downloads/work/todo/tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/modbus_rtu.c`

结果：

- 新增 `0x10FF = 0x55AA` 解锁
- 解锁 5 秒内才允许进入工厂模式/强制休眠这类关键控制
- 避免普通总线写操作直接改系统关键状态

### 7. SOC 模块先做止血修复

修改文件：

- `/Users/cs/Downloads/work/todo/tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/SocEnhance.c`

结果：

- 补了 `get_dispsoc()`
- 修复 `Get_OpenCircuit_Value_new()` 无返回值问题
- 增加简单 OCV 查表
- `soc_param_lib_init()` 调整为先恢复 cycle，再算 SOH/容量
- 满充校准增加小电流持续时间条件，避免粗暴误拉满

---

## 当前推荐的 BMS 结构

### SOC

建议长期目标：

- `soc_input`
- `soc_core`
- `soc_store`
- `soc_diag`

推荐算法：

- 主估计：库仑积分
- 辅修正：静置 OCV 小步修正
- 保底：满充/空电端点校准
- 补偿：零点电流校准 + 温度影响

### 存储

建议统一走 `flash_blob_store`：

- 参数类：整块 blob
- 运行态：小 blob
- 配置类：小 blob
- SOC 快照：中等 blob

不再建议：

- 每个模块自己定义一套 sector 记录格式
- 单 sector 裸擦裸写
- 无版本、无校验、无序号的数据块

---

## 仍然建议继续做的事

1. 把 SOC 完整快照迁移到 `flash_blob_store`
2. 给 AFE 写配置增加完整事务结果上报
3. 把保护参数写接口做成白名单 + 工厂模式约束
4. 给 SOC 增加 `current offset` 校准和静置 OCV 修正
5. 清理旧的死代码和不再使用的宏分支
