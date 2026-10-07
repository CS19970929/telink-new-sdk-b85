# AFE Hardware Protection V2

## 1. 目的

原厂审查后的严格新写入、历史参数兼容、配置一致性门禁及验证状态见 [AFE修复记录](AFE_AUDIT_REMEDIATION.md)。35-word AFE payload 保持；后续业务 Flash 重划与内部 journal schema 3 见 [STORAGE](STORAGE.md)。

软件三级保护与 AFE 芯片硬件保护是两条独立安全通道：

- 软件保护：`g_bms_protection_params`，First / Second / Third / Recover / Filter；
- AFE硬件保护：`bms_afe_hw_profile_t`，没有 First/Second/Third；
- 修改软件保护不得副作用重写 AFE HW profile；
- 修改 AFE HW profile 不得改 `g_bms_protection_params`。

当前 monorepo 使用 CFG2 payload 格式、内部 journal schema 3，旧开发记录拒绝且不迁移。软件和 AFE 运行参数独立管理；同属 Config 记录但字段与更新编号独立。D008 初始默认取编译期软件默认种子并规范化，SH 默认来自产品头文件。

D014 当前从产品配置建立独立 AFE 默认 profile；schema/model 不匹配时重建该默认值。D014 的软件保护参数不参与 AFE 默认值生成。

## 2. Backend

- D008：DVC1124，`afe_model=0x1124`；
- D011 / D013 / D014：SH3673510 backend，`afe_model=0x3510`。

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

上位机/测试工具必须同时显示 requested 与 effective，不能把芯片量化值伪装成用户输入。SH effective 的温度字段目前未反算寄存器量化温度，不能把显示值当成独立温度 readback 或实际动作温度；还需检查寄存器与实测。

## 5. 写授权会话

Custom Modbus function `0x42`：

```text
OPEN      0x01
HEARTBEAT 0x02
CLOSE     0x03
STATUS    0x04
```

Session timeout：60 s。成功commit后会自动关闭会话。

会话为设备级，UART 与 BLE 共用同一写授权。四产品的任意 BLE disconnect 都撤销授权并丢弃未完成碎片；因此 disconnect 后 UART 也必须重新 OPEN。会话期限与碎片期限仍分别计时，不能用 HEARTBEAT 无限延长未完成碎片。

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

## 7. 当前上位机真实能力

唯一真源是 `feature/windows-afe-hw-protection-editor-v2` 分支的 `bms-tool-windows/`：

- `BmsTool.Windows`：客户版；
- `BmsFactoryTest.Windows`：内部完整测试版；
- `BmsTool.Cli`：自动化、诊断与 OTA；
- `BmsTool.Android`：移动端。

四个入口复用同一协议/诊断/OTA核心。AFE Hardware Protection V2 使用 `0x42` 授权、35-word
原子写入以及 requested/effective readback；不得用普通单寄存器写入绕过事务。完整诊断还会独立读取
`0x2500/0x2523/0x2540`，不依赖 D008 参数能力窗口。D014 固件诊断适配见
[D014_DIAGNOSTICS.md](D014_DIAGNOSTICS.md)。上位机功能以其维护分支实际版本为准，本次文档整理未重新验证各客户端构建。

## 8. 修改默认值的位置

- Common profile结构/validation/default builder：`bms_afe_hw_profile.h/.c`；
- 持久化：`bms_config_store.c` / `storage_record.c`；
- D008量化/应用：DVC1124 backend；
- D011/D013/D014量化/应用：`sh3673510_control.c`；
- 产品板级静态安全配置：D008 的 `dvc1124_project_config.h` 或 SH 产品的 `bms/products/<product>/bms_sh3673510_config.h`。

普通产品阈值调整不要改寄存器真值头文件。

## 9. 安全限制

公共接口只统一语义和事务，不授权未评审产品值。D008 SCD/WDT/Body-Diode、D011/D013 SC/温度、Rsense、GPIO、NTC、load/wake策略等仍需按各产品 `*_PRODUCT_REFERENCE.md` 和 `HARDWARE_VALIDATION.md` 完成硬件签核。
