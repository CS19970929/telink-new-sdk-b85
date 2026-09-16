# AFE Hardware Protection V2

## 1. 目的

软件三级保护与 AFE 芯片硬件保护是两条独立安全通道：

- 软件保护：`g_tParam.protect`，First / Second / Third / Recover / Filter；
- AFE硬件保护：`bms_afe_hw_profile_t`，没有 First/Second/Third；
- 修改软件保护不得副作用重写 AFE HW profile；
- 修改 AFE HW profile 不得改 `g_tParam.protect`。

首次升级到该架构时，如果持久 profile 为空（schema/model均为0），固件允许从历史软件参数建立一次 migration default；保存成功后两套参数独立演进。

## 2. Backend

- D008：DVC1124，`afe_model=0x1124`；
- D011 / D013：SH3673510 backend，`afe_model=0x3510`。

公共协议只传语义值。DVC/SH 的寄存器、Rsense、步进、delay、capability由各自backend校验和量化。

## 3. 35-word profile

| Word | 内容 | 单位 |
|---:|---|---|
| 0 | schema version | - |
| 1 | AFE model | - |
| 2..9 | COV/CUV trip、delay、recover、recover confirm | mV / ms |
| 10..15 | OCD1/OCD2 与公共恢复 | 0.1A / ms |
| 16..21 | OCC1/OCC2 与公共恢复 | 0.1A / ms |
| 22..24 | SC/SCD trip、delay、recover confirm | 0.1A / us / ms |
| 25..33 | HW temperature trip/recover | `(°C+40)*10` / ms |
| 34 | enable_mask | bitmap |

不支持的 capability bit 必须拒绝，不能静默忽略。

## 4. Modbus map

### Requested / persisted

```text
0x2500..0x2522 : 35 words
```

写入只接受完整 35-word Modbus 0x10 transaction；partial write拒绝。

### Metadata

```text
0x2523 capabilities
0x2524 profile valid
0x2525 product shunt uOhm
0x2526 product cell count
0x2527 AFE watchdog seconds
0x2528 privileged session active
0x2529 apply state
0x252A last error
0x252B interface version (=2)
```

### Effective

```text
0x2540..0x2562 : 35 words, read-only
```

上位机/测试工具必须同时显示 requested 与 effective，不能把芯片量化后的值伪装成用户输入值。

## 5. 写授权会话

Custom Modbus function `0x42`：

```text
OPEN      0x01
HEARTBEAT 0x02
CLOSE     0x03
STATUS    0x04
```

Session timeout：60 s。成功commit后会自动关闭会话。

该会话是写门禁，不是密码学安全机制。

## 6. Transaction语义

一次硬件保护写入：

```text
read previous
 -> validate complete candidate
 -> persist candidate
 -> apply backend
 -> read back requested
 -> read back effective
 -> verify
 -> success
```

apply/verify失败时恢复并重新apply previous profile；rollback本身失败则：

```text
apply_state = CONFIG_INCONSISTENT
```

此时工具不得显示“写入成功”。

## 7. Windows 上位机唯一真源

D008/D011/D013 的 Windows 上位机不在当前产品固件分支中。唯一真源为本仓库
分支 `feature/windows-afe-hw-protection-editor-v2` 下的：

```text
bms-tool-windows/BmsTool.Windows/
bms-tool-windows/BmsFactoryTest.Windows/
```

客户版与内部测试版的公共协议能力必须同步维护。历史 `tools/` 下的旧
Assistant 客户端均已废弃，不得作为实现、构建或测试依据。

正式 AFE HW profile 写入必须由上位机完成 `0x42` 授权会话、35-word
原子写、requested/effective readback、verify 和 rollback 状态显示；不得用
普通单寄存器写绕过事务。BLE 分包/transport 由上述 Windows 真源实现和验证，
不得通过修改固件协议来迁就废弃客户端。

## 8. 修改默认值的位置

- Common profile结构/validation/migration：`bms_afe_hw_profile.h/.c`；
- 持久化：`bms_cold_kv_store.c`；
- D008量化/应用：DVC1124 backend；
- D011/D013量化/应用：`sh3673510_control.c`；
- 产品板级静态安全配置：各分支 `dvc1124_project_config.h` 或 `sh3673510_project_config.h`。

普通产品阈值调整不要改寄存器真值头文件。

## 9. 安全限制

公共接口只统一语义和事务，不授权未评审产品值。D008 SCD/WDT/Body-Diode、D011/D013 SC/温度、Rsense、GPIO、NTC、load/wake策略等仍需按各产品 `*_PRODUCT_REFERENCE.md` 和 `HARDWARE_VALIDATION.md` 完成硬件签核。
