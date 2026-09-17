# AFE Hardware Protection V2

## 1. 目的

软件三级保护与 AFE 芯片硬件保护是两条独立安全通道：

- 软件保护：`g_tParam.protect`，First / Second / Third / Recover / Filter；
- AFE硬件保护：`bms_afe_hw_profile_t`，没有 First/Second/Third；
- 修改软件保护不得副作用重写 AFE HW profile；
- 修改 AFE HW profile 不得改 `g_tParam.protect`。

开发期不迁移旧参数。Config schema 2 初始化或独立 AFE revision 改变时，从编译期默认构造 Requested 并原子保存；详见 [存储升级实现](D008_STORAGE_UPGRADE_IMPLEMENTATION.md)。

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

- Common profile结构/validation/default：`bms_afe_hw_profile.h/.c`；
- 持久化：`bms_cold_kv_store.c`；
- D008量化/应用：DVC1124 backend；
- D011/D013量化/应用：`sh3673510_control.c`；
- 产品板级静态安全配置：各分支 `dvc1124_project_config.h` 或 `sh3673510_project_config.h`。

普通产品阈值调整不要改寄存器真值头文件。

## 9. 安全限制

公共接口只统一语义和事务，不授权未评审产品值。D008 SCD/WDT/Body-Diode、D011/D013 SC/温度、Rsense、GPIO、NTC、load/wake策略等仍需按各产品 `*_PRODUCT_REFERENCE.md` 和 `HARDWARE_VALIDATION.md` 完成硬件签核。

## 10. MTU=23 分片事务（access protocol version 2）

2026-09-17：经用户授权扩展现有 0x42 会话，保留原 0x10/0x2500 完整写入路径、寄存器和 Flash 布局。无需协商更大 MTU。OPEN 回应中的 protocol version 为 2；旧固件小 MTU 时 Windows 拒绝发送参数帧，需更新配套固件或使用串口。

所有多字节字段大端，CRC16/Modbus 低字节在前：

- STAGE 请求：`01 42 05 tokenHi tokenLo offset count data CRClo CRChi`。data 为原 79-byte Modbus 0x10 完整帧片段；count=1..11，每包最多20字节。
- STAGE 成功回应：`01 42 05 00 tokenHi tokenLo nextOffset CRClo CRChi`。
- COMMIT 请求：`01 42 06 tokenHi tokenLo CRClo CRChi`；成功回应 `01 42 06 00 CRClo CRChi`。
- 错误回应为 `addr 42 command status CRClo CRChi`；status=1 非法请求、2 授权失败、3 不支持、4 完整事务失败（需读 apply_state/last_error）。

片段须严格按 offset 连续写入，逐包确认；每次 OPEN 清空暂存并产生新 token。片段间隔超过5秒、关闭/过期会话、BLE断开均丢弃未提交数据。暂存固定79字节，不分片写 Flash 或 AFE。只有收齐并验证完整帧 CRC、地址2500、35words 后才调用既有参数事务。COMMIT 消耗暂存，失败和应答丢失不得重放；上位机重新读 Requested/Effective/apply_state 后再由用户决定下一步。会话是操作门禁，不是安全认证。

SCD 通过 enable_mask bit6 配置，Windows 只新增此位的显式0/1编辑，不修改其他位。启用前必须输入已确认电流/延时。DVC1124 电流按板级 Rsense 换算到10..630mV、10mV档位；延时7.81us档位，Windows启用检查8..1999us，实际以 Effective 为准。不得默认开启或猜测安全阈值。HW=0编译时 Effective仍为关闭，修改 Requested不会绕过编译开关。

串口连接保持端口打开，用只读 D120 探测等待一线通自动切换；单次650ms、间隔100ms、最多24次且总截止20秒。有效身份/兼容窗口应答才判定连接，支持取消。串口不再沿用 BLE重建/GATT错误。未改变固件一线通调度或5秒UART空闲回退。

验证：`python tests/afe_hw_fragment_host_check.py` 执行实际会话代码；Windows `test-afe-fragments.ps1` 验证客户端分片/确认/取消与延迟串口应答。实板 BLE 丢包/断连、SCD动作、电流阈值及一线通切换仍为 TODO_VERIFY_HW；Host测试不能证明物理短路保护效果。
